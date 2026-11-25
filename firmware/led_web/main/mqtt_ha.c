/*
 * Topics (BASE = MQTT_BASE_TOPIC, PFX = HA_DISCOVERY_PREFIX, ID = DEVICE_ID) :
 *
 *   BASE/status          online / offline (testament MQTT), conservé
 *   BASE/state           {"state":"ON","brightness":200,"color_mode":"rgb","color":{"r":..,"g":..,"b":..}}, conservé
 *   BASE/set             commande au même format, + "transition" en secondes
 *   BASE/preset/state    "Aucun" ou "Preset N", conservé
 *   BASE/preset/set      "Preset N" (ou "N")
 *   BASE/preset/save     n'importe quel message : enregistre la couleur actuelle comme preset
 *   BASE/diag            {"rssi":-55,"ip":"192.168.1.42","uptime":3600}
 *
 *   PFX/<composant>/ID/<objet>/config   annonces de découverte, conservées
 *   PFX/status                          « online » quand Home Assistant redémarre : on se ré-annonce
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cJSON.h"
#include "esp_app_desc.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "mqtt_client.h"
#include "app_config.h"
#include "led_ctrl.h"
#include "net.h"
#include "mqtt_ha.h"

static const char *TAG = "mqtt_ha";

#define DIAG_PERIOD_US  (60LL * 1000 * 1000)
#define NO_PRESET       "Aucun"

static esp_mqtt_client_handle_t s_client;
static volatile bool s_connected;
static esp_timer_handle_t s_diag_timer;

static char t_status[128], t_state[128], t_set[128];
static char t_preset_state[128], t_preset_set[128], t_preset_save[128];
static char t_diag[128], t_ha_status[128];

/* ---------- Publication ---------- */

static void publish_str(const char *topic, const char *payload, bool retain)
{
    if (!s_connected) {
        return;
    }
    /* enqueue : ne bloque pas la tâche appelante (serveur web, tâche MQTT...) */
    esp_mqtt_client_enqueue(s_client, topic, payload, 0, 1, retain, true);
}

static void publish_json(const char *topic, cJSON *json, bool retain)
{
    char *payload = cJSON_PrintUnformatted(json);
    if (payload) {
        publish_str(topic, payload, retain);
        cJSON_free(payload);
    }
    cJSON_Delete(json);
}

static void publish_state(void)
{
    led_state_t st;
    led_ctrl_get(&st, NULL);

    cJSON *j = cJSON_CreateObject();
    cJSON_AddStringToObject(j, "state", st.on ? "ON" : "OFF");
    cJSON_AddNumberToObject(j, "brightness", st.brightness);
    cJSON_AddStringToObject(j, "color_mode", "rgb");
    cJSON *color = cJSON_AddObjectToObject(j, "color");
    cJSON_AddNumberToObject(color, "r", st.r);
    cJSON_AddNumberToObject(color, "g", st.g);
    cJSON_AddNumberToObject(color, "b", st.b);
    publish_json(t_state, j, true);

    char preset[16];
    if (st.active_preset) {
        snprintf(preset, sizeof(preset), "Preset %d", st.active_preset);
    } else {
        snprintf(preset, sizeof(preset), NO_PRESET);
    }
    publish_str(t_preset_state, preset, true);
}

static void publish_diag(void)
{
    char ip[16];
    char payload[96];
    net_get_ip(ip, sizeof(ip));
    snprintf(payload, sizeof(payload), "{\"rssi\":%d,\"ip\":\"%s\",\"uptime\":%lld}",
             net_get_rssi(), ip, (long long)(esp_timer_get_time() / 1000000));
    publish_str(t_diag, payload, false);
}

static void diag_timer_cb(void *arg)
{
    publish_diag();
}

/* ---------- Découverte Home Assistant ---------- */

static cJSON *device_json(void)
{
    char model[48], url[40], ip[16];
    snprintf(model, sizeof(model), "ESP32 + %d pixels %s", CFG_LED_COUNT, CFG_LED_MODEL);
    net_get_ip(ip, sizeof(ip));

    cJSON *d = cJSON_CreateObject();
    cJSON *ids = cJSON_AddArrayToObject(d, "identifiers");
    cJSON_AddItemToArray(ids, cJSON_CreateString(CFG_DEVICE_ID));
    cJSON_AddStringToObject(d, "name", CFG_DEVICE_NAME);
    cJSON_AddStringToObject(d, "manufacturer", "DIY");
    cJSON_AddStringToObject(d, "model", model);
    cJSON_AddStringToObject(d, "sw_version", esp_app_get_description()->version);
    if (ip[0]) {
        snprintf(url, sizeof(url), "http://%s/", ip);
        cJSON_AddStringToObject(d, "configuration_url", url);
    }
    return d;
}

/* Champs communs : identifiant unique, disponibilité, appareil */
static cJSON *entity_json(const char *name, const char *object)
{
    char uid[96];
    snprintf(uid, sizeof(uid), "%s_%s", CFG_DEVICE_ID, object);

    cJSON *j = cJSON_CreateObject();
    if (name) {
        cJSON_AddStringToObject(j, "name", name);
    } else {
        cJSON_AddNullToObject(j, "name");   /* entité principale : prend le nom de l'appareil */
    }
    cJSON_AddStringToObject(j, "unique_id", uid);
    cJSON_AddStringToObject(j, "availability_topic", t_status);
    cJSON_AddItemToObject(j, "device", device_json());
    return j;
}

static void discovery_topic(char *out, size_t len, const char *component, const char *object)
{
    snprintf(out, len, "%s/%s/%s/%s/config", CFG_HA_DISCOVERY_PREFIX, component, CFG_DEVICE_ID, object);
}

static void publish_preset_discovery(void)
{
    led_preset_info_t presets[LED_MAX_PRESETS];
    led_ctrl_get(NULL, presets);

    cJSON *j = entity_json("Preset", "preset");
    cJSON_AddStringToObject(j, "command_topic", t_preset_set);
    cJSON_AddStringToObject(j, "state_topic", t_preset_state);
    cJSON_AddStringToObject(j, "icon", "mdi:palette");
    cJSON *options = cJSON_AddArrayToObject(j, "options");
    cJSON_AddItemToArray(options, cJSON_CreateString(NO_PRESET));
    for (int i = 0; i < LED_MAX_PRESETS; i++) {
        if (presets[i].used) {
            char name[16];
            snprintf(name, sizeof(name), "Preset %d", i + 1);
            cJSON_AddItemToArray(options, cJSON_CreateString(name));
        }
    }

    char topic[160];
    discovery_topic(topic, sizeof(topic), "select", "preset");
    publish_json(topic, j, true);
}

static void publish_discovery(void)
{
    char topic[160];

    /* Lumière : marche/arrêt, luminosité, couleur RGB ; le schéma JSON gère aussi le fondu */
    cJSON *light = entity_json(NULL, "light");
    cJSON_AddStringToObject(light, "schema", "json");
    cJSON_AddStringToObject(light, "command_topic", t_set);
    cJSON_AddStringToObject(light, "state_topic", t_state);
    cJSON_AddBoolToObject(light, "brightness", true);
    cJSON_AddNumberToObject(light, "brightness_scale", 255);
    cJSON *modes = cJSON_AddArrayToObject(light, "supported_color_modes");
    cJSON_AddItemToArray(modes, cJSON_CreateString("rgb"));
    cJSON_AddStringToObject(light, "icon", "mdi:led-strip-variant");
    discovery_topic(topic, sizeof(topic), "light", "light");
    publish_json(topic, light, true);

    /* Sélection du preset */
    publish_preset_discovery();

    /* Bouton « Enregistrer comme preset » */
    cJSON *save = entity_json("Enregistrer comme preset", "save_preset");
    cJSON_AddStringToObject(save, "command_topic", t_preset_save);
    cJSON_AddStringToObject(save, "icon", "mdi:content-save");
    discovery_topic(topic, sizeof(topic), "button", "save_preset");
    publish_json(topic, save, true);

    /* Diagnostic : signal Wi-Fi */
    cJSON *rssi = entity_json("Signal Wi-Fi", "rssi");
    cJSON_AddStringToObject(rssi, "state_topic", t_diag);
    cJSON_AddStringToObject(rssi, "value_template", "{{ value_json.rssi }}");
    cJSON_AddStringToObject(rssi, "unit_of_measurement", "dBm");
    cJSON_AddStringToObject(rssi, "device_class", "signal_strength");
    cJSON_AddStringToObject(rssi, "state_class", "measurement");
    cJSON_AddStringToObject(rssi, "entity_category", "diagnostic");
    discovery_topic(topic, sizeof(topic), "sensor", "rssi");
    publish_json(topic, rssi, true);

    /* Diagnostic : adresse IP */
    cJSON *ip = entity_json("Adresse IP", "ip");
    cJSON_AddStringToObject(ip, "state_topic", t_diag);
    cJSON_AddStringToObject(ip, "value_template", "{{ value_json.ip }}");
    cJSON_AddStringToObject(ip, "icon", "mdi:ip-network");
    cJSON_AddStringToObject(ip, "entity_category", "diagnostic");
    discovery_topic(topic, sizeof(topic), "sensor", "ip");
    publish_json(topic, ip, true);
}

/* Appelés par led_ctrl après chaque changement, quelle qu'en soit l'origine */
static void on_led_state_changed(void)
{
    publish_state();
}

static void on_led_presets_changed(void)
{
    publish_preset_discovery();
    publish_state();
}

/* ---------- Commandes reçues ---------- */

static int clamp(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static void handle_light_command(const char *data, int len)
{
    cJSON *j = cJSON_ParseWithLength(data, len);
    if (j == NULL) {
        ESP_LOGW(TAG, "Commande JSON invalide");
        return;
    }

    led_cmd_t cmd = LED_CMD_INIT;
    const cJSON *state = cJSON_GetObjectItem(j, "state");
    if (cJSON_IsString(state)) {
        cmd.has_on = true;
        cmd.on = strcmp(state->valuestring, "ON") == 0;
    }
    const cJSON *br = cJSON_GetObjectItem(j, "brightness");
    if (cJSON_IsNumber(br)) {
        int v = clamp(br->valueint, 0, 255);
        if (v == 0) {
            cmd.has_on = true;
            cmd.on = false;
        } else {
            cmd.has_brightness = true;
            cmd.brightness = v;
        }
    }
    const cJSON *color = cJSON_GetObjectItem(j, "color");
    if (cJSON_IsObject(color)) {
        const cJSON *r = cJSON_GetObjectItem(color, "r");
        const cJSON *g = cJSON_GetObjectItem(color, "g");
        const cJSON *b = cJSON_GetObjectItem(color, "b");
        if (cJSON_IsNumber(r) && cJSON_IsNumber(g) && cJSON_IsNumber(b)) {
            cmd.has_color = true;
            cmd.r = clamp(r->valueint, 0, 255);
            cmd.g = clamp(g->valueint, 0, 255);
            cmd.b = clamp(b->valueint, 0, 255);
        }
    }
    const cJSON *tr = cJSON_GetObjectItem(j, "transition");
    if (cJSON_IsNumber(tr)) {
        cmd.transition_ms = clamp((int)(tr->valuedouble * 1000.0), 0, 60000);
    }
    cJSON_Delete(j);

    if (cmd.has_on || cmd.has_color || cmd.has_brightness) {
        led_ctrl_apply(&cmd);
    }
}

static void handle_preset_command(const char *data, int len)
{
    char buf[24];
    int n = len < (int)sizeof(buf) - 1 ? len : (int)sizeof(buf) - 1;
    memcpy(buf, data, n);
    buf[n] = '\0';

    const char *digits = buf;
    while (*digits && (*digits < '0' || *digits > '9')) {
        digits++;
    }
    int id = atoi(digits);
    if (id < 1 || led_ctrl_preset_apply(id) != 0) {
        /* « Aucun » ou preset inexistant : on renvoie l'état réel pour resynchroniser HA */
        publish_state();
    }
}

static bool topic_is(const esp_mqtt_event_handle_t ev, const char *topic)
{
    return ev->topic_len == (int)strlen(topic) && memcmp(ev->topic, topic, ev->topic_len) == 0;
}

static void mqtt_event_handler(void *arg, esp_event_base_t base, int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t ev = event_data;

    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "Connecté au broker MQTT");
        s_connected = true;
        esp_mqtt_client_publish(s_client, t_status, "online", 0, 1, 1);
        esp_mqtt_client_subscribe(s_client, t_set, 1);
        esp_mqtt_client_subscribe(s_client, t_preset_set, 1);
        esp_mqtt_client_subscribe(s_client, t_preset_save, 1);
        esp_mqtt_client_subscribe(s_client, t_ha_status, 1);
        publish_discovery();
        publish_state();
        publish_diag();
        break;

    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGW(TAG, "Déconnecté du broker MQTT, nouvelle tentative...");
        s_connected = false;
        break;

    case MQTT_EVENT_DATA:
        /* Nos commandes sont courtes : on ignore les messages fragmentés */
        if (ev->current_data_offset != 0 || ev->data_len != ev->total_data_len) {
            break;
        }
        if (topic_is(ev, t_set)) {
            handle_light_command(ev->data, ev->data_len);
        } else if (topic_is(ev, t_preset_set)) {
            handle_preset_command(ev->data, ev->data_len);
        } else if (topic_is(ev, t_preset_save)) {
            if (led_ctrl_preset_add() < 0) {
                ESP_LOGW(TAG, "Plus de place pour un preset");
            }
        } else if (topic_is(ev, t_ha_status)) {
            if (ev->data_len == 6 && memcmp(ev->data, "online", 6) == 0) {
                ESP_LOGI(TAG, "Home Assistant a redémarré : nouvelle annonce");
                publish_discovery();
                publish_state();
                publish_diag();
            }
        }
        break;

    case MQTT_EVENT_ERROR:
        ESP_LOGW(TAG, "Erreur MQTT");
        break;

    default:
        break;
    }
}

void mqtt_ha_start(void)
{
    if (strlen(CFG_MQTT_URI) == 0) {
        ESP_LOGI(TAG, "MQTT désactivé (MQTT_URI vide)");
        return;
    }

    snprintf(t_status, sizeof(t_status), "%s/status", CFG_MQTT_BASE_TOPIC);
    snprintf(t_state, sizeof(t_state), "%s/state", CFG_MQTT_BASE_TOPIC);
    snprintf(t_set, sizeof(t_set), "%s/set", CFG_MQTT_BASE_TOPIC);
    snprintf(t_preset_state, sizeof(t_preset_state), "%s/preset/state", CFG_MQTT_BASE_TOPIC);
    snprintf(t_preset_set, sizeof(t_preset_set), "%s/preset/set", CFG_MQTT_BASE_TOPIC);
    snprintf(t_preset_save, sizeof(t_preset_save), "%s/preset/save", CFG_MQTT_BASE_TOPIC);
    snprintf(t_diag, sizeof(t_diag), "%s/diag", CFG_MQTT_BASE_TOPIC);
    snprintf(t_ha_status, sizeof(t_ha_status), "%s/status", CFG_HA_DISCOVERY_PREFIX);

    esp_mqtt_client_config_t cfg = {
        .broker.address.uri = CFG_MQTT_URI,
        .credentials.client_id = CFG_DEVICE_ID,
        .credentials.username = strlen(CFG_MQTT_USER) ? CFG_MQTT_USER : NULL,
        .credentials.authentication.password = strlen(CFG_MQTT_PASSWORD) ? CFG_MQTT_PASSWORD : NULL,
        .session.keepalive = 30,
        .session.last_will = {
            .topic = t_status,
            .msg = "offline",
            .qos = 1,
            .retain = 1,
        },
        .network.reconnect_timeout_ms = 5000,
        .buffer.size = 2048,
        .buffer.out_size = 2048,
    };

    s_client = esp_mqtt_client_init(&cfg);
    if (s_client == NULL) {
        ESP_LOGE(TAG, "Configuration MQTT invalide (vérifie MQTT_URI)");
        return;
    }
    led_ctrl_set_listeners(on_led_state_changed, on_led_presets_changed);
    esp_mqtt_client_register_event(s_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(s_client);

    const esp_timer_create_args_t timer_args = {
        .callback = diag_timer_cb,
        .name = "mqtt_diag",
    };
    if (esp_timer_create(&timer_args, &s_diag_timer) == ESP_OK) {
        esp_timer_start_periodic(s_diag_timer, DIAG_PERIOD_US);
    }
    ESP_LOGI(TAG, "MQTT : %s, topics %s/*", CFG_MQTT_URI, CFG_MQTT_BASE_TOPIC);
}
