# Personnal-AI-HomeLAB

Serveur IA personnel auto-hébergé sur un PC i5 (7e gén., 16 Go, sans GPU) :
deux assistants (Assistant du quotidien et Coach), Home Assistant, voix locale,
accès à distance via Tailscale. Multi-utilisateur (Hugo et Flo).

Architecture retenue : **option A** — le modèle tourne via l'API Claude,
tout le reste (données, outils MCP, voix, domotique) reste à la maison.

## Services (phase 1)

| Service | Rôle | Exposé |
|---|---|---|
| `caddy` | HTTPS avec certificats Tailscale (`*.ts.net`) | IP Tailscale seulement (443, 8443) |
| `open-webui` | Chat multi-utilisateur branché sur l'API Claude | via Caddy |
| `homeassistant` | Domotique, réseau hôte (ESPHome, mDNS) | via Caddy :8443 et réseau local |
| `whisper` | Voix → texte (Wyoming, `small-int8`, français) | `127.0.0.1:10300` |
| `piper` | Texte → voix (Wyoming, `fr_FR-siwis-medium`) | `127.0.0.1:10200` |
| `postgres` | Base du backend (phase 3) | non exposé |

> Docker contourne `ufw` pour les ports publiés : aucun service n'est publié sur `0.0.0.0`.

## Installation (Debian 13, sans bureau)

1. BIOS : « Restore on AC power loss » = Power On, pas de mise en veille.
2. Paquets de base :
   ```bash
   sudo apt update && sudo apt install -y curl ca-certificates ufw unattended-upgrades jq git
   ```
3. Docker (lire le script avant de l'exécuter) :
   ```bash
   curl -fsSL https://get.docker.com -o get-docker.sh && less get-docker.sh
   sudo sh get-docker.sh && sudo usermod -aG docker $USER
   ```
4. Tailscale, puis activer **MagicDNS** et **HTTPS Certificates** dans la console (page DNS) :
   ```bash
   curl -fsSL https://tailscale.com/install.sh | sh
   sudo tailscale up
   ```
5. Pare-feu (adapter le réseau local, autoriser SSH avant `enable`) :
   ```bash
   sudo ufw default deny incoming && sudo ufw default allow outgoing
   sudo ufw allow in on tailscale0
   sudo ufw allow from 192.168.1.0/24 to any port 22,8123 proto tcp
   sudo ufw enable
   ```
6. Déploiement :
   ```bash
   cp .env.example .env && chmod 600 .env && nano .env
   docker compose up -d && docker compose logs -f
   ```
7. Premiers réglages :
   - **Open WebUI** (`https://<TS_HOSTNAME>`) : le premier compte est admin, les suivants sont à valider.
   - **Home Assistant** (`https://<TS_HOSTNAME>:8443`) : intégration Wyoming ×2
     (`127.0.0.1:10300` Whisper, `127.0.0.1:10200` Piper), pipeline Assist en français.
   - **Console Anthropic** : fixer une limite de dépense mensuelle.

## Feuille de route

1. ✅ Docker Compose, Open WebUI, Home Assistant, Tailscale
2. Sauvegardes automatiques (PostgreSQL, Home Assistant, Open WebUI)
3. Backend FastAPI : profils Hugo / Flo, agents Assistant et Coach, `mcp-agenda`, `mcp-mail`
4. `mcp-santé` (PostgreSQL + Row-Level Security), import Apple Santé via Raccourcis iOS
5. `mcp-garmin`
6. Micros ESP32-S3 (ESPHome) reliés à Home Assistant
7. App iPhone (SwiftUI, HealthKit)

## Points d'attention

- Images `whisper`, `piper`, `open-webui` sur `latest` / `main` : épingler des versions une fois validé.
- Open WebUI utilise la couche de compatibilité OpenAI de l'API Anthropic (suffisant pour le chat) ;
  le backend utilisera le SDK officiel `anthropic`.
- Mails : accès en lecture seule au départ, aucune action d'envoi sans validation (risque d'injection de prompt).
