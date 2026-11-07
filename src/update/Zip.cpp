#include "update/Zip.h"

#include <algorithm>
#include <array>
#include <cstring>

namespace bsl::update {

namespace {

const std::array<uint32_t, 256>& crcTable()
{
    static const std::array<uint32_t, 256> table = [] {
        std::array<uint32_t, 256> t{};
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k)
                c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            t[i] = c;
        }
        return t;
    }();
    return table;
}

struct Huff
{
    uint16_t count[16];
    uint16_t symbol[288];
};

constexpr uint16_t kLenBase[29] = {3,  4,  5,  6,  7,  8,  9,  10, 11,  13,  15,  17,  19,  23, 27,
                                   31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
constexpr uint16_t kLenExtra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
constexpr uint16_t kDistBase[30] = {1,   2,   3,   4,   5,   7,    9,    13,   17,   25,   33,   49,   65,    97,    129,
                                    193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
constexpr uint16_t kDistExtra[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

class Inflater
{
public:
    using Sink = std::function<bool(const uint8_t*, size_t)>;

    Inflater(std::ifstream& file, uint64_t compressedSize, Sink sink)
        : file_(file), remaining_(compressedSize), sink_(std::move(sink)), in_(1 << 16), win_(kWindow)
    {
    }

    bool run(std::string& error)
    {
        bool last = false;
        while (!last) {
            last = bits(1) != 0;
            const int type = bits(2);
            int err = 0;
            if (failed_)
                break;
            switch (type) {
            case 0: err = stored(); break;
            case 1: err = fixedBlock(); break;
            case 2: err = dynamicBlock(); break;
            default: err = -1; break;
            }
            if (err != 0 || failed_) {
                error = errorText_.empty() ? "dados compactados inválidos" : errorText_;
                return false;
            }
        }
        if (failed_ || !flush()) {
            error = errorText_.empty() ? "dados compactados inválidos" : errorText_;
            return false;
        }
        return true;
    }

private:
    static constexpr size_t kWindow = 32768;

    void fail(const char* text)
    {
        if (!failed_) {
            failed_ = true;
            errorText_ = text;
        }
    }

    int bits(int need)
    {
        uint32_t val = bitBuf_;
        while (bitCnt_ < need) {
            if (inPos_ == inLen_ && !refill()) {
                fail("arquivo ZIP truncado");
                return 0;
            }
            val |= static_cast<uint32_t>(in_[inPos_++]) << bitCnt_;
            bitCnt_ += 8;
        }
        bitBuf_ = val >> need;
        bitCnt_ -= need;
        return static_cast<int>(val & ((1u << need) - 1));
    }

    bool refill()
    {
        if (remaining_ == 0)
            return false;
        const size_t want = static_cast<size_t>(std::min<uint64_t>(remaining_, in_.size()));
        file_.read(reinterpret_cast<char*>(in_.data()), static_cast<std::streamsize>(want));
        const size_t got = static_cast<size_t>(file_.gcount());
        if (got == 0)
            return false;
        remaining_ -= got;
        inPos_ = 0;
        inLen_ = got;
        return true;
    }

    bool flush()
    {
        if (outPos_ == 0)
            return true;
        const bool ok = sink_(win_.data(), outPos_);
        outPos_ = 0;
        if (!ok)
            fail("cancelado");
        return ok;
    }

    void emit(uint8_t b)
    {
        win_[outPos_++] = b;
        ++produced_;
        if (outPos_ == kWindow)
            flush();
    }

    int stored()
    {
        bitBuf_ = 0;
        bitCnt_ = 0;
        if (inPos_ == inLen_ && !refill())
            return -1;
        auto byte = [&]() -> int {
            if (inPos_ == inLen_ && !refill())
                return -1;
            return in_[inPos_++];
        };
        const int a = byte(), b = byte(), c = byte(), d = byte();
        if (a < 0 || b < 0 || c < 0 || d < 0)
            return -1;
        const unsigned len = static_cast<unsigned>(a | (b << 8));
        const unsigned nlen = static_cast<unsigned>(c | (d << 8));
        if (len != (~nlen & 0xFFFFu))
            return -2;
        for (unsigned i = 0; i < len; ++i) {
            const int v = byte();
            if (v < 0)
                return -1;
            emit(static_cast<uint8_t>(v));
            if (failed_)
                return -3;
        }
        return 0;
    }

    int decode(const Huff& h)
    {
        int code = 0, first = 0, index = 0;
        for (int len = 1; len <= 15; ++len) {
            code |= bits(1);
            if (failed_)
                return -1;
            const int count = h.count[len];
            if (code - count < first)
                return h.symbol[index + (code - first)];
            index += count;
            first += count;
            first <<= 1;
            code <<= 1;
        }
        return -10;
    }

    static bool construct(Huff& h, const uint8_t* length, int n)
    {
        std::memset(h.count, 0, sizeof(h.count));
        for (int s = 0; s < n; ++s)
            ++h.count[length[s]];
        if (h.count[0] == n)
            return true;
        int left = 1;
        for (int len = 1; len <= 15; ++len) {
            left <<= 1;
            left -= h.count[len];
            if (left < 0)
                return false;
        }
        uint16_t offs[16];
        offs[1] = 0;
        for (int len = 1; len < 15; ++len)
            offs[len + 1] = static_cast<uint16_t>(offs[len] + h.count[len]);
        for (int s = 0; s < n; ++s)
            if (length[s] != 0)
                h.symbol[offs[length[s]]++] = static_cast<uint16_t>(s);
        return true;
    }

    int codes(const Huff& lencode, const Huff& distcode)
    {
        while (true) {
            int symbol = decode(lencode);
            if (symbol < 0)
                return symbol;
            if (symbol < 256) {
                emit(static_cast<uint8_t>(symbol));
                if (failed_)
                    return -3;
            } else if (symbol == 256) {
                return 0;
            } else {
                symbol -= 257;
                if (symbol >= 29)
                    return -9;
                const int len = kLenBase[symbol] + bits(kLenExtra[symbol]);
                const int dsym = decode(distcode);
                if (dsym < 0)
                    return dsym;
                if (dsym >= 30)
                    return -9;
                const uint64_t dist = static_cast<uint64_t>(kDistBase[dsym] + bits(kDistExtra[dsym]));
                if (failed_)
                    return -3;
                if (dist > produced_)
                    return -11;
                for (int i = 0; i < len; ++i) {
                    const size_t from = (outPos_ + kWindow - static_cast<size_t>(dist % kWindow)) % kWindow;
                    emit(win_[from]);
                    if (failed_)
                        return -3;
                }
            }
        }
    }

    int fixedBlock()
    {
        static const std::pair<Huff, Huff> tables = [] {
            std::pair<Huff, Huff> t{};
            uint8_t lengths[288];
            int s = 0;
            for (; s < 144; ++s) lengths[s] = 8;
            for (; s < 256; ++s) lengths[s] = 9;
            for (; s < 280; ++s) lengths[s] = 7;
            for (; s < 288; ++s) lengths[s] = 8;
            construct(t.first, lengths, 288);
            for (s = 0; s < 30; ++s) lengths[s] = 5;
            construct(t.second, lengths, 30);
            return t;
        }();
        return codes(tables.first, tables.second);
    }

    int dynamicBlock()
    {
        static const uint8_t order[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
        const int nlen = bits(5) + 257;
        const int ndist = bits(5) + 1;
        const int ncode = bits(4) + 4;
        if (failed_)
            return -1;
        if (nlen > 286 || ndist > 30)
            return -3;
        uint8_t lengths[320] = {};
        for (int i = 0; i < ncode; ++i)
            lengths[order[i]] = static_cast<uint8_t>(bits(3));
        Huff lencode, distcode;
        if (!construct(lencode, lengths, 19))
            return -4;
        int index = 0;
        std::memset(lengths, 0, sizeof(lengths));
        uint8_t all[320] = {};
        while (index < nlen + ndist) {
            int symbol = decode(lencode);
            if (symbol < 0)
                return symbol;
            if (symbol < 16) {
                all[index++] = static_cast<uint8_t>(symbol);
            } else {
                int len = 0;
                int rep;
                if (symbol == 16) {
                    if (index == 0)
                        return -5;
                    len = all[index - 1];
                    rep = 3 + bits(2);
                } else if (symbol == 17) {
                    rep = 3 + bits(3);
                } else {
                    rep = 11 + bits(7);
                }
                if (failed_ || index + rep > nlen + ndist)
                    return -6;
                while (rep--)
                    all[index++] = static_cast<uint8_t>(len);
            }
        }
        if (all[256] == 0)
            return -7;
        if (!construct(lencode, all, nlen))
            return -8;
        if (!construct(distcode, all + nlen, ndist))
            return -8;
        return codes(lencode, distcode);
    }

    std::ifstream& file_;
    uint64_t remaining_;
    Sink sink_;
    std::vector<uint8_t> in_;
    size_t inPos_ = 0, inLen_ = 0;
    uint32_t bitBuf_ = 0;
    int bitCnt_ = 0;
    std::vector<uint8_t> win_;
    size_t outPos_ = 0;
    uint64_t produced_ = 0;
    bool failed_ = false;
    std::string errorText_;
};

uint16_t rd16(const uint8_t* p)
{
    return static_cast<uint16_t>(p[0] | (p[1] << 8));
}
uint32_t rd32(const uint8_t* p)
{
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16)
           | (static_cast<uint32_t>(p[3]) << 24);
}
uint64_t rd64(const uint8_t* p)
{
    return static_cast<uint64_t>(rd32(p)) | (static_cast<uint64_t>(rd32(p + 4)) << 32);
}

void setError(std::string* error, const std::string& text)
{
    if (error)
        *error = text;
}

}

uint32_t crc32Update(uint32_t crc, const void* data, size_t size)
{
    const auto& t = crcTable();
    const auto* p = static_cast<const uint8_t*>(data);
    uint32_t c = ~crc;
    for (size_t i = 0; i < size; ++i)
        c = t[(c ^ p[i]) & 0xFF] ^ (c >> 8);
    return ~c;
}

bool isSafeZipPath(const std::string& name)
{
    if (name.empty() || name.front() == '/' || name.find('\\') != std::string::npos || name.find(':') != std::string::npos
        || name.find('\0') != std::string::npos)
        return false;
    size_t start = 0;
    while (start < name.size()) {
        size_t slash = name.find('/', start);
        const bool lastSegment = slash == std::string::npos;
        const std::string seg = name.substr(start, lastSegment ? std::string::npos : slash - start);
        if (seg.empty()) {
            if (lastSegment)
                break;
            return false;
        }
        std::string trimmed = seg;
        while (!trimmed.empty() && (trimmed.back() == '.' || trimmed.back() == ' '))
            trimmed.pop_back();
        if (trimmed.empty() || seg == "..")
            return false;
        if (lastSegment)
            break;
        start = slash + 1;
    }
    return true;
}

bool ZipReader::open(const std::filesystem::path& zipFile, std::string* error)
{
    entries_.clear();
    file_.close();
    file_.clear();
    path_ = zipFile;
    file_.open(zipFile, std::ios::binary);
    if (!file_) {
        setError(error, "não foi possível abrir o arquivo");
        return false;
    }
    file_.seekg(0, std::ios::end);
    const int64_t fileSize = static_cast<int64_t>(file_.tellg());
    if (fileSize < 22) {
        setError(error, "não é um arquivo ZIP");
        return false;
    }
    const int64_t tailSize = std::min<int64_t>(fileSize, 65557);
    std::vector<uint8_t> tail(static_cast<size_t>(tailSize));
    file_.seekg(fileSize - tailSize);
    file_.read(reinterpret_cast<char*>(tail.data()), tailSize);
    int64_t eocd = -1;
    for (int64_t i = tailSize - 22; i >= 0; --i) {
        if (rd32(&tail[static_cast<size_t>(i)]) == 0x06054b50u) {
            eocd = i;
            break;
        }
    }
    if (eocd < 0) {
        setError(error, "não é um arquivo ZIP (fim do diretório não encontrado)");
        return false;
    }
    const uint8_t* e = &tail[static_cast<size_t>(eocd)];
    uint64_t count = rd16(e + 10);
    uint64_t cdSize = rd32(e + 12);
    uint64_t cdOffset = rd32(e + 16);
    if (count == 0xFFFF || cdSize == 0xFFFFFFFFu || cdOffset == 0xFFFFFFFFu) {
        if (eocd >= 20 && rd32(&tail[static_cast<size_t>(eocd) - 20]) == 0x07064b50u) {
            const uint64_t off64 = rd64(&tail[static_cast<size_t>(eocd) - 20 + 8]);
            uint8_t rec[56];
            file_.seekg(static_cast<std::streamoff>(off64));
            file_.read(reinterpret_cast<char*>(rec), sizeof(rec));
            if (!file_ || rd32(rec) != 0x06064b50u) {
                setError(error, "ZIP64 inválido");
                return false;
            }
            count = rd64(rec + 32);
            cdSize = rd64(rec + 40);
            cdOffset = rd64(rec + 48);
        } else {
            setError(error, "ZIP64 inválido");
            return false;
        }
    }
    if (cdOffset + cdSize > static_cast<uint64_t>(fileSize) || cdSize > (512ull << 20)) {
        setError(error, "diretório do ZIP inválido");
        return false;
    }
    std::vector<uint8_t> cd(static_cast<size_t>(cdSize));
    file_.clear();
    file_.seekg(static_cast<std::streamoff>(cdOffset));
    file_.read(reinterpret_cast<char*>(cd.data()), static_cast<std::streamsize>(cd.size()));
    if (!file_) {
        setError(error, "diretório do ZIP truncado");
        return false;
    }
    size_t pos = 0;
    for (uint64_t i = 0; i < count; ++i) {
        if (pos + 46 > cd.size() || rd32(&cd[pos]) != 0x02014b50u) {
            setError(error, "diretório do ZIP inválido");
            return false;
        }
        const uint8_t* h = &cd[pos];
        ZipEntry z;
        z.flags = rd16(h + 8);
        z.method = rd16(h + 10);
        z.crc32 = rd32(h + 16);
        z.compressedSize = rd32(h + 20);
        z.size = rd32(h + 24);
        const size_t nlen = rd16(h + 28), elen = rd16(h + 30), clen = rd16(h + 32);
        z.localOffset = rd32(h + 42);
        if (pos + 46 + nlen + elen + clen > cd.size()) {
            setError(error, "diretório do ZIP inválido");
            return false;
        }
        z.name.assign(reinterpret_cast<const char*>(h + 46), nlen);
        std::replace(z.name.begin(), z.name.end(), '\\', '/');
        while (z.name.rfind("./", 0) == 0)
            z.name.erase(0, 2);
        const uint8_t* x = h + 46 + nlen;
        size_t xp = 0;
        while (xp + 4 <= elen) {
            const uint16_t id = rd16(x + xp), sz = rd16(x + xp + 2);
            if (xp + 4 + sz > elen)
                break;
            if (id == 0x0001) {
                size_t q = xp + 4;
                if (z.size == 0xFFFFFFFFu && q + 8 <= xp + 4 + sz) { z.size = rd64(x + q); q += 8; }
                if (z.compressedSize == 0xFFFFFFFFu && q + 8 <= xp + 4 + sz) { z.compressedSize = rd64(x + q); q += 8; }
                if (z.localOffset == 0xFFFFFFFFu && q + 8 <= xp + 4 + sz) { z.localOffset = rd64(x + q); q += 8; }
            }
            xp += 4 + sz;
        }
        z.isDirectory = !z.name.empty() && z.name.back() == '/';
        entries_.push_back(std::move(z));
        pos += 46 + nlen + elen + clen;
    }
    return true;
}

const ZipEntry* ZipReader::find(const std::string& name) const
{
    for (const ZipEntry& e : entries_)
        if (e.name == name)
            return &e;
    return nullptr;
}

bool ZipReader::extractTo(const ZipEntry& entry, const std::function<bool(const uint8_t*, size_t)>& sink, std::string* error)
{
    if (entry.flags & 1) {
        setError(error, "arquivo criptografado não é suportado");
        return false;
    }
    if (entry.method != 0 && entry.method != 8) {
        setError(error, "método de compressão não suportado (" + std::to_string(entry.method) + ")");
        return false;
    }
    uint8_t lh[30];
    file_.clear();
    file_.seekg(static_cast<std::streamoff>(entry.localOffset));
    file_.read(reinterpret_cast<char*>(lh), sizeof(lh));
    if (!file_ || rd32(lh) != 0x04034b50u) {
        setError(error, "cabeçalho local inválido em " + entry.name);
        return false;
    }
    const uint64_t dataOffset = entry.localOffset + 30 + rd16(lh + 26) + rd16(lh + 28);
    file_.seekg(static_cast<std::streamoff>(dataOffset));

    uint32_t crc = 0;
    uint64_t total = 0;
    bool sinkOk = true;
    auto counting = [&](const uint8_t* data, size_t n) {
        crc = crc32Update(crc, data, n);
        total += n;
        if (total > entry.size)
            return false;
        sinkOk = sink(data, n);
        return sinkOk;
    };

    if (entry.method == 0) {
        std::vector<uint8_t> buf(1 << 16);
        uint64_t left = entry.compressedSize;
        while (left > 0) {
            const size_t want = static_cast<size_t>(std::min<uint64_t>(left, buf.size()));
            file_.read(reinterpret_cast<char*>(buf.data()), static_cast<std::streamsize>(want));
            if (static_cast<size_t>(file_.gcount()) != want) {
                setError(error, "arquivo ZIP truncado em " + entry.name);
                return false;
            }
            if (!counting(buf.data(), want)) {
                setError(error, sinkOk ? "dados maiores que o declarado em " + entry.name : "cancelado");
                return false;
            }
            left -= want;
        }
    } else {
        Inflater inflater(file_, entry.compressedSize, counting);
        std::string err;
        if (!inflater.run(err)) {
            setError(error, (sinkOk && total <= entry.size ? err : (sinkOk ? "dados maiores que o declarado" : "cancelado"))
                                + " (" + entry.name + ")");
            return false;
        }
    }
    if (total != entry.size) {
        setError(error, "tamanho diferente do declarado em " + entry.name);
        return false;
    }
    if (crc != entry.crc32) {
        setError(error, "CRC-32 não confere em " + entry.name + " (arquivo corrompido)");
        return false;
    }
    return true;
}

bool ZipReader::extract(const ZipEntry& entry, const std::filesystem::path& dest, std::string* error,
                        const std::function<bool(uint64_t)>& progress)
{
    std::error_code ec;
    std::filesystem::create_directories(dest.parent_path(), ec);
    std::ofstream out(dest, std::ios::binary | std::ios::trunc);
    if (!out) {
        setError(error, "não foi possível gravar " + dest.string());
        return false;
    }
    uint64_t written = 0;
    const bool ok = extractTo(
        entry,
        [&](const uint8_t* data, size_t n) {
            out.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(n));
            written += n;
            return static_cast<bool>(out) && (!progress || progress(written));
        },
        error);
    out.close();
    if (!ok || !out) {
        if (ok && error)
            *error = "falha ao gravar no disco";
        std::filesystem::remove(dest, ec);
        return false;
    }
    return true;
}

bool ZipReader::readAll(const ZipEntry& entry, std::string& out, std::string* error)
{
    out.clear();
    return extractTo(
        entry,
        [&](const uint8_t* data, size_t n) {
            out.append(reinterpret_cast<const char*>(data), n);
            return out.size() <= (64u << 20);
        },
        error);
}

}
