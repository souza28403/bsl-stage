#include "seqfx.h"

#include "core/Generators.h"
#include "fx/bsl/BslCommon.h"
#include "fx/bsl/BslEffects.h"
#include "fx/bsl/BslSources.h"

#include <glad/gl.h>

#include <memory>
#include <string>

namespace seqfx {

using namespace bsl;
using namespace bsl::fx::bsllight;

void testarFonte()
{
    secao("fonte de efeito");

    const char* ids[] = {kGeradorContornoId, kGeradorLadrilhosId, kGeradorBordaNeonId};
    const char* efeitos[] = {kContornoId, kLadrilhosId, kBordaNeonId};

    for (int i = 0; i < 3; ++i) {
        const std::string id = ids[i];
        const BslPluginInfo* plugin = findBslPlugin(id);
        VERIFICAR(plugin != nullptr && plugin->generator);
        const BslPluginInfo* efeito = findBslPlugin(efeitos[i]);
        VERIFICAR(efeito != nullptr && !efeito->generator);

        const GeneratorInfo* info = findGenerator(id);
        VERIFICAR(info != nullptr);
        if (!info)
            continue;
        VERIFICAR(info->describe && info->create);
        const std::vector<media::ParamDesc> descs = info->describe(media::ParamValues());
        VERIFICAR(!descs.empty());
        bool fundoTransparente = false;
        for (const media::ParamDesc& d : descs)
            if (d.id == "fundo" && d.def.is_string() && d.def.get<std::string>() == "transparente")
                fundoTransparente = true;
        VERIFICAR(fundoTransparente);

        std::unique_ptr<media::ISource> fonte = info->create(64, 48);
        VERIFICAR(fonte != nullptr);
        if (!fonte)
            continue;
        VERIFICAR(!fonte->isOpen());
        std::string erro;
        VERIFICAR(fonte->open(std::filesystem::path(), &erro));
        VERIFICAR(fonte->isOpen());
        fonte->setTempo(2.5, 128.0f);
        fonte->update(0.016);
        fonte->update(0.016);
        VERIFICAR(fonte->texture() != 0);
        VERIFICAR(fonte->width() == 64 && fonte->height() == 48);
        VERIFICAR(fonte->hasAlpha());
        VERIFICAR(fonte->paramDescs().size() == descs.size());
        VERIFICAR(fonte->status().level == media::SourceStatus::Level::Ok);
        VERIFICAR(glGetError() == GL_NO_ERROR);

        media::ParamValues valores;
        valores.set("fundo", media::Json("preto"));
        fonte->setParams(valores);
        fonte->update(0.016);
        VERIFICAR(fonte->texture() != 0);
        VERIFICAR(glGetError() == GL_NO_ERROR);

        fonte->close();
        VERIFICAR(!fonte->isOpen());
        VERIFICAR(fonte->texture() == 0);
    }

    const GeneratorInfo* inexistente = findGenerator("bsl:gerador-que-nao-existe");
    VERIFICAR(inexistente == nullptr);
    VERIFICAR(glGetError() == GL_NO_ERROR);
}

}
