#include "update/Apply.h"
#include "update/IssueReport.h"
#include "update/Markdown.h"
#include "update/Policy.h"
#include "update/Release.h"
#include "update/SemVer.h"
#include "update/Service.h"
#include "update/Sha256.h"
#include "update/Zip.h"

#include <windows.h>

#include <chrono>
#include <cstdio>
#include <fstream>
#include <functional>
#include <iterator>
#include <map>
#include <random>
#include <sstream>
#include <thread>

using namespace bsl::update;
namespace fs = std::filesystem;

namespace {

int g_failures = 0;
int g_checks = 0;

void check(bool ok, const std::string& what)
{
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::printf("  FALHA: %s\n", what.c_str());
    }
}

#define CHECK(cond) check((cond), #cond " (linha " + std::to_string(__LINE__) + ")")
#define CHECK_MSG(cond, msg) check((cond), std::string(msg) + " (linha " + std::to_string(__LINE__) + ")")

std::string readFile(const fs::path& p)
{
    std::ifstream in(p, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

void writeFile(const fs::path& p, const std::string& data)
{
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);
    std::ofstream out(p, std::ios::binary | std::ios::trunc);
    out << data;
}

fs::path fixture(const char* name)
{
    return fs::path(BSL_FIXTURES_DIR) / "update" / name;
}

bool waitFor(const std::function<bool()>& pred, int timeoutMs = 8000)
{
    const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (std::chrono::steady_clock::now() < end) {
        if (pred())
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return pred();
}

std::string makeStoredZip(const std::vector<std::pair<std::string, std::string>>& files, bool corruptCrc = false)
{
    std::string out, central;
    auto le16 = [](std::string& s, uint16_t v) { s.push_back(char(v & 255)); s.push_back(char(v >> 8)); };
    auto le32 = [](std::string& s, uint32_t v) { for (int i = 0; i < 4; ++i) s.push_back(char((v >> (8 * i)) & 255)); };
    for (const auto& [name, data] : files) {
        uint32_t crc = crc32Update(0, data.data(), data.size());
        if (corruptCrc)
            crc ^= 0xFFu;
        const uint32_t offset = static_cast<uint32_t>(out.size());
        le32(out, 0x04034b50); le16(out, 20); le16(out, 0); le16(out, 0); le16(out, 0); le16(out, 0x21);
        le32(out, crc); le32(out, static_cast<uint32_t>(data.size())); le32(out, static_cast<uint32_t>(data.size()));
        le16(out, static_cast<uint16_t>(name.size())); le16(out, 0);
        out += name;
        out += data;
        le32(central, 0x02014b50); le16(central, 20); le16(central, 20); le16(central, 0); le16(central, 0); le16(central, 0);
        le16(central, 0x21); le32(central, crc); le32(central, static_cast<uint32_t>(data.size()));
        le32(central, static_cast<uint32_t>(data.size())); le16(central, static_cast<uint16_t>(name.size()));
        le16(central, 0); le16(central, 0); le16(central, 0); le16(central, 0); le32(central, 0); le32(central, offset);
        central += name;
    }
    const uint32_t cdOffset = static_cast<uint32_t>(out.size());
    out += central;
    le32(out, 0x06054b50); le16(out, 0); le16(out, 0); le16(out, static_cast<uint16_t>(files.size()));
    le16(out, static_cast<uint16_t>(files.size())); le32(out, static_cast<uint32_t>(central.size())); le32(out, cdOffset);
    le16(out, 0);
    return out;
}

std::map<std::string, std::string> snapshot(const fs::path& dir)
{
    std::map<std::string, std::string> m;
    std::error_code ec;
    for (auto it = fs::recursive_directory_iterator(dir, ec); it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (it->is_regular_file(ec))
            m[fs::relative(it->path(), dir, ec).generic_string()] = readFile(it->path());
    }
    return m;
}

void testSemVer()
{
    std::printf("[semver]\n");
    CHECK(parseSemVer("1.2.3").valid);
    CHECK(parseSemVer("v1.2.3").valid);
    CHECK(parseSemVer("V1.2").valid && parseSemVer("V1.2").patch == 0);
    CHECK(parseSemVer("1").valid && parseSemVer("1").minor == 0);
    CHECK(!parseSemVer("").valid);
    CHECK(!parseSemVer("abc").valid);
    CHECK(!parseSemVer("1.2.3.4").valid);
    CHECK(!parseSemVer("1.2.x").valid);
    CHECK(!parseSemVer("1.2.3-").valid);
    CHECK(parseSemVer("1.2.3+build.5").valid && parseSemVer("1.2.3+build.5").pre.empty());
    CHECK(parseSemVer("v1.3.0-beta.2").isPrerelease());
    CHECK(parseSemVer("v1.3.0-beta.2").toString() == "1.3.0-beta.2");

    auto cmp = [](const char* a, const char* b) { return compare(parseSemVer(a), parseSemVer(b)); };
    CHECK(cmp("1.2.3", "1.2.3") == 0);
    CHECK(cmp("1.2.3", "1.2.4") < 0);
    CHECK(cmp("1.10.0", "1.9.0") > 0);
    CHECK(cmp("2.0.0", "1.99.99") > 0);
    CHECK(cmp("v1.2.3", "1.2.3") == 0);
    CHECK(cmp("1.2.3+a", "1.2.3+b") == 0);
    CHECK(cmp("1.0.0-alpha", "1.0.0") < 0);
    const char* order[] = {"1.0.0-alpha", "1.0.0-alpha.1", "1.0.0-alpha.beta", "1.0.0-beta", "1.0.0-beta.2", "1.0.0-beta.11",
                           "1.0.0-rc.1", "1.0.0"};
    for (size_t i = 0; i + 1 < std::size(order); ++i)
        CHECK_MSG(cmp(order[i], order[i + 1]) < 0, std::string(order[i]) + " < " + order[i + 1]);
    CHECK(isNewer(parseSemVer("1.2.0"), parseSemVer("1.1.9")));
    CHECK(!isNewer(parseSemVer("1.2.0"), parseSemVer("1.2.0")));
    CHECK(!isNewer(parseSemVer("garbage"), parseSemVer("0.1.0")));
    CHECK(compare(parseSemVer("x"), parseSemVer("0.0.1")) < 0);
}

void testRelease()
{
    std::printf("[release]\n");
    Release r;
    std::string err;
    CHECK(parseRelease(readFile(fixture("release-latest.json")), r, &err));
    CHECK(r.tag == "v1.2.0" && r.version.valid && r.version.minor == 2);
    CHECK(!r.prerelease && !r.draft && !r.isBeta());
    CHECK(r.assets.size() == 4);
    CHECK(r.published == "2026-11-03T12:00:00Z");
    CHECK(r.body.find("Atualizações pelo GitHub") != std::string::npos);
    CHECK(formatPublishedDate(r.published) == "03/11/2026");
    CHECK(formatPublishedDate("lixo").empty());

    Release bad;
    CHECK(!parseRelease("isto nao e json", bad, &err) && !err.empty());
    CHECK(!parseRelease("{\"name\":\"sem tag\"}", bad, &err));
    CHECK(!parseRelease("[1,2]", bad, &err));

    const AssetChoice c = chooseAsset(r);
    CHECK(c.kind == AssetKind::Zip && c.asset && c.asset->name == "BSL-Stage-1.2.0-win64.zip");
    const Asset* sums = findChecksumsAsset(r);
    CHECK(sums && sums->name == "SHA256SUMS.txt");

    Release inst;
    CHECK(parseRelease(readFile(fixture("release-installer-only.json")), inst, &err));
    const AssetChoice ci = chooseAsset(inst);
    CHECK(ci.kind == AssetKind::Installer && ci.asset && ci.asset->name == "BSL-Stage-2.0.0-Setup.exe");
    CHECK(findChecksumsAsset(inst) == nullptr);

    Release none;
    CHECK(parseRelease("{\"tag_name\":\"v3.0.0\",\"assets\":[{\"name\":\"codigo.zip\",\"browser_download_url\":\"https://github.com/x/y/s.zip\"},"
                       "{\"name\":\"BSL-Stage-3.0.0-src.zip\",\"browser_download_url\":\"https://github.com/x/y/t.zip\"}]}", none));
    CHECK(chooseAsset(none).kind == AssetKind::Zip);
    Release onlySrc;
    CHECK(parseRelease("{\"tag_name\":\"v3.0.0\",\"assets\":[{\"name\":\"BSL-Stage-3.0.0-src.zip\",\"browser_download_url\":\"https://github.com/x/y/t.zip\"}]}", onlySrc));
    CHECK(chooseAsset(onlySrc).kind == AssetKind::None);

    Release withMsi;
    CHECK(parseRelease("{\"tag_name\":\"v1.4.0\",\"assets\":["
                       "{\"name\":\"BSL-Stage-1.4.0-win64.zip\",\"browser_download_url\":\"https://github.com/x/y/z.zip\"},"
                       "{\"name\":\"BSL-Stage-1.4.0-src.msi\",\"browser_download_url\":\"https://github.com/x/y/s.msi\"},"
                       "{\"name\":\"BSL-Stage-1.4.0-win64.msi\",\"browser_download_url\":\"https://github.com/x/y/m.msi\"}]}", withMsi));
    const AssetChoice cz = chooseAsset(withMsi, false);
    CHECK(cz.kind == AssetKind::Zip && cz.asset && cz.asset->name == "BSL-Stage-1.4.0-win64.zip");
    const AssetChoice cm = chooseAsset(withMsi, true);
    CHECK(cm.kind == AssetKind::Msi && cm.asset && cm.asset->name == "BSL-Stage-1.4.0-win64.msi");
    CHECK(chooseAsset(r, true).kind == AssetKind::None);
    Release onlyMsi;
    CHECK(parseRelease("{\"tag_name\":\"v1.4.0\",\"assets\":[{\"name\":\"BSL-Stage-1.4.0-win64.msi\",\"browser_download_url\":\"https://github.com/x/y/m.msi\"}]}", onlyMsi));
    CHECK(chooseAsset(onlyMsi, false).kind == AssetKind::None);
    CHECK(chooseAsset(onlyMsi, true).kind == AssetKind::Msi);

    std::vector<Release> list;
    CHECK(parseReleaseList(readFile(fixture("release-list.json")), list, &err));
    CHECK(list.size() == 5);
    int stable = pickNewest(list, false);
    CHECK(stable >= 0 && list[static_cast<size_t>(stable)].tag == "v1.2.0");
    int beta = pickNewest(list, true);
    CHECK(beta >= 0 && list[static_cast<size_t>(beta)].tag == "v1.3.0-beta.10");
    CHECK(pickNewest({}, true) == -1);

    const auto sumsTable = parseChecksums("# comentario\r\n"
                                          "ABCDEF0123456789abcdef0123456789abcdef0123456789abcdef0123456789  BSL-Stage-1.2.0-win64.zip\r\n"
                                          "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef *./dist/Outro Arquivo.zip\n"
                                          "linha invalida\n"
                                          "zzzzzz456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef  nao-hex.zip\n");
    CHECK(sumsTable.size() == 2);
    CHECK(sumsTable.count("bsl-stage-1.2.0-win64.zip") && sumsTable.at("bsl-stage-1.2.0-win64.zip")[0] == 'a');
    CHECK(sumsTable.count("outro arquivo.zip"));

    CHECK(isTrustedDownloadUrl("https://github.com/bsl-light/bsl-stage/releases/download/v1/x.zip"));
    CHECK(isTrustedDownloadUrl("https://objects.githubusercontent.com/abc"));
    CHECK(!isTrustedDownloadUrl("http://github.com/x"));
    CHECK(!isTrustedDownloadUrl("https://github.com.evil.com/x"));
    CHECK(!isTrustedDownloadUrl("https://evil.com/github.com/x"));
    CHECK(!isTrustedDownloadUrl("https://github.com@evil.com/x"));
    CHECK(!isTrustedDownloadUrl("ftp://github.com/x"));
    CHECK(safeFileName("..\\..\\evil.zip") == "evil.zip");
    CHECK(safeFileName("a:b*c?.zip") == "abc.zip");
    CHECK(safeFileName("..").empty());
}

void testSha256()
{
    std::printf("[sha256]\n");
    CHECK(sha256Hex("") == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    CHECK(sha256Hex("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    CHECK(sha256Hex("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq")
          == "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
    Sha256 h;
    std::string chunk(997, 'a');
    size_t left = 1000000;
    while (left > 0) {
        const size_t n = std::min(left, chunk.size());
        h.update(chunk.data(), n);
        left -= n;
    }
    CHECK(Sha256::toHex(h.finish()) == "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");

    const fs::path tmp = fs::temp_directory_path() / ("bslupd_sha_" + std::to_string(GetCurrentProcessId()) + ".bin");
    writeFile(tmp, "abc");
    std::string hex;
    CHECK(sha256File(tmp, hex) && hex == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    CHECK(!sha256File(tmp, hex, [](uint64_t) { return false; }) && hex.empty());
    CHECK(!sha256File(tmp.string() + ".nao-existe", hex));
    std::error_code ec;
    fs::remove(tmp, ec);
}

void testZip()
{
    std::printf("[zip]\n");
    CHECK(isSafeZipPath("a/b/c.txt"));
    CHECK(isSafeZipPath("pasta/"));
    CHECK(!isSafeZipPath("../x"));
    CHECK(!isSafeZipPath("a/../../x"));
    CHECK(!isSafeZipPath("/abs"));
    CHECK(!isSafeZipPath("C:/x"));
    CHECK(!isSafeZipPath("a\\b"));
    CHECK(!isSafeZipPath("a//b"));
    CHECK(!isSafeZipPath(".. /x"));
    CHECK(!isSafeZipPath("..."));
    CHECK(!isSafeZipPath(""));
    CHECK(crc32Update(0, "123456789", 9) == 0xCBF43926u);

    ZipReader zip;
    std::string err;
    CHECK_MSG(zip.open(fixture("BSL-Stage-1.2.0-win64.zip"), &err), err);
    std::map<std::string, std::string> expected;
    {
        std::istringstream in(readFile(fixture("expected.txt")));
        std::string hash, name;
        while (in >> hash >> name)
            expected[name] = hash;
    }
    int checked = 0;
    for (const auto& [name, hash] : expected) {
        const ZipEntry* e = zip.find("BSL-Stage-1.2.0/" + name);
        CHECK_MSG(e != nullptr, "entrada " + name);
        if (!e)
            continue;
        std::string data;
        CHECK_MSG(zip.readAll(*e, data, &err), name + ": " + err);
        CHECK_MSG(sha256Hex(data) == hash, "conteudo de " + name);
        ++checked;
    }
    CHECK(checked == 5);
    const ZipEntry* big = zip.find("BSL-Stage-1.2.0/data/big.txt");
    CHECK(big && big->method == 8 && big->compressedSize < big->size / 2);
    const ZipEntry* dir = zip.find("BSL-Stage-1.2.0/assets/");
    CHECK(dir && dir->isDirectory);

    const fs::path tmp = fs::temp_directory_path() / ("bslupd_zip_" + std::to_string(GetCurrentProcessId()));
    std::error_code ec;
    fs::remove_all(tmp, ec);
    CHECK(big && zip.extract(*big, tmp / "sub" / "big.txt", &err));
    CHECK(sha256Hex(readFile(tmp / "sub" / "big.txt")) == expected["data/big.txt"]);

    std::string raw = readFile(fixture("BSL-Stage-1.2.0-win64.zip"));
    writeFile(tmp / "ruim.zip", raw.substr(0, raw.size() / 2));
    ZipReader bad;
    CHECK(!bad.open(tmp / "ruim.zip", &err));
    writeFile(tmp / "texto.zip", "isto nao e um zip, so texto comum com mais de 22 bytes");
    CHECK(!bad.open(tmp / "texto.zip", &err));
    CHECK(!bad.open(tmp / "nao-existe.zip", &err));
    {
        std::string flipped = makeStoredZip({{"a.txt", "conteudo importante"}});
        flipped[40] ^= 0x55;
        writeFile(tmp / "crc.zip", flipped);
        ZipReader z;
        CHECK(z.open(tmp / "crc.zip", &err));
        std::string data;
        CHECK(!z.readAll(z.entries()[0], data, &err) && err.find("CRC") != std::string::npos);
    }
    fs::remove_all(tmp, ec);
}

void testPolicy()
{
    std::printf("[politica]\n");
    const int64_t day = kCheckIntervalSeconds;
    Prefs p;
    CHECK(p.autoCheck && !p.beta && p.skippedVersion.empty() && p.lastCheckUnix == 0);
    const int64_t now = 1'800'000'000;
    CHECK(dueForAutoCheck(p, now));
    p.lastCheckUnix = now - 60;
    CHECK(!dueForAutoCheck(p, now));
    p.lastCheckUnix = now - day + 1;
    CHECK(!dueForAutoCheck(p, now));
    p.lastCheckUnix = now - day;
    CHECK(dueForAutoCheck(p, now));
    p.lastCheckUnix = now + 3600;
    CHECK(!dueForAutoCheck(p, now));
    p.lastCheckUnix = now + 3 * day;
    CHECK(dueForAutoCheck(p, now));
    p.lastCheckUnix = 0;
    p.autoCheck = false;
    CHECK(!dueForAutoCheck(p, now));

    ShowState s;
    CHECK(!showBlocksAutoCheck(s));
    ShowState a = s; a.outputLive = true;       CHECK(showBlocksAutoCheck(a));
    ShowState b = s; b.windowFullscreen = true; CHECK(showBlocksAutoCheck(b));
    ShowState c = s; c.layoutLocked = true;     CHECK(showBlocksAutoCheck(c));
    ShowState d = s; d.ecoProfile = true;       CHECK(showBlocksAutoCheck(d));

    Release stable, beta;
    parseRelease("{\"tag_name\":\"v1.2.0\"}", stable);
    parseRelease("{\"tag_name\":\"v1.3.0-beta.1\",\"prerelease\":true}", beta);
    const SemVer cur = parseSemVer("1.1.0");
    Prefs pr;
    CHECK(shouldOffer(pr, stable, cur));
    CHECK(!shouldOffer(pr, stable, parseSemVer("1.2.0")));
    CHECK(!shouldOffer(pr, stable, parseSemVer("1.3.0")));
    CHECK(!shouldOffer(pr, beta, cur));
    pr.beta = true;
    CHECK(shouldOffer(pr, beta, cur));
    pr.beta = false;
    pr.skippedVersion = "v1.2.0";
    CHECK(isSkipped(pr, stable) && !shouldOffer(pr, stable, cur));
    CHECK(isUpdateFor(pr, stable, cur));
    pr.skippedVersion = "1.2.0";
    CHECK(isSkipped(pr, stable));
    Release newer;
    parseRelease("{\"tag_name\":\"v1.2.1\"}", newer);
    CHECK(!isSkipped(pr, newer) && shouldOffer(pr, newer, cur));
    Release draft;
    parseRelease("{\"tag_name\":\"v5.0.0\",\"draft\":true}", draft);
    CHECK(!shouldOffer(Prefs{}, draft, cur));
    Release stable130;
    parseRelease("{\"tag_name\":\"v1.3.0\"}", stable130);
    CHECK(shouldOffer(Prefs{}, stable130, parseSemVer("1.3.0-beta.4")));
}

void testMarkdown()
{
    std::printf("[markdown]\n");
    Release r;
    parseRelease(readFile(fixture("release-latest.json")), r);
    const auto blocks = parseMarkdown(r.body);
    int h2 = 0, h3 = 0, bullets = 0, numbered = 0, quotes = 0, paragraphs = 0;
    bool noMarks = true;
    for (const MdBlock& b : blocks) {
        h2 += b.kind == MdKind::Heading2;
        h3 += b.kind == MdKind::Heading3;
        bullets += b.kind == MdKind::Bullet;
        numbered += b.kind == MdKind::Numbered;
        quotes += b.kind == MdKind::Quote;
        paragraphs += b.kind == MdKind::Paragraph;
        if (b.text.find("**") != std::string::npos || b.text.find('`') != std::string::npos || b.text.find("](") != std::string::npos)
            noMarks = false;
    }
    CHECK(h2 == 1 && h3 == 1 && bullets == 2 && numbered == 2 && quotes == 1 && paragraphs == 1);
    CHECK(noMarks);
    CHECK(blocks.front().kind == MdKind::Heading2 && blocks.front().text == "Novidades");
    CHECK(blocks[1].kind == MdKind::Bullet && blocks[1].text.rfind("Atualizações pelo GitHub: avisa", 0) == 0);
    CHECK(stripInlineMarkdown("**negrito** e `codigo` e [link](https://x.y) e ![img](a.png) <!-- c --> fim") == "negrito e codigo e link e img  fim");
    const auto code = parseMarkdown("antes\n```\nlinha 1\n  linha 2\n```\ndepois\n---\n- a\n  - b\n");
    CHECK(code.size() == 7);
    CHECK(code[1].kind == MdKind::Code && code[1].text == "linha 1" && code[2].text == "  linha 2");
    CHECK(code[4].kind == MdKind::Rule);
    CHECK(code[6].kind == MdKind::Bullet && code[6].indent == 1);
    CHECK(parseMarkdown("").empty());
    const auto joined = parseMarkdown("linha um\nlinha dois\n\nnovo paragrafo");
    CHECK(joined.size() == 2 && joined[0].text == "linha um linha dois");
}

void testIssue()
{
    std::printf("[issue]\n");
    CHECK(urlEncode("abc-._~123") == "abc-._~123");
    CHECK(urlEncode("a b&c=d\n") == "a%20b%26c%3Dd%0A");
    CHECK(urlEncode("ação") == "a%C3%A7%C3%A3o");
    CHECK(std::string(labelFor(ReportKind::Bug)) == "bug" && std::string(labelFor(ReportKind::Improvement)) == "enhancement");

    ReportInput in;
    in.kind = ReportKind::Bug;
    in.title = "Tela preta";
    in.description = "A saida ficou preta.\r\nDepois do show.";
    in.steps = "1. abrir\n2. tocar";
    in.system = {"0.1.0", "Windows 11 (build 26200)", "NVIDIA RTX", "4.6.0 NVIDIA 555.1", "Balanceado", "16,0 GB"};
    in.logTail = "[info] linha do log";
    std::string body = buildReportBody(in);
    CHECK(body.find("### Descrição do problema") != std::string::npos);
    CHECK(body.find("### Passos para reproduzir") != std::string::npos);
    CHECK(body.find("Informações do sistema") == std::string::npos && body.find("NVIDIA") == std::string::npos);
    CHECK(body.find("linha do log") == std::string::npos);
    CHECK(body.find('\r') == std::string::npos);
    in.includeSystem = true;
    in.includeLog = true;
    body = buildReportBody(in);
    CHECK(body.find("### Informações do sistema") != std::string::npos && body.find("Windows 11 (build 26200)") != std::string::npos
          && body.find("4.6.0 NVIDIA 555.1") != std::string::npos && body.find("16,0 GB") != std::string::npos);
    CHECK(body.find("```\n[info] linha do log\n```") != std::string::npos);
    in.kind = ReportKind::Improvement;
    CHECK(buildReportBody(in).find("### Descrição da ideia") != std::string::npos);
    ReportInput empty;
    CHECK(buildReportBody(empty).find("não informado") != std::string::npos);

    const IssueUrl small = buildIssueUrl("bsl-light/bsl-stage", ReportKind::Bug, "Tela preta", body);
    CHECK(!small.truncated && small.remainder.empty());
    CHECK(small.url.rfind("https://github.com/bsl-light/bsl-stage/issues/new?title=Tela%20preta&body=", 0) == 0);
    CHECK(small.url.size() >= 6 && small.url.substr(small.url.size() - 11) == "&labels=bug");
    CHECK(small.url.find(' ') == std::string::npos && small.url.find('\n') == std::string::npos);
    const IssueUrl enh = buildIssueUrl("a/b", ReportKind::Improvement, "t", "b");
    CHECK(enh.url.substr(enh.url.size() - 19) == "&labels=enhancement");

    std::string big;
    std::mt19937 rng(42);
    while (big.size() < 20000) {
        big += "linha de teste com acentuação, símbolos & = ? # e número " + std::to_string(rng() % 100000) + "\n";
    }
    const IssueUrl cut = buildIssueUrl("bsl-light/bsl-stage", ReportKind::Bug, "Titulo longo " + std::string(300, 'x'), big);
    CHECK_MSG(cut.truncated, "corpo grande deve ser cortado");
    CHECK_MSG(cut.url.size() <= kMaxIssueUrl, "URL de " + std::to_string(cut.url.size()) + " caracteres");
    CHECK(cut.url.size() > kMaxIssueUrl - 400);
    CHECK(!cut.remainder.empty());
    const std::string kept = cut.bodyInUrl.substr(0, cut.bodyInUrl.find("\n\n_[texto cortado"));
    CHECK_MSG(kept + cut.remainder == big, "prefixo + resto == corpo original");
    CHECK(cut.bodyInUrl.find("área de transferência") != std::string::npos);
    CHECK(cut.url.find("&labels=bug") != std::string::npos);
    std::string utf(3000, 'x');
    for (size_t i = 0; i < 3000; ++i)
        utf += "ç";
    const IssueUrl cutUtf = buildIssueUrl("a/b", ReportKind::Bug, "t", utf);
    CHECK(cutUtf.truncated && cutUtf.url.size() <= kMaxIssueUrl);
    CHECK(cutUtf.url.find("%C3%A7") != std::string::npos);
    bool halfChar = false;
    for (size_t p = cutUtf.url.find("%C3"); p != std::string::npos; p = cutUtf.url.find("%C3", p + 1))
        if (p + 4 >= cutUtf.url.size() || cutUtf.url[p + 3] != '%' || std::string("89AB").find(cutUtf.url[p + 4]) == std::string::npos)
            halfChar = true;
    CHECK(!halfChar);
    const IssueUrl tight = buildIssueUrl("a/b", ReportKind::Bug, "t", std::string(2000, 'y'), 2200);
    CHECK(!tight.truncated);
    const IssueUrl tight2 = buildIssueUrl("a/b", ReportKind::Bug, "t", std::string(2300, 'y'), 2200);
    CHECK(tight2.truncated && tight2.url.size() <= 2200);

    const std::string log = "[info] abriu C:\\Users\\Gabriel Souza\\Desktop\\show.bslstage\n[aviso] PC-DO-GABRIEL sem placa\n"
                            "[info] c:/users/gabriel souza/appdata ok\n";
    const std::string red = redactPersonalData(log, "C:\\Users\\Gabriel Souza", "Gabriel Souza", "PC-DO-GABRIEL");
    CHECK(red.find("Gabriel Souza") == std::string::npos && red.find("PC-DO-GABRIEL") == std::string::npos);
    CHECK(red.find("%USERPROFILE%\\Desktop\\show.bslstage") != std::string::npos);
    CHECK(red.find("%USERPROFILE%/appdata") != std::string::npos);
    CHECK(tailLines("a\nb\nc\nd\n", 2, 1000) == "c\nd");
    CHECK(tailLines("a\nb", 10, 1000) == "a\nb");
    CHECK(tailLines(std::string(100, 'z') + "\nfim\n", 5, 20) == "fim");
}

struct TestRepo
{
    static constexpr const char* kRepo = "bsl-light/bsl-stage";
    static std::string api(const char* suffix) { return std::string("https://api.github.com/repos/") + kRepo + "/releases" + suffix; }
    static std::string asset(const char* name) { return std::string("https://github.com/bsl-light/bsl-stage/releases/download/v1.2.0/") + name; }
};

FakeHttp* addGoodRelease(FakeHttp& http)
{
    http.addBody(TestRepo::api("/latest"), readFile(fixture("release-latest.json")));
    http.addBody(TestRepo::api("?per_page=10"), readFile(fixture("release-list.json")));
    http.addFile(TestRepo::asset("BSL-Stage-1.2.0-win64.zip"), fixture("BSL-Stage-1.2.0-win64.zip"));
    http.addFile(TestRepo::asset("SHA256SUMS.txt"), fixture("SHA256SUMS.txt"));
    return &http;
}

void testService(const fs::path& root)
{
    std::printf("[servico]\n");
    const Prefs prefs;

    {
        auto http = std::make_unique<FakeHttp>();
        FakeHttp* h = addGoodRelease(*http);
        Service sv;
        sv.configure("1.1.0", TestRepo::kRepo, root / "svc1");
        sv.setHttp(std::move(http));
        sv.startCheck(prefs, false);
        CHECK(waitFor([&] { return !sv.status().checking && sv.status().hasUpdate; }));
        const Status st = sv.status();
        CHECK(st.phase == Phase::Available && st.release.tag == "v1.2.0" && st.assetName == "BSL-Stage-1.2.0-win64.zip");
        CHECK(sv.brief().hasUpdate && sv.brief().version == "1.2.0");
        CHECK(h->requests.size() == 1 && h->requests[0] == TestRepo::api("/latest"));
        CheckOutcome o;
        CHECK(waitFor([&] { return sv.takeCheckOutcome(o); }) && o.ok && !o.manual && o.checkedUnix > 1'700'000'000);
        CHECK(!sv.takeCheckOutcome(o));
        CHECK(fs::exists(root / "svc1" / "last-release.json"));

        Service sv2;
        sv2.configure("1.1.0", TestRepo::kRepo, root / "svc1");
        sv2.loadPersistedState(prefs);
        CHECK(sv2.brief().hasUpdate && sv2.brief().version == "1.2.0" && sv2.status().phase == Phase::Available);
        Service sv3;
        sv3.configure("1.2.0", TestRepo::kRepo, root / "svc1");
        sv3.loadPersistedState(prefs);
        CHECK(!sv3.brief().hasUpdate);
        CHECK(!fs::exists(root / "svc1" / "last-release.json"));
    }

    {
        auto http = std::make_unique<FakeHttp>();
        addGoodRelease(*http);
        Service sv;
        sv.configure("1.2.0", TestRepo::kRepo, root / "svc2");
        sv.setHttp(std::move(http));
        sv.startCheck(prefs, true);
        CHECK(waitFor([&] { return !sv.busy() && !sv.status().checking; }));
        CHECK(!sv.status().hasUpdate && sv.status().upToDate && sv.status().checkError.empty());
        CheckOutcome o;
        CHECK(sv.takeCheckOutcome(o) && o.ok && o.manual);
    }
    {
        auto http = std::make_unique<FakeHttp>();
        Service sv;
        sv.configure("1.0.0", TestRepo::kRepo, root / "svc3");
        sv.setHttp(std::move(http));
        sv.startCheck(prefs, true);
        CHECK(waitFor([&] { return !sv.busy(); }));
        CHECK(!sv.status().hasUpdate && !sv.status().checkNote.empty() && sv.status().checkError.empty());
        CheckOutcome o;
        CHECK(sv.takeCheckOutcome(o) && o.ok);
    }
    {
        auto http = std::make_unique<FakeHttp>();
        http->addOffline(TestRepo::api("/latest"));
        Service sv;
        sv.configure("1.0.0", TestRepo::kRepo, root / "svc4");
        sv.setHttp(std::move(http));
        sv.startCheck(prefs, false);
        CHECK(waitFor([&] { return !sv.busy(); }));
        CheckOutcome o;
        CHECK(sv.takeCheckOutcome(o) && !o.ok && o.offline && !o.manual);
        CHECK(!sv.status().hasUpdate && sv.status().checkOffline);
        CHECK(sv.brief().phase == Phase::Idle && !sv.brief().hasUpdate);
    }
    {
        auto http = std::make_unique<FakeHttp>();
        http->addBody(TestRepo::api("/latest"), "<html>nao e json</html>");
        Service sv;
        sv.configure("1.0.0", TestRepo::kRepo, root / "svc5");
        sv.setHttp(std::move(http));
        sv.startCheck(prefs, true);
        CHECK(waitFor([&] { return !sv.busy(); }));
        CheckOutcome o;
        CHECK(sv.takeCheckOutcome(o) && !o.ok && !o.error.empty());
    }

    {
        auto http = std::make_unique<FakeHttp>();
        FakeHttp* h = addGoodRelease(*http);
        Service sv;
        sv.configure("1.2.0", TestRepo::kRepo, root / "svc6");
        sv.setHttp(std::move(http));
        Prefs beta;
        beta.beta = true;
        sv.startCheck(beta, true);
        CHECK(waitFor([&] { return !sv.busy(); }));
        CHECK(sv.status().hasUpdate && sv.status().release.tag == "v1.3.0-beta.10");
        CHECK(h->requests.size() == 1 && h->requests[0] == TestRepo::api("?per_page=10"));
        Service sv2;
        sv2.configure("1.2.0", TestRepo::kRepo, root / "svc6b");
        auto http2 = std::make_unique<FakeHttp>();
        addGoodRelease(*http2);
        sv2.setHttp(std::move(http2));
        sv2.startCheck(prefs, true);
        CHECK(waitFor([&] { return !sv2.busy(); }));
        CHECK(!sv2.status().hasUpdate);
    }

    {
        auto http = std::make_unique<FakeHttp>();
        addGoodRelease(*http);
        http->setChunks(6);
        Service sv;
        sv.configure("1.1.0", TestRepo::kRepo, root / "svc7");
        sv.setHttp(std::move(http));
        sv.startCheck(prefs, true);
        CHECK(waitFor([&] { return sv.status().hasUpdate; }));
        CHECK(waitFor([&] { return !sv.busy(); }));
        sv.startDownload();
        CHECK(waitFor([&] { return sv.status().phase == Phase::Ready; }));
        const Status st = sv.status();
        CHECK(st.checksumVerified && st.sha256 == sha256Hex(readFile(fixture("BSL-Stage-1.2.0-win64.zip"))));
        CHECK(fs::exists(st.stagedFile) && st.stagedFile.filename() == "BSL-Stage-1.2.0-win64.zip");
        CHECK(readFile(st.stagedFile) == readFile(fixture("BSL-Stage-1.2.0-win64.zip")));
        CHECK(!fs::exists(st.stagedFile.string() + ".part"));
        CHECK(fs::exists(root / "svc7" / "pending.json"));
        CHECK(sv.readyToInstall());
        Service again;
        again.configure("1.1.0", TestRepo::kRepo, root / "svc7");
        again.loadPersistedState(prefs);
        CHECK(again.status().phase == Phase::Ready && again.readyToInstall());
        sv.discard();
        CHECK(sv.status().phase == Phase::Available && !fs::exists(root / "svc7" / "pending.json") && !fs::exists(st.stagedFile));
    }

    {
        auto http = std::make_unique<FakeHttp>();
        addGoodRelease(*http);
        std::string sums = readFile(fixture("SHA256SUMS.txt"));
        sums.replace(sums.find("BSL-Stage-1.2.0-win64.zip") - 66, 4, "dead");
        http->addBody(TestRepo::asset("SHA256SUMS.txt"), sums);
        Service sv;
        sv.configure("1.1.0", TestRepo::kRepo, root / "svc8");
        sv.setHttp(std::move(http));
        sv.startCheck(prefs, true);
        CHECK(waitFor([&] { return sv.status().hasUpdate && !sv.busy(); }));
        sv.startDownload();
        CHECK(waitFor([&] { return !sv.busy(); }));
        const Status st = sv.status();
        CHECK(st.phase == Phase::Available && st.error.find("SHA-256") != std::string::npos);
        CHECK(!fs::exists(root / "svc8" / "pending.json") && !sv.readyToInstall());
        size_t files = 0;
        std::error_code ec;
        if (fs::exists(root / "svc8" / "downloads"))
            for (auto& e : fs::recursive_directory_iterator(root / "svc8" / "downloads", ec))
                files += e.is_regular_file();
        CHECK_MSG(files == 0, "o arquivo com hash errado nao pode sobrar em disco");
    }

    {
        auto http = std::make_unique<FakeHttp>();
        http->addBody(TestRepo::api("/latest"), [] {
            std::string j = readFile(fixture("release-latest.json"));
            const size_t a = j.find("    {\n      \"name\": \"SHA256SUMS.txt\"");
            const size_t b = j.find("    {\n      \"name\": \"BSL-Stage-1.2.0-win64.zip\"");
            j.erase(a, b - a);
            return j;
        }());
        http->addFile(TestRepo::asset("BSL-Stage-1.2.0-win64.zip"), fixture("BSL-Stage-1.2.0-win64.zip"));
        Service sv;
        sv.configure("1.1.0", TestRepo::kRepo, root / "svc9");
        sv.setHttp(std::move(http));
        sv.startCheck(prefs, true);
        CHECK(waitFor([&] { return sv.status().hasUpdate && !sv.busy(); }));
        sv.startDownload();
        CHECK(waitFor([&] { return sv.status().phase == Phase::ConfirmNoChecksum; }));
        CHECK(!sv.status().checksumVerified && !sv.status().sha256.empty());
        CHECK(!sv.readyToInstall() && !fs::exists(root / "svc9" / "pending.json"));
        sv.acceptWithoutChecksum();
        CHECK(sv.status().phase == Phase::Ready && sv.readyToInstall());
        sv.discard();
        CHECK(sv.status().phase == Phase::Available);
    }

    {
        auto http = std::make_unique<FakeHttp>();
        addGoodRelease(*http);
        http->setChunks(50);
        http->setChunkDelayMs(20);
        Service sv;
        sv.configure("1.1.0", TestRepo::kRepo, root / "svc10");
        sv.setHttp(std::move(http));
        sv.startCheck(prefs, true);
        CHECK(waitFor([&] { return sv.status().hasUpdate && !sv.busy(); }));
        sv.startDownload();
        CHECK(waitFor([&] { return sv.status().phase == Phase::Downloading && sv.status().done > 0; }));
        sv.cancel();
        CHECK(waitFor([&] { return !sv.busy(); }));
        const Status st = sv.status();
        CHECK(st.phase == Phase::Available && st.error.empty() && !sv.readyToInstall());
        std::error_code ec;
        size_t files = 0;
        if (fs::exists(root / "svc10" / "downloads"))
            for (auto& e : fs::recursive_directory_iterator(root / "svc10" / "downloads", ec))
                files += e.is_regular_file();
        CHECK_MSG(files == 0, "cancelar nao deixa arquivo parcial");
    }

    {
        auto http = std::make_unique<FakeHttp>();
        std::string j = readFile(fixture("release-latest.json"));
        const std::string good = "https://github.com/bsl-light/bsl-stage/releases/download/v1.2.0/BSL-Stage-1.2.0-win64.zip";
        j.replace(j.find(good), good.size(), "https://evil.example.com/BSL-Stage-1.2.0-win64.zip");
        http->addBody(TestRepo::api("/latest"), j);
        Service sv;
        sv.configure("1.1.0", TestRepo::kRepo, root / "svc11");
        sv.setHttp(std::move(http));
        sv.startCheck(prefs, true);
        CHECK(waitFor([&] { return sv.status().hasUpdate && !sv.busy(); }));
        sv.startDownload();
        CHECK(waitFor([&] { return !sv.busy(); }));
        CHECK(sv.status().phase == Phase::Available && sv.status().error.find("confiável") != std::string::npos);
    }

    {
        const std::string msiBytes = std::string("\xD0\xCF\x11\xE0\xA1\xB1\x1A\xE1", 8) + std::string(5000, 'm');
        const fs::path msiFile = root / "fake-msi" / "BSL-Stage-1.2.0-win64.msi";
        writeFile(msiFile, msiBytes);
        const std::string msiName = "BSL-Stage-1.2.0-win64.msi";
        auto makeHttp = [&](const std::string& body, const std::string& sums) {
            auto http = std::make_unique<FakeHttp>();
            http->addBody(TestRepo::api("/latest"),
                          "{\"tag_name\":\"v1.2.0\",\"assets\":["
                          "{\"name\":\"BSL-Stage-1.2.0-win64.zip\",\"size\":10,\"browser_download_url\":\"" + TestRepo::asset("BSL-Stage-1.2.0-win64.zip") + "\"},"
                          "{\"name\":\"" + msiName + "\",\"size\":" + std::to_string(body.size()) + ",\"browser_download_url\":\"" + TestRepo::asset(msiName.c_str()) + "\"},"
                          "{\"name\":\"SHA256SUMS.txt\",\"size\":200,\"browser_download_url\":\"" + TestRepo::asset("SHA256SUMS.txt") + "\"}]}");
            http->addFile(TestRepo::asset("BSL-Stage-1.2.0-win64.zip"), fixture("BSL-Stage-1.2.0-win64.zip"));
            writeFile(root / "fake-msi" / "body.bin", body);
            http->addFile(TestRepo::asset(msiName.c_str()), root / "fake-msi" / "body.bin");
            http->addBody(TestRepo::asset("SHA256SUMS.txt"), sums);
            return http;
        };
        const std::string goodSums = sha256Hex(msiBytes) + "  " + msiName + "\n";

        Service sv;
        sv.configure("1.1.0", TestRepo::kRepo, root / "svc-msi1");
        sv.setInstalledByMsi(true);
        sv.setHttp(makeHttp(msiBytes, goodSums));
        sv.startCheck(prefs, true);
        CHECK(waitFor([&] { return sv.status().hasUpdate && !sv.busy(); }));
        CHECK(sv.status().assetKind == AssetKind::Msi && sv.status().assetName == msiName);
        sv.startDownload();
        CHECK(waitFor([&] { return sv.status().phase == Phase::Ready; }));
        CHECK(sv.status().checksumVerified && sv.status().stagedFile.filename() == msiName && sv.readyToInstall());
        CHECK(readFile(root / "svc-msi1" / "pending.json").find("\"msi\"") != std::string::npos);
        Service again;
        again.configure("1.1.0", TestRepo::kRepo, root / "svc-msi1");
        again.setInstalledByMsi(true);
        again.loadPersistedState(prefs);
        CHECK(again.status().phase == Phase::Ready && again.status().assetKind == AssetKind::Msi);
        Service wrongMode;
        wrongMode.configure("1.1.0", TestRepo::kRepo, root / "svc-msi1");
        wrongMode.loadPersistedState(prefs);
        CHECK(wrongMode.status().phase == Phase::Available && !wrongMode.readyToInstall());

        Service bad;
        bad.configure("1.1.0", TestRepo::kRepo, root / "svc-msi2");
        bad.setInstalledByMsi(true);
        bad.setHttp(makeHttp(msiBytes, std::string(64, 'a') + "  " + msiName + "\n"));
        bad.startCheck(prefs, true);
        CHECK(waitFor([&] { return bad.status().hasUpdate && !bad.busy(); }));
        bad.startDownload();
        CHECK(waitFor([&] { return !bad.busy(); }));
        CHECK(bad.status().phase == Phase::Available && bad.status().error.find("SHA-256") != std::string::npos && !bad.readyToInstall());

        const std::string notMsi(5000, 'x');
        Service fake;
        fake.configure("1.1.0", TestRepo::kRepo, root / "svc-msi3");
        fake.setInstalledByMsi(true);
        fake.setHttp(makeHttp(notMsi, sha256Hex(notMsi) + "  " + msiName + "\n"));
        fake.startCheck(prefs, true);
        CHECK(waitFor([&] { return fake.status().hasUpdate && !fake.busy(); }));
        fake.startDownload();
        CHECK(waitFor([&] { return !fake.busy(); }));
        CHECK(fake.status().phase == Phase::Available && fake.status().error.find(".msi") != std::string::npos && !fake.readyToInstall());

        Service zipMode;
        zipMode.configure("1.1.0", TestRepo::kRepo, root / "svc-msi4");
        zipMode.setHttp(makeHttp(msiBytes, goodSums));
        zipMode.startCheck(prefs, true);
        CHECK(waitFor([&] { return zipMode.status().hasUpdate && !zipMode.busy(); }));
        CHECK(zipMode.status().assetKind == AssetKind::Zip && zipMode.status().assetName == "BSL-Stage-1.2.0-win64.zip");
    }

    {
        const fs::path d = root / "svc12";
        writeFile(d / "last-result.json", "{\"ok\":false,\"to\":\"1.2.0\",\"error\":\"arquivo em uso\",\"rolledBack\":true}");
        Service sv;
        sv.configure("1.1.0", TestRepo::kRepo, d);
        bool ok = true;
        const std::string msg = sv.takeLastUpdateMessage(&ok);
        CHECK(!ok && msg.find("arquivo em uso") != std::string::npos && msg.find("anterior") != std::string::npos);
        CHECK(sv.takeLastUpdateMessage(&ok).empty());
        writeFile(d / "last-result.json", "{\"ok\":true,\"to\":\"1.2.0\"}");
        const std::string good = sv.takeLastUpdateMessage(&ok);
        CHECK(ok && good.find("1.2.0") != std::string::npos);
    }
}

struct ApplyFixture
{
    fs::path base, target, work, zip;
    std::map<std::string, std::string> original;
};

ApplyFixture makeApplyFixture(const fs::path& root, const char* name)
{
    ApplyFixture f;
    f.base = root / name;
    f.target = f.base / "app";
    f.work = f.base / "work";
    f.zip = f.base / "pacote.zip";
    std::error_code ec;
    fs::remove_all(f.base, ec);
    writeFile(f.target / "BSLStage.exe", "exe-antigo");
    writeFile(f.target / "a.dll", "a-antiga");
    writeFile(f.target / "sub" / "b.dll", "b-antiga");
    writeFile(f.target / "so-do-usuario.txt", "nao mexer");
    writeFile(f.target / "settings.json", "{\"config\":\"do usuario\"}");
    f.original = snapshot(f.target);
    return f;
}

ApplyOptions optionsFor(const ApplyFixture& f)
{
    ApplyOptions o;
    o.zipFile = f.zip;
    o.targetDir = f.target;
    o.stagingDir = f.work / "staging";
    o.backupDir = f.work / "backup" / "1.1.0";
    o.lockRetries = 2;
    o.lockRetryDelayMs = 20;
    return o;
}

const std::vector<std::pair<std::string, std::string>> kNewPackage = {
    {"BSL-Stage-1.2.0/BSLStage.exe", "exe-novo"},
    {"BSL-Stage-1.2.0/a.dll", "a-nova"},
    {"BSL-Stage-1.2.0/novo.dll", "arquivo-novo"},
    {"BSL-Stage-1.2.0/sub/b.dll", "b-nova"},
    {"BSL-Stage-1.2.0/settings.json", "{\"config\":\"DO PACOTE\"}"},
};

void testApply(const fs::path& root)
{
    std::printf("[atualizador]\n");
    std::error_code ec;

    {
        ApplyFixture f = makeApplyFixture(root, "ap1");
        writeFile(f.zip, makeStoredZip(kNewPackage));
        const ApplyResult r = applyZipUpdate(optionsFor(f));
        CHECK_MSG(r.ok, r.error);
        CHECK(r.replaced == 3 && r.added == 1 && r.skipped == 1);
        CHECK(readFile(f.target / "BSLStage.exe") == "exe-novo");
        CHECK(readFile(f.target / "a.dll") == "a-nova");
        CHECK(readFile(f.target / "sub" / "b.dll") == "b-nova");
        CHECK(readFile(f.target / "novo.dll") == "arquivo-novo");
        CHECK(readFile(f.target / "so-do-usuario.txt") == "nao mexer");
        CHECK(readFile(f.target / "settings.json") == "{\"config\":\"do usuario\"}");
        CHECK(readFile(f.work / "backup" / "1.1.0" / "BSLStage.exe") == "exe-antigo");
        CHECK(readFile(f.work / "backup" / "1.1.0" / "sub" / "b.dll") == "b-antiga");
        CHECK(!fs::exists(f.work / "staging"));
    }

    {
        ApplyFixture f = makeApplyFixture(root, "ap1b");
        writeFile(f.zip, makeStoredZip({{"BSLStage.exe", "exe-raiz"}, {"x/y.txt", "y"}}));
        const ApplyResult r = applyZipUpdate(optionsFor(f));
        CHECK_MSG(r.ok, r.error);
        CHECK(readFile(f.target / "BSLStage.exe") == "exe-raiz" && readFile(f.target / "x" / "y.txt") == "y");
    }

    for (int failAt : {1, 2, 3}) {
        ApplyFixture f = makeApplyFixture(root, ("ap2_" + std::to_string(failAt)).c_str());
        writeFile(f.zip, makeStoredZip(kNewPackage));
        ApplyOptions o = optionsFor(f);
        o.failHook = [failAt](int index, const std::string&) { return index == failAt; };
        const ApplyResult r = applyZipUpdate(o);
        CHECK(!r.ok && r.rolledBack && !r.rollbackFailed);
        CHECK_MSG(snapshot(f.target) == f.original, "apos a falha na posicao " + std::to_string(failAt) + " a pasta deve ser identica a original");
        CHECK(!fs::exists(f.target / "novo.dll"));
    }

    {
        ApplyFixture f = makeApplyFixture(root, "ap3");
        writeFile(f.zip, makeStoredZip(kNewPackage));
        const fs::path locked = f.target / "a.dll";
        HANDLE h = CreateFileW(locked.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        CHECK(h != INVALID_HANDLE_VALUE);
        const ApplyResult r = applyZipUpdate(optionsFor(f));
        CHECK(!r.ok && r.rolledBack && !r.rollbackFailed);
        CHECK_MSG(r.error.find("a.dll") != std::string::npos, "o erro diz qual arquivo: " + r.error);
        if (h != INVALID_HANDLE_VALUE)
            CloseHandle(h);
        CHECK_MSG(snapshot(f.target) == f.original, "arquivo em uso: pasta identica a original");
    }

    {
        ApplyFixture f = makeApplyFixture(root, "ap4");
        writeFile(f.zip, makeStoredZip({{"BSL-Stage-1.2.0/BSLStage.exe", "novo"}, {"../fora.txt", "malicioso"}}));
        ApplyResult r = applyZipUpdate(optionsFor(f));
        CHECK(!r.ok && !r.rolledBack && r.error.find("inseguro") != std::string::npos);
        CHECK(snapshot(f.target) == f.original && !fs::exists(f.base / "fora.txt") && !fs::exists(f.work / "fora.txt"));

        writeFile(f.zip, makeStoredZip({{"BSL-Stage-1.2.0/BSLStage.exe", "novo"}, {"C:/Windows/evil.dll", "x"}}));
        r = applyZipUpdate(optionsFor(f));
        CHECK(!r.ok && snapshot(f.target) == f.original);

        writeFile(f.zip, makeStoredZip({{"BSL-Stage-1.2.0/a.dll", "sem exe"}}));
        r = applyZipUpdate(optionsFor(f));
        CHECK(!r.ok && r.error.find("BSLStage.exe") != std::string::npos && snapshot(f.target) == f.original);

        writeFile(f.zip, makeStoredZip(kNewPackage, true));
        r = applyZipUpdate(optionsFor(f));
        CHECK(!r.ok && !r.rolledBack && r.error.find("CRC") != std::string::npos);
        CHECK_MSG(snapshot(f.target) == f.original, "CRC errado nao pode alterar a instalacao");

        writeFile(f.zip, "isto nao e um zip, mas tem bastante texto para passar do tamanho minimo");
        r = applyZipUpdate(optionsFor(f));
        CHECK(!r.ok && snapshot(f.target) == f.original);
    }

    {
        ApplyFixture f = makeApplyFixture(root, "ap5");
        fs::remove_all(f.target, ec);
        fs::create_directories(f.target, ec);
        f.zip = fixture("BSL-Stage-1.2.0-win64.zip");
        const ApplyResult r = applyZipUpdate(optionsFor(f));
        CHECK_MSG(r.ok, r.error);
        std::map<std::string, std::string> expected;
        std::istringstream in(readFile(fixture("expected.txt")));
        std::string hash, name;
        while (in >> hash >> name)
            expected[name] = hash;
        for (const auto& [rel, h] : expected)
            CHECK_MSG(sha256Hex(readFile(f.target / rel)) == h, "arquivo instalado " + rel);
    }

    CHECK(isProtectedUserFile("settings.json") && isProtectedUserFile("sub/Settings.JSON"));
    CHECK(isProtectedUserFile("imgui.ini") && isProtectedUserFile("shows/meu show.bslstage") && isProtectedUserFile("atalhos.bslkeys"));
    CHECK(isProtectedUserFile("projects/x.bin") && isProtectedUserFile("Projetos/x.bin"));
    CHECK(!isProtectedUserFile("BSLStage.exe") && !isProtectedUserFile("assets/isf/exemplo.fs") && !isProtectedUserFile("avcodec-63.dll"));

    CHECK(isDirectoryWritable(root));
    CHECK(!isDirectoryWritable(root / "nao-existe"));
}

void testUpdaterExe(const fs::path& root)
{
    std::printf("[bslstage_updater.exe]\n");
    wchar_t self[MAX_PATH * 2];
    const DWORD n = GetModuleFileNameW(nullptr, self, static_cast<DWORD>(std::size(self)));
    const fs::path updater = fs::path(std::wstring(self, n)).parent_path() / L"bslstage_updater.exe";
    CHECK_MSG(fs::exists(updater), "bslstage_updater.exe ao lado do teste");
    if (!fs::exists(updater))
        return;

    auto runUpdater = [&](const std::wstring& args, DWORD& exitCode, double& seconds, bool withChild) {
        PROCESS_INFORMATION child{};
        DWORD pid = 0;
        if (withChild) {
            STARTUPINFOW csi{};
            csi.cb = sizeof(csi);
            std::wstring cmd = L"cmd.exe /c ping -n 3 127.0.0.1 > nul";
            if (CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &csi, &child))
                pid = child.dwProcessId;
        }
        std::wstring cmd = L"\"" + updater.wstring() + L"\" " + (pid ? L"--pid " + std::to_wstring(pid) + L" " : L"") + args;
        STARTUPINFOW si{};
        si.cb = sizeof(si);
        PROCESS_INFORMATION pi{};
        const auto t0 = std::chrono::steady_clock::now();
        const BOOL ok = CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi);
        CHECK(ok != FALSE);
        if (ok) {
            WaitForSingleObject(pi.hProcess, 60000);
            GetExitCodeProcess(pi.hProcess, &exitCode);
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
        }
        seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        if (child.hProcess) {
            CloseHandle(child.hThread);
            CloseHandle(child.hProcess);
        }
    };

    {
        ApplyFixture f = makeApplyFixture(root, "exe1");
        f.zip = fixture("BSL-Stage-1.2.0-win64.zip");
        DWORD code = 99;
        double secs = 0;
        runUpdater(L"--work \"" + f.work.wstring() + L"\" --from 1.1.0 --to 1.2.0 --zip \"" + f.zip.wstring() + L"\" --target \""
                       + f.target.wstring() + L"\"",
                   code, secs, true);
        CHECK_MSG(code == 0, "codigo de saida " + std::to_string(code));
        CHECK_MSG(secs > 1.5, "esperou o processo do app fechar (" + std::to_string(secs) + " s)");
        const std::string result = readFile(f.work / "last-result.json");
        CHECK_MSG(result.find("\"ok\": true") != std::string::npos, result);
        CHECK(sha256Hex(readFile(f.target / "data" / "big.txt")).size() == 64 && fs::exists(f.target / "data" / "big.txt"));
        CHECK(readFile(f.target / "BSLStage.exe").rfind("MZ-fake-executable-v1.2.0", 0) == 0);
        CHECK(readFile(f.work / "backup" / "1.1.0" / "BSLStage.exe") == "exe-antigo");
        CHECK(readFile(f.target / "settings.json") == "{\"config\":\"do usuario\"}" && readFile(f.target / "so-do-usuario.txt") == "nao mexer");
        CHECK(fs::exists(f.work / "updater.log"));
    }
    {
        ApplyFixture f = makeApplyFixture(root, "exe2");
        DWORD code = 99;
        double secs = 0;
        runUpdater(L"--work \"" + f.work.wstring() + L"\" --from 1.1.0 --to 1.2.0 --zip \"" + (f.base / "nao-existe.zip").wstring()
                       + L"\" --target \"" + f.target.wstring() + L"\"",
                   code, secs, false);
        CHECK(code == 1);
        CHECK(readFile(f.work / "last-result.json").find("\"ok\": false") != std::string::npos);
        CHECK(snapshot(f.target) == f.original);
    }
}

}

int main()
{
    const fs::path root = fs::temp_directory_path() / ("bslstage_updatetest_" + std::to_string(GetCurrentProcessId()));
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root, ec);

    testSemVer();
    testRelease();
    testSha256();
    testZip();
    testPolicy();
    testMarkdown();
    testIssue();
    testService(root);
    testApply(root);
    testUpdaterExe(root);

    fs::remove_all(root, ec);
    std::printf("\n%d verificações, %d falhas\n", g_checks, g_failures);
    std::printf(g_failures == 0 ? "OK\n" : "FALHOU\n");
    return g_failures == 0 ? 0 : 1;
}
