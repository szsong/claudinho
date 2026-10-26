#!/usr/bin/env python3
"""Claude Desktop (Cowork) -> Claudinho.

Os hooks do Claude Code nao rodam nas tarefas do Cowork do Claude Desktop. Mas cada
sessao do Cowork tem um audit.jsonl que o Desktop aumenta a cada evento. Este
vigia le so as linhas novas e manda ao Claudinho (pela ponte BLE em 127.0.0.1:8765,
ou pelo IP do config) os mesmos eventos que os hooks mandam:

  user (texto)                 -> prompt      (pensando)
  assistant com tool_use       -> ferramenta  (trabalhando; a categoria vem do NOME da ferramenta)
  AskUserQuestion/ExitPlanMode -> atencao     (pergunta / plano)
  result (end_turn)            -> parou       (ocioso)

Privacidade: so le o tipo de cada evento e o nome da ferramenta. Texto de mensagem,
argumentos de ferramenta e resultados nunca sao lidos para fora do arquivo nem enviados.
O historico que ja existe e ignorado (so vale o que aparece depois que o vigia comeca).

Uso: python3 desktop_watcher.py            (fica rodando; Ctrl-C para parar)
     python3 desktop_watcher.py --dry-run  (so mostra o que enviaria)
Config: CLAUDINHO_IP e CLAUDINHO_TOKEN de ~/.claudinho/claudinho.env (como a ponte);
CLAUDINHO_DESKTOP_DIR troca a pasta vigiada.
"""
import glob, hashlib, json, os, sys, time, urllib.request

BASE = os.environ.get("CLAUDINHO_DESKTOP_DIR") or os.path.expanduser(
    "~/Library/Application Support/Claude/local-agent-mode-sessions")
DRY = "--dry-run" in sys.argv
CATEGORIAS = {
    "Bash": "terminal", "PowerShell": "terminal",
    "Edit": "codando", "Write": "codando", "MultiEdit": "codando", "NotebookEdit": "codando",
    "Read": "lendo", "Grep": "lendo", "Glob": "lendo", "LS": "lendo",
    "Task": "agente", "Agent": "agente", "WebSearch": "web", "WebFetch": "web",
}


def config():
    cfg = {}
    try:
        for l in open(os.path.expanduser("~/.claudinho/claudinho.env")).read().splitlines():
            if "=" in l and not l.startswith("#"):
                k, v = l.split("=", 1); cfg[k.strip()] = v.strip()
    except OSError:
        pass
    return cfg


def log(*a):
    print(time.strftime("%H:%M:%S"), *a, file=sys.stderr, flush=True)


def envia(tipo, acao, detalhe, sessao):
    if DRY:
        log("enviaria:", tipo, acao, detalhe, sessao); return
    cfg = config()
    ip, token = cfg.get("CLAUDINHO_IP"), cfg.get("CLAUDINHO_TOKEN")
    if not ip or not token:
        log("sem CLAUDINHO_IP/CLAUDINHO_TOKEN em ~/.claudinho/claudinho.env"); return
    corpo = json.dumps({"tipo": tipo, "humor": "", "acao": acao, "detalhe": detalhe, "sessao": sessao}).encode()
    req = urllib.request.Request("http://%s/evento" % ip, data=corpo, method="POST",
                                 headers={"Authorization": "Bearer " + token, "Content-Type": "application/json"})
    try:
        urllib.request.urlopen(req, timeout=2).read()
    except Exception as e:
        log("envio falhou:", e)


def evento_de(d):
    """(tipo, acao, detalhe) para um evento do audit.jsonl, ou None se nao muda o estado."""
    t = d.get("type")
    c = (d.get("message") or {}).get("content")
    if t == "user":
        return ("prompt", "", "") if isinstance(c, str) else None      # tool_result nao conta
    if t == "assistant" and isinstance(c, list):
        for b in c:
            if isinstance(b, dict) and b.get("type") == "tool_use":
                nome = str(b.get("name") or "")
                if nome == "AskUserQuestion": return ("atencao", "", "pergunta")
                if nome == "ExitPlanMode": return ("atencao", "", "plano")
                return ("ferramenta", CATEGORIAS.get(nome.split("__")[-1], ""), "")
        return None
    if t == "result":
        return ("erro", "", "") if d.get("is_error") else ("parou", "", "")
    return None


def varre(estado, inicio):
    for f in glob.glob(os.path.join(BASE, "*", "*", "local_*", "audit.jsonl")):
        try:
            tam = os.path.getsize(f)
        except OSError:
            continue
        if f not in estado:                                            # arquivo novo: do comeco; antigo: so o que vier depois
            estado[f] = 0 if os.path.getmtime(f) >= inicio else tam
            log("vigiando", os.path.basename(os.path.dirname(f)), "(do comeco)" if estado[f] == 0 else "(so o novo)")
        if tam <= estado[f]:
            continue
        with open(f, "rb") as fh:
            fh.seek(estado[f]); dados = fh.read()
        fim = dados.rfind(b"\n") + 1                                   # so linhas completas
        if not fim:
            continue
        estado[f] += fim
        ultimo = None
        for linha in dados[:fim].splitlines():
            try:
                d = json.loads(linha)
            except ValueError:
                continue
            ev = evento_de(d)
            if ev:
                sessao = hashlib.sha1(str(d.get("session_id") or "").encode()).hexdigest()[:8]
                if ultimo == ev + (sessao,):                           # o mesmo evento em seguida: nao repete
                    continue
                envia(ev[0], ev[1], ev[2], sessao); ultimo = ev + (sessao,)


def main():
    inicio = time.time(); estado = {}
    log("vigiando", BASE, "(simulacao)" if DRY else "")
    while True:
        try:
            varre(estado, inicio)
        except Exception as e:
            log("erro:", e)
        time.sleep(1)


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        pass
