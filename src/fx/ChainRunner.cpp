#include "fx/ChainRunner.h"

#include "app/Log.h"

#include <exception>

namespace bsl::fx {

FboPool::Lease ChainRunner::run(EffectChain& chain, const EffectContext& ctx, GLuint inputTex, int w, int h)
{
    FboPool::Lease held;
    if (!ctx.pool || !chain.hasActive() || inputTex == 0)
        return held;

    GLuint cur = inputTex;
    for (EffectInstance& inst : chain.items) {
        if (inst.bypass || inst.faulted)
            continue;
        IEffect* effect = inst.ensureEffect();
        if (!effect)
            continue;
        inst.syncParams();

        FboPool::Lease out = ctx.pool->acquire(w, h);
        try {
            effect->process(ctx, cur, w, h, *out);
        } catch (const std::exception& e) {
            inst.faulted = true;
            log::warn("Efeito \"" + inst.typeId + "\" falhou e foi desligado: " + e.what());
            continue;
        }
        inst.pollEffect();
        held = std::move(out);
        cur = held->texture();

        glBindVertexArray(0);
        glUseProgram(0);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glActiveTexture(GL_TEXTURE0);
        glDisable(GL_BLEND);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_SCISSOR_TEST);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    }
    return held;
}

}
