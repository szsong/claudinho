# Claudinho host setup (macOS) — migration guide

Audience: a human or an AI agent setting up the Claudinho **host side** on a new Mac. Follow the steps in order and run the checks. Last updated 2026-10-26 from a working install.

The physical board is not migrated: it keeps its firmware and only needs to be in Bluetooth range of the new Mac. Only one computer can hold the BLE connection at a time, so stop the bridge on the old Mac first (`launchctl bootout gui/$(id -u)/com.claudinho.ponte`).

## Agent instructions

- Run the steps yourself where you can. Ask the user only for: the Bluetooth "Allow" click (Step 3), confirming the board's ID (Step 4), and a Claude Code restart.
- Do not flash firmware, push to GitHub, or modify the old Mac unless the user asks.
- Never print or commit the contents of `claudinho.env` (it holds a token).
- Verify each step (the "Check" lines) before moving on.

## What gets installed

```
Claude Code hooks + status line
        |  HTTP POST /evento, /estado (curl)
        v
127.0.0.1:8765   <- ponte_ble.py (BLE bridge, runs as a background .app via launchd)
        |  Bluetooth LE, Nordic UART service
        v
Physical Claudinho board  (advertises as "Claudinho-6961", MAC D8:3B:DA:A0:69:60)
```

| Piece | Where it ends up | Source |
|---|---|---|
| Claude Code plugin `claudinho@claudinho` | `~/.claude/plugins/...` | GitHub `szsong/claudinho` (marketplace) |
| Status line script + plugin config | `~/.claude/plugins/data/claudinho-claudinho/` | plugin installer |
| BLE bridge `ponte_ble.py`, venv, `Claudinho Ponte.app`, launchd job | `~/.claudinho/`, `~/Library/LaunchAgents/com.claudinho.ponte.plist` | repo `host/instala_ponte.sh` |
| Bridge config `claudinho.env` | `~/.claudinho/` and the plugin data dir (same values) | created in Step 4 |
| Optional: Claude Desktop (Cowork) watcher | run from the repo | repo `host/desktop_watcher.py` (see note in Step 6) |

## Step 1: Get the repo

The bridge code lives in the repo, branch `claude/project-thread-aegs4h` (fork of the plugin with the Waveshare board support, status box, and the `CLAUDINHO_BLE_ID` pin). Default location used so far: `~/ClaudeCode/claudinho/repo`.

```bash
mkdir -p ~/ClaudeCode/claudinho
git clone https://github.com/szsong/claudinho ~/ClaudeCode/claudinho/repo
cd ~/ClaudeCode/claudinho/repo && git checkout claude/project-thread-aegs4h
ls host/        # expect: ponte_ble.py  instala_ponte.sh
```

Requires macOS, `git`, and `python3` 3.10+ (the old Mac used 3.13; Xcode command line tools provide `python3` and `codesign`).

## Step 2: Install the plugin

In Claude Code:

```
/plugin marketplace add szsong/claudinho
/plugin install claudinho@claudinho
```

Or merge this into `~/.claude/settings.json`:

```json
{
  "enabledPlugins": { "claudinho@claudinho": true },
  "extraKnownMarketplaces": {
    "claudinho": { "source": { "source": "github", "repo": "szsong/claudinho" } }
  }
}
```

Restart Claude Code. Check: `ls ~/.claude/plugins/cache/claudinho/claudinho/` shows a version folder (was 1.19.0).

## Step 3: Install the bridge

```bash
bash ~/ClaudeCode/claudinho/repo/host/instala_ponte.sh
```

This creates `~/.claudinho/venv` (installs `bleak`), copies `ponte_ble.py`, builds and ad-hoc signs `Claudinho Ponte.app` (needed so macOS will show the Bluetooth permission prompt), and loads the launchd job `com.claudinho.ponte`.

When macOS asks to allow Bluetooth for "Claudinho Ponte", click **Allow**. If missed: System Settings > Privacy & Security > Bluetooth, enable it, then re-run the script.

Check: `curl -s http://127.0.0.1:8765/ponte` returns `{"conectada": ...}`.

## Step 4: Config file

Both copies must have the same content; the plugin-data one is what the hooks read. `CLAUDINHO_IP` must be exactly `127.0.0.1:8765`. The bridge ignores the token but the plugin scripts require 16+ characters, so any random value works.

```bash
TOKEN=$(openssl rand -hex 24)
D="$HOME/.claude/plugins/data/claudinho-claudinho"
mkdir -p "$D"
for f in "$HOME/.claudinho/claudinho.env" "$D/claudinho.env"; do
cat > "$f" <<EOF
CLAUDINHO_IP=127.0.0.1:8765
CLAUDINHO_TOKEN=$TOKEN
CLAUDINHO_MAC=D8:3B:DA:A0:69:60
CLAUDINHO_BLE_ID=6961
EOF
chmod 600 "$f"
done
```

- `CLAUDINHO_BLE_ID=6961` pins the bridge to this board (BLE name `Claudinho-6961`, sometimes `Claude-6961`). The owner has a second board (ESP32-C6, `Claude-5CEA`); without the pin the bridge can attach to it after the Mac wakes. The ID is the suffix in the `achei Claudinho-XXXX` line of `~/.claudinho/ponte.log`. Confirm it with the user if the board might be different.
- `CLAUDINHO_MAC` is informational (used for finding the board when flashing).
- Optional Wi-Fi mode: the old setup also had `CLAUDINHO_IP_WIFI=192.168.68.135` (unused while `CLAUDINHO_IP` points at the bridge). Going back to Wi-Fi needs the token the board was provisioned with; it is in `~/.claudinho/claudinho.env.bak-wifi` on the old Mac and is deliberately not in this file.
- Do NOT run `claudinho.sh virtual` or `virtual parar`: they overwrite `CLAUDINHO_IP`. If that happens, set it back to `127.0.0.1:8765`.

No restart is needed for the env file, but restart the bridge after the first config so it picks up the pin: `kill $(pgrep -f ponte_ble.py)` (launchd restarts it).

## Step 5: Status line

From inside Claude Code (or any shell), with the plugin installed:

```bash
R=$(ls -d ~/.claude/plugins/cache/claudinho/claudinho/*/ | tail -1)
python3 "${R}scripts/instalar-statusline.py" "$HOME/.claude/plugins/data/claudinho-claudinho"
```

Expected result in `~/.claude/settings.json` (with the new home path):

```json
"statusLine": {
  "type": "command",
  "command": "CLAUDINHO_DADOS=<HOME>/.claude/plugins/data/claudinho-claudinho python3 <HOME>/.claude/plugins/data/claudinho-claudinho/claudinho-statusline.py",
  "refreshInterval": 60
}
```

If the user already has a status line, the installer keeps it visible and stores the old command as `CLAUDINHO_STATUSLINE_ANTERIOR` in the plugin `claudinho.env`. Restart Claude Code. Check: the file `claudinho-statusline.py` exists in the plugin data dir.

## Step 6: Optional — Claude Desktop (Cowork) watcher

Claude Code hooks do not run inside Claude Desktop's Cowork tasks. `host/desktop_watcher.py` tails Cowork's `audit.jsonl` files and sends the same events to the bridge. It reads only event types and tool names, never message text.

Note: as of this writing the file existed only on the old Mac (untracked in git), so it may be missing from a fresh clone. If `~/ClaudeCode/claudinho/repo/host/desktop_watcher.py` is not there, copy it from the old Mac (or ask the user to commit and push it first).

```bash
python3 ~/ClaudeCode/claudinho/repo/host/desktop_watcher.py --dry-run   # shows what it would send
python3 ~/ClaudeCode/claudinho/repo/host/desktop_watcher.py             # runs until Ctrl-C
```

It reads `CLAUDINHO_IP` / `CLAUDINHO_TOKEN` from `~/.claudinho/claudinho.env` and watches `~/Library/Application Support/Claude/local-agent-mode-sessions` (override with `CLAUDINHO_DESKTOP_DIR`). It was run by hand on the old Mac, not as a launchd service.

## Step 7: Verify end to end

```bash
curl -s http://127.0.0.1:8765/ponte            # expect {"conectada": true}
tail -5 ~/.claudinho/ponte.log                 # expect "achei Claudinho-6961" then "conectado"

R=$(ls -d ~/.claude/plugins/cache/claudinho/claudinho/*/ | tail -1)
export CLAUDINHO_DADOS="$HOME/.claude/plugins/data/claudinho-claudinho"
bash "${R}scripts/claudinho.sh" cara parou      # idle face
bash "${R}scripts/claudinho.sh" cara atencao    # needs-input face
```

Both should print `ok` and the physical board should change face. Face types: inicio, prompt, ferramenta, erro, parou, atencao, compact, commit, teste_ok, teste_falhou, fim, dormir.

Test a state directly through the bridge, without Claude Code:

```bash
ev(){ printf 'header = "Authorization: Bearer x"\nheader = "Content-Type: application/json"\ndata = "%s"\n' "$(echo "$1" | sed 's/"/\\"/g')" | curl -s -X POST http://127.0.0.1:8765/evento -K -; echo; }
ev '{"tipo":"prompt","humor":"","acao":"","detalhe":"","sessao":"t"}'            # thinking (green)
ev '{"tipo":"atencao","humor":"","acao":"","detalhe":"perm:Bash","sessao":"t"}'  # needs input (yellow)
ev '{"tipo":"parou","humor":"","acao":"","detalhe":"","sessao":"t"}'             # idle (sky blue)
```

## Troubleshooting

- `conectada: false` / no "achei" in the log: board out of range, off, or still connected to the old Mac. Stop the old bridge there.
- Bluetooth denied: System Settings > Privacy & Security > Bluetooth, enable Claudinho Ponte, then `launchctl bootout gui/$(id -u)/com.claudinho.ponte` and re-run `host/instala_ponte.sh`.
- Events return `ok` but the board does nothing: check `/ponte`, and that `CLAUDINHO_IP` is exactly `127.0.0.1:8765` in the plugin-data `claudinho.env`.
- Face stops reacting after sleep: `tail ~/.claudinho/ponte.log`. If it says `achei Claude-5CEA`, the bridge attached to the wrong board; make sure `CLAUDINHO_BLE_ID` is set, then `kill $(pgrep -f ponte_ble.py)`.
- Python: recreate the venv on the new Mac (`rm -rf ~/.claudinho/venv`, re-run the installer) rather than copying the old one.
- Permission/question/plan wording ("Allow Bash?") needs the fork's `scripts/evento.sh` and the `PermissionRequest` hook in `hooks/hooks.json`. The plugin installed from GitHub only has them once the branch is merged to the default branch and the plugin is updated (`/plugin marketplace update claudinho`, then `/plugin update claudinho@claudinho`, restart). Until then the board shows "Claude is waiting".
- Stop/uninstall: `launchctl bootout gui/$(id -u)/com.claudinho.ponte; rm ~/Library/LaunchAgents/com.claudinho.ponte.plist`.

## Only if you will change or reflash the board

Firmware lives in `firmware/` of the same repo (PlatformIO env `ws169-s3-v1`). Texts are in `idiomas/en.txt` and `idiomas/pt-BR.txt` (run `python3 idiomas/gerar.py` after editing). See `README.md` for the flashing workflow. Rules learned the hard way: flash only the S3 board (MAC `d8:3b:da:a0:69:60`) and find its port by MAC, never by guessing, because a second board on USB changes the port numbering; the panel needs `c.rgb_order = true` in `tela_ws169.h`.

## Secrets

No secrets are in this file. The bridge does not check the token, so a fresh random one is fine. The only real secret, the old Wi-Fi token, stays on the old Mac.
