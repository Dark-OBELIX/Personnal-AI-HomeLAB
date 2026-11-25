/*
 * Configuration du firmware.
 * Les valeurs viennent de ../.env (via env_config.h, généré à la compilation) ;
 * celles absentes du .env prennent les valeurs par défaut ci-dessous.
 */
#pragma once

#include "env_config.h"

/* ── Identité ── */
#ifndef CFG_DEVICE_NAME
#define CFG_DEVICE_NAME "Bandeau LED"
#endif
#ifndef CFG_DEVICE_ID
#define CFG_DEVICE_ID "bandeau_led"
#endif
#ifndef CFG_DEVICE_HOSTNAME
#define CFG_DEVICE_HOSTNAME "led-bandeau"
#endif

/* ── Bandeau ── */
#ifndef CFG_LED_GPIO
#define CFG_LED_GPIO 22
#endif
#ifndef CFG_LED_COUNT
#define CFG_LED_COUNT 150
#endif
#ifndef CFG_LED_MODEL
#define CFG_LED_MODEL "WS2812"
#endif
#ifndef CFG_LED_COLOR_ORDER
#define CFG_LED_COLOR_ORDER "GRB"
#endif
#ifndef CFG_LED_MAX_BRIGHTNESS
#define CFG_LED_MAX_BRIGHTNESS 255
#endif
#ifndef CFG_LED_GAMMA
#define CFG_LED_GAMMA 0
#endif
#ifndef CFG_DEFAULT_TRANSITION_MS
#define CFG_DEFAULT_TRANSITION_MS 300
#endif

/* ── MQTT / Home Assistant ── */
#ifndef CFG_MQTT_URI
#define CFG_MQTT_URI ""            /* vide : MQTT désactivé */
#endif
#ifndef CFG_MQTT_USER
#define CFG_MQTT_USER ""
#endif
#ifndef CFG_MQTT_PASSWORD
#define CFG_MQTT_PASSWORD ""
#endif
#ifndef CFG_MQTT_BASE_TOPIC
#define CFG_MQTT_BASE_TOPIC "led/" CFG_DEVICE_ID
#endif
#ifndef CFG_HA_DISCOVERY_PREFIX
#define CFG_HA_DISCOVERY_PREFIX "homeassistant"
#endif

/* ── Mise à jour par le Wi-Fi ── */
#ifndef CFG_OTA_PASSWORD
#define CFG_OTA_PASSWORD ""        /* vide : OTA désactivée */
#endif

/* ── Vérifications à la compilation ── */
_Static_assert(sizeof(CFG_WIFI_SSID) - 1 <= 32, "WIFI_SSID : 32 caractères maximum");
_Static_assert(sizeof(CFG_WIFI_PASSWORD) - 1 <= 63, "WIFI_PASSWORD : 63 caractères maximum");
_Static_assert(CFG_LED_COUNT >= 1 && CFG_LED_COUNT <= 2000, "LED_COUNT : entre 1 et 2000");
_Static_assert(CFG_LED_GPIO >= 0 && CFG_LED_GPIO <= 39, "LED_GPIO : broche invalide pour un ESP32");
_Static_assert(CFG_LED_MAX_BRIGHTNESS >= 1 && CFG_LED_MAX_BRIGHTNESS <= 255, "LED_MAX_BRIGHTNESS : entre 1 et 255");
_Static_assert(CFG_DEFAULT_TRANSITION_MS <= 60000, "DEFAULT_TRANSITION_MS : 60000 maximum");
_Static_assert(sizeof(CFG_DEVICE_HOSTNAME) - 1 <= 32, "DEVICE_HOSTNAME : 32 caractères maximum");
