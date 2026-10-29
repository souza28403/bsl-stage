"""Gera src/ui/Icons.h a partir do mapa de codepoints do Lucide (tools/data/lucide-info.json).

Uso:  py -3 tools/gen_icons.py

Lucide (ISC) - lucide-static 1.61.0, font/lucide.ttf em assets/fonts (licenca em assets/fonts/LICENSE-Lucide.txt).
Para usar outro icone: acrescente o nome do Lucide em ICONS abaixo e rode o script de novo.
"""
import json
import os
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
INFO = os.path.join(ROOT, "tools", "data", "lucide-info.json")
OUT = os.path.join(ROOT, "src", "ui", "Icons.h")
LUCIDE_VERSION = "1.61.0"

# Nome no Lucide -> constante (ICON_<NOME>); so os usados pela interface
ICONS = [
    # transporte
    ("play", "PLAY"), ("pause", "PAUSE"), ("square", "STOP"), ("minus", "MINUS"), ("plus", "PLUS"),
    ("rotate-ccw", "RESYNC"), ("skip-back", "SKIP_BACK"),
    # camadas
    ("x", "X"), ("eye", "EYE"), ("eye-off", "EYE_OFF"),
    # tipos de arquivo / pastas
    ("film", "FILM"), ("image", "IMAGE"), ("file-text", "FILE_TEXT"), ("presentation", "PRESENTATION"),
    ("file", "FILE"), ("globe", "GLOBE"), ("folder", "FOLDER"), ("folder-open", "FOLDER_OPEN"),
    ("corner-left-up", "CORNER_LEFT_UP"),
    # geradores
    ("palette", "PALETTE"), ("type", "TYPE"), ("monitor", "MONITOR"), ("video", "VIDEO"), ("grid-3x3", "GRID"),
    # clip
    ("chevron-left", "CHEVRON_LEFT"), ("chevron-right", "CHEVRON_RIGHT"), ("repeat", "REPEAT"),
    ("arrow-right-to-line", "ARROW_RIGHT_TO_LINE"), ("arrow-left-right", "ARROW_LEFT_RIGHT"),
    # mapeamento
    ("square-plus", "SQUARE_PLUS"), ("copy", "COPY"), ("trash-2", "TRASH"), ("scan", "SCAN"),
    # menus
    ("file-plus", "FILE_PLUS"), ("save", "SAVE"), ("save-all", "SAVE_ALL"),
    # efeitos (Fase 4)
    ("sparkles", "SPARKLES"), ("grip-vertical", "GRIP"), ("ellipsis", "ELLIPSIS"),
    ("chevron-down", "CHEVRON_DOWN"), ("move", "MOVE"), ("sliders-horizontal", "SLIDERS"),
    ("droplet", "DROPLET"), ("contrast", "CONTRAST"), ("sun", "SUN"), ("pipette", "PIPETTE"),
    ("square-dashed", "SQUARE_DASHED"), ("crop", "CROP"), ("flip-horizontal", "FLIP_H"),
    ("flower-2", "FLOWER"), ("grid-2x2", "GRID_2X2"), ("zap", "ZAP"), ("blend", "BLEND"),
    ("circle-dot", "CIRCLE_DOT"), ("layers", "LAYERS"), ("refresh-cw", "REFRESH"), ("folder-cog", "FOLDER_COG"),
    ("search", "SEARCH"), ("file-code", "FILE_CODE"), ("undo-2", "UNDO"),
    # plugins FFGL (Fase 4B)
    ("plug", "PLUG"), ("triangle-alert", "ALERT"), ("package-plus", "PACKAGE_PLUS"),
    # audio nativo e plugins BSL Light (Fase 4C)
    ("audio-lines", "AUDIO_LINES"), ("mic", "MIC"), ("headphones", "HEADPHONES"),
    # editor de mapeamento: ima, desfazer/refazer, vista, alinhar, distribuir e encostar
    ("magnet", "MAGNET"), ("redo-2", "REDO"), ("maximize", "MAXIMIZE"), ("ruler", "RULER"),
    ("align-start-vertical", "ALIGN_LEFT"), ("align-center-vertical", "ALIGN_CENTER_H"),
    ("align-end-vertical", "ALIGN_RIGHT"), ("align-start-horizontal", "ALIGN_TOP"),
    ("align-center-horizontal", "ALIGN_MIDDLE_V"), ("align-end-horizontal", "ALIGN_BOTTOM"),
    ("align-horizontal-distribute-center", "DISTRIBUTE_H"), ("align-vertical-distribute-center", "DISTRIBUTE_V"),
    ("arrow-up-to-line", "ARROW_UP_TO_LINE"), ("arrow-down-to-line", "ARROW_DOWN_TO_LINE"),
    ("arrow-left-to-line", "ARROW_LEFT_TO_LINE"),
    # Pixel Map: cadeado do input, exportar/importar, CSV, padrao HD
    ("lock", "LOCK"), ("lock-open", "LOCK_OPEN"), ("download", "DOWNLOAD"), ("upload", "UPLOAD"),
    ("file-spreadsheet", "FILE_SPREADSHEET"), ("image-down", "IMAGE_DOWN"), ("image-plus", "IMAGE_PLUS"),
]


def utf8_escape(codepoint):
    return "".join("\\x%02X" % b for b in chr(codepoint).encode("utf-8"))


def main():
    with open(INFO, "rb") as f:
        info = json.loads(f.read().decode("utf-8"))
    lines = [
        "// GERADO por tools/gen_icons.py - nao edite a mao.",
        "// Lucide (ISC) lucide-static %s: font/lucide.ttf em assets/fonts (licenca em LICENSE-Lucide.txt)." % LUCIDE_VERSION,
        "#pragma once",
        "",
        "// Literais UTF-8 de um glifo na area de uso privado (macros, para concatenar: ICON_PLAY \"##id\").",
        "// So aparecem com a fonte de icones mesclada (Theme.cpp).",
    ]
    missing = []
    for name, const in ICONS:
        entry = info.get(name)
        if not entry:
            missing.append(name)
            continue
        code = int(entry["encodedCode"].lstrip("\\"), 16)
        lines.append('#define ICON_%s "%s" // %s U+%04X' % (const, utf8_escape(code), name, code))
    lines.append("")
    if missing:
        print("icones inexistentes no Lucide: %s" % ", ".join(missing), file=sys.stderr)
        return 1
    with open(OUT, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(lines))
    print("%d icones -> %s" % (len(ICONS), os.path.relpath(OUT, ROOT)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
