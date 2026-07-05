#ifndef DIALOG_POOL_H
#define DIALOG_POOL_H

#include <gtk/gtk.h>

// Types de dialogues dans le pool
typedef enum {
    DIALOG_TYPE_PROCESS_DETAILS,
    DIALOG_TYPE_SERVICE_DETAILS,
    DIALOG_TYPE_ABOUT,
    DIALOG_TYPE_PREFERENCES,
    DIALOG_TYPE_ERROR,
    DIALOG_TYPE_CONFIRMATION,
    DIALOG_TYPE_COUNT
} dialog_type_t;

// Structure pour les entrées du pool (exposée pour accès aux textviews)
typedef struct {
    GtkWidget *dialog;
    GtkWidget *textview;      // Pour process details (legacy)
    GtkWidget *textview_details;     // Pour section Details
    GtkWidget *textview_cmdline;     // Pour section Command Line
    GtkWidget *textview_extra;       // Pour section Extra Infos
    GtkWidget *label_extra;          // Label de la section Extra
    GtkWidget *more_toggle;          // Bouton bascule more/less details
    GtkWidget *right_panel;          // Panneau extensible de droite
    GtkWidget *scrolled_more;        // Scrolled window pour le panneau de droite
    GtkWidget *textview_more;        // Textview pour "Identification and State"
    GtkWidget *textview_sched;       // Textview pour "Scheduling and Performance"
    GtkWidget *textview_memio;       // Textview pour "Memory and IO Resources"
    GtkWidget *textview_files;       // Textview pour "Files, Network and Security"
    GtkWidget *textview_advanced;    // Textview pour "Advanced"
    GtkWidget *label_more;           // Label pour more details
    GtkWidget *scrolled;      // Pour process details  
    GtkWidget *content_area;  // Zone de contenu
    GtkWidget *main_hbox;     // HBox principale pour ajout dynamique du panneau
    GtkWidget *horizontal_container;  // Container horizontal (scrolled + right_panel)
    guint8 flags;
    dialog_type_t type;
} dialog_pool_entry_t;

// Fonctions du pool de dialogues
GtkWidget* get_pooled_dialog(dialog_type_t type);
void return_pooled_dialog(GtkWidget *dialog);
void cleanup_dialog_pool(void);

#endif // DIALOG_POOL_H
