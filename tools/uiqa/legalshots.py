"""Screenshot de documentação da janela Ajuda > Licenças e avisos… (docs/screenshot-licencas.png), aberta na aba Terceiros.

Uso:  py -3 tools/uiqa/legalshots.py

Roda o BSLStage com --qa (configurações isoladas em build/qa/legalshots, nenhuma rede, o navegador nunca é aberto). Abre a aba Terceiros, expande o primeiro componente (GLFW) e o texto completo da licença dele.
"""
import os
import shutil
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import driver
from run import expect, menu

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
EXE = os.path.join(ROOT, "build", "Release", "BSLStage.exe")
DOCS = os.path.join(ROOT, "docs")


def main():
    qa_dir = os.path.join(ROOT, "build", "qa", "legalshots")
    shutil.rmtree(qa_dir, ignore_errors=True)
    app = driver.App(EXE, qa_dir, ["--ui-mode", "window"], size=(1600, 900))
    try:
        app.click("tab:Efeitos", "dock-tabs")
        app.wait_frames(4)
        menu(app, "Ajuda", "##menu_licencas")
        app.wait_state(lambda s: s["legal"]["windowOpen"], "janela Licenças e avisos aberta", timeout=4)
        app.click("##legal_aba_terceiros")
        app.wait_state(lambda s: s["legal"]["tab"] == 1, "aba Terceiros", timeout=3)
        st = app.state["legal"]
        expect(st["docsFound"] and st["thirdParty"] >= 25, "lista de terceiros lida do THIRD-PARTY-NOTICES.md: %s" % (st,))
        app.click("##tp_0")
        app.wait_frames(4)
        app.click("##tp_0_0")
        app.wait_state(lambda s: s["legal"]["openedTexts"] == 1, "texto da licença expandido", timeout=3)
        app.hover(20, 880)
        app.wait_frames(8)
        path = app.screenshot(os.path.join(DOCS, "screenshot-licencas.png"))
        print("ok", path)
    finally:
        app.quit()


if __name__ == "__main__":
    main()
