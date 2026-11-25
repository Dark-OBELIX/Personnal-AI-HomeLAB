#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "mdns.h"
#include "app_config.h"
#include "net.h"

static const char *TAG = "net";

#define WIFI_CONNECTED_BIT BIT0

static EventGroupHandle_t s_events;
static esp_ip4_addr_t s_ip;
static bool s_mdns_started;

static void start_mdns(void)
{
    if (s_mdns_started) {
        return;
    }
    if (mdns_init() != ESP_OK) {
        ESP_LOGW(TAG, "mDNS indisponible");
        return;
    }
    mdns_hostname_set(CFG_DEVICE_HOSTNAME);
    mdns_instance_name_set(CFG_DEVICE_NAME);
    mdns_service_add(CFG_DEVICE_NAME, "_http", "_tcp", 80, NULL, 0);
    s_mdns_started = true;
    ESP_LOGI(TAG, "mDNS : http://%s.local/", CFG_DEVICE_HOSTNAME);
}

static void event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGW(TAG, "Wi-Fi perdu, reconnexion...");
        xEventGroupClearBits(s_events, WIFI_CONNECTED_BIT);
        s_ip.addr = 0;
        esp_wifi_connect();
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)data;
        s_ip = event->ip_info.ip;
        ESP_LOGI(TAG, "Connecté, IP : " IPSTR, IP2STR(&s_ip));
        start_mdns();
        xEventGroupSetBits(s_events, WIFI_CONNECTED_BIT);
    }
}

void net_start(void)
{
    s_events = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_t *netif = esp_netif_create_default_wifi_sta();
    esp_netif_set_hostname(netif, CFG_DEVICE_HOSTNAME);  /* nom vu par la box (DHCP) */

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                        &event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                        &event_handler, NULL, NULL));

    wifi_config_t wifi_config = { 0 };
    /* Longueurs vérifiées à la compilation (app_config.h) */
    memcpy(wifi_config.sta.ssid, CFG_WIFI_SSID, sizeof(CFG_WIFI_SSID) - 1);
    memcpy(wifi_config.sta.password, CFG_WIFI_PASSWORD, sizeof(CFG_WIFI_PASSWORD) - 1);

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_LOGI(TAG, "Connexion au Wi-Fi \"%s\"...", CFG_WIFI_SSID);
}

bool net_wait_connected(TickType_t timeout)
{
    EventBits_t bits = xEventGroupWaitBits(s_events, WIFI_CONNECTED_BIT, pdFALSE, pdTRUE, timeout);
    return (bits & WIFI_CONNECTED_BIT) != 0;
}

void net_get_ip(char *buf, size_t len)
{
    if (s_ip.addr == 0) {
        buf[0] = '\0';
        return;
    }
    snprintf(buf, len, IPSTR, IP2STR(&s_ip));
}

int net_get_rssi(void)
{
    wifi_ap_record_t ap;
    if (esp_wifi_sta_get_ap_info(&ap) != ESP_OK) {
        return 0;
    }
    return ap.rssi;
}
