#ifndef NETWORK_MANAGER_H
#define NETWORK_MANAGER_H

#include <glib.h>

#include "types.h"

#define MAX_NETWORK_INTERFACES 16
#define INTERFACE_NAME_MAX 16
#define INTERFACE_DESC_MAX 64

// Types d'interfaces réseau
#define NET_TYPE_ETHERNET 1
#define NET_TYPE_WIFI 801
#define NET_TYPE_LOOPBACK 772

typedef struct {
    char name[INTERFACE_NAME_MAX];           // nom interface (eth0, wlan0)
    char description[INTERFACE_DESC_MAX];    // description/driver
    int type;                               // 1=Ethernet, 801=WiFi, 772=Loopback
    guint8 flags;                           // NET_IS_UP, NET_HAS_CARRIER, NET_IS_PHYSICAL
    int speed;                              // vitesse en Mbps (-1 si N/A)
    
    // Buffers circulaires pour les graphiques
    gint rx_samples[PERFORMANCE_SAMPLES_COUNT];      // KB/s reçus
    gint tx_samples[PERFORMANCE_SAMPLES_COUNT];      // KB/s transmis
    gint activity_samples[PERFORMANCE_SAMPLES_COUNT]; // % activité
} network_info_t;

// Structure pour le polling adaptatif des interfaces réseau
typedef struct {
    network_info_t *iface;          // Pointeur vers l'interface
    gint poll_frequency;            // 1=chaque cycle, 2=tous les 2 cycles, etc.
    gint cycles_since_poll;         // Compteur de cycles depuis dernier poll
    long last_rx_bytes;             // Dernière valeur RX pour calcul activité
    long last_tx_bytes;             // Dernière valeur TX pour calcul activité
    guint8 activity_level;          // 0=inactive, 1=faible, 2=modérée, 3=élevée
    guint8 consecutive_inactive;    // Compteur cycles consécutifs inactifs
} adaptive_network_t;

typedef struct {
    network_info_t interfaces[MAX_NETWORK_INTERFACES];
    int interface_count;
    int current_index;
    guint8 flags;                           // MANAGER_BUFFER_FULL
    int selected_interface_index;
} network_manager_t;

// Variable globale pour accès depuis interface.c
extern network_manager_t network_manager;

// Fonctions publiques
void init_network_manager(void);
void cleanup_network_manager(void);
void update_network_data(void);

int get_network_count(void);
network_info_t *get_network_info(int index);
void set_selected_network(int index);
int get_selected_network_index(void);

// Fonctions utilitaires
gboolean is_physical_interface(const char *interface_name);
int get_interface_type(const char *interface_name);
int get_interface_speed(const char *interface_name);
int get_interface_pci_name(const char *iface, char *outbuf, size_t bufsize);

// Nouvelles fonctions pour récupérer les informations d'interface
typedef enum {
    INTERFACE_TYPE_ETHERNET = 0,
    INTERFACE_TYPE_WIFI
} interface_connection_type_t;

typedef struct {
    interface_connection_type_t type;
    char ipv4[16];  // INET_ADDRSTRLEN
    char ipv6[46];  // INET6_ADDRSTRLEN
    char mac[18];   // XX:XX:XX:XX:XX:XX + null
    char ssid[33];  // Max SSID length (32) + null
} interface_details_t;

int get_interface_details(const char* interface_name, interface_details_t* details);
int get_mac_address(const char *iface, char *mac_str);
int get_wifi_ssid(const char *iface, char *ssid, size_t len);
const char* interface_connection_type_to_string(interface_connection_type_t type);

#endif // NETWORK_MANAGER_H
