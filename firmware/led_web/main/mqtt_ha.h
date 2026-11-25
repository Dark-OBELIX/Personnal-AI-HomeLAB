/*
 * MQTT avec découverte automatique Home Assistant.
 * Entités créées : lumière (couleur, luminosité, fondu), sélection de preset,
 * bouton « Enregistrer comme preset », signal Wi-Fi et adresse IP.
 */
#pragma once

/* Ne fait rien si MQTT_URI est vide dans .env. */
void mqtt_ha_start(void);
