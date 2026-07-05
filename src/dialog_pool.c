#include "dialog_pool.h"
#include <gtk/gtk.h>
#include <string.h>
#include "responsive_layout.h"
#include "common.h"
#include "functions.h"

// Pool d'objets GTK pour éviter create/destroy répétés
#define MAX_DIALOGS_PER_TYPE 4

// Fonction helper pour créer une section (label + textview)
static GtkWidget* create_section(GtkWidget *container, const gchar *label_text) {
    // Créer le label avec couleur bleue
    GtkWidget *label = gtk_label_new(NULL);
    gchar *markup = g_strdup_printf("<b><span foreground='#0066CC'>─ %s:</span></b>", label_text);
    gtk_label_set_markup(GTK_LABEL(label), markup);
    g_free(markup);
    gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
    gtk_box_pack_start(GTK_BOX(container), label, FALSE, FALSE, 0);
    
    // Créer le textview
    GtkWidget *textview = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(textview), FALSE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(textview), GTK_WRAP_WORD);
    gtk_box_pack_start(GTK_BOX(container), textview, FALSE, FALSE, 0);
    
    return textview;
}

// Flags pour dialog_pool_entry_t
#define DIALOG_FLAG_IN_USE    0x01

// Structure définie dans dialog_pool.h

static dialog_pool_entry_t dialog_pool[DIALOG_TYPE_COUNT][MAX_DIALOGS_PER_TYPE];
static gint pool_counts[DIALOG_TYPE_COUNT] = {0};

// Fonction pour configurer un dialogue selon son type
OPTIMIZE_SIZE
static void setup_dialog_for_type(dialog_pool_entry_t *entry, dialog_type_t type) {
    entry->type = type;
    
    switch (type) {
        case DIALOG_TYPE_PROCESS_DETAILS: {
            entry->dialog = gtk_dialog_new_with_buttons("Process Details",
                                                       NULL,
                                                       GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                                       "Kill", GTK_RESPONSE_REJECT,
                                                       "OK", GTK_RESPONSE_OK,
                                                       NULL);
            // Utiliser les dimensions responsives pour le dialogue des détails de processus
            gint detailsdialog_width = (gint)(layout_config.current_workspace_width * layout_config.detailsdialog_width_percent);
            gint detailsdialog_height = (gint)(layout_config.current_workspace_height * layout_config.detailsdialog_height_percent);
            gtk_window_set_default_size(GTK_WINDOW(entry->dialog), detailsdialog_width, detailsdialog_height);
            gtk_widget_set_size_request(entry->dialog, detailsdialog_width, detailsdialog_height);
            gtk_window_set_resizable(GTK_WINDOW(entry->dialog), TRUE);
            
            entry->content_area = gtk_dialog_get_content_area(GTK_DIALOG(entry->dialog));
            
            // Créer la scrolled window principale
            entry->scrolled = gtk_scrolled_window_new(NULL, NULL);
            gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(entry->scrolled), 
                                         GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
            
            // HBox principale pour layout horizontal (gauche + panneau extensible droite)
            entry->main_hbox = gtk_hbox_new(FALSE, 10);
            gtk_container_set_border_width(GTK_CONTAINER(entry->main_hbox), 10);
            
            // VBox de gauche (sections existantes)
            GtkWidget *left_vbox = gtk_vbox_new(FALSE, 10);
            
            // Boutons dans l'action area (bas du dialogue)
            entry->more_toggle = gtk_toggle_button_new_with_label("more details");
            GtkWidget *copy_button = gtk_button_new_with_label("Copy");
            GtkWidget *refresh_button = gtk_button_new_with_label("Refresh");
            GtkWidget *action_area = gtk_dialog_get_action_area(GTK_DIALOG(entry->dialog));
            if (action_area) {
                // Utiliser START pour que les boutons restent à gauche
                gtk_button_box_set_layout(GTK_BUTTON_BOX(action_area), GTK_BUTTONBOX_START);
                
                // Calculer la marge proportionnelle à la largeur du dialogue
                // Utiliser un pourcentage de la largeur pour une distribution harmonieuse
                gint left_margin = (gint)(detailsdialog_width * 0.02);  // 2% de la largeur
                if (left_margin < 10) left_margin = 10;  // Minimum 10px
                gint button_spacing = (gint)(detailsdialog_width * 0.01);  // 1% de la largeur
                if (button_spacing < 8) button_spacing = 8;  // Minimum 8px
                
                // Ajouter le toggle "more details" avec marge calculée
                gtk_box_pack_start(GTK_BOX(action_area), entry->more_toggle, FALSE, FALSE, left_margin);
                gtk_box_reorder_child(GTK_BOX(action_area), entry->more_toggle, 0);
                gtk_widget_set_size_request(entry->more_toggle, 110, -1);
                g_object_set_data(G_OBJECT(entry->more_toggle), "details_dialog", entry->dialog);
                
                // Ajouter le bouton "Copy" avec espacement
                gtk_box_pack_start(GTK_BOX(action_area), copy_button, FALSE, FALSE, button_spacing);
                gtk_box_reorder_child(GTK_BOX(action_area), copy_button, 1);
                g_object_set_data(G_OBJECT(copy_button), "details_dialog", entry->dialog);
                
                // Ajouter le bouton "Refresh" avec espacement
                gtk_box_pack_start(GTK_BOX(action_area), refresh_button, FALSE, FALSE, button_spacing);
                gtk_box_reorder_child(GTK_BOX(action_area), refresh_button, 2);
                g_object_set_data(G_OBJECT(refresh_button), "details_dialog", entry->dialog);
            }
            
            // Créer les 3 sections de la vue simple avec la fonction helper
            entry->textview_details = create_section(left_vbox, "Details");
            entry->textview_cmdline = create_section(left_vbox, "Command Line");
            entry->textview_extra = create_section(left_vbox, "Extra Infos");
            
            // Garder le label_extra pour compatibilité (si besoin de le modifier dynamiquement)
            entry->label_extra = NULL;
            
            // Panneau de droite (more details) - Structure IDENTIQUE au bloc gauche
            // right_panel = équivalent de left_vbox (sera dans scrolled_more)
            entry->right_panel = gtk_vbox_new(FALSE, 10);
            gtk_container_set_border_width(GTK_CONTAINER(entry->right_panel), 10);
            
            // Créer les 5 sections avec la fonction helper
            GtkWidget *textview_ident = create_section(entry->right_panel, "Identification and State");
            GtkWidget *textview_sched = create_section(entry->right_panel, "Scheduling and Performance");
            GtkWidget *textview_memio = create_section(entry->right_panel, "Memory and IO Resources");
            GtkWidget *textview_files = create_section(entry->right_panel, "Files, Network and Security");
            GtkWidget *textview_advanced = create_section(entry->right_panel, "Advanced");
            
            // Stocker les pointeurs dans la structure
            entry->label_more = NULL; // Plus besoin de stocker le premier label
            entry->textview_more = textview_ident;
            entry->textview_sched = textview_sched;
            entry->textview_memio = textview_memio;
            entry->textview_files = textview_files;
            entry->textview_advanced = textview_advanced;
            
            // Scrolled window contenant right_panel (comme scrolled contient main_hbox→left_vbox)
            entry->scrolled_more = gtk_scrolled_window_new(NULL, NULL);
            gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(entry->scrolled_more), 
                                         GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
            gtk_scrolled_window_add_with_viewport(GTK_SCROLLED_WINDOW(entry->scrolled_more), entry->right_panel);
            
            gtk_box_pack_start(GTK_BOX(entry->main_hbox), left_vbox, TRUE, TRUE, 0);
            // Le panneau de droite n'est PAS dans main_hbox (il aura sa propre scrollbar)
            
            gtk_scrolled_window_add_with_viewport(GTK_SCROLLED_WINDOW(entry->scrolled), entry->main_hbox);
            
            // Créer un bouton toggle avec l'icône ► pour déplier/replier (10x20 pixels)
            GtkWidget *expand_button = gtk_toggle_button_new();
            GdkPixbuf *arrowr_pixbuf_orig = get_embedded_icon_pixbuf("arrowr");
            if (arrowr_pixbuf_orig) {
                // Vérifier la taille et redimensionner avec NEAREST pour éviter l'interpolation
                int width = gdk_pixbuf_get_width(arrowr_pixbuf_orig);
                int height = gdk_pixbuf_get_height(arrowr_pixbuf_orig);
                GdkPixbuf *arrowr_pixbuf;
                if (width != 10 || height != 20) {
                    arrowr_pixbuf = gdk_pixbuf_scale_simple(arrowr_pixbuf_orig, 10, 20, GDK_INTERP_NEAREST);
                    g_object_unref(arrowr_pixbuf_orig);
                } else {
                    arrowr_pixbuf = arrowr_pixbuf_orig;
                }
                GtkWidget *arrow_image = gtk_image_new_from_pixbuf(arrowr_pixbuf);
                gtk_misc_set_padding(GTK_MISC(arrow_image), 0, 0);
                gtk_container_add(GTK_CONTAINER(expand_button), arrow_image);
                g_object_unref(arrowr_pixbuf);
            }
            gtk_button_set_relief(GTK_BUTTON(expand_button), GTK_RELIEF_NONE); // Pas de relief pour un look plus propre
            gtk_widget_set_size_request(expand_button, -1, 60); // Augmenter la hauteur du bouton (icône reste 10x20)
            g_object_set_data(G_OBJECT(expand_button), "details_dialog", entry->dialog);
            
            // VBox pour que le bouton expand prenne toute la hauteur
            GtkWidget *expand_vbox = gtk_vbox_new(FALSE, 0);
            gtk_box_pack_start(GTK_BOX(expand_vbox), expand_button, TRUE, TRUE, 0); // Bouton s'étend sur toute la hauteur
            gtk_widget_show_all(expand_vbox); // Afficher le vbox et tous ses enfants
            
            // Layout horizontal : scrolled + bouton expand + scrolled_more
            entry->horizontal_container = gtk_hbox_new(FALSE, 0); // Pas d'espacement
            gtk_box_pack_start(GTK_BOX(entry->horizontal_container), entry->scrolled, TRUE, TRUE, 0);
            gtk_box_pack_start(GTK_BOX(entry->horizontal_container), expand_vbox, FALSE, FALSE, 0); // Pas de marge
            gtk_box_pack_start(GTK_BOX(entry->horizontal_container), entry->scrolled_more, TRUE, TRUE, 0);
            gtk_box_pack_start(GTK_BOX(entry->content_area), entry->horizontal_container, TRUE, TRUE, 5);
            
            // Stocker le bouton expand pour y accéder plus tard
            g_object_set_data(G_OBJECT(entry->dialog), "expand_button", expand_button);
            
            // État initial : panneau de droite CACHÉ (affiché seulement si "more details" activé)
            // Note: On doit d'abord show_all pour initialiser les widgets, puis hide le scrolled_more
            gtk_widget_show_all(entry->scrolled_more);
            gtk_widget_hide(entry->scrolled_more);
            
            entry->textview = entry->textview_details;
            break;
        }
        
        case DIALOG_TYPE_SERVICE_DETAILS: {
            // Dialogue similaire à PROCESS_DETAILS mais avec boutons différents
            entry->dialog = gtk_dialog_new_with_buttons("Service Details",
                                                       NULL,
                                                       GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                                       "Stop", GTK_RESPONSE_REJECT,
                                                       "Start", GTK_RESPONSE_APPLY,
                                                       "OK", GTK_RESPONSE_OK,
                                                       NULL);
            
            // Utiliser les dimensions responsives unifiées
            gint dialog_width = (gint)(layout_config.current_workspace_width * layout_config.detailsdialog_width_percent);
            gint dialog_height = (gint)(layout_config.current_workspace_height * layout_config.detailsdialog_height_percent);
            gtk_window_set_default_size(GTK_WINDOW(entry->dialog), dialog_width, dialog_height);
            gtk_widget_set_size_request(entry->dialog, dialog_width, dialog_height);
            gtk_window_set_resizable(GTK_WINDOW(entry->dialog), TRUE);
            
            entry->content_area = gtk_dialog_get_content_area(GTK_DIALOG(entry->dialog));
            
            // Créer la scrolled window principale
            entry->scrolled = gtk_scrolled_window_new(NULL, NULL);
            gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(entry->scrolled), 
                                         GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
            
            // HBox principale pour layout horizontal
            entry->main_hbox = gtk_hbox_new(FALSE, 10);
            gtk_container_set_border_width(GTK_CONTAINER(entry->main_hbox), 10);
            
            // VBox de gauche (sections existantes)
            GtkWidget *left_vbox = gtk_vbox_new(FALSE, 10);
            
            // Boutons dans l'action area
            entry->more_toggle = gtk_toggle_button_new_with_label("more details");
            GtkWidget *copy_button = gtk_button_new_with_label("Copy");
            GtkWidget *action_area = gtk_dialog_get_action_area(GTK_DIALOG(entry->dialog));
            if (action_area) {
                gtk_button_box_set_layout(GTK_BUTTON_BOX(action_area), GTK_BUTTONBOX_START);
                
                gint left_margin = (gint)(dialog_width * 0.02);
                if (left_margin < 10) left_margin = 10;
                gint button_spacing = (gint)(dialog_width * 0.01);
                if (button_spacing < 8) button_spacing = 8;
                
                gtk_box_pack_start(GTK_BOX(action_area), entry->more_toggle, FALSE, FALSE, left_margin);
                gtk_box_reorder_child(GTK_BOX(action_area), entry->more_toggle, 0);
                gtk_widget_set_size_request(entry->more_toggle, 110, -1);
                g_object_set_data(G_OBJECT(entry->more_toggle), "details_dialog", entry->dialog);
                
                gtk_box_pack_start(GTK_BOX(action_area), copy_button, FALSE, FALSE, button_spacing);
                gtk_box_reorder_child(GTK_BOX(action_area), copy_button, 1);
                g_object_set_data(G_OBJECT(copy_button), "details_dialog", entry->dialog);
            }
            
            // Créer les 3 sections de la vue simple
            entry->textview_details = create_section(left_vbox, "Service Information");
            entry->textview_cmdline = create_section(left_vbox, "Status");
            entry->textview_extra = create_section(left_vbox, "Configuration");
            entry->label_extra = NULL;
            
            // Panneau de droite (more details)
            entry->right_panel = gtk_vbox_new(FALSE, 10);
            gtk_container_set_border_width(GTK_CONTAINER(entry->right_panel), 10);
            
            // Section 1: Processus
            GtkWidget *textview_processus = create_section(entry->right_panel, "Processus");
            
            // Section 2: Configuration
            GtkWidget *textview_config_more = create_section(entry->right_panel, "Configuration");
            
            // Section 3: Ressources
            GtkWidget *textview_ressources = create_section(entry->right_panel, "Ressources");
            
            // Section 4: Dependencies
            GtkWidget *textview_dependencies = create_section(entry->right_panel, "Dependencies");
            
            // Section 5: Security
            GtkWidget *textview_security = create_section(entry->right_panel, "Security");
            
            // Stocker les pointeurs
            entry->textview_more = textview_processus;
            entry->textview_sched = textview_config_more;
            entry->textview_memio = textview_ressources;
            entry->textview_files = textview_dependencies;
            entry->textview_advanced = textview_security;
            
            // Scrolled window pour right_panel
            entry->scrolled_more = gtk_scrolled_window_new(NULL, NULL);
            gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(entry->scrolled_more), 
                                         GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
            gtk_scrolled_window_add_with_viewport(GTK_SCROLLED_WINDOW(entry->scrolled_more), entry->right_panel);
            
            gtk_box_pack_start(GTK_BOX(entry->main_hbox), left_vbox, TRUE, TRUE, 0);
            gtk_scrolled_window_add_with_viewport(GTK_SCROLLED_WINDOW(entry->scrolled), entry->main_hbox);
            
            // Créer le bouton expand avec icône
            GtkWidget *expand_button = gtk_toggle_button_new();
            GdkPixbuf *arrowr_pixbuf_orig = get_embedded_icon_pixbuf("arrowr");
            if (arrowr_pixbuf_orig) {
                int width = gdk_pixbuf_get_width(arrowr_pixbuf_orig);
                int height = gdk_pixbuf_get_height(arrowr_pixbuf_orig);
                GdkPixbuf *arrowr_pixbuf;
                if (width != 10 || height != 20) {
                    arrowr_pixbuf = gdk_pixbuf_scale_simple(arrowr_pixbuf_orig, 10, 20, GDK_INTERP_NEAREST);
                    g_object_unref(arrowr_pixbuf_orig);
                } else {
                    arrowr_pixbuf = arrowr_pixbuf_orig;
                }
                GtkWidget *arrow_image = gtk_image_new_from_pixbuf(arrowr_pixbuf);
                gtk_misc_set_padding(GTK_MISC(arrow_image), 0, 0);
                gtk_container_add(GTK_CONTAINER(expand_button), arrow_image);
                g_object_unref(arrowr_pixbuf);
            }
            gtk_button_set_relief(GTK_BUTTON(expand_button), GTK_RELIEF_NONE);
            gtk_widget_set_size_request(expand_button, -1, 60); // Augmenter la hauteur du bouton (icône reste 10x20)
            g_object_set_data(G_OBJECT(expand_button), "details_dialog", entry->dialog);
            
            // VBox pour que le bouton expand prenne toute la hauteur
            GtkWidget *expand_vbox = gtk_vbox_new(FALSE, 0);
            gtk_box_pack_start(GTK_BOX(expand_vbox), expand_button, TRUE, TRUE, 0); // Bouton s'étend sur toute la hauteur
            gtk_widget_show_all(expand_vbox);
            
            // Layout horizontal
            entry->horizontal_container = gtk_hbox_new(FALSE, 0);
            gtk_box_pack_start(GTK_BOX(entry->horizontal_container), entry->scrolled, TRUE, TRUE, 0);
            gtk_box_pack_start(GTK_BOX(entry->horizontal_container), expand_vbox, FALSE, FALSE, 0);
            gtk_box_pack_start(GTK_BOX(entry->horizontal_container), entry->scrolled_more, TRUE, TRUE, 0);
            gtk_box_pack_start(GTK_BOX(entry->content_area), entry->horizontal_container, TRUE, TRUE, 5);
            
            g_object_set_data(G_OBJECT(entry->dialog), "expand_button", expand_button);
            
            // État initial : panneau de droite caché
            gtk_widget_show_all(entry->scrolled_more);
            gtk_widget_hide(entry->scrolled_more);
            
            entry->textview = entry->textview_details;
            break;
        }
            
        case DIALOG_TYPE_ABOUT:
            entry->dialog = gtk_about_dialog_new();
            gtk_container_set_border_width((GtkContainer*)entry->dialog, 2);
            break;
            
        case DIALOG_TYPE_PREFERENCES:
            entry->dialog = gtk_dialog_new_with_buttons("Preferences", NULL, 0, NULL);
            gtk_dialog_add_button(GTK_DIALOG(entry->dialog), "Cancel", GTK_RESPONSE_REJECT);
            gtk_dialog_add_button(GTK_DIALOG(entry->dialog), "OK", GTK_RESPONSE_ACCEPT);
            
            // Utiliser les dimensions responsives pour le dialogue des préférences
            gint prefdialog_width = (gint)(layout_config.current_workspace_width * layout_config.prefdialog_width_percent);
            gint prefdialog_height = (gint)(layout_config.current_workspace_height * layout_config.prefdialog_height_percent);
            gtk_window_set_default_size(GTK_WINDOW(entry->dialog), prefdialog_width, prefdialog_height);
            
            entry->content_area = gtk_dialog_get_content_area(GTK_DIALOG(entry->dialog));
            break;
            
        case DIALOG_TYPE_ERROR:
            entry->dialog = gtk_message_dialog_new(NULL,
                                                  GTK_DIALOG_MODAL,
                                                  GTK_MESSAGE_ERROR,
                                                  GTK_BUTTONS_OK,
                                                  "Error");
            break;
            
        case DIALOG_TYPE_CONFIRMATION:
            entry->dialog = gtk_message_dialog_new(NULL,
                                                  GTK_DIALOG_MODAL,
                                                  GTK_MESSAGE_QUESTION,
                                                  GTK_BUTTONS_YES_NO,
                                                  "Confirm");
            break;
            
        default:
            entry->dialog = gtk_dialog_new();
            break;
    }
}

// Obtenir un dialogue du pool (réutilisation ou création)
HOT_FUNCTION
GtkWidget* get_pooled_dialog(dialog_type_t type) {
    if (UNLIKELY(type >= DIALOG_TYPE_COUNT)) return NULL;
    
    // Chercher un dialogue libre du bon type
    for (gint i = 0; i < pool_counts[type]; i++) {
        dialog_pool_entry_t *entry = &dialog_pool[type][i];
        if (!(entry->flags & DIALOG_FLAG_IN_USE)) {
            entry->flags |= DIALOG_FLAG_IN_USE;
            
            // Réinitialiser le dialogue pour réutilisation
            if (type == DIALOG_TYPE_PROCESS_DETAILS) {
                if (entry->textview_details) {
                    GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(entry->textview_details));
                    gtk_text_buffer_set_text(buffer, "", 0);
                }
                if (entry->textview_cmdline) {
                    GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(entry->textview_cmdline));
                    gtk_text_buffer_set_text(buffer, "", 0);
                }
                if (entry->textview_extra) {
                    GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(entry->textview_extra));
                    gtk_text_buffer_set_text(buffer, "", 0);
                }
            }
            
            // Stocker la référence à l'entry dans le dialogue
            g_object_set_data(G_OBJECT(entry->dialog), "pool_entry", entry);
            
            return entry->dialog;
        }
    }
    
    // Créer un nouveau dialogue si le pool n'est pas plein
    if (pool_counts[type] < MAX_DIALOGS_PER_TYPE) {
        dialog_pool_entry_t *entry = &dialog_pool[type][pool_counts[type]];
        setup_dialog_for_type(entry, type);
        entry->flags |= DIALOG_FLAG_IN_USE;
        pool_counts[type]++;
        
        // Stocker la référence à l'entry dans le dialogue
        g_object_set_data(G_OBJECT(entry->dialog), "pool_entry", entry);
        
        return entry->dialog;
    }
    
    // Pool plein - fallback vers création directe (très rare)
    g_warning("Dialog pool full for type %d, falling back to direct creation", type);
    
    switch (type) {
        case DIALOG_TYPE_PROCESS_DETAILS:
            return gtk_dialog_new_with_buttons("Process Details", NULL,
                                             GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                             "Kill", GTK_RESPONSE_REJECT,
                                             "OK", GTK_RESPONSE_OK, NULL);
        case DIALOG_TYPE_SERVICE_DETAILS:
            return gtk_dialog_new_with_buttons("Service Details", NULL,
                                             GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                             "Stop", GTK_RESPONSE_REJECT,
                                             "Start", GTK_RESPONSE_APPLY,
                                             "OK", GTK_RESPONSE_OK, NULL);
        case DIALOG_TYPE_ABOUT:
            return gtk_about_dialog_new();
        case DIALOG_TYPE_ERROR:
            return gtk_message_dialog_new(NULL, GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR, 
                                        GTK_BUTTONS_OK, "Error");
        default:
            return gtk_dialog_new();
    }
}

// Retourner un dialogue au pool (cache au lieu de destroy)
HOT_FUNCTION
void return_pooled_dialog(GtkWidget *dialog) {
    if (UNLIKELY(!dialog)) return;
    
    // Chercher le dialogue dans le pool
    for (gint type = 0; type < DIALOG_TYPE_COUNT; type++) {
        for (gint i = 0; i < pool_counts[type]; i++) {
            dialog_pool_entry_t *entry = &dialog_pool[type][i];
            if (entry->dialog == dialog && (entry->flags & DIALOG_FLAG_IN_USE)) {
                entry->flags &= ~DIALOG_FLAG_IN_USE;
                
                // Cacher au lieu de détruire
                gtk_widget_hide(dialog);
                
                // Nettoyage spécifique selon le type
                if (type == DIALOG_TYPE_PROCESS_DETAILS && entry->textview) {
                    GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(entry->textview));
                    gtk_text_buffer_set_text(buffer, "", 0);
                }
                
                return;
            }
        }
    }
    
    // Dialogue non trouvé dans le pool - le détruire normalement
    gtk_widget_destroy(dialog);
}

// Nettoyage du pool à la fermeture
OPTIMIZE_SIZE
void cleanup_dialog_pool(void) {
    for (gint type = 0; type < DIALOG_TYPE_COUNT; type++) {
        for (gint i = 0; i < pool_counts[type]; i++) {
            dialog_pool_entry_t *entry = &dialog_pool[type][i];
            if (entry->dialog) {
                gtk_widget_destroy(entry->dialog);
                entry->dialog = NULL;
                entry->textview = NULL;
                entry->scrolled = NULL;
                entry->content_area = NULL;
                entry->flags = 0;
            }
        }
        pool_counts[type] = 0;
    }
}
