#include "fx/IsfDoc.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <regex>
#include <set>
#include <sstream>

namespace bsl::fx {

namespace {

using Json = nlohmann::json;

constexpr size_t kMaxInputs = 128;
constexpr size_t kMaxPasses = 32;

bool isIdentStart(char c)
{
    return std::isalpha(static_cast<unsigned char>(c)) || c == '_';
}

bool isIdentChar(char c)
{
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
}

bool validName(const std::string& s)
{
    if (s.empty() || !isIdentStart(s[0]))
        return false;
    return std::all_of(s.begin(), s.end(), isIdentChar);
}

int countLines(const std::string& s)
{
    return static_cast<int>(std::count(s.begin(), s.end(), '\n'));
}

// Numero tolerante: numero, booleano (0/1) ou texto numerico
bool asNumber(const Json& j, double& out)
{
    if (j.is_number()) {
        out = j.get<double>();
        return std::isfinite(out);
    }
    if (j.is_boolean()) {
        out = j.get<bool>() ? 1.0 : 0.0;
        return true;
    }
    if (j.is_string()) {
        char* end = nullptr;
        const std::string s = j.get<std::string>();
        out = std::strtod(s.c_str(), &end);
        return end != s.c_str() && std::isfinite(out);
    }
    return false;
}

double numberOr(const Json& obj, const char* key, double def)
{
    const auto it = obj.find(key);
    double v = def;
    if (it != obj.end() && asNumber(*it, v))
        return v;
    return def;
}

bool boolOr(const Json& obj, const char* key, bool def)
{
    const auto it = obj.find(key);
    double v = 0.0;
    if (it != obj.end() && asNumber(*it, v))
        return v != 0.0;
    return def;
}

std::string stringOr(const Json& obj, const char* key)
{
    const auto it = obj.find(key);
    return (it != obj.end() && it->is_string()) ? it->get<std::string>() : std::string();
}

template <size_t N>
bool numberArray(const Json& obj, const char* key, float (&out)[N], size_t minCount)
{
    const auto it = obj.find(key);
    if (it == obj.end() || !it->is_array() || it->size() < minCount)
        return false;
    for (size_t i = 0; i < N && i < it->size(); ++i) {
        double v = 0.0;
        if (!asNumber((*it)[i], v))
            return false;
        out[i] = static_cast<float>(v);
    }
    return true;
}

IsfInputType typeFromString(const std::string& t)
{
    if (t == "float") return IsfInputType::Float;
    if (t == "bool") return IsfInputType::Bool;
    if (t == "long") return IsfInputType::Long;
    if (t == "color") return IsfInputType::Color;
    if (t == "point2D") return IsfInputType::Point2D;
    if (t == "event") return IsfInputType::Event;
    if (t == "image") return IsfInputType::Image;
    if (t == "audio") return IsfInputType::Audio;
    if (t == "audioFFT") return IsfInputType::AudioFft;
    if (t == "cube") return IsfInputType::Cube;
    return IsfInputType::Unknown;
}

bool isSamplerType(IsfInputType t)
{
    return t == IsfInputType::Image || t == IsfInputType::Audio || t == IsfInputType::AudioFft || t == IsfInputType::Cube;
}

// "linha" do arquivo onde comeca o offset (1-based)
int lineOfOffset(const std::string& s, size_t offset)
{
    return 1 + static_cast<int>(std::count(s.begin(), s.begin() + static_cast<std::ptrdiff_t>(std::min(offset, s.size())), '\n'));
}

std::string exprFromJson(const Json& obj, const char* key)
{
    const auto it = obj.find(key);
    if (it == obj.end())
        return {};
    if (it->is_string())
        return it->get<std::string>();
    if (it->is_number())
        return it->dump();
    return {};
}

} // namespace

bool IsfDoc::isEffect() const
{
    return std::any_of(inputs.begin(), inputs.end(),
                       [](const IsfInput& i) { return i.type == IsfInputType::Image && i.name == "inputImage"; });
}

IsfParseResult parseIsf(const std::string& fragmentIn, const std::string& vertex, const std::string& name)
{
    IsfParseResult res;
    std::string fragment = fragmentIn;
    if (fragment.compare(0, 3, "\xEF\xBB\xBF") == 0)
        fragment.erase(0, 3); // BOM

    const size_t open = fragment.find("/*");
    if (open == std::string::npos) {
        res.error = "Sem o cabeçalho JSON do ISF (comentário /* ... */ no começo do arquivo).";
        return res;
    }
    const size_t close = fragment.find("*/", open + 2);
    if (close == std::string::npos) {
        res.error = "Comentário JSON do ISF não foi fechado (falta */).";
        return res;
    }
    const std::string jsonText = fragment.substr(open + 2, close - open - 2);
    const int commentLine = lineOfOffset(fragment, open);

    Json root;
    try {
        root = Json::parse(jsonText, nullptr, true, true); // aceita comentarios dentro do JSON
    } catch (const Json::parse_error& e) {
        // O texto do nlohmann é "[json.exception.parse_error.101] parse error at line 3, column 5: <detalhe>"; só o detalhe interessa
        std::string detail = e.what();
        const size_t colon = detail.find(": ", detail.find("column"));
        if (colon != std::string::npos)
            detail = detail.substr(colon + 2);
        res.error = "Cabeçalho JSON inválido na linha " + std::to_string(commentLine + lineOfOffset(jsonText, e.byte) - 1)
                    + " do arquivo: " + detail;
        return res;
    }
    if (!root.is_object()) {
        res.error = "O cabeçalho JSON do ISF precisa ser um objeto { ... }.";
        return res;
    }

    auto doc = std::make_shared<IsfDoc>();
    doc->name = name;
    doc->description = stringOr(root, "DESCRIPTION");
    doc->credit = stringOr(root, "CREDIT");
    doc->fragmentSource = fragment;
    doc->vertexSource = vertex;
    if (const auto it = root.find("CATEGORIES"); it != root.end() && it->is_array())
        for (const Json& c : *it)
            if (c.is_string())
                doc->categories.push_back(c.get<std::string>());

    if (const auto it = root.find("INPUTS"); it != root.end()) {
        if (!it->is_array()) {
            res.error = "INPUTS precisa ser uma lista.";
            return res;
        }
        std::set<std::string> seen;
        for (const Json& ij : *it) {
            if (!ij.is_object())
                continue;
            if (doc->inputs.size() >= kMaxInputs) {
                doc->warnings.push_back("Entradas demais: o restante foi ignorado.");
                break;
            }
            IsfInput in;
            in.name = stringOr(ij, "NAME");
            if (!validName(in.name)) {
                doc->warnings.push_back("Entrada com NAME inválido ignorada: \"" + in.name + "\"");
                continue;
            }
            if (!seen.insert(in.name).second) {
                doc->warnings.push_back("Entrada repetida ignorada: " + in.name);
                continue;
            }
            in.label = stringOr(ij, "LABEL");
            if (in.label.empty())
                in.label = in.name;
            in.type = typeFromString(stringOr(ij, "TYPE"));
            switch (in.type) {
            case IsfInputType::Float:
                in.minV = numberOr(ij, "MIN", 0.0);
                in.maxV = numberOr(ij, "MAX", 1.0);
                if (in.maxV <= in.minV)
                    in.maxV = in.minV + 1.0;
                in.defV = std::clamp(numberOr(ij, "DEFAULT", in.minV), in.minV, in.maxV);
                break;
            case IsfInputType::Bool:
                in.defBool = boolOr(ij, "DEFAULT", false);
                break;
            case IsfInputType::Long: {
                if (const auto vit = ij.find("VALUES"); vit != ij.end() && vit->is_array())
                    for (const Json& v : *vit) {
                        double d = 0.0;
                        if (asNumber(v, d))
                            in.values.push_back(static_cast<long long>(std::llround(d)));
                    }
                if (const auto lit = ij.find("LABELS"); lit != ij.end() && lit->is_array())
                    for (const Json& l : *lit)
                        in.labels.push_back(l.is_string() ? l.get<std::string>() : l.dump());
                if (in.values.empty()) { // sem VALUES: faixa MIN..MAX
                    const long long lo = std::llround(numberOr(ij, "MIN", 0.0));
                    const long long hi = std::min(std::llround(numberOr(ij, "MAX", 1.0)), lo + 63);
                    for (long long v = lo; v <= hi; ++v)
                        in.values.push_back(v);
                }
                in.defV = numberOr(ij, "DEFAULT", static_cast<double>(in.values.front()));
                break;
            }
            case IsfInputType::Color:
                if (!numberArray(ij, "DEFAULT", in.defColor, 3))
                    in.defColor[0] = in.defColor[1] = in.defColor[2] = 0.0f;
                if (const auto it2 = ij.find("DEFAULT"); it2 == ij.end() || !it2->is_array() || it2->size() < 4)
                    in.defColor[3] = 1.0f;
                break;
            case IsfInputType::Point2D:
                numberArray(ij, "DEFAULT", in.defPoint, 2);
                numberArray(ij, "MIN", in.minPoint, 2);
                numberArray(ij, "MAX", in.maxPoint, 2);
                break;
            case IsfInputType::Event:
            case IsfInputType::Image:
                break;
            case IsfInputType::Audio:
            case IsfInputType::AudioFft:
                doc->warnings.push_back("Entrada de áudio \"" + in.name + "\" não é suportada (ignorada).");
                break;
            case IsfInputType::Cube:
                doc->warnings.push_back("Entrada cube \"" + in.name + "\" não é suportada (ignorada).");
                break;
            case IsfInputType::Unknown:
                doc->warnings.push_back("Entrada \"" + in.name + "\" com TYPE desconhecido ignorada.");
                continue;
            }
            doc->inputs.push_back(std::move(in));
        }
    }

    if (const auto it = root.find("PASSES"); it != root.end() && it->is_array()) {
        for (const Json& pj : *it) {
            if (!pj.is_object())
                continue;
            if (doc->passes.size() >= kMaxPasses) {
                doc->warnings.push_back("Passes demais: o restante foi ignorado.");
                break;
            }
            IsfPass p;
            p.target = stringOr(pj, "TARGET");
            if (!p.target.empty() && !validName(p.target)) {
                doc->warnings.push_back("TARGET inválido ignorado: " + p.target);
                p.target.clear();
            }
            p.persistent = boolOr(pj, "PERSISTENT", false);
            p.isFloat = boolOr(pj, "FLOAT", false);
            p.widthExpr = exprFromJson(pj, "WIDTH");
            p.heightExpr = exprFromJson(pj, "HEIGHT");
            doc->passes.push_back(std::move(p));
        }
    }

    if (const auto it = root.find("IMPORTED"); it != root.end()) {
        auto add = [&](const std::string& nm, const Json& v) {
            if (!validName(nm) || !v.is_object())
                return;
            if (stringOr(v, "TYPE") == "cube") {
                doc->warnings.push_back("Imagem cube \"" + nm + "\" não é suportada (ignorada).");
                return;
            }
            const std::string path = stringOr(v, "PATH");
            if (!path.empty())
                doc->imported.push_back({nm, path});
        };
        if (it->is_object()) {
            for (auto& [k, v] : it->items())
                add(k, v);
        } else if (it->is_array()) {
            for (const Json& v : *it)
                if (v.is_object())
                    add(stringOr(v, "NAME"), v);
        }
    }

    res.ok = true;
    res.doc = std::move(doc);
    return res;
}

std::vector<media::ParamDesc> isfParamDescs(const IsfDoc& doc)
{
    using namespace media::param;
    std::vector<media::ParamDesc> out;
    for (const IsfInput& in : doc.inputs) {
        switch (in.type) {
        case IsfInputType::Float:
            out.push_back(makeFloat(in.name, in.label, static_cast<float>(in.defV), static_cast<float>(in.minV),
                                    static_cast<float>(in.maxV), "%.3f"));
            break;
        case IsfInputType::Bool:
            out.push_back(makeBool(in.name, in.label, in.defBool));
            break;
        case IsfInputType::Long: {
            std::vector<media::ParamOption> opts;
            for (size_t i = 0; i < in.values.size(); ++i)
                opts.push_back({std::to_string(in.values[i]),
                                i < in.labels.size() && !in.labels[i].empty() ? in.labels[i]
                                                                              : std::to_string(in.values[i])});
            out.push_back(makeEnum(in.name, in.label, std::move(opts),
                                   std::to_string(static_cast<long long>(std::llround(in.defV)))));
            break;
        }
        case IsfInputType::Color:
            out.push_back(makeColor(in.name, in.label, {in.defColor[0], in.defColor[1], in.defColor[2], in.defColor[3]}));
            break;
        case IsfInputType::Point2D: {
            media::ParamDesc d = makeCustom(in.name, in.label, "point2d",
                                            media::Json::array({floatJson(in.defPoint[0]), floatJson(in.defPoint[1])}));
            d.min = std::min(in.minPoint[0], in.minPoint[1]);
            d.max = std::max(in.maxPoint[0], in.maxPoint[1]);
            if (d.max <= d.min) { // MIN/MAX nao informados
                d.min = std::min<double>({d.min, in.defPoint[0], in.defPoint[1]});
                d.max = std::max<double>({d.min + 1.0, 4096.0, in.defPoint[0], in.defPoint[1]});
            }
            out.push_back(std::move(d));
            break;
        }
        case IsfInputType::Event: {
            media::ParamDesc d = makeCustom(in.name, in.label, "event", media::Json(0));
            d.min = 0.0;
            d.max = 2147483647.0; // contador de cliques (nao pode ser limitado a 0..1)
            out.push_back(std::move(d));
            break;
        }
        default:
            break; // imagens e audio nao viram parametro
        }
    }
    return out;
}

// ---------------------------------------------------------------------------------------------------------------
// Expressoes
// ---------------------------------------------------------------------------------------------------------------

namespace {

class ExprParser
{
public:
    ExprParser(const std::string& s, const IsfVariableLookup& v) : s_(s), var_(v) {}

    bool parse(double& out)
    {
        if (!expr(out))
            return false;
        skip();
        return pos_ == s_.size() && std::isfinite(out);
    }

private:
    void skip()
    {
        while (pos_ < s_.size() && std::isspace(static_cast<unsigned char>(s_[pos_])))
            ++pos_;
    }

    bool expr(double& out)
    {
        if (!term(out))
            return false;
        for (;;) {
            skip();
            if (pos_ < s_.size() && (s_[pos_] == '+' || s_[pos_] == '-')) {
                const char op = s_[pos_++];
                double rhs = 0.0;
                if (!term(rhs))
                    return false;
                out = op == '+' ? out + rhs : out - rhs;
            } else {
                return true;
            }
        }
    }

    bool term(double& out)
    {
        if (!unary(out))
            return false;
        for (;;) {
            skip();
            if (pos_ < s_.size() && (s_[pos_] == '*' || s_[pos_] == '/')) {
                const char op = s_[pos_++];
                double rhs = 0.0;
                if (!unary(rhs))
                    return false;
                if (op == '*')
                    out *= rhs;
                else if (rhs != 0.0)
                    out /= rhs;
                else
                    return false;
            } else {
                return true;
            }
        }
    }

    bool unary(double& out)
    {
        skip();
        if (pos_ < s_.size() && (s_[pos_] == '-' || s_[pos_] == '+')) {
            const bool neg = s_[pos_++] == '-';
            if (!unary(out))
                return false;
            if (neg)
                out = -out;
            return true;
        }
        return primary(out);
    }

    bool primary(double& out)
    {
        skip();
        if (pos_ >= s_.size())
            return false;
        const char c = s_[pos_];
        if (c == '(') {
            ++pos_;
            if (!expr(out))
                return false;
            skip();
            if (pos_ >= s_.size() || s_[pos_] != ')')
                return false;
            ++pos_;
            return true;
        }
        if (std::isdigit(static_cast<unsigned char>(c)) || c == '.') {
            const char* begin = s_.c_str() + pos_;
            char* end = nullptr;
            out = std::strtod(begin, &end);
            if (end == begin)
                return false;
            pos_ += static_cast<size_t>(end - begin);
            return true;
        }
        const bool dollar = c == '$';
        if (dollar || isIdentStart(c)) {
            if (dollar)
                ++pos_;
            const size_t start = pos_;
            while (pos_ < s_.size() && isIdentChar(s_[pos_]))
                ++pos_;
            const std::string id = s_.substr(start, pos_ - start);
            if (id.empty())
                return false;
            skip();
            if (!dollar && pos_ < s_.size() && s_[pos_] == '(')
                return call(id, out);
            return var_ && var_(id, out);
        }
        return false;
    }

    bool call(const std::string& fn, double& out)
    {
        ++pos_; // (
        std::vector<double> args;
        skip();
        if (pos_ < s_.size() && s_[pos_] == ')') {
            ++pos_;
        } else {
            for (;;) {
                double a = 0.0;
                if (!expr(a))
                    return false;
                args.push_back(a);
                skip();
                if (pos_ < s_.size() && s_[pos_] == ',') {
                    ++pos_;
                    continue;
                }
                if (pos_ < s_.size() && s_[pos_] == ')') {
                    ++pos_;
                    break;
                }
                return false;
            }
        }
        auto need = [&](size_t n) { return args.size() == n; };
        if (fn == "floor" && need(1)) out = std::floor(args[0]);
        else if (fn == "ceil" && need(1)) out = std::ceil(args[0]);
        else if (fn == "round" && need(1)) out = std::round(args[0]);
        else if (fn == "abs" && need(1)) out = std::fabs(args[0]);
        else if (fn == "sqrt" && need(1)) out = std::sqrt(std::max(0.0, args[0]));
        else if (fn == "min" && need(2)) out = std::min(args[0], args[1]);
        else if (fn == "max" && need(2)) out = std::max(args[0], args[1]);
        else if (fn == "pow" && need(2)) out = std::pow(args[0], args[1]);
        else return false;
        return true;
    }

    const std::string& s_;
    const IsfVariableLookup& var_;
    size_t pos_ = 0;
};

} // namespace

bool evalIsfExpression(const std::string& expr, const IsfVariableLookup& variable, double& result)
{
    ExprParser p(expr, variable);
    return p.parse(result);
}

// ---------------------------------------------------------------------------------------------------------------
// Geracao do GLSL
// ---------------------------------------------------------------------------------------------------------------

namespace {

std::string rewriteImpl(const std::string& s, bool frag);

// Procura o ')' que fecha o '(' em 'open'; separa os argumentos de profundidade 1. false se nao fecha.
bool splitCall(const std::string& s, size_t open, size_t& close, std::vector<std::string>& args)
{
    int depth = 0;
    size_t argStart = open + 1;
    for (size_t i = open; i < s.size(); ++i) {
        const char c = s[i];
        if (c == '(' || c == '[' || c == '{') {
            ++depth;
        } else if (c == ')' || c == ']' || c == '}') {
            --depth;
            if (depth == 0) {
                args.push_back(s.substr(argStart, i - argStart));
                close = i;
                return true;
            }
        } else if (c == ',' && depth == 1) {
            args.push_back(s.substr(argStart, i - argStart));
            argStart = i + 1;
        }
    }
    return false;
}

std::string trim(const std::string& s)
{
    size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a])))
        ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1])))
        --b;
    return s.substr(a, b - a);
}

// Fora de comentarios o GLSL so tem ASCII; dentro deles alguns drivers reclamam de UTF-8 (acentos do cabecalho JSON)
std::string asciiOnly(std::string s)
{
    for (char& c : s)
        if (static_cast<unsigned char>(c) >= 0x80)
            c = ' ';
    return s;
}

std::string rewriteImpl(const std::string& s, bool frag)
{
    std::string out;
    out.reserve(s.size() + 64);
    const size_t n = s.size();
    size_t i = 0;
    while (i < n) {
        const char c = s[i];
        if (c == '/' && i + 1 < n && s[i + 1] == '/') { // comentario de linha
            size_t j = s.find('\n', i);
            if (j == std::string::npos)
                j = n;
            out.append(s, i, j - i);
            i = j;
            continue;
        }
        if (c == '/' && i + 1 < n && s[i + 1] == '*') { // comentario de bloco
            size_t j = s.find("*/", i + 2);
            j = j == std::string::npos ? n : j + 2;
            out.append(s, i, j - i);
            i = j;
            continue;
        }
        if (std::isdigit(static_cast<unsigned char>(c)) || (c == '.' && i + 1 < n && std::isdigit(static_cast<unsigned char>(s[i + 1])))) {
            size_t j = i + 1; // literal numerico (1.0e-3, 0x1F, 2u): nao e identificador
            while (j < n) {
                const char d = s[j];
                if (std::isalnum(static_cast<unsigned char>(d)) || d == '.' || d == '_')
                    ++j;
                else if ((d == '+' || d == '-') && (s[j - 1] == 'e' || s[j - 1] == 'E'))
                    ++j;
                else
                    break;
            }
            out.append(s, i, j - i);
            i = j;
            continue;
        }
        if (!isIdentStart(c)) {
            out += c;
            ++i;
            continue;
        }
        size_t j = i;
        while (j < n && isIdentChar(s[j]))
            ++j;
        const std::string id = s.substr(i, j - i);

        // Troca simples de nomes
        if (id == "gl_FragColor") { out += "isf_FragColor"; i = j; continue; }
        if (id == "texture2D") { out += "texture"; i = j; continue; }
        if (id == "texture2DLod") { out += "textureLod"; i = j; continue; }
        if (id == "vv_FragNormCoord") { out += "isf_FragNormCoord"; i = j; continue; }
        if (id == "vv_vertShaderInit") { out += "isf_vertShaderInit"; i = j; continue; }
        if (id == "varying") { out += frag ? "in" : "out"; i = j; continue; }

        const bool isImg = id == "IMG_PIXEL" || id == "IMG_NORM_PIXEL" || id == "IMG_THIS_PIXEL"
                           || id == "IMG_THIS_NORM_PIXEL" || id == "IMG_SIZE";
        if (isImg) {
            size_t k = j;
            while (k < n && std::isspace(static_cast<unsigned char>(s[k])))
                ++k;
            size_t close = 0;
            std::vector<std::string> args;
            if (k < n && s[k] == '(' && splitCall(s, k, close, args) && !args.empty()) {
                const std::string name = trim(args[0]);
                const size_t want = (id == "IMG_PIXEL" || id == "IMG_NORM_PIXEL") ? 2 : 1;
                if (validName(name) && args.size() == want) {
                    std::string rep;
                    if (id == "IMG_PIXEL")
                        rep = "_isf_pixel(" + name + ", _" + name + "_imgSize, _" + name + "_flip, " + rewriteImpl(trim(args[1]), frag) + ")";
                    else if (id == "IMG_NORM_PIXEL")
                        rep = "_isf_normPixel(" + name + ", _" + name + "_flip, " + rewriteImpl(trim(args[1]), frag) + ")";
                    else if (id == "IMG_SIZE")
                        rep = "_" + name + "_imgSize";
                    else
                        rep = "_isf_normPixel(" + name + ", _" + name + "_flip, isf_FragNormCoord)";
                    // Mantem a contagem de linhas: as quebras que sumiram (entre o nome e os parenteses, no 1o argumento)
                    const std::string original = s.substr(i, close + 1 - i);
                    const int lost = countLines(original) - countLines(rep);
                    out += rep;
                    if (lost > 0)
                        out.append(static_cast<size_t>(lost), '\n');
                    i = close + 1;
                    continue;
                }
            }
        }
        out += id;
        i = j;
    }
    return out;
}

const char* glslTypeFor(IsfInputType t)
{
    switch (t) {
    case IsfInputType::Float: return "float";
    case IsfInputType::Bool:
    case IsfInputType::Event: return "bool";
    case IsfInputType::Long: return "int";
    case IsfInputType::Color: return "vec4";
    case IsfInputType::Point2D: return "vec2";
    default: return "";
    }
}

} // namespace

std::string rewriteIsfSource(const std::string& source, bool fragmentStage)
{
    return rewriteImpl(source, fragmentStage);
}

IsfSources buildIsfSources(const IsfDoc& doc)
{
    IsfSources out;

    // Samplers: inputs de imagem (e audio/cube, que viram uma textura vazia), imported e TARGET dos passes
    std::set<std::string> seen;
    auto addImage = [&](const std::string& nm) {
        if (validName(nm) && seen.insert(nm).second)
            out.imageNames.push_back(nm);
    };
    for (const IsfInput& in : doc.inputs)
        if (isSamplerType(in.type))
            addImage(in.name);
    for (const IsfImported& im : doc.imported)
        addImage(im.name);
    for (const IsfPass& p : doc.passes)
        if (!p.target.empty())
            addImage(p.target);

    std::string common;
    common += "uniform int PASSINDEX;\n";
    common += "uniform vec2 RENDERSIZE;\n";
    common += "uniform float TIME;\n";
    common += "uniform float TIMEDELTA;\n";
    common += "uniform vec4 DATE;\n";
    common += "uniform int FRAMEINDEX;\n";
    for (const IsfInput& in : doc.inputs) {
        const char* t = glslTypeFor(in.type);
        if (*t)
            common += std::string("uniform ") + t + " " + in.name + ";\n";
    }
    for (const std::string& nm : out.imageNames) {
        common += "uniform sampler2D " + nm + ";\n";
        common += "uniform vec2 _" + nm + "_imgSize;\n";
        common += "uniform bool _" + nm + "_flip;\n";
    }

    // Fragment
    std::string fp = "#version 330 core\n";
    fp += common;
    fp += "in vec2 isf_FragNormCoord;\n";
    fp += "out vec4 isf_FragColor;\n";
    fp += "vec4 _isf_normPixel(sampler2D s, bool flip, vec2 uv) { return texture(s, vec2(uv.x, flip ? 1.0 - uv.y : uv.y)); }\n";
    fp += "vec4 _isf_pixel(sampler2D s, vec2 sz, bool flip, vec2 p) { return _isf_normPixel(s, flip, p / sz); }\n";
    out.fragmentPreambleLines = countLines(fp);
    out.fragment = fp + asciiOnly(rewriteImpl(doc.fragmentSource, true));

    // Vertex
    std::string vp = "#version 330 core\n";
    vp += common;
    vp += "uniform float _isf_flipOut;\n";
    vp += "out vec2 isf_FragNormCoord;\n";
    vp += "void isf_vertShaderInit()\n{\n";
    vp += "    vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));\n";
    vp += "    isf_FragNormCoord = p;\n";
    vp += "    gl_Position = vec4(p.x * 2.0 - 1.0, (p.y * 2.0 - 1.0) * _isf_flipOut, 0.0, 1.0);\n";
    vp += "}\n";
    out.vertexPreambleLines = countLines(vp);
    if (doc.vertexSource.empty())
        out.vertex = vp + "void main()\n{\n    isf_vertShaderInit();\n}\n";
    else
        out.vertex = vp + asciiOnly(rewriteImpl(doc.vertexSource, false));
    return out;
}

IsfCompileLog remapGlslLog(const std::string& log, int preambleLines, const char* stage)
{
    static const std::regex nvidia(R"(^\s*(\d+)\((\d+)\)\s*:\s*(.*)$)");
    static const std::regex mesa(R"(^\s*(?:ERROR|WARNING)?\s*:?\s*(\d+):(\d+)(?:\(\d+\))?\s*:\s*(.*)$)");
    IsfCompileLog out;
    std::istringstream in(log);
    std::string line;
    std::string text;
    while (std::getline(in, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == '\0'))
            line.pop_back();
        if (trim(line).empty())
            continue;
        std::smatch m;
        std::string msg;
        int lineNo = 0;
        bool matched = false;
        if (std::regex_match(line, m, nvidia) || std::regex_match(line, m, mesa)) {
            matched = true;
            lineNo = std::atoi(m[2].str().c_str()) - preambleLines;
            msg = m[3].str();
        }
        if (!text.empty())
            text += '\n';
        if (matched && lineNo >= 1) {
            text += "Linha " + std::to_string(lineNo) + ": " + msg;
            if (out.firstLine == 0 && (line.find("rror") != std::string::npos || line.find("ERROR") != std::string::npos))
                out.firstLine = lineNo;
        } else if (matched) {
            text += std::string(stage ? stage : "shader") + " (preâmbulo interno): " + msg;
        } else {
            text += line;
        }
    }
    out.text = text;
    return out;
}

} // namespace bsl::fx
