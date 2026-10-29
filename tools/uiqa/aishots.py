"""Screenshots de documentacao dos efeitos com IA, 1920x1080:

  docs/screenshot-vhs.png      BSL VHS (sem IA)
  docs/screenshot-relight.png  BSL Relight 2.5D
  docs/screenshot-aura.png     BSL Performer Aura & Fluid, com a mascara real da IA (MODNet) numa pessoa em movimento
  docs/screenshot-zslice.png   Mapeamento com 3 slices recebendo zonas de profundidade diferentes do Z-Slice

Uso:  py -3 tools/uiqa/aishots.py [vhs] [relight] [aura] [zslice]      (sem argumentos: todos)

Gera um projeto (build/qa/aishots/*.bslstage) com o video de teste (testmedia/h264_1080p.mp4) e o efeito na cadeia do clip,
abre o app com layout limpo (--qa), dispara o clip, espera a IA carregar (Relight, Aura e Z-Slice usam a IA local de
verdade) e pede o screenshot pelo comando "shot" do app. Precisa do Resolume? Nao. Mexe no mouse so para tirar o ponteiro
de cima dos controles.

O Aura precisa de movimento: o script gera build/qa/aishots/person_sway.mp4 (com o ffmpeg do third_party) balancando
testmedia/person.jpg para os lados. O zslice usa a propria foto como clip e opera o assistente "Rotear zonas para slices..."
com o mouse de verdade antes de tirar o screenshot do Mapeamento (e confere o resultado no estado do app).
"""
import glob
import json
import os
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import driver  # noqa: E402

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
EXE = os.path.join(ROOT, "build", "Release", "BSLStage.exe")
VIDEO = os.path.join(ROOT, "testmedia", "h264_1080p.mp4").replace("\\", "/")
PERSON = os.path.join(ROOT, "testmedia", "person.jpg").replace("\\", "/")
DOCS = os.path.join(ROOT, "docs")
OUT = os.path.join(ROOT, "build", "qa", "aishots")


def project(effects, name, video=VIDEO, layer_effects=None, output=None, fit="fit"):
    """Projeto de uma camada com um clip e a cadeia de efeitos no clip (ou na camada)."""
    os.makedirs(OUT, exist_ok=True)
    layer = {"name": "Video", "opacity": 1, "blend": "normal", "bypass": False, "solo": False}
    if layer_effects:
        layer["effects"] = layer_effects
    doc = {
        "format": "bslstage",
        "version": 1,
        "composition": {"width": 1920, "height": 1080, "fps": 60, "bpm": 124, "master": {"opacity": 1, "bypass": False}, "currentDeck": 0},
        "layers": [layer],
        "decks": [{"name": "Show", "columns": 8, "clips": [
            {"layer": 0, "column": 0, "path": video, "speed": 1, "loopMode": "loop", "fitMode": fit, "effects": effects}]}],
        "output": output or {"screens": [{"name": "Tela 1", "kind": "disabled", "width": 1920, "height": 1080, "slices": [{"name": "Slice 1"}]}]},
    }
    path = os.path.join(OUT, name)
    with open(path, "w", encoding="utf-8") as f:
        json.dump(doc, f, indent=2)
    return path


def sway_video():
    """Video de teste: a foto da pessoa balancando para os lados (a IA precisa de movimento para o Aura)."""
    out = os.path.join(OUT, "person_sway.mp4")
    if os.path.exists(out):
        return out.replace("\\", "/")
    os.makedirs(OUT, exist_ok=True)
    ff = glob.glob(os.path.join(ROOT, "third_party", "ffmpeg", "*", "bin", "ffmpeg.exe"))[0]
    vf = ("scale=2304:1296:force_original_aspect_ratio=increase,"
          "crop=1920:1080:x='(iw-ow)/2+110*sin(2*PI*t/2.4)':y='(ih-oh)/2+10*sin(2*PI*t/1.7)',format=yuv420p")
    subprocess.run([ff, "-y", "-hide_banner", "-loglevel", "error", "-loop", "1", "-framerate", "30", "-i", PERSON.replace("/", os.sep),
                    "-t", "12", "-vf", vf, "-c:v", "mpeg4", "-q:v", "2", out], check=True)
    return out.replace("\\", "/")


def shot(proj, out_name, settle, tab="efeitos"):
    qa_dir = os.path.join(OUT, "qa")
    app = driver.App(EXE, qa_dir, [proj, "--trigger", "1", "1", "--tab", tab], size=(1920, 1080))
    try:
        time.sleep(settle)
        app.wait_frames(10)
        app.hover(960, 1075)  # mouse fora dos controles
        app.wait_frames(6)
        path = os.path.join(DOCS, out_name)
        app.screenshot(path)
        print("ok", path)
    finally:
        app.quit()


def vhs():
    fx = [{"type": "bsl:vhs", "bypass": False, "params": {"preset": "worn", "playText": True, "dateStamp": True}}]
    shot(project(fx, "vhs.bslstage"), "screenshot-vhs.png", 6.0)


def relight():
    fx = [{"type": "bsl:relight-25d", "bypass": False, "params": {
        "relief": 45, "bump": 160, "ambient": 18,
        "l1_on": True, "l1_x": 18, "l1_y": 30, "l1_z": 30, "l1_color": [1.0, 0.55, 0.2, 1.0], "l1_intensity": 170, "l1_radius": 140,
        "l2_on": True, "l2_x": 82, "l2_y": 70, "l2_z": 30, "l2_color": [0.25, 0.55, 1.0, 1.0], "l2_intensity": 170, "l2_radius": 140}}]
    shot(project(fx, "relight.bslstage"), "screenshot-relight.png", 12.0)


def aura():
    fx = [{"type": "bsl:performer-aura", "bypass": False, "params": {
        "mode": "aura", "colorA": [0.1, 0.55, 1.0, 1.0], "colorB": [1.0, 0.2, 0.75, 1.0],
        "intensity": 150, "glow": 60, "motionForce": 170, "thickness": 12, "dissipation": 30, "quality": "mid",
        "cutPerson": True}}]
    shot(project(fx, "aura.bslstage", video=sway_video()), "screenshot-aura.png", 16.0)


def slice_json(name, x0):
    """Slice que mostra o canvas inteiro (1920x1080) num quadro 16:9 da tela de saida."""
    w, h = 600, 337.5
    y0 = 150.0
    return {"name": name, "enabled": True, "input": {"x": 0, "y": 0, "w": 1920, "h": 1080},
            "corners": [{"x": x0, "y": y0}, {"x": x0 + w, "y": y0}, {"x": x0 + w, "y": y0 + h}, {"x": x0, "y": y0 + h}]}


def zslice():
    zs = [{"type": "bsl:z-slice", "bypass": False, "params": {"zones": "3", "t1": 46, "t2": 20, "feather": 8}}]
    out = {"screens": [{"name": "Telao", "kind": "disabled", "width": 1920, "height": 1080, "slices": [
        slice_json("Totem frente", 20), slice_json("Painel meio", 660), slice_json("Parede fundo", 1300)]}]}
    proj = project([], "zslice.bslstage", video=PERSON, layer_effects=zs, output=out, fit="fill")
    app = driver.App(EXE, os.path.join(OUT, "qa"), [proj, "--trigger", "1", "1", "--tab", "camada"], size=(1920, 1080))
    try:
        time.sleep(14.0)  # a IA de profundidade carrega
        app.wait_frames(10)
        # Assistente do painel do efeito (Camada): a sugestao ja liga Frente, Meio e Fundo aos 3 slices, em ordem
        app.click("##camada_efeito_1_zrotear")
        app.find("##camada_efeito_1_zrota_aplicar", timeout=5.0)
        app.click("##camada_efeito_1_zrota_aplicar")
        st = app.wait_state(lambda s: [sl["zFilter"] for sl in s["screens"][0]["slices"]] == [1, 2, 3],
                            "zonas roteadas Frente, Meio, Fundo", timeout=5.0)
        print("zFilter dos slices:", [sl["zFilter"] for sl in st["screens"][0]["slices"]],
              "zSource:", [sl["zSource"] for sl in st["screens"][0]["slices"]])
        # Mapeamento: selecionar o primeiro slice mostra a zona nas Propriedades
        app.click("###modo_mapeamento", "MainMenuBar")
        app.wait_state(lambda s: s["mode"] == "mapping", "modo Mapeamento", timeout=4.0)
        app.wait_frames(20)
        # O assistente entra no desfazer do Mapeamento: desfazer zera as zonas, refazer devolve
        undo0 = app.state["mapping"]["undo"]
        if undo0 < 1:
            raise driver.QaFailure("o assistente nao entrou no desfazer do Mapeamento (undo=%d)" % undo0)
        app.click("##map_desfazer", "Entrada")
        app.wait_state(lambda s: [sl["zFilter"] for sl in s["screens"][0]["slices"]] == [0, 0, 0], "desfazer zera as zonas", timeout=4.0)
        app.click("##map_refazer", "Entrada")
        app.wait_state(lambda s: [sl["zFilter"] for sl in s["screens"][0]["slices"]] == [1, 2, 3], "refazer devolve as zonas", timeout=4.0)
        print("desfazer/refazer ok (undo=%d)" % undo0)
        app.click("Totem frente", "Lista")
        app.hover(960, 1075)
        app.wait_frames(30)
        path = os.path.join(DOCS, "screenshot-zslice.png")
        app.screenshot(path)
        print("ok", path)
    finally:
        app.quit()


def main():
    steps = {"vhs": vhs, "relight": relight, "aura": aura, "zslice": zslice}
    wanted = sys.argv[1:] or list(steps)
    for name in wanted:
        steps[name]()


if __name__ == "__main__":
    main()
