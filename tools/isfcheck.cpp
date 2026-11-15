#include "plugin/IsfCheck.h"
#include "platform/ScanUtil.h"

#include <glad/gl.h>
#include <GLFW/glfw3.h>

#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace bsl;

namespace {

int usage()
{
    std::fprintf(stderr,
                 "Uso: bslstage_isfcheck <arquivo.fs | pasta> [...] [--png saida.png] [--png-dir pasta] [--time s] [--size LxA]\n"
                 "                      [--input imagem] [--set nome=valor] [--audio] [--json] [--allow-blank]\n");
    return 2;
}

std::vector<fs::path> collect(const fs::path& p)
{
    std::vector<fs::path> out;
    std::error_code ec;
    if (fs::is_directory(p, ec)) {
        for (fs::recursive_directory_iterator it(p, ec), end; !ec && it != end; it.increment(ec))
            if (it->is_regular_file() && it->path().extension() == ".fs")
                out.push_back(it->path());
        std::sort(out.begin(), out.end());
    } else {
        out.push_back(p);
    }
    return out;
}

}

int main(int argc, char** argv)
{
    SetConsoleOutputCP(CP_UTF8);

    std::vector<fs::path> inputs;
    plugin::CheckOptions opt;
    fs::path pngDir;
    bool json = false, allowBlank = false;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&](const char* what) -> const char* {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "faltou o valor de %s\n", what);
                return nullptr;
            }
            return argv[++i];
        };
        if (a == "--png") {
            const char* v = next("--png");
            if (!v)
                return usage();
            opt.pngOut = fs::path(std::u8string(reinterpret_cast<const char8_t*>(v)));
        } else if (a == "--png-dir") {
            const char* v = next("--png-dir");
            if (!v)
                return usage();
            pngDir = fs::path(std::u8string(reinterpret_cast<const char8_t*>(v)));
        } else if (a == "--time") {
            const char* v = next("--time");
            if (!v)
                return usage();
            opt.time = std::atof(v);
        } else if (a == "--size") {
            const char* v = next("--size");
            int w = 0, h = 0;
            const char* cur = v;
            if (!v || !(bslplat::scan::integer(cur, w) && bslplat::scan::text(cur, "x") && bslplat::scan::integer(cur, h)) || w < 16 || h < 16 || w > 4096 || h > 4096) {
                std::fprintf(stderr, "--size espera LARGURAxALTURA (ex.: 1280x720), de 16 a 4096\n");
                return usage();
            }
            opt.width = w;
            opt.height = h;
        } else if (a == "--input") {
            const char* v = next("--input");
            if (!v)
                return usage();
            opt.inputImage = fs::path(std::u8string(reinterpret_cast<const char8_t*>(v)));
        } else if (a == "--set") {
            const char* v = next("--set");
            if (!v)
                return usage();
            const std::string kv = v;
            const size_t eq = kv.find('=');
            if (eq == std::string::npos || eq == 0) {
                std::fprintf(stderr, "--set espera nome=valor\n");
                return usage();
            }
            opt.sets.emplace_back(kv.substr(0, eq), kv.substr(eq + 1));
        } else if (a == "--audio") {
            opt.audio = true;
        } else if (a == "--json") {
            json = true;
        } else if (a == "--allow-blank") {
            allowBlank = true;
        } else if (a == "-h" || a == "--help") {
            usage();
            return 0;
        } else if (!a.empty() && a[0] == '-') {
            std::fprintf(stderr, "opcao desconhecida: %s\n", a.c_str());
            return usage();
        } else {
            for (const fs::path& f : collect(fs::path(std::u8string(reinterpret_cast<const char8_t*>(argv[i])))))
                inputs.push_back(f);
        }
    }
    if (inputs.empty())
        return usage();
    if (!opt.pngOut.empty() && inputs.size() > 1) {
        std::fprintf(stderr, "--png vale para um arquivo so; use --png-dir para vários\n");
        return usage();
    }

    if (!glfwInit()) {
        std::fprintf(stderr, "Não foi possível iniciar o GLFW.\n");
        return 2;
    }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    GLFWwindow* win = glfwCreateWindow(64, 64, "bslstage_isfcheck", nullptr, nullptr);
    if (!win) {
        std::fprintf(stderr, "Este computador não tem OpenGL 3.3 (ou o driver não abriu um contexto).\n");
        glfwTerminate();
        return 2;
    }
    glfwMakeContextCurrent(win);
    if (!gladLoadGL(glfwGetProcAddress)) {
        std::fprintf(stderr, "Falha ao carregar o OpenGL.\n");
        return 2;
    }
    if (!json)
        std::printf("GPU: %s\n\n", reinterpret_cast<const char*>(glGetString(GL_RENDERER)));

    int exitCode = 0;
    std::string jsonOut = "[";
    for (size_t i = 0; i < inputs.size(); ++i) {
        const fs::path& f = inputs[i];
        plugin::CheckOptions o = opt;
        if (!pngDir.empty())
            o.pngOut = pngDir / (f.stem().u8string() + u8".png");
        const plugin::CheckResult r = plugin::checkIsfFile(f, o);
        const std::string label = [&] {
            const std::u8string u = f.filename().u8string();
            return std::string(u.begin(), u.end());
        }();
        if (json) {
            jsonOut += (i ? ",\n" : "\n") + r.toJson(label);
        } else {
            std::printf("%s\n", r.toText(label).c_str());
            if (!o.pngOut.empty() && r.rendered) {
                const std::u8string u = o.pngOut.u8string();
                std::printf("PNG: %s\n\n", std::string(u.begin(), u.end()).c_str());
            }
        }
        if (!r.ok())
            exitCode = std::max(exitCode, 1);
        else if (r.blank && !allowBlank && exitCode == 0)
            exitCode = 3;
    }
    if (json)
        std::printf("%s\n]\n", jsonOut.c_str());
    else if (inputs.size() > 1)
        std::printf("%zu arquivo(s) verificados: %s\n", inputs.size(), exitCode == 0 ? "tudo certo" : "há problemas (veja acima)");

    glfwDestroyWindow(win);
    glfwTerminate();
    return exitCode;
}
