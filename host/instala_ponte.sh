#!/usr/bin/env bash
# Instala a ponte Bluetooth como servico do usuario no macOS (launchd): sobe no
# login, reinicia se cair. Cria o venv com o bleak e aponta o Claudinho para a ponte.
set -euo pipefail
AQUI="$(cd "$(dirname "$0")" && pwd)"; BASE="$HOME/.claudinho"
mkdir -p "$BASE"
[ -x "$BASE/venv/bin/python" ] || python3 -m venv "$BASE/venv"
"$BASE/venv/bin/pip" install -q bleak
cp "$AQUI/ponte_ble.py" "$BASE/ponte_ble.py"
PL="$HOME/Library/LaunchAgents/com.claudinho.ponte.plist"
cat > "$PL" <<EOP
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
  <key>Label</key><string>com.claudinho.ponte</string>
  <key>ProgramArguments</key><array><string>$BASE/venv/bin/python</string><string>$BASE/ponte_ble.py</string></array>
  <key>RunAtLoad</key><true/><key>KeepAlive</key><true/>
  <key>StandardErrorPath</key><string>$BASE/ponte.log</string>
</dict></plist>
EOP
launchctl bootout "gui/$(id -u)/com.claudinho.ponte" 2>/dev/null || true
launchctl bootstrap "gui/$(id -u)" "$PL"
echo "ponte instalada. Log: $BASE/ponte.log"
echo "Falta apontar o Claudinho para ela: CLAUDINHO_IP=127.0.0.1:8765 em $BASE/claudinho.env"
