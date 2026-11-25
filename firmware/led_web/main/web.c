/*
 * API HTTP (inchangée par rapport à la version précédente, pour garder les raccourcis existants) :
 *   GET  /                     page de contrôle
 *   GET  /state                état JSON
 *   GET  /set?on=1&color=ff8800&br=200[&t=500]   chaque paramètre est optionnel (t : fondu en ms)
 *   GET  /presetN              lance le preset N, ou éteint s'il est déjà actif
 *   GET  /api/presets/add      enregistre la couleur actuelle comme preset
 *   GET  /api/presets/del?id=N supprime un preset
 *   POST /ota                  nouveau firmware (en-tête X-OTA-Password), puis redémarrage
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "app_config.h"
#include "led_ctrl.h"
#include "web.h"

static const char *TAG = "web";

#define OTA_CHUNK 4096

extern const char index_html_start[] asm("_binary_index_html_start");
extern const char index_html_end[]   asm("_binary_index_html_end");

/* Nom de l'appareil échappé pour le JSON (calculé une fois) */
static char s_name_json[96];

static void json_escape(const char *in, char *out, size_t len)
{
    size_t j = 0;
    for (size_t i = 0; in[i] && j + 2 < len; i++) {
        if (in[i] == '"' || in[i] == '\\') {
            out[j++] = '\\';
        }
        out[j++] = in[i];
    }
    out[j] = '\0';
}

static esp_err_t send_state(httpd_req_t *req)
{
    led_state_t st;
    led_preset_info_t presets[LED_MAX_PRESETS];
    led_ctrl_get(&st, presets);

    char json[1200];
    int n = snprintf(json, sizeof(json),
                     "{\"name\":\"%s\",\"on\":%d,\"color\":\"%02x%02x%02x\",\"br\":%d,\"active\":%d,\"presets\":[",
                     s_name_json, st.on, st.r, st.g, st.b, st.brightness, st.active_preset);
    bool first = true;
    for (int i = 0; i < LED_MAX_PRESETS; i++) {
        if (!presets[i].used) {
            continue;
        }
        n += snprintf(json + n, sizeof(json) - n, "%s{\"id\":%d,\"color\":\"%02x%02x%02x\",\"br\":%d}",
                      first ? "" : ",", i + 1, presets[i].r, presets[i].g, presets[i].b,
                      presets[i].brightness);
        first = false;
    }
    snprintf(json + n, sizeof(json) - n, "]}");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_sendstr(req, json);
}

static esp_err_t root_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html; charset=UTF-8");
    return httpd_resp_send(req, index_html_start, index_html_end - index_html_start);
}

static esp_err_t state_handler(httpd_req_t *req)
{
    return send_state(req);
}

static esp_err_t set_handler(httpd_req_t *req)
{
    char query[128];
    char val[16];

    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Paramètres manquants");
        return ESP_FAIL;
    }

    led_cmd_t cmd = LED_CMD_INIT;
    if (httpd_query_key_value(query, "on", val, sizeof(val)) == ESP_OK) {
        cmd.has_on = true;
        cmd.on = atoi(val) != 0;
    }
    if (httpd_query_key_value(query, "color", val, sizeof(val)) == ESP_OK && strlen(val) == 6) {
        uint32_t rgb = strtoul(val, NULL, 16);
        cmd.has_color = true;
        cmd.r = (rgb >> 16) & 0xFF;
        cmd.g = (rgb >> 8) & 0xFF;
        cmd.b = rgb & 0xFF;
    }
    if (httpd_query_key_value(query, "br", val, sizeof(val)) == ESP_OK) {
        int br = atoi(val);
        cmd.has_brightness = true;
        cmd.brightness = br < 1 ? 1 : (br > 255 ? 255 : br);
    }
    if (httpd_query_key_value(query, "t", val, sizeof(val)) == ESP_OK) {
        int t = atoi(val);
        cmd.transition_ms = t < 0 ? 0 : (t > 60000 ? 60000 : t);
    }

    led_ctrl_apply(&cmd);
    return send_state(req);
}

static esp_err_t preset_handler(httpd_req_t *req)
{
    int id = atoi(req->uri + strlen("/preset"));
    if (led_ctrl_preset_toggle(id) != 0) {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Preset inexistant");
        return ESP_FAIL;
    }
    return send_state(req);
}

static esp_err_t preset_add_handler(httpd_req_t *req)
{
    if (led_ctrl_preset_add() < 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Plus de place pour un preset");
        return ESP_FAIL;
    }
    return send_state(req);
}

static esp_err_t preset_del_handler(httpd_req_t *req)
{
    char query[32];
    char val[8];
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK ||
        httpd_query_key_value(query, "id", val, sizeof(val)) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Paramètre id manquant");
        return ESP_FAIL;
    }
    if (led_ctrl_preset_delete(atoi(val)) != 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "id invalide");
        return ESP_FAIL;
    }
    return send_state(req);
}

/* ---------- Mise à jour par le Wi-Fi ---------- */

static esp_err_t ota_handler(httpd_req_t *req)
{
    if (strlen(CFG_OTA_PASSWORD) == 0) {
        httpd_resp_send_err(req, HTTPD_403_FORBIDDEN, "OTA désactivée (OTA_PASSWORD vide)");
        return ESP_FAIL;
    }
    char pass[72];
    if (httpd_req_get_hdr_value_str(req, "X-OTA-Password", pass, sizeof(pass)) != ESP_OK ||
        strcmp(pass, CFG_OTA_PASSWORD) != 0) {
        ESP_LOGW(TAG, "OTA refusée : mot de passe incorrect");
        httpd_resp_send_err(req, HTTPD_403_FORBIDDEN, "Mot de passe OTA incorrect");
        return ESP_FAIL;
    }
    if (req->content_len == 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Firmware vide");
        return ESP_FAIL;
    }

    const esp_partition_t *part = esp_ota_get_next_update_partition(NULL);
    if (part == NULL) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                            "Pas de partition OTA : flashe une fois en USB avec la nouvelle table de partitions");
        return ESP_FAIL;
    }

    esp_ota_handle_t ota;
    esp_err_t err = esp_ota_begin(part, OTA_WITH_SEQUENTIAL_WRITES, &ota);
    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, esp_err_to_name(err));
        return ESP_FAIL;
    }

    char *buf = malloc(OTA_CHUNK);
    if (buf == NULL) {
        esp_ota_abort(ota);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Mémoire insuffisante");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "OTA : réception de %d octets vers %s", req->content_len, part->label);
    size_t remaining = req->content_len;
    while (remaining > 0) {
        int n = httpd_req_recv(req, buf, MIN(remaining, OTA_CHUNK));
        if (n == HTTPD_SOCK_ERR_TIMEOUT) {
            continue;
        }
        if (n <= 0) {
            err = ESP_FAIL;
            break;
        }
        err = esp_ota_write(ota, buf, n);
        if (err != ESP_OK) {
            break;
        }
        remaining -= n;
    }
    free(buf);

    if (err != ESP_OK) {
        esp_ota_abort(ota);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Réception du firmware interrompue");
        return ESP_FAIL;
    }
    err = esp_ota_end(ota);
    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
                            err == ESP_ERR_OTA_VALIDATE_FAILED ? "Image invalide" : esp_err_to_name(err));
        return ESP_FAIL;
    }
    err = esp_ota_set_boot_partition(part);
    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, esp_err_to_name(err));
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "OTA terminée, redémarrage");
    httpd_resp_sendstr(req, "OK : redemarrage sur la nouvelle version\n");
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
    return ESP_OK;
}

void web_start(void)
{
    json_escape(CFG_DEVICE_NAME, s_name_json, sizeof(s_name_json));

    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.uri_match_fn = httpd_uri_match_wildcard;  /* pour /preset* */
    config.max_uri_handlers = 10;
    config.stack_size = 6144;
    config.lru_purge_enable = true;

    ESP_ERROR_CHECK(httpd_start(&server, &config));

    const httpd_uri_t handlers[] = {
        { .uri = "/",                .method = HTTP_GET,  .handler = root_handler },
        { .uri = "/state",           .method = HTTP_GET,  .handler = state_handler },
        { .uri = "/set",             .method = HTTP_GET,  .handler = set_handler },
        { .uri = "/api/presets/add", .method = HTTP_GET,  .handler = preset_add_handler },
        { .uri = "/api/presets/del", .method = HTTP_GET,  .handler = preset_del_handler },
        { .uri = "/preset*",         .method = HTTP_GET,  .handler = preset_handler },
        { .uri = "/ota",             .method = HTTP_POST, .handler = ota_handler },
    };
    for (size_t i = 0; i < sizeof(handlers) / sizeof(handlers[0]); i++) {
        httpd_register_uri_handler(server, &handlers[i]);
    }
    ESP_LOGI(TAG, "Serveur web démarré");
}
