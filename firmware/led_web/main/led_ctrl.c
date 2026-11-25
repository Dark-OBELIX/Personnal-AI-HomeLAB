#include <math.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs.h"
#include "led_strip.h"
#include "app_config.h"
#include "led_ctrl.h"

static const char *TAG = "led_ctrl";

/* Délai avant d'écrire en flash après le dernier changement (ménage la flash quand on glisse un curseur) */
#define SAVE_DELAY_US     (2 * 1000 * 1000)
/* Période d'animation pendant un fondu (50 images/s) */
#define FRAME_MS          20

/* Formats stockés en NVS : identiques à la version précédente du firmware (réglages conservés). */
typedef struct {
    uint8_t on;
    uint8_t r, g, b;
    uint8_t brightness;
    uint8_t active_preset;
} nvs_settings_t;

typedef struct {
    uint8_t used;
    uint8_t r, g, b;
    uint8_t brightness;
} nvs_preset_t;

static nvs_settings_t s_set = { .on = 0, .r = 255, .g = 255, .b = 255, .brightness = 255 };
static nvs_preset_t s_presets[LED_MAX_PRESETS];

static SemaphoreHandle_t s_lock;
static esp_timer_handle_t s_save_timer;
static led_strip_handle_t s_strip;
static TaskHandle_t s_render_task;
static led_listener_t s_on_state;
static led_listener_t s_on_presets;

/* Fondu : couleur affichée (s_cur) qui va de s_from vers s_to en s_fade_ms */
static float s_cur[3], s_from[3], s_to[3];
static int64_t s_fade_start_us;
static uint32_t s_fade_ms;

static uint8_t s_order[3];      /* canal (0=R, 1=G, 2=B) envoyé en 1re, 2e et 3e position sur le fil */
static uint8_t s_gamma[256];

#define LOCK()   xSemaphoreTake(s_lock, portMAX_DELAY)
#define UNLOCK() xSemaphoreGive(s_lock)

/* ---------- Paramètres matériels ---------- */

static void parse_color_order(void)
{
    const char *o = CFG_LED_COLOR_ORDER;
    bool ok = strlen(o) == 3;
    for (int i = 0; ok && i < 3; i++) {
        switch (o[i]) {
        case 'R': case 'r': s_order[i] = 0; break;
        case 'G': case 'g': s_order[i] = 1; break;
        case 'B': case 'b': s_order[i] = 2; break;
        default: ok = false;
        }
    }
    if (ok && s_order[0] != s_order[1] && s_order[0] != s_order[2] && s_order[1] != s_order[2]) {
        return;
    }
    ESP_LOGW(TAG, "LED_COLOR_ORDER invalide (\"%s\"), GRB utilisé", o);
    s_order[0] = 1;
    s_order[1] = 0;
    s_order[2] = 2;
}

static void build_gamma(void)
{
    for (int i = 0; i < 256; i++) {
        s_gamma[i] = CFG_LED_GAMMA ? (uint8_t)(powf(i / 255.0f, 2.2f) * 255.0f + 0.5f) : (uint8_t)i;
    }
}

static void strip_init(void)
{
    led_strip_config_t strip_config = {
        .strip_gpio_num = CFG_LED_GPIO,
        .max_leds = CFG_LED_COUNT,
        /* Format fixe GRB : l'ordre réel est géré par LED_COLOR_ORDER dans strip_write() */
        .led_pixel_format = LED_PIXEL_FORMAT_GRB,
        .led_model = strcmp(CFG_LED_MODEL, "SK6812") == 0 ? LED_MODEL_SK6812 : LED_MODEL_WS2812,
        .flags.invert_out = false,
    };
    led_strip_rmt_config_t rmt_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10 * 1000 * 1000,
        .flags.with_dma = false,
    };
    ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_config, &rmt_config, &s_strip));
    led_strip_clear(s_strip);
    ESP_LOGI(TAG, "Bandeau : %d pixels %s (%s) sur GPIO %d",
             CFG_LED_COUNT, CFG_LED_MODEL, CFG_LED_COLOR_ORDER, CFG_LED_GPIO);
}

/* Couleur unie sur tout le bandeau. px[] est en R, G, B. */
static void strip_write(const uint8_t px[3])
{
    /* Avec le format GRB, le pilote envoie (green, red, blue) dans cet ordre :
     * on place donc dans chaque argument le canal attendu à cette position. */
    uint8_t first = px[s_order[0]], second = px[s_order[1]], third = px[s_order[2]];
    for (int i = 0; i < CFG_LED_COUNT; i++) {
        led_strip_set_pixel(s_strip, i, second, first, third);
    }
    led_strip_refresh(s_strip);
}

/* ---------- Rendu ---------- */

static void compute_target_locked(float out[3])
{
    if (!s_set.on) {
        out[0] = out[1] = out[2] = 0.0f;
        return;
    }
    float k = (s_set.brightness / 255.0f) * (CFG_LED_MAX_BRIGHTNESS / 255.0f);
    out[0] = s_set.r * k;
    out[1] = s_set.g * k;
    out[2] = s_set.b * k;
}

static void start_fade_locked(int32_t transition_ms)
{
    memcpy(s_from, s_cur, sizeof(s_cur));
    compute_target_locked(s_to);
    s_fade_start_us = esp_timer_get_time();
    s_fade_ms = transition_ms < 0 ? CFG_DEFAULT_TRANSITION_MS : (uint32_t)transition_ms;
}

static void render_task(void *arg)
{
    int last[3] = { -1, -1, -1 };
    bool animating = true;

    for (;;) {
        ulTaskNotifyTake(pdTRUE, animating ? pdMS_TO_TICKS(FRAME_MS) : portMAX_DELAY);

        uint8_t px[3];
        LOCK();
        int64_t elapsed_ms = (esp_timer_get_time() - s_fade_start_us) / 1000;
        if (s_fade_ms == 0 || elapsed_ms >= (int64_t)s_fade_ms) {
            memcpy(s_cur, s_to, sizeof(s_cur));
            animating = false;
        } else {
            float t = (float)elapsed_ms / (float)s_fade_ms;
            for (int c = 0; c < 3; c++) {
                s_cur[c] = s_from[c] + (s_to[c] - s_from[c]) * t;
            }
            animating = true;
        }
        for (int c = 0; c < 3; c++) {
            float v = s_cur[c] < 0.0f ? 0.0f : (s_cur[c] > 255.0f ? 255.0f : s_cur[c]);
            px[c] = s_gamma[(uint8_t)(v + 0.5f)];
        }
        UNLOCK();

        if (px[0] != last[0] || px[1] != last[1] || px[2] != last[2]) {
            strip_write(px);
            for (int c = 0; c < 3; c++) {
                last[c] = px[c];
            }
        }
    }
}

/* ---------- Sauvegarde NVS ---------- */

static void settings_load(void)
{
    nvs_handle_t nvs;
    if (nvs_open("led", NVS_READONLY, &nvs) != ESP_OK) {
        ESP_LOGI(TAG, "Aucun réglage enregistré, valeurs par défaut");
        return;
    }
    nvs_settings_t loaded;
    size_t len = sizeof(loaded);
    if (nvs_get_blob(nvs, "settings", &loaded, &len) == ESP_OK && len == sizeof(loaded)) {
        s_set = loaded;
        if (s_set.brightness == 0) {
            s_set.brightness = 255;
        }
        ESP_LOGI(TAG, "Réglage restauré : on=%d couleur=%02x%02x%02x lum=%d",
                 s_set.on, s_set.r, s_set.g, s_set.b, s_set.brightness);
    }
    len = sizeof(s_presets);
    if (nvs_get_blob(nvs, "presets", s_presets, &len) != ESP_OK || len != sizeof(s_presets)) {
        memset(s_presets, 0, sizeof(s_presets));
    }
    nvs_close(nvs);
}

static void settings_save_cb(void *arg)
{
    nvs_settings_t copy;
    LOCK();
    copy = s_set;
    UNLOCK();

    nvs_handle_t nvs;
    if (nvs_open("led", NVS_READWRITE, &nvs) != ESP_OK) {
        ESP_LOGE(TAG, "Impossible d'ouvrir la NVS");
        return;
    }
    nvs_set_blob(nvs, "settings", &copy, sizeof(copy));
    nvs_commit(nvs);
    nvs_close(nvs);
    ESP_LOGI(TAG, "Réglage enregistré");
}

static void presets_save(void)
{
    nvs_preset_t copy[LED_MAX_PRESETS];
    LOCK();
    memcpy(copy, s_presets, sizeof(copy));
    UNLOCK();

    nvs_handle_t nvs;
    if (nvs_open("led", NVS_READWRITE, &nvs) != ESP_OK) {
        ESP_LOGE(TAG, "Impossible d'ouvrir la NVS");
        return;
    }
    nvs_set_blob(nvs, "presets", copy, sizeof(copy));
    nvs_commit(nvs);
    nvs_close(nvs);
}

/* À appeler hors verrou après chaque modification de l'état */
static void state_changed(void)
{
    xTaskNotifyGive(s_render_task);
    esp_timer_stop(s_save_timer);   /* relance le délai à chaque changement */
    esp_timer_start_once(s_save_timer, SAVE_DELAY_US);
    if (s_on_state) {
        s_on_state();
    }
}

static void presets_changed(void)
{
    presets_save();
    if (s_on_presets) {
        s_on_presets();
    }
}

/* ---------- API ---------- */

void led_ctrl_init(void)
{
    s_lock = xSemaphoreCreateMutex();
    parse_color_order();
    build_gamma();
    settings_load();

    const esp_timer_create_args_t timer_args = {
        .callback = settings_save_cb,
        .name = "led_save",
    };
    ESP_ERROR_CHECK(esp_timer_create(&timer_args, &s_save_timer));

    strip_init();

    /* Premier affichage immédiat, sans fondu */
    compute_target_locked(s_to);
    memcpy(s_cur, s_to, sizeof(s_cur));
    memcpy(s_from, s_to, sizeof(s_from));
    s_fade_ms = 0;

    xTaskCreate(render_task, "led_render", 3072, NULL, 5, &s_render_task);
}

void led_ctrl_set_listeners(led_listener_t on_state_changed, led_listener_t on_presets_changed)
{
    s_on_state = on_state_changed;
    s_on_presets = on_presets_changed;
}

void led_ctrl_get(led_state_t *state, led_preset_info_t presets[LED_MAX_PRESETS])
{
    LOCK();
    if (state) {
        state->on = s_set.on;
        state->r = s_set.r;
        state->g = s_set.g;
        state->b = s_set.b;
        state->brightness = s_set.brightness;
        state->active_preset = s_set.active_preset;
    }
    if (presets) {
        for (int i = 0; i < LED_MAX_PRESETS; i++) {
            presets[i].used = s_presets[i].used;
            presets[i].r = s_presets[i].r;
            presets[i].g = s_presets[i].g;
            presets[i].b = s_presets[i].b;
            presets[i].brightness = s_presets[i].brightness;
        }
    }
    UNLOCK();
}

void led_ctrl_apply(const led_cmd_t *cmd)
{
    LOCK();
    if (cmd->has_on) {
        s_set.on = cmd->on ? 1 : 0;
    }
    if (cmd->has_color) {
        s_set.r = cmd->r;
        s_set.g = cmd->g;
        s_set.b = cmd->b;
        s_set.active_preset = 0;    /* réglage manuel : plus de preset actif */
    }
    if (cmd->has_brightness) {
        s_set.brightness = cmd->brightness ? cmd->brightness : 1;
        s_set.active_preset = 0;
    }
    start_fade_locked(cmd->transition_ms);
    UNLOCK();
    state_changed();
}

static void load_preset_locked(int id)
{
    const nvs_preset_t *p = &s_presets[id - 1];
    s_set.on = 1;
    s_set.r = p->r;
    s_set.g = p->g;
    s_set.b = p->b;
    s_set.brightness = p->brightness ? p->brightness : 1;
    s_set.active_preset = id;
}

static int preset_set(int id, bool toggle)
{
    if (id < 1 || id > LED_MAX_PRESETS) {
        return -1;
    }
    LOCK();
    if (!s_presets[id - 1].used) {
        UNLOCK();
        return -1;
    }
    if (toggle && s_set.on && s_set.active_preset == id) {
        s_set.on = 0;
        ESP_LOGI(TAG, "Preset %d : extinction", id);
    } else {
        load_preset_locked(id);
        ESP_LOGI(TAG, "Preset %d lancé", id);
    }
    start_fade_locked(-1);
    UNLOCK();
    state_changed();
    return 0;
}

int led_ctrl_preset_toggle(int id)
{
    return preset_set(id, true);
}

int led_ctrl_preset_apply(int id)
{
    return preset_set(id, false);
}

int led_ctrl_preset_add(void)
{
    int id = -1;
    LOCK();
    for (int i = 0; i < LED_MAX_PRESETS; i++) {
        if (!s_presets[i].used) {
            s_presets[i] = (nvs_preset_t) {
                .used = 1,
                .r = s_set.r,
                .g = s_set.g,
                .b = s_set.b,
                .brightness = s_set.brightness,
            };
            s_set.active_preset = i + 1;
            id = i + 1;
            break;
        }
    }
    UNLOCK();
    if (id < 0) {
        ESP_LOGW(TAG, "Plus de place pour un preset");
        return -1;
    }
    ESP_LOGI(TAG, "Preset %d enregistré", id);
    presets_changed();
    state_changed();
    return id;
}

int led_ctrl_preset_delete(int id)
{
    if (id < 1 || id > LED_MAX_PRESETS) {
        return -1;
    }
    LOCK();
    s_presets[id - 1].used = 0;
    if (s_set.active_preset == id) {
        s_set.active_preset = 0;
    }
    UNLOCK();
    ESP_LOGI(TAG, "Preset %d supprimé", id);
    presets_changed();
    state_changed();
    return 0;
}
