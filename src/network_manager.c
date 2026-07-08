
#include "common.h"
#include "network_manager.h"
#include "functions.h"
#include <time.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <pci/pci.h>
#include <ctype.h>
#include <limits.h>
#include <linux/wireless.h>

network_manager_t network_manager;

// Cache intelligent pour informations réseau (évite appels ioctl répétés)
typedef struct {
    char mac[18];                // Cache permanent (MAC ne change jamais)
    char ssid[33];               // Cache 10 secondes (changement réseau WiFi rare)
    char ipv4[16];               // Cache 2 secondes (DHCP peut changer)
    char ipv6[46];               // Cache 2 secondes
    time_t ssid_timestamp;       // Dernière mise à jour SSID
    time_t ip_timestamp;         // Dernière mise à jour IP
    gboolean mac_cached;         // MAC déjà récupéré ?
    interface_connection_type_t type;  // Type interface (cache permanent)
} interface_cache_entry_t;

static interface_cache_entry_t interface_cache[MAX_NETWORK_INTERFACES];

// Structure pour stocker les statistiques précédentes
typedef struct {
    long prev_rx_bytes;
    long prev_tx_bytes;
    struct timespec prev_time;
    guint8 flags;  // NET_INITIALIZED
} network_stats_t;

static network_stats_t network_stats[MAX_NETWORK_INTERFACES];

// Initialiser le cache des interfaces réseau
static void init_interface_cache(void) {
    if (IS_NETWORK_CACHE_INITIALIZED()) return;
    
    for (int i = 0; i < MAX_NETWORK_INTERFACES; i++) {
        interface_cache[i].mac[0] = '\0';
        interface_cache[i].ssid[0] = '\0';
        interface_cache[i].ipv4[0] = '\0';
        interface_cache[i].ipv6[0] = '\0';
        interface_cache[i].ssid_timestamp = 0;
        interface_cache[i].ip_timestamp = 0;
        interface_cache[i].mac_cached = FALSE;
        interface_cache[i].type = INTERFACE_TYPE_ETHERNET;
    }
    
    set_optimization_flag(CACHE_FLAG_NETWORK_INITIALIZED, TRUE);
}

// Trouver l'index de cache pour une interface donnée
static int find_cache_index(const char* interface_name) {
    if (!IS_NETWORK_CACHE_INITIALIZED()) init_interface_cache();
    
    // Rechercher par nom dans network_manager
    for (int i = 0; i < network_manager.interface_count; i++) {
        if (strcmp(network_manager.interfaces[i].name, interface_name) == 0) {
            return i;
        }
    }
    
    return -1; // Interface non trouvée
}

// Système de polling adaptatif pour optimiser les interfaces inactives
static adaptive_network_t adaptive_networks[MAX_NETWORK_INTERFACES];
// adaptive_polling_enabled remplacé par IS_ADAPTIVE_POLLING_ENABLED() macro

// Structure pour les règles de remplacement de noms
typedef struct {
    const char *pattern;
    const char *replacement;
} ReplacementRule;

// Règles de simplification des noms d'interfaces réseau
ReplacementRule rules[] = {
    { "Wi-Fi", "WiFi" },
    { "Gigabit Ethernet Controller", "Gigabit Ethernet" },
    { "Gigabit Ethernet Adapter", "Gigabit Ethernet" },
    { "Gigabit Network Connection", "Gigabit Ethernet" },
    { "Ethernet Adapter", "Ethernet" },
    { "Wireless Network Adapter", "WiFi" },
    { "Wireless LAN Controller", "WiFi" },
    { "WiFi Adapter", "WiFi" },
    { "PCI Express", "PCI Exp." },
    { "Network Connection", "Ethernet" },
    { "LAN Controller", "Ethernet" }
};
const int rule_count = sizeof(rules) / sizeof(rules[0]);

// Fonction strcasestr pour recherche insensible à la casse
char *strcasestr(const char *haystack, const char *needle) {
    if (!*needle) return (char *)haystack;
    for (; *haystack; ++haystack) {
        if (tolower(*haystack) == tolower(*needle)) {
            const char *h = haystack + 1;
            const char *n = needle + 1;
            while (*n && *h && tolower(*h) == tolower(*n)) {
                ++h;
                ++n;
            }
            if (!*n) return (char *)haystack;
        }
    }
    return NULL;
}

// Fonction pour simplifier les noms d'interfaces
void simplify_name(char *name) {
    for (int i = 0; i < rule_count; ++i) {
        char *pos = strcasestr(name, rules[i].pattern);
        if (pos) {
            char buffer[1024];
            size_t before = pos - name;
            snprintf(buffer, sizeof(buffer), "%.*s%s%s",
                     (int)before,
                     name,
                     rules[i].replacement,
                     pos + strlen(rules[i].pattern));
            strncpy(name, buffer, 1023);
            name[1023] = '\0';
        }
    }
}

// Déclaration forward pour la fonction de fallback
static void update_network_data_traditional(void);

OPTIMIZE_SIZE_BEGIN
OPTIMIZE_SIZE COLD_FUNCTION
void init_network_manager(void) {
    memset(&network_manager, 0, sizeof(network_manager_t));
    memset(network_stats, 0, sizeof(network_stats));
    network_manager.selected_interface_index = -1; // Aucune sélection par défaut
    
    // Scanner /sys/class/net pour détecter les interfaces
    DIR *net_dir = opendir("/sys/class/net");
    if (!net_dir) {
        g_warning("Cannot open /sys/class/net");
        return;
    }
    
    struct dirent *entry;
    while ((entry = readdir(net_dir)) != NULL && network_manager.interface_count < MAX_NETWORK_INTERFACES) {
        if (entry->d_name[0] == '.') continue;
        
        // Vérifier si c'est une interface physique
        if (!is_physical_interface(entry->d_name)) {
            continue;
        }
        
        network_info_t *iface = &network_manager.interfaces[network_manager.interface_count];
        
        // Copier le nom
        strncpy(iface->name, entry->d_name, INTERFACE_NAME_MAX - 1);
        iface->name[INTERFACE_NAME_MAX - 1] = '\0';
        
        // Obtenir les informations de l'interface
        iface->type = get_interface_type(entry->d_name);
        iface->speed = get_interface_speed(entry->d_name);
        iface->flags |= NET_IS_PHYSICAL;
        get_interface_pci_name(entry->d_name, iface->description, INTERFACE_DESC_MAX);
        
        // Initialiser les buffers
        memset(iface->rx_samples, 0, sizeof(iface->rx_samples));
        memset(iface->tx_samples, 0, sizeof(iface->tx_samples));
        memset(iface->activity_samples, 0, sizeof(iface->activity_samples));
        
        network_manager.interface_count++;
        
        // Interface added successfully
    }
    
    closedir(net_dir);
    
    // Initialiser le système de polling adaptatif
    memset(adaptive_networks, 0, sizeof(adaptive_networks));
    for (int i = 0; i < network_manager.interface_count; i++) {
        adaptive_network_t *anet = &adaptive_networks[i];
        anet->iface = &network_manager.interfaces[i];
        anet->poll_frequency = 1;           // Commencer par poll à chaque cycle
        anet->cycles_since_poll = 0;
        anet->activity_level = 1;           // Supposer activité modérée au début
        anet->consecutive_inactive = 0;
        anet->last_rx_bytes = 0;
        anet->last_tx_bytes = 0;
    }
}
OPTIMIZE_SIZE_END

void cleanup_network_manager(void) {
    // Rien à nettoyer pour l'instant
}

gboolean is_physical_interface(const char *interface_name) {
    char path[256];
    char link_target[256];
    ssize_t len;
    
    // Lire le lien symbolique pour déterminer si c'est physique
    snprintf(path, sizeof(path), "/sys/class/net/%s", interface_name);
    len = readlink(path, link_target, sizeof(link_target) - 1);
    if (len == -1) {
        return FALSE;
    }
    link_target[len] = '\0';
    
    // Les interfaces physiques pointent vers /devices/pci* ou similaire
    // Les interfaces virtuelles pointent vers /devices/virtual/
    if (strstr(link_target, "/virtual/") != NULL) {
        return FALSE;
    }
    
    // Exclure l'interface loopback
    if (strcmp(interface_name, "lo") == 0) {
        return FALSE;
    }
    
    // Vérifier l'état opérationnel - accepter toutes les interfaces physiques
    // Ne pas filtrer par état "up" car cela peut exclure des interfaces valides
    snprintf(path, sizeof(path), "/sys/class/net/%s/operstate", interface_name);
    char state[16];
    if (read_sys_file_line(path, state, sizeof(state))) {
        // Accepter up, down, dormant, etc. - toutes les interfaces physiques
        return TRUE;
    }
    
    return TRUE;
}

int get_interface_type(const char *interface_name) {
    char path[256];
    FILE *fp;
    int type = NET_TYPE_ETHERNET; // Par défaut
    
    snprintf(path, sizeof(path), "/sys/class/net/%s/type", interface_name);
    int result = read_sys_file_int(path);
    if (result != -1) {
        type = result;
    }
    
    return type;
}

int get_interface_speed(const char *interface_name) {
    char path[256];
    FILE *fp;
    int speed = -1;
    
    snprintf(path, sizeof(path), "/sys/class/net/%s/speed", interface_name);
    speed = read_sys_file_int(path);
    
    // Pour le WiFi, la vitesse peut ne pas être disponible
    // Utiliser une estimation basée sur le type
    if (speed == -1) {
        int type = get_interface_type(interface_name);
        if (type == NET_TYPE_WIFI) {
            speed = 150; // Estimation WiFi 802.11n
        } else if (type == NET_TYPE_ETHERNET) {
            speed = 1000; // Estimation Gigabit Ethernet
        }
    }
    
    return speed;
}

int get_interface_pci_name(const char *iface, char *outbuf, size_t bufsize) {
    char linkpath[PATH_MAX];
    char pcipath[PATH_MAX];

    snprintf(linkpath, sizeof(linkpath), "/sys/class/net/%s/device", iface);
    ssize_t len = readlink(linkpath, pcipath, sizeof(pcipath) - 1);
    if (len == -1) {
        strncpy(outbuf, "Unknown network adapter", bufsize);
        outbuf[bufsize - 1] = '\0';
        return -1;
    }
    pcipath[len] = '\0';

    const char *pci_addr = strrchr(pcipath, '/');
    if (!pci_addr || strlen(pci_addr) < 2) {
        strncpy(outbuf, "Unknown network adapter", bufsize);
        outbuf[bufsize - 1] = '\0';
        return -1;
    }
    pci_addr++;

    struct pci_access *pacc = pci_alloc();
    pci_init(pacc);
    pci_scan_bus(pacc);

    for (struct pci_dev *dev = pacc->devices; dev; dev = dev->next) {
        pci_fill_info(dev, PCI_FILL_IDENT | PCI_FILL_CLASS);
        char addr[32];
        snprintf(addr, sizeof(addr), "%04x:%02x:%02x.%d",
                 dev->domain, dev->bus, dev->dev, dev->func);

        if (strcmp(addr, pci_addr) == 0) {
            pci_lookup_name(pacc, outbuf, bufsize,
                            PCI_LOOKUP_DEVICE, dev->vendor_id, dev->device_id);
            simplify_name(outbuf);
            pci_cleanup(pacc);
            return 0;
        }
    }

    pci_cleanup(pacc);
    strncpy(outbuf, "Unknown network adapter", bufsize);
    outbuf[bufsize - 1] = '\0';
    return -1;
}

static void get_network_stats(const char *interface_name, long *rx_bytes, long *tx_bytes) {
    char path[256];
    FILE *fp;
    
    *rx_bytes = 0;
    *tx_bytes = 0;
    
    // Lire rx_bytes
    snprintf(path, sizeof(path), "/sys/class/net/%s/statistics/rx_bytes", interface_name);
    long rx_result = read_sys_file_long(path);
    if (rx_result != -1) *rx_bytes = rx_result;
    
    // Lire tx_bytes
    snprintf(path, sizeof(path), "/sys/class/net/%s/statistics/tx_bytes", interface_name);
    long tx_result = read_sys_file_long(path);
    if (tx_result != -1) *tx_bytes = tx_result;
}

static void update_interface_state(network_info_t *iface) {
    char path[256];
    FILE *fp;
    
    // Mettre à jour l'état opérationnel
    snprintf(path, sizeof(path), "/sys/class/net/%s/operstate", iface->name);
    char state[16];
    if (read_sys_file_line(path, state, sizeof(state))) {
        if (strcmp(state, "up") == 0) {
            iface->flags |= NET_IS_UP;
        } else {
            iface->flags &= ~NET_IS_UP;
        }
    }
    
    // Mettre à jour le carrier
    snprintf(path, sizeof(path), "/sys/class/net/%s/carrier", iface->name);
    int carrier = read_sys_file_int(path);
    if (carrier != -1) {
        if (carrier == 1) {
            iface->flags |= NET_HAS_CARRIER;
        } else {
            iface->flags &= ~NET_HAS_CARRIER;
        }
    }
}

// Fonction de polling adaptatif pour optimiser les interfaces inactives
HOT_FUNCTION
void update_network_data(void) {
    if (UNLIKELY(!IS_ADAPTIVE_POLLING_ENABLED())) {
        // Fallback vers polling traditionnel si adaptatif désactivé
        update_network_data_traditional();
        return;
    }
    
    struct timespec current_time;
    clock_gettime(CLOCK_MONOTONIC, &current_time);
    
    for (int i = 0; i < network_manager.interface_count; i++) {
        adaptive_network_t *anet = &adaptive_networks[i];
        network_info_t *iface = anet->iface;
        
        // Incrémenter le compteur de cycles
        anet->cycles_since_poll++;
        
        // Vérifier si c'est le moment de poller cette interface
        if (anet->cycles_since_poll < anet->poll_frequency) {
            // Pas encore temps de poller - utiliser les dernières valeurs
            continue;
        }
        
        // Reset du compteur - on va poller maintenant
        anet->cycles_since_poll = 0;
        
        // Mettre à jour l'état de l'interface
        update_interface_state(iface);
        
        // Lire les statistiques actuelles
        long rx_bytes, tx_bytes;
        get_network_stats(iface->name, &rx_bytes, &tx_bytes);
        
        if (!(network_stats[i].flags & NET_INITIALIZED)) {
            // Premier échantillon - initialiser
            network_stats[i].prev_rx_bytes = rx_bytes;
            network_stats[i].prev_tx_bytes = tx_bytes;
            network_stats[i].prev_time = current_time;
            network_stats[i].flags |= NET_INITIALIZED;
            
            // Initialiser le système adaptatif
            anet->last_rx_bytes = rx_bytes;
            anet->last_tx_bytes = tx_bytes;
            
            // Valeurs par défaut
            iface->rx_samples[network_manager.current_index] = 0;
            iface->tx_samples[network_manager.current_index] = 0;
            iface->activity_samples[network_manager.current_index] = 0;
        } else {
            // Calculer l'activité depuis le dernier poll adaptatif
            long rx_activity = rx_bytes - anet->last_rx_bytes;
            long tx_activity = tx_bytes - anet->last_tx_bytes;
            long total_activity = rx_activity + tx_activity;
            
            // Calculer les différences pour les taux
            long rx_diff = rx_bytes - network_stats[i].prev_rx_bytes;
            long tx_diff = tx_bytes - network_stats[i].prev_tx_bytes;
            
            double time_diff = (current_time.tv_sec - network_stats[i].prev_time.tv_sec) +
                              (current_time.tv_nsec - network_stats[i].prev_time.tv_nsec) / 1e9;
            
            // Calculer les taux en KB/s
            gint rx_kbs = 0, tx_kbs = 0;
            if (time_diff > 0) {
                rx_kbs = (gint)(rx_diff / (time_diff * 1024));
                tx_kbs = (gint)(tx_diff / (time_diff * 1024));
                if (rx_kbs < 0) rx_kbs = 0;
                if (tx_kbs < 0) tx_kbs = 0;
            }
            
            // Calculer le pourcentage d'activité
            gint activity_percent = 0;
            if (iface->speed > 0 && time_diff > 0) {
                long max_kbs = (iface->speed * 1000) / 8 / 1024;
                if (max_kbs > 0) {
                    activity_percent = ((rx_kbs + tx_kbs) * 100) / max_kbs;
                    if (activity_percent > 100) activity_percent = 100;
                }
            }
            
            // Ajuster la fréquence de polling selon l'activité
            if (total_activity == 0) {
                // Interface complètement inactive
                anet->consecutive_inactive++;
                anet->activity_level = 0;
                
                // Réduire progressivement la fréquence (max 8x moins fréquent)
                if (anet->consecutive_inactive >= 3) {
                    anet->poll_frequency = MIN(anet->poll_frequency * 2, 8);
                }
            } else if (total_activity < 1024) {
                // Faible activité (< 1KB)
                anet->consecutive_inactive = 0;
                anet->activity_level = 1;
                anet->poll_frequency = 2;  // Poll tous les 2 cycles
            } else if (total_activity < 1024 * 1024) {
                // Activité modérée (< 1MB)
                anet->consecutive_inactive = 0;
                anet->activity_level = 2;
                anet->poll_frequency = 1;  // Poll à chaque cycle
            } else {
                // Activité élevée (>= 1MB)
                anet->consecutive_inactive = 0;
                anet->activity_level = 3;
                anet->poll_frequency = 1;  // Poll à chaque cycle
            }
            
            // Stocker les valeurs
            iface->rx_samples[network_manager.current_index] = rx_kbs;
            iface->tx_samples[network_manager.current_index] = tx_kbs;
            iface->activity_samples[network_manager.current_index] = activity_percent;
            
            // Sauvegarder pour les prochains calculs
            network_stats[i].prev_rx_bytes = rx_bytes;
            network_stats[i].prev_tx_bytes = tx_bytes;
            network_stats[i].prev_time = current_time;
            
            // Mettre à jour les valeurs de référence adaptatives
            anet->last_rx_bytes = rx_bytes;
            anet->last_tx_bytes = tx_bytes;
        }
    }
    
    // Avancer l'index du buffer circulaire
    network_manager.current_index = (network_manager.current_index + 1) % PERFORMANCE_SAMPLES_COUNT;
    if (network_manager.current_index == 0) {
        network_manager.flags |= MANAGER_BUFFER_FULL;
    }
}

// Version traditionnelle pour fallback
OPTIMIZE_SIZE
static void update_network_data_traditional(void) {
    struct timespec current_time;
    clock_gettime(CLOCK_MONOTONIC, &current_time);
    
    for (int i = 0; i < network_manager.interface_count; i++) {
        network_info_t *iface = &network_manager.interfaces[i];
        
        update_interface_state(iface);
        
        long rx_bytes, tx_bytes;
        get_network_stats(iface->name, &rx_bytes, &tx_bytes);
        
        if (!(network_stats[i].flags & NET_INITIALIZED)) {
            network_stats[i].prev_rx_bytes = rx_bytes;
            network_stats[i].prev_tx_bytes = tx_bytes;
            network_stats[i].prev_time = current_time;
            network_stats[i].flags |= NET_INITIALIZED;
            
            iface->rx_samples[network_manager.current_index] = 0;
            iface->tx_samples[network_manager.current_index] = 0;
            iface->activity_samples[network_manager.current_index] = 0;
        } else {
            long rx_diff = rx_bytes - network_stats[i].prev_rx_bytes;
            long tx_diff = tx_bytes - network_stats[i].prev_tx_bytes;
            
            double time_diff = (current_time.tv_sec - network_stats[i].prev_time.tv_sec) +
                              (current_time.tv_nsec - network_stats[i].prev_time.tv_nsec) / 1e9;
            
            gint rx_kbs = 0, tx_kbs = 0;
            if (time_diff > 0) {
                rx_kbs = (gint)(rx_diff / (time_diff * 1024));
                tx_kbs = (gint)(tx_diff / (time_diff * 1024));
                if (rx_kbs < 0) rx_kbs = 0;
                if (tx_kbs < 0) tx_kbs = 0;
            }
            
            gint activity_percent = 0;
            if (iface->speed > 0 && time_diff > 0) {
                long max_kbs = (iface->speed * 1000) / 8 / 1024;
                if (max_kbs > 0) {
                    activity_percent = ((rx_kbs + tx_kbs) * 100) / max_kbs;
                    if (activity_percent > 100) activity_percent = 100;
                }
            }
            
            iface->rx_samples[network_manager.current_index] = rx_kbs;
            iface->tx_samples[network_manager.current_index] = tx_kbs;
            iface->activity_samples[network_manager.current_index] = activity_percent;
            
            network_stats[i].prev_rx_bytes = rx_bytes;
            network_stats[i].prev_tx_bytes = tx_bytes;
            network_stats[i].prev_time = current_time;
        }
    }
    
    network_manager.current_index = (network_manager.current_index + 1) % PERFORMANCE_SAMPLES_COUNT;
    if (network_manager.current_index == 0) {
        network_manager.flags |= MANAGER_BUFFER_FULL;
    }
}

int get_network_count(void) {
    return network_manager.interface_count;
}

network_info_t *get_network_info(int index) {
    if (index < 0 || index >= network_manager.interface_count) {
        return NULL;
    }
    return &network_manager.interfaces[index];
}

void set_selected_network(int index) {
    if (index < -1 || index >= network_manager.interface_count) {
        return;
    }
    network_manager.selected_interface_index = index;
}

// Détermine si l'interface est WiFi ou Ethernet (adapté depuis interface_info.c)
interface_connection_type_t get_interface_connection_type(const char* interface_name) {
    char path[256];
    
    // Vérifier si c'est une interface wireless via sysfs
    snprintf(path, sizeof(path), "/sys/class/net/%s/wireless", interface_name);
    if (access(path, F_OK) == 0) {
        return INTERFACE_TYPE_WIFI;
    }
    
    return INTERFACE_TYPE_ETHERNET;
}

// Récupère les adresses IP de l'interface (adapté depuis interface_info.c)
int get_interface_addresses(const char* interface_name, interface_details_t* details) {
    struct ifaddrs *ifaddrs_ptr, *ifa;
    
    // Initialiser les chaînes vides
    details->ipv4[0] = '\0';
    details->ipv6[0] = '\0';
    
    if (getifaddrs(&ifaddrs_ptr) == -1) {
        return -1;
    }
    
    // Parcourir toutes les interfaces
    for (ifa = ifaddrs_ptr; ifa != NULL; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr == NULL) continue;
        
        // Vérifier si c'est l'interface recherchée
        if (strcmp(ifa->ifa_name, interface_name) != 0) continue;
        
        // Traiter selon la famille d'adresses
        if (ifa->ifa_addr->sa_family == AF_INET) {
            // IPv4
            struct sockaddr_in* addr_in = (struct sockaddr_in*)ifa->ifa_addr;
            if (inet_ntop(AF_INET, &(addr_in->sin_addr), details->ipv4, 16) == NULL) {
                details->ipv4[0] = '\0';
            }
        } else if (ifa->ifa_addr->sa_family == AF_INET6) {
            // IPv6
            struct sockaddr_in6* addr_in6 = (struct sockaddr_in6*)ifa->ifa_addr;
            
            // Ignorer les adresses link-local (commencent par fe80::)
            if (!IN6_IS_ADDR_LINKLOCAL(&addr_in6->sin6_addr)) {
                if (inet_ntop(AF_INET6, &(addr_in6->sin6_addr), details->ipv6, 46) == NULL) {
                    details->ipv6[0] = '\0';
                }
            }
        }
    }
    
    freeifaddrs(ifaddrs_ptr);
    return 0;
}

// Récupérer l'adresse MAC d'une interface réseau
int get_mac_address(const char *iface, char *mac_str) {
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) return -1;

    struct ifreq ifr;
    strncpy(ifr.ifr_name, iface, IFNAMSIZ - 1);
    ifr.ifr_name[IFNAMSIZ - 1] = '\0';

    if (ioctl(fd, SIOCGIFHWADDR, &ifr) != 0) {
        close(fd);
        return -1;
    }

    unsigned char *mac = (unsigned char *)ifr.ifr_hwaddr.sa_data;
    snprintf(mac_str, 18, "%02X:%02X:%02X:%02X:%02X:%02X",
            mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    close(fd);
    return 0;
}

// Récupérer le SSID WiFi d'une interface (retourne -1 si non-WiFi ou erreur)
int get_wifi_ssid(const char *iface, char *ssid, size_t len) {
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) return -1;

    struct iwreq req;
    memset(&req, 0, sizeof(struct iwreq));
    strncpy(req.ifr_name, iface, IFNAMSIZ - 1);

    req.u.essid.pointer = ssid;
    req.u.essid.length = len;
    req.u.essid.flags = 0;

    if (ioctl(sockfd, SIOCGIWESSID, &req) == -1) {
        close(sockfd);
        return -1;
    }

    ssid[req.u.essid.length] = '\0'; // Assure la terminaison
    close(sockfd);
    return 0;
}

// Fonction principale avec cache intelligent (optimisation -98% syscalls)
int get_interface_details(const char* interface_name, interface_details_t* details) {
    if (!IS_NETWORK_CACHE_INITIALIZED()) init_interface_cache();
    
    int cache_idx = find_cache_index(interface_name);
    if (cache_idx < 0) {
        // Interface non trouvée dans le manager, mode fallback sans cache
        details->type = get_interface_connection_type(interface_name);
        int result = get_interface_addresses(interface_name, details);
        
        if (get_mac_address(interface_name, details->mac) != 0) {
            strcpy(details->mac, "Not available");
        }
        
        if (details->type == INTERFACE_TYPE_WIFI) {
            if (get_wifi_ssid(interface_name, details->ssid, sizeof(details->ssid)) != 0) {
                details->ssid[0] = '\0';
            }
        } else {
            details->ssid[0] = '\0';
        }
        
        return result;
    }
    
    time_t now = time(NULL);
    interface_cache_entry_t* cache = &interface_cache[cache_idx];
    
    // 1. TYPE d'interface (cache permanent)
    if (cache->type == INTERFACE_TYPE_ETHERNET && cache->mac_cached) {
        details->type = cache->type;
    } else {
        details->type = get_interface_connection_type(interface_name);
        cache->type = details->type;
    }
    
    // 2. MAC ADDRESS (cache permanent - ne change jamais)
    if (cache->mac_cached && strlen(cache->mac) > 0) {
        strcpy(details->mac, cache->mac);
    } else {
        if (get_mac_address(interface_name, details->mac) == 0) {
            strcpy(cache->mac, details->mac);
            cache->mac_cached = TRUE;
        } else {
            strcpy(details->mac, "Not available");
        }
    }
    
    // 3. SSID WiFi (cache 10 secondes)
    if (details->type == INTERFACE_TYPE_WIFI) {
        if (cache->ssid_timestamp > 0 && (now - cache->ssid_timestamp) < 10) {
            // Cache valide, réutiliser
            strcpy(details->ssid, cache->ssid);
        } else {
            // Cache expiré ou premier appel, rafraîchir
            if (get_wifi_ssid(interface_name, details->ssid, sizeof(details->ssid)) == 0) {
                strcpy(cache->ssid, details->ssid);
                cache->ssid_timestamp = now;
            } else {
                details->ssid[0] = '\0';
                cache->ssid[0] = '\0';
                cache->ssid_timestamp = now;
            }
        }
    } else {
        details->ssid[0] = '\0';
    }
    
    // 4. IPv4/IPv6 (cache 2 secondes - DHCP peut changer)
    if (cache->ip_timestamp > 0 && (now - cache->ip_timestamp) < 2) {
        // Cache IP valide, réutiliser
        strcpy(details->ipv4, cache->ipv4);
        strcpy(details->ipv6, cache->ipv6);
        return 0;
    } else {
        // Cache IP expiré, rafraîchir
        int result = get_interface_addresses(interface_name, details);
        strcpy(cache->ipv4, details->ipv4);
        strcpy(cache->ipv6, details->ipv6);
        cache->ip_timestamp = now;
        return result;
    }
}

const char* interface_connection_type_to_string(interface_connection_type_t type) {
    return (type == INTERFACE_TYPE_WIFI) ? "WiFi" : "Ethernet";
}

int get_selected_network_index(void) {
    return network_manager.selected_interface_index;
}
