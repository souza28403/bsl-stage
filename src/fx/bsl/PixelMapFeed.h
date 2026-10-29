#pragma once

// Configuracao do Pixel Map (paineis e escala) que a fonte "Padrao de calibracao BSL" desenha. O app publica aqui a
// configuracao do projeto a cada quadro (como o SliceFeed faz com os slices); a fonte so redesenha quando algo muda.

#include "output/PixelMap.h"

#include <cstdint>
#include <mutex>

namespace bsl::fx::bsllight {

class PixelMapFeed
{
public:
    static PixelMapFeed& instance();

    // Publica a configuracao. So conta como mudanca se algo mudou de fato.
    void set(const pixelmap::Config& config);
    pixelmap::Config config() const;
    std::uint64_t version() const;

private:
    mutable std::mutex mutex_;
    pixelmap::Config config_;
    std::uint64_t version_ = 1;
};

} // namespace bsl::fx::bsllight
