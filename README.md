# Personnal-AI-HomeLAB

Serveur personnel auto-hébergé sur un PC i5 (7e gén., 8 Go, sans GPU), sous Ubuntu :
Home Assistant, un modèle local (Qwen3 4B via Ollama), une base nutrition et hydratation,
et un bot Discord comme interface pour Hugo et Flo. Accès à distance via Tailscale.

## Services

| Service | Rôle | Exposé |
|---|---|---|
| `homeassistant` | Domotique, réseau de l'hôte (mDNS, ESPHome, Matter) | réseau local :8123 et `tailscale serve` |
| `ollama` | Modèle `qwen3:4b` sur le CPU, contexte 4096, cache KV q8 | `127.0.0.1:11434` |
| `postgres` | Repas, eau, poids, données Garmin | non exposé |
| `assistant` | Bot Discord (profil `bot`, code à venir) | aucun port, connexion sortante vers Discord |

> Docker contourne `ufw` pour les ports publiés : aucun service n'est publié sur `0.0.0.0`.

## Prérequis sur le serveur

Ubuntu, Docker, Tailscale et `ufw` déjà installés :

```bash
sudo ufw status            # actif : tailscale0 autorisé, 22 et 8123 depuis 192.168.1.0/24
```

Dans la console Tailscale (page DNS) : **MagicDNS** et **HTTPS Certificates** activés,
et **Disable key expiry** sur la machine `serveurhome`.

## Démarrage

```bash
git clone https://github.com/Dark-OBELIX/Personnal-AI-HomeLAB.git ~/homelab && cd ~/homelab
cp .env.example .env && chmod 600 .env
sed -i "s|^POSTGRES_PASSWORD=.*|POSTGRES_PASSWORD=$(openssl rand -hex 24)|" .env
docker compose up -d
docker compose ps          # 3 services : running / healthy
```

Télécharger le modèle (environ 2,5 Go) puis mesurer sa vitesse :

```bash
docker exec ollama ollama pull qwen3:4b
docker exec -it ollama ollama run qwen3:4b --verbose "Bonjour, présente-toi en une phrase. /no_think"
```

`eval rate` donne la vitesse de génération en tokens/s.

## Home Assistant à distance (Tailscale)

```bash
sudo tailscale serve --bg http://localhost:8123
```

Home Assistant est alors joignable sur `https://serveurhome.<tailnet>.ts.net`, uniquement
depuis tes appareils Tailscale. Sur le réseau local : `http://192.168.1.111:8123`.

Dans l'app Home Assistant (iPhone) : URL interne = adresse locale associée au Wi-Fi de la maison,
URL externe = adresse `ts.net`. Dans l'app Tailscale : activer **VPN On Demand**.

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
6. Domotique : clé Zigbee, commandes `/maison`, micros ESP32
7. Plus tard : réseau domotique séparé (Raspberry Pi sous OpenWrt)

## Points d'attention

- 8 Go de RAM : Ollama est limité à 5 Go (`mem_limit`) pour protéger Home Assistant et PostgreSQL.
- Images `ollama` et `homeassistant` sur `latest` / `stable` : épingler des versions une fois validé.
- Mails : lecture seule ; toute écriture dans l'agenda demande une confirmation dans Discord.
