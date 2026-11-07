"""Screenshot da janela "DMX / Art-Net" (docs/screenshot-dmx.png) recebendo dados de verdade por loopback.

Uso:  py -3 tools/uiqa/dmxshot.py [--aba monitor|patch|rede]

Sobe o `bslstage_dmxtest --send` (Art-Net + sACN para 127.0.0.1, universos 1 a 3) e o BSLStage com `--dmx-demo 127.0.0.1`
(receptor ligado so no loopback, nesta execucao, com o patch de exemplo e a janela aberta). Nada e aberto na rede, entao o
Firewall do Windows nao pergunta nada. Usa `--qa`: as configuracoes ficam numa pasta isolada.
"""
import argparse
import os
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import driver

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
EXE = os.path.join(ROOT, "build", "Release", "BSLStage.exe")
SENDER = os.path.join(ROOT, "build", "Release", "bslstage_dmxtest.exe")
DOCS = os.path.join(ROOT, "docs")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--aba", default="monitor", choices=["monitor", "patch", "rede"])
    ap.add_argument("--saida", default=os.path.join(DOCS, "screenshot-dmx.png"))
    args = ap.parse_args()

    qa_dir = os.path.join(ROOT, "build", "qa", "dmxshot")
    sender = subprocess.Popen([SENDER, "--send", "127.0.0.1", "40"], cwd=os.path.dirname(SENDER))
    app = driver.App(EXE, qa_dir, ["--dmx-demo", "127.0.0.1"], size=(1600, 900))
    try:
        app.wait_state(lambda s: s["dmx"]["running"] and len(s["dmx"]["universes"]) == 3
                       and all(u["active"] and u["pps"] > 10 for u in s["dmx"]["universes"]),
                       "3 universos recebendo dados", timeout=20)
        time.sleep(1.0)
        app.wait_frames(10)
        if args.aba != "monitor":
            app.click("##dmx_aba_" + args.aba)
            app.wait_frames(6)
        app.hover(10, 880)
        app.wait_frames(6)
        path = app.screenshot(args.saida)
        print("ok", path)
        st = app.state
        print("dmx:", {k: st["dmx"][k] for k in ("state", "running", "applied", "blocks", "maps")})
    finally:
        app.quit()
        sender.terminate()


if __name__ == "__main__":
    main()
