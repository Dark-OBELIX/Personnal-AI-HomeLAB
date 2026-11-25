/*
 * Bandeau LED piloté par Wi-Fi (ESP-IDF)
 * - configuration paramétrique : ../.env (Wi-Fi, bandeau, MQTT, OTA)
 * - page web et API HTTP (compatibles avec la version précédente, URL /presetN comprises)
 * - MQTT avec découverte automatique Home Assistant
 * - dernier réglage restauré au démarrage, fondus entre les couleurs
 * - mise à jour par le Wi-Fi (POST /ota), avec retour arrière automatique en cas d'échec
 */
#include "freertos/FreeRTOS.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "nvs_flash.h"
#include "app_config.h"
#include "led_ctrl.h"
#include "mqtt_ha.h"
#include "net.h"
#include "web.h"

static const char *TAG = "led_web";

/* Délai laissé à une nouvelle version pour se connecter au Wi-Fi avant de revenir à l'ancienne */
#define ROLLBACK_WIFI_TIMEOUT_MS 60000

static bool running_pending_verify(void)
{
    esp_ota_img_states_t state;
    return esp_ota_get_state_partition(esp_ota_get_running_partition(), &state) == ESP_OK &&
           state == ESP_OTA_IMG_PENDING_VERIFY;
}

void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    /* Le bandeau reprend son dernier réglage avant même le Wi-Fi */
    led_ctrl_init();
    ESP_LOGI(TAG, "%s : bandeau prêt", CFG_DEVICE_NAME);

    net_start();
    if (!net_wait_connected(pdMS_TO_TICKS(ROLLBACK_WIFI_TIMEOUT_MS))) {
        if (running_pending_verify()) {
            ESP_LOGE(TAG, "La nouvelle version ne se connecte pas au Wi-Fi : retour à la précédente");
            esp_ota_mark_app_invalid_rollback_and_reboot();
        }
        ESP_LOGW(TAG, "Toujours pas de Wi-Fi, on continue d'attendre");
        net_wait_connected(portMAX_DELAY);
    }

    web_start();
    mqtt_ha_start();

    /* Wi-Fi et serveur web opérationnels : cette version est validée */
    if (running_pending_verify()) {
        esp_ota_mark_app_valid_cancel_rollback();
        ESP_LOGI(TAG, "Nouvelle version validée");
    }
    ESP_LOGI(TAG, "Prêt : http://%s.local/", CFG_DEVICE_HOSTNAME);
}
