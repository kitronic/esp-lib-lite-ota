#!/usr/bin/env bash
#
# LiteOTA Test Server
#
# Kitronic — https://github.com/kitronic/esp-lib-lite-ota
#
# Serves project.json and project.txt over plain HTTP.
# Usage:  ./serve.sh [port]
#

PORT="${1:-8080}"

echo "LiteOTA test server on http://0.0.0.0:${PORT}"
echo "  JSON: http://<your-ip>:${PORT}/project.json"
echo "  TXT:  http://<your-ip>:${PORT}/project.txt"
echo
echo "Press Ctrl+C to stop."
echo

cd "$(dirname "$0")"
python3 -m http.server "${PORT}"