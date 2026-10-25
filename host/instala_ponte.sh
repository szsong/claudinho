#!/usr/bin/env bash
# Instala a ponte Bluetooth como servico do usuario no macOS (launchd).
# O macOS mata na hora (SIGABRT) um programa sem descricao de uso do Bluetooth,
# e um python solto nao tem; por isso a ponte roda dentro de um .app minimo
# (Claudinho Ponte.app) cujo Info.plist traz NSBluetoothAlwaysUsageDescription.
# Na primeira vez o macOS pergunta se o app pode usar o Bluetooth: permitir.
set -euo pipefail
AQUI="$(cd "$(dirname "$0")" && pwd)"; BASE="$HOME/.claudinho"
APP="$BASE/Claudinho Ponte.app"
mkdir -p "$BASE" "$APP/Contents/MacOS"
[ -x "$BASE/venv/bin/python" ] || python3 -m venv "$BASE/venv"
"$BASE/venv/bin/pip" install -q bleak
cp "$AQUI/ponte_ble.py" "$BASE/ponte_ble.py"
cat > "$APP/Contents/Info.plist" <<EOP
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
  <key>CFBundleIdentifier</key><string>com.claudinho.ponte</string>
  <key>CFBundleName</key><string>Claudinho Ponte</string>
  <key>CFBundleExecutable</key><string>ponte</string>
  <key>CFBundlePackageType</key><string>APPL</string>
  <key>LSUIElement</key><true/>
  <key>NSBluetoothAlwaysUsageDescription</key><string>O Claudinho usa o Bluetooth para mostrar o que o Claude Code esta fazendo.</string>
</dict></plist>
EOP
cat > "$APP/Contents/MacOS/ponte" <<EOP
#!/bin/bash
exec "$BASE/venv/bin/python" "$BASE/ponte_ble.py" 2>>"$BASE/ponte.log"
EOP
chmod +x "$APP/Contents/MacOS/ponte"
codesign --force --deep -s - "$APP" 2>/dev/null || true
PL="$HOME/Library/LaunchAgents/com.claudinho.ponte.plist"
cat > "$PL" <<EOP
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
  <key>Label</key><string>com.claudinho.ponte</string>
  <key>ProgramArguments</key><array><string>/usr/bin/open</string><string>-W</string><string>-a</string><string>$APP</string></array>
  <key>RunAtLoad</key><true/><key>KeepAlive</key><true/>
</dict></plist>
EOP
launchctl bootout "gui/$(id -u)/com.claudinho.ponte" 2>/dev/null || true
launchctl bootstrap "gui/$(id -u)" "$PL"
echo "ponte instalada. Log: $BASE/ponte.log"
echo "Se o macOS perguntar sobre Bluetooth, permita. O Claudinho precisa de CLAUDINHO_IP=127.0.0.1:8765 em $BASE/claudinho.env"
