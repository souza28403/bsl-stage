"""Screenshot de documentação: slice ligado a uma camada própria (item 8 do Melhorias).

  docs/screenshot-slice-camada.png   Mapeamento com o slice "Testeira" ligado a uma camada só dele (só nos slices
                                     vinculados, sem Strobe) e o slice "Central" na composição, onde a camada "Central"
                                     tem Strobe: a Testeira não pisca.

Uso:  py -3 tools/uiqa/sliceshots.py

Não usa mouse nem teclado: monta um projeto .bslstage em build/qa/sliceshots e abre o app com --screenshot.
"""
import json
import os
import subprocess
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
EXE = os.path.join(ROOT, "build", "Release", "BSLStage.exe")
OUT = os.path.join(ROOT, "docs", "screenshot-slice-camada.png")
WORK = os.path.join(ROOT, "build", "qa", "sliceshots")


def slice_json(name, inp, corners, source=None):
    x, y, w, h = inp
    j = {
        "name": name, "enabled": True,
        "input": {"x": x, "y": y, "w": w, "h": h},
        "corners": [{"x": cx, "y": cy} for cx, cy in corners],
        "flipH": False, "flipV": False, "opacity": 1, "gain": [1, 1, 1], "brightness": 0,
        "softEdges": [{"width": 0, "gamma": 1}] * 4,
    }
    if source:
        j["source"] = "layer"
        j["layerId"] = source
        j["includeMaster"] = False
    return j


def rect(x0, y0, x1, y1):
    return [(x0, y0), (x1, y0), (x1, y1), (x0, y1)]


def main():
    os.makedirs(WORK, exist_ok=True)
    project = {
        "format": "bslstage", "version": 1,
        "composition": {"width": 1920, "height": 1080, "fps": 60, "bpm": 128, "currentDeck": 0, "nextLayerId": 3},
        "layers": [
            {"id": 1, "name": "Central", "opacity": 1, "blend": "normal", "bypass": False, "solo": False,
             "effects": [{"type": "native:strobe", "bypass": False,
                          "params": {"mode": "sync", "division": "2", "duty": 0.9}}]},
            {"id": 2, "name": "Testeira", "opacity": 1, "blend": "normal", "bypass": False, "solo": False,
             "sliceOnly": True},
        ],
        "decks": [{"name": "Deck 1", "columns": 2, "clips": [
            {"layer": 0, "column": 0, "generator": "gen:padrao-de-teste", "speed": 1, "loopMode": "loop", "fitMode": "fit"},
            {"layer": 1, "column": 0, "generator": "gen:texto", "speed": 1, "loopMode": "loop", "fitMode": "fit",
             "params": {"text": "TESTEIRA", "size": 300, "bg": True, "bgMode": "full", "bgOpacity": 1.0,
                        "bgColor": [0.1, 0.35, 0.85, 1.0]}},
        ]}],
        "output": {"screens": [{
            "name": "Palco", "enabled": True, "kind": "disabled", "width": 1920, "height": 1080,
            "spoutName": "BSL Stage - Palco", "ndiName": "BSL Stage - Palco", "ndiBgra": False,
            "slices": [
                slice_json("Testeira", (0, 455, 1920, 170), rect(0, 0, 1920, 170), source=2),
                slice_json("Central", (0, 0, 1920, 1080), rect(240, 200, 1680, 1010)),
            ]}]},
    }
    proj_path = os.path.join(WORK, "slice-camada.bslstage")
    with open(proj_path, "w", encoding="utf-8") as f:
        json.dump(project, f, ensure_ascii=False, indent=2)

    if os.path.exists(OUT):
        os.remove(OUT)
    cmd = [EXE, proj_path, "--mode", "mapping", "--trigger", "1", "1", "--trigger", "2", "1",
           "--window-size", "1920", "1080", "--ui-mode", "window", "--screenshot", OUT, "6"]
    subprocess.run(cmd, check=False, timeout=120)
    if not os.path.exists(OUT):
        print("screenshot não gerado")
        return 1
    print("ok", OUT)
    return 0


if __name__ == "__main__":
    sys.exit(main())
