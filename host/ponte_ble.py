#!/usr/bin/env python3
"""Ponte Bluetooth do Claudinho.

Os hooks e a status line do plugin falam HTTP (curl). Esta ponte fica em
127.0.0.1:8765 recebendo esses mesmos POST /evento e POST /estado e repassa
cada um para a placa por Bluetooth (BLE), como uma linha  E{json}  ou  S{json}.
Uso: apontar CLAUDINHO_IP=127.0.0.1:8765 (e qualquer CLAUDINHO_TOKEN com 16+
caracteres) em ~/.claudinho/claudinho.env. O segredo nao e conferido: so o
proprio PC chega aqui (127.0.0.1) e a placa so aceita eventos, nada perigoso.

Requer: pip install bleak   (python3 -m venv ~/.claudinho/venv && ~/.claudinho/venv/bin/pip install bleak)
No macOS, a primeira vez pede permissao de Bluetooth para o terminal/launchd.
"""
import asyncio, json, os, sys, time
from bleak import BleakClient, BleakScanner

PORTA = 8765
NOME = "Claudinho-"
SERVICO = "6e400001-b5a3-f393-e0a9-e50e24dcca9e"
RX = "6e400002-b5a3-f393-e0a9-e50e24dcca9e"


def ble_id():
    """Sufixo do nome BLE da placa (ex.: 6961 em Claudinho-6961), de CLAUDINHO_BLE_ID em
    ~/.claudinho/claudinho.env. Com ele, a ponte so conecta nessa placa (e nao em outro
    aparelho que tambem fale o servico UART, como uma segunda placa na mesma mesa)."""
    try:
        for l in open(os.path.expanduser("~/.claudinho/claudinho.env")).read().splitlines():
            if l.startswith("CLAUDINHO_BLE_ID="): return l.split("=", 1)[1].strip()
    except OSError:
        pass
    return ""


def combina(d, a):
    nome = a.local_name or d.name or ""
    alvo = ble_id()                      # relido a cada busca: vale sem reiniciar
    if alvo:                             # a placa anuncia Claudinho-XXXX ou, as vezes, Claude-XXXX
        return nome.endswith("-" + alvo) and nome.startswith(("Claudinho-", "Claude-"))
    return nome.startswith(NOME) or SERVICO in [u.lower() for u in a.service_uuids]


fila: "asyncio.Queue[bytes]" = asyncio.Queue(maxsize=50)
estado = {"conectada": False}


def log(*a):
    print(time.strftime("%H:%M:%S"), *a, file=sys.stderr, flush=True)


async def http(reader, writer):
    try:
        cab = await asyncio.wait_for(reader.readuntil(b"\r\n\r\n"), 3)
        linhas = cab.decode("latin-1").split("\r\n")
        metodo, caminho, _ = linhas[0].split(" ", 2)
        tam = 0
        for l in linhas[1:]:
            if l.lower().startswith("content-length:"):
                tam = int(l.split(":", 1)[1])
        corpo = await asyncio.wait_for(reader.readexactly(tam), 3) if tam else b""
        tipo = {"/evento": "E", "/estado": "S"}.get(caminho)
        if metodo == "POST" and tipo and tam < 600:
            try:
                d = json.loads(corpo)
                d["enviado_em"] = int(time.time())   # a placa sem Wi-Fi nao tem NTP: acerta o relogio por aqui
                linha = tipo.encode() + json.dumps(d, separators=(",", ":")).encode() + b"\n"
            except ValueError:
                writer.write(b"HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\n\r\n"); return
            if fila.full():
                fila.get_nowait()
            fila.put_nowait(linha)
            resp = b"ok\n"
        elif metodo == "GET" and caminho == "/ponte":
            resp = json.dumps(estado).encode()
        else:
            writer.write(b"HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n"); return
        writer.write(b"HTTP/1.1 200 OK\r\nContent-Length: %d\r\n\r\n" % len(resp) + resp)
    except Exception as e:
        log("http:", e)
    finally:
        try:
            await writer.drain(); writer.close()
        except Exception:
            pass


async def envia(cli, linha):
    # escreve em pedaços do tamanho do MTU; o fim da mensagem é o \n
    n = max(20, (cli.mtu_size or 23) - 3)
    for i in range(0, len(linha), n):
        await cli.write_gatt_char(RX, linha[i:i + n], response=True)


async def ble():
    while True:
        try:
            log("procurando", NOME + "...")
            disp = await BleakScanner.find_device_by_filter(combina, timeout=15)
            if not disp:
                continue
            log("achei", disp.name, disp.address)
            async with BleakClient(disp) as cli:
                estado["conectada"] = True
                log("conectado")
                while cli.is_connected:
                    try:
                        linha = await asyncio.wait_for(fila.get(), 5)
                    except asyncio.TimeoutError:
                        continue
                    try:
                        await envia(cli, linha)
                    except Exception as e:
                        log("envio falhou:", e)
                        break
        except Exception as e:
            log("ble:", e)
            await asyncio.sleep(3)
        finally:
            estado["conectada"] = False


async def main():
    srv = await asyncio.start_server(http, "127.0.0.1", PORTA)
    log("ponte em http://127.0.0.1:%d" % PORTA)
    async with srv:
        await asyncio.gather(srv.serve_forever(), ble())


if __name__ == "__main__":
    asyncio.run(main())
