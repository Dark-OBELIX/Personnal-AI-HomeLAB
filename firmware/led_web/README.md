# led_web — firmware du bandeau LED

ESP32 + bandeau adressable (WS2812 / WS2811 / SK6812), ESP-IDF 5.2.

- **Configuration paramétrique** dans `.env` : Wi-Fi, broche, nombre de pixels, ordre des couleurs,
  MQTT, OTA. Rien de sensible dans le code ni dans Git.
- **Page web et API HTTP** inchangées : les URL `/presetN` existantes continuent de marcher.
- **MQTT avec découverte automatique Home Assistant** : l'appareil apparaît tout seul, avec
  une lumière (couleur, luminosité, fondu), un sélecteur de preset, un bouton « Enregistrer
  comme preset », le signal Wi-Fi et l'adresse IP. L'état est publié à chaque changement,
  quelle qu'en soit l'origine (page web, preset, Home Assistant).
- **Fondus** entre deux réglages, **dernier réglage restauré** au démarrage.
- **Mise à jour par le Wi-Fi** (`./ota.sh`), avec retour automatique à la version précédente
  si la nouvelle ne se connecte pas au Wi-Fi dans la minute.
- **Nom réseau et mDNS** : `http://led-chambre.local/`.

## Configuration

```bash
cp .env.example .env
nano .env
```

| Clé | Rôle | Défaut |
|---|---|---|
| `WIFI_SSID`, `WIFI_PASSWORD` | Wi-Fi (obligatoires) | — |
| `DEVICE_NAME` | nom dans Home Assistant | `Bandeau LED` |
| `DEVICE_ID` | topics MQTT et identifiants HA (`[a-z0-9_]`) | `bandeau_led` |
| `DEVICE_HOSTNAME` | nom réseau, `http://<nom>.local/` (`[a-z0-9-]`) | `led-bandeau` |
| `LED_GPIO`, `LED_COUNT` | broche et nombre de pixels | `22`, `150` |
| `LED_MODEL` | `WS2812` (aussi WS2811 à 800 kHz) ou `SK6812` | `WS2812` |
| `LED_COLOR_ORDER` | ordre des couleurs sur le fil (`GRB`, `RGB`, `BRG`…) | `GRB` |
| `LED_MAX_BRIGHTNESS` | plafond global, pour ménager l'alimentation | `255` |
| `LED_GAMMA` | `1` = correction gamma | `0` |
| `DEFAULT_TRANSITION_MS` | fondu par défaut | `300` |
| `MQTT_URI`, `MQTT_USER`, `MQTT_PASSWORD` | broker (vide = MQTT désactivé) | vide |
| `MQTT_BASE_TOPIC` | racine des topics | `led/<DEVICE_ID>` |
| `HA_DISCOVERY_PREFIX` | préfixe de découverte HA | `homeassistant` |
| `OTA_PASSWORD` | mot de passe de mise à jour (vide = OTA désactivée) | vide |

Valeurs avec espaces ou caractères spéciaux : entre guillemets. Les erreurs (clé obligatoire
manquante, nombre invalide, identifiant mal formé) arrêtent la compilation avec un message clair.

> WS2811 en 12 V : une puce pilote 3 LED, donc `LED_COUNT` = nombre de puces, pas de LED.

## Premier flash (USB)

La table de partitions change (deux emplacements pour l'OTA) : **le premier flash se fait en USB**,
en effaçant l'ancienne configuration de compilation.

```bash
. $HOME/esp/esp-idf/export.sh          # adapte au chemin de ton ESP-IDF
cd firmware/led_web
rm -f sdkconfig                         # repartir de sdkconfig.defaults
idf.py set-target esp32
idf.py build flash monitor
```

Le réglage et les presets enregistrés sont conservés si la NVS est lisible ; sinon le bandeau
repart des valeurs par défaut (presets à recréer).

## Mises à jour suivantes (Wi-Fi)

```bash
idf.py build && ./ota.sh                # ou ./ota.sh 192.168.1.42
```

## Topics MQTT

| Topic | Sens | Contenu |
|---|---|---|
| `<base>/status` | ESP → | `online` / `offline` (testament), conservé |
| `<base>/state` | ESP → | `{"state":"ON","brightness":200,"color_mode":"rgb","color":{"r":255,"g":180,"b":107}}` |
| `<base>/set` | → ESP | même format, + `"transition"` en secondes |
| `<base>/preset/state` | ESP → | `Aucun` ou `Preset N` |
| `<base>/preset/set` | → ESP | `Preset N` |
| `<base>/preset/save` | → ESP | n'importe quel message |
| `<base>/diag` | ESP → | `{"rssi":-55,"ip":"192.168.1.42","uptime":3600}`, toutes les minutes |

Test depuis le serveur :

```bash
docker exec mosquitto mosquitto_sub -u homelab -P '<MQTT_PASSWORD>' -t 'led/#' -v
docker exec mosquitto mosquitto_pub -u homelab -P '<MQTT_PASSWORD>' -t led/bandeau_chambre/set \
  -m '{"state":"ON","color":{"r":255,"g":80,"b":0},"brightness":120,"transition":2}'
```

## API HTTP

| Requête | Effet |
|---|---|
| `GET /state` | état JSON (avec `name`) |
| `GET /set?on=1&color=ff8800&br=200&t=500` | chaque paramètre est optionnel (`t` : fondu en ms) |
| `GET /presetN` | lance le preset N, ou éteint s'il est déjà actif |
| `GET /api/presets/add` · `GET /api/presets/del?id=N` | enregistre · supprime un preset |
| `POST /ota` | nouveau firmware, en-tête `X-OTA-Password` |

L'API HTTP n'a pas d'authentification (sauf l'OTA) : à réserver au réseau local.

## Organisation du code

| Fichier | Rôle |
|---|---|
| `main/main.c` | démarrage, validation d'une nouvelle version OTA |
| `main/led_ctrl.c` | état, presets, NVS, rendu avec fondu (tâche dédiée, accès protégé par mutex) |
| `main/net.c` | Wi-Fi, nom réseau, mDNS |
| `main/web.c` | page web, API HTTP, OTA |
| `main/mqtt_ha.c` | MQTT, découverte Home Assistant |
| `main/env_config.cmake` | lecture de `.env` → `env_config.h` généré à la compilation |
