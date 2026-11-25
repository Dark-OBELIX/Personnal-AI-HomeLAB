/*
 * Contrôle du bandeau : état, presets, sauvegarde en NVS et rendu avec fondu.
 * Toutes les fonctions sont utilisables depuis n'importe quelle tâche (serveur web, MQTT...).
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define LED_MAX_PRESETS 12

typedef struct {
    bool on;
    uint8_t r, g, b;
    uint8_t brightness;     /* 1 à 255 */
    uint8_t active_preset;  /* 0 = aucun, sinon numéro du preset (1..LED_MAX_PRESETS) */
} led_state_t;

typedef struct {
    bool used;
    uint8_t r, g, b;
    uint8_t brightness;
} led_preset_info_t;

/* Commande partielle : seuls les champs has_* à true sont appliqués. */
typedef struct {
    bool has_on;
    bool on;
    bool has_color;
    uint8_t r, g, b;
    bool has_brightness;
    uint8_t brightness;
    int32_t transition_ms;  /* -1 : fondu par défaut (DEFAULT_TRANSITION_MS) */
} led_cmd_t;

#define LED_CMD_INIT { .transition_ms = -1 }

typedef void (*led_listener_t)(void);

/* Restaure le dernier réglage et allume le bandeau en conséquence (avant même le Wi-Fi). */
void led_ctrl_init(void);

/* Copie de l'état courant ; presets peut être NULL. */
void led_ctrl_get(led_state_t *state, led_preset_info_t presets[LED_MAX_PRESETS]);

void led_ctrl_apply(const led_cmd_t *cmd);

/* Page web et URL /presetN : lance le preset, ou éteint s'il est déjà actif. 0 si OK, -1 sinon. */
int led_ctrl_preset_toggle(int id);
/* Home Assistant : applique toujours le preset. 0 si OK, -1 sinon. */
int led_ctrl_preset_apply(int id);
/* Enregistre la couleur et la luminosité actuelles. Retourne le numéro du preset, ou -1 si plein. */
int led_ctrl_preset_add(void);
int led_ctrl_preset_delete(int id);

/* Appelés après chaque changement d'état, ou de la liste des presets (hors verrou). */
void led_ctrl_set_listeners(led_listener_t on_state_changed, led_listener_t on_presets_changed);
