/* Wi-Fi (station), nom réseau et mDNS. */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include "freertos/FreeRTOS.h"

/* Démarre le Wi-Fi et la reconnexion automatique. */
void net_start(void);

/* Attend une adresse IP. Retourne false si le délai est dépassé. */
bool net_wait_connected(TickType_t timeout);

/* Adresse IP actuelle (chaîne vide si pas connecté). */
void net_get_ip(char *buf, size_t len);

/* Force du signal Wi-Fi en dBm (0 si inconnue). */
int net_get_rssi(void);
