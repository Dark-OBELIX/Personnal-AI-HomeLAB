# Personnal-AI-HomeLAB

Serveur personnel auto-hébergé sur un PC i5 (7e gén., 8 Go, sans GPU), sous Ubuntu :
Home Assistant, un modèle local (Qwen3 4B via Ollama), une base nutrition et hydratation,
et un bot Discord comme interface pour Hugo et Flo. Accès à distance via Tailscale.

## Services

| Service | Rôle | Exposé |
|---|---|---|
| `homeassistant` | Domotique, réseau de l'hôte (mDNS, ESPHome, Matter) | réseau local :8123 et `tailscale serve` (TCP) |
| `mosquitto` | Broker MQTT (bandeau LED, objets ESP32), avec mot de passe | réseau local :1883 (règle `ufw`) |
| `ollama` | Modèle `qwen3:4b` sur le CPU, contexte 4096, cache KV q8 | `127.0.0.1:11434` |
| `postgres` | Repas, eau, poids, données Garmin | non exposé |
| `assistant` | Bot Discord (profil `bot`, code à venir) | aucun port, connexion sortante vers Discord |

> Docker contourne `ufw` pour les ports publiés : aucun service n'est publié sur `0.0.0.0`.

## Prérequis sur le serveur

Ubuntu, Docker, Tailscale et `ufw` déjà installés :

```bash
sudo ufw status            # actif : tailscale0 autorisé, 22 et 8123 depuis 192.168.1.0/24
sudo ufw allow from 192.168.1.0/24 to any port 1883 proto tcp   # MQTT pour les ESP du réseau local
```

Dans la console Tailscale (page DNS) : **MagicDNS** et **HTTPS Certificates** activés,
et **Disable key expiry** sur la machine `serveurhome`.

## Démarrage

```bash
git clone https://github.com/Dark-OBELIX/Personnal-AI-HomeLAB.git ~/homelab && cd ~/homelab
cp .env.example .env && chmod 600 .env
sed -i "s|^POSTGRES_PASSWORD=.*|POSTGRES_PASSWORD=$(openssl rand -hex 24)|" .env
sed -i "s|^MQTT_PASSWORD=.*|MQTT_PASSWORD=$(openssl rand -hex 16)|" .env
docker compose up -d
docker compose ps          # 4 services : running / healthy
```

Télécharger le modèle (environ 2,5 Go) puis mesurer sa vitesse :

```bash
docker exec ollama ollama pull qwen3:4b
docker exec -it ollama ollama run qwen3:4b --think=false --verbose "Bonjour, présente-toi en une phrase."
```

`eval rate` donne la vitesse de génération (mesuré : environ 5,7 tokens/s, RAM en simple canal).
La réflexion se désactive par option (`--think=false`, ou `think: false` dans l'API), pas par `/no_think` dans le message.

## Home Assistant à distance (Tailscale)

```bash
sudo tailscale serve --bg --tls-terminated-tcp=443 tcp://localhost:8123
```

Home Assistant est alors joignable sur `https://serveurhome.<tailnet>.ts.net`, uniquement
depuis tes appareils Tailscale. Sur le réseau local : `http://192.168.1.111:8123`.

Tailscale gère le certificat HTTPS et transmet la connexion telle quelle, sans en-têtes de proxy :
aucun réglage de « proxy de confiance » n'est nécessaire dans Home Assistant. Contrepartie :
Home Assistant voit les connexions distantes comme venant de `127.0.0.1`.

> Les versions récentes de Home Assistant ignorent le bloc `http:` de `configuration.yaml`
> (réglages dans Paramètres → Système → Réseau, avec confirmation sous 5 minutes).
> Le relais HTTP classique (`tailscale serve --bg http://localhost:8123`) renvoie donc une erreur 400
> tant que le proxy n'y est pas déclaré.

Dans l'app Home Assistant (iPhone) : URL interne = adresse locale associée au Wi-Fi de la maison,
URL externe = adresse `ts.net`. Dans l'app Tailscale : activer **VPN On Demand**.

## Bandeau LED de la chambre (MQTT)

Le firmware est dans [`firmware/led_web/`](firmware/led_web/README.md) : configuration dans son propre `.env`
(Wi-Fi, nombre de pixels, broker MQTT…), MQTT avec découverte automatique Home Assistant, OTA.

1. **Brancher Home Assistant sur Mosquitto** : Paramètres → Appareils et services → Ajouter une intégration
   → **MQTT** → broker `127.0.0.1`, port `1883`, utilisateur et mot de passe = `MQTT_USER` / `MQTT_PASSWORD` du `.env`.
2. **Flasher le bandeau** (voir son README) avec `MQTT_URI=mqtt://192.168.1.111:1883` et les mêmes identifiants.
   L'appareil apparaît tout seul dans l'intégration MQTT : lumière, preset, bouton « Enregistrer comme preset »,
   signal Wi-Fi, adresse IP.
3. **Carte de tableau de bord** `led-strip-card` (couleur, luminosité, presets) :
   - le fichier est déjà dans `homeassistant/www/` (servi sur `/local/`) ; après un `git pull`, redémarrer HA
     si le dossier `www` vient d'être créé ;
   - Paramètres → Tableaux de bord → ⋮ → **Ressources** → Ajouter : `/local/led-strip-card.js`, type
     « Module JavaScript » (menu visible avec le *mode avancé* activé dans ton profil) ;
   - dans un tableau de bord, ajouter une carte « Manuel » :
     ```yaml
     type: custom:led-strip-card
     entity: light.bandeau_chambre
     transition: 0.5
     ```
   Sans carte personnalisée, la carte native *Tuile* avec la fonction « Luminosité » et la fenêtre de
   l'entité (roue chromatique) suffisent aussi.

## Bot Discord (étape 2)

Quand le dossier `assistant/` existe :

```bash
sudo ufw allow from 172.30.0.0/24 to any port 8123 proto tcp   # le bot joint Home Assistant
docker compose --profile bot up -d --build
```

## Feuille de route

1. ✅ Socle : Home Assistant, Ollama, PostgreSQL, accès Tailscale
2. Coach : bot Discord, `/eau`, `/repas` (table CIQUAL), `/poids`, `/bilan`
3. Agenda : Google Agenda (`/agenda`, `/ajout`, `/creneau`)
4. Garmin et automatisations (briefing 07:30, rappels d'eau, bilans)
5. Mails en lecture seule, messages libres avec l'agent
6. Domotique : ✅ bandeau LED en MQTT ; clé Zigbee, commandes `/maison`, micros ESP32
7. Plus tard : réseau domotique séparé (Raspberry Pi sous OpenWrt)

## Points d'attention

- 8 Go de RAM : Ollama est limité à 5 Go (`mem_limit`) pour protéger Home Assistant et PostgreSQL.
- Images `ollama` et `homeassistant` sur `latest` / `stable` : épingler des versions une fois validé.
- Mails : lecture seule ; toute écriture dans l'agenda demande une confirmation dans Discord.
