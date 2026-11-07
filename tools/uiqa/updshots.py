"""Screenshots de documentacao das atualizacoes e dos reportes (docs/screenshot-atualizacao.png e docs/screenshot-reportar.png).

Uso:  py -3 tools/uiqa/updshots.py

Roda o BSLStage com --qa e --fake-release: um release FALSO (tools/fixtures/update/release-latest.json) no lugar do GitHub, entao
nao ha nenhuma rede. A verificacao automatica acha a "versao 1.2.0", o aviso aparece na barra de status e o clique abre a janela
com as notas. Depois abre Ajuda > Reportar problema, preenche e marca as caixas opcionais. O navegador nunca e aberto no --qa.
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
FIXTURE = os.path.join(ROOT, "tools", "fixtures", "update", "release-latest.json")


def main():
    fake = os.path.join(ROOT, "build", "qa", "updshots_fake")
    shutil.rmtree(fake, ignore_errors=True)
    os.makedirs(fake)
    shutil.copy(FIXTURE, os.path.join(fake, "release.json"))

    qa_dir = os.path.join(ROOT, "build", "qa", "updshots")
    shutil.rmtree(qa_dir, ignore_errors=True)
    app = driver.App(EXE, qa_dir, ["--fake-release", fake], size=(1600, 900))
    try:
        app.click("tab:Efeitos", "dock-tabs")
        app.wait_frames(4)
        app.wait_state(lambda s: s["update"]["notice"], "aviso discreto na barra de status", timeout=20)
        st = app.state["update"]
        expect(st["noticeText"] == "Atualização 1.2.0 disponível", "texto do aviso: " + st["noticeText"])
        expect(not st["windowOpen"], "nenhuma janela abre sozinha (sem pop-up)")
        expect(app.has("##atualizacao_aviso"), "o aviso e clicavel")

        app.click("##atualizacao_aviso")
        app.wait_state(lambda s: s["update"]["windowOpen"], "janela de notas aberta", timeout=4)
        expect(app.has("##atualizacao_baixar") and app.has("##atualizacao_lembrar") and app.has("##atualizacao_pular"),
               "os tres botoes")
        app.hover(20, 880)
        app.wait_frames(8)
        path = app.screenshot(os.path.join(DOCS, "screenshot-atualizacao.png"))
        print("ok", path)

        app.click("##atualizacao_lembrar")
        app.wait_state(lambda s: not s["update"]["windowOpen"] and not s["update"]["notice"], "aviso adiado", timeout=4)

        menu(app, "Ajuda", "##menu_reportar")
        app.wait_state(lambda s: s["report"]["open"], "janela de reporte aberta", timeout=4)
        app.click("##reportar_titulo")
        app.type_text("Saida preta ao trocar de coluna")
        app.click("##reportar_descricao")
        app.type_text("Ao disparar a coluna 3 com o PDF, a saida do monitor 2 fica preta por 2 segundos.")
        app.click("##reportar_passos")
        app.type_text("1. Abrir o projeto de exemplo. 2. Disparar a coluna 3.")
        app.click("##reportar_sistema")
        app.click("##reportar_log")
        app.wait_state(lambda s: s["report"]["system"] and s["report"]["log"], "caixas marcadas")
        app.hover(20, 880)
        app.wait_frames(8)
        path = app.screenshot(os.path.join(DOCS, "screenshot-reportar.png"))
        print("ok", path)
    finally:
        app.quit()


if __name__ == "__main__":
    main()
