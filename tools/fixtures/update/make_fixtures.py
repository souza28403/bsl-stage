"""Gera os fixtures do bslstage_updatetest (sem rede): um .zip com deflate de verdade (Huffman dinamico), a lista de hashes
esperados e o SHA256SUMS.txt do release de teste.

Uso:  py -3 tools/fixtures/update/make_fixtures.py

Os JSONs de release (release-*.json) sao escritos a mao no formato da API do GitHub e nao sao gerados aqui.
O conteudo e deterministico (semente fixa), entao rodar de novo gera os mesmos arquivos de texto; o .zip muda so pela data.
"""
import hashlib
import os
import random
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
TOP = "BSL-Stage-1.2.0/"


def words_text(seed, n):
    rnd = random.Random(seed)
    vocab = ["palco", "camada", "projeção", "mapeamento", "painel", "LED", "clip", "efeito", "saída", "tela", "preto",
             "brilho", "ganho", "borda", "canvas", "slice", "show", "luz", "vídeo", "áudio", "BPM", "atalho"]
    out = []
    for i in range(n):
        out.append("%s %s %d" % (rnd.choice(vocab), rnd.choice(vocab), rnd.randint(0, 9999)))
    return ("\n".join(out) + "\n").encode("utf-8")


def main():
    files = [
        (TOP + "BSLStage.exe", b"MZ-fake-executable-v1.2.0\n" * 40, zipfile.ZIP_DEFLATED),
        (TOP + "bslstage_worker.exe", b"MZ-fake-worker-v1.2.0\n", zipfile.ZIP_STORED),
        (TOP + "data/big.txt", words_text(7, 6000), zipfile.ZIP_DEFLATED),
        (TOP + "data/small.txt", "olá, palco\n".encode("utf-8"), zipfile.ZIP_DEFLATED),
        (TOP + "data/empty.txt", b"", zipfile.ZIP_STORED),
        (TOP + "assets/", b"", zipfile.ZIP_STORED),
    ]
    zpath = os.path.join(HERE, "BSL-Stage-1.2.0-win64.zip")
    with zipfile.ZipFile(zpath, "w") as z:
        for name, data, method in files:
            info = zipfile.ZipInfo(name, date_time=(2026, 11, 3, 12, 0, 0))
            info.compress_type = method
            z.writestr(info, data, compresslevel=9) if method == zipfile.ZIP_DEFLATED else z.writestr(info, data)

    with open(os.path.join(HERE, "expected.txt"), "w", encoding="utf-8", newline="\n") as f:
        for name, data, _ in files:
            if not name.endswith("/"):
                f.write("%s  %s\n" % (hashlib.sha256(data).hexdigest(), name[len(TOP):]))

    zhash = hashlib.sha256(open(zpath, "rb").read()).hexdigest()
    with open(os.path.join(HERE, "SHA256SUMS.txt"), "w", encoding="utf-8", newline="\n") as f:
        f.write("# gerado por make_fixtures.py\n")
        f.write("%s  BSL-Stage-1.2.0-win64.zip\n" % zhash)
        f.write("%s *BSL-Stage-1.2.0-src.zip\n" % ("0" * 64))
    print("ok", zpath, zhash)


if __name__ == "__main__":
    main()
