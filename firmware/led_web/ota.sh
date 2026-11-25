#!/usr/bin/env bash
# Envoie le firmware compilé (build/led_web.bin) à l'ESP par le Wi-Fi.
# Usage : ./ota.sh [adresse]     (par défaut : <DEVICE_HOSTNAME>.local, lu dans .env)
set -euo pipefail
cd "$(dirname "$0")"

env_get() {
    grep -E "^$1[[:space:]]*=" .env 2>/dev/null | tail -n 1 | cut -d= -f2- \
        | sed -E 's/^[[:space:]]*//; s/^"([^"]*)".*$/\1/; s/^'"'"'([^'"'"']*)'"'"'.*$/\1/; s/[[:space:]]+#.*$//; s/[[:space:]]*$//'
}

[ -f .env ] || { echo "Fichier .env introuvable (cp .env.example .env)"; exit 1; }
BIN=build/led_web.bin
[ -f "$BIN" ] || { echo "Firmware introuvable : lance d'abord idf.py build"; exit 1; }

PASS="$(env_get OTA_PASSWORD)"
[ -n "$PASS" ] || { echo "OTA_PASSWORD est vide dans .env : OTA désactivée"; exit 1; }
HOSTNAME_CFG="$(env_get DEVICE_HOSTNAME)"
TARGET="${1:-${HOSTNAME_CFG:-led-bandeau}.local}"

echo "Envoi de $BIN ($(du -h "$BIN" | cut -f1)) vers http://$TARGET/ota ..."
curl --fail-with-body --show-error --max-time 180 \
     -H "X-OTA-Password: $PASS" \
     -H "Content-Type: application/octet-stream" \
     --data-binary @"$BIN" \
     "http://$TARGET/ota"
echo "L'ESP redémarre sur la nouvelle version."
