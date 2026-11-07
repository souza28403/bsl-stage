#pragma once

#include "fx/EffectChain.h"

namespace bsl::fx {

class ChainRunner
{
public:
    static FboPool::Lease run(EffectChain& chain, const EffectContext& ctx, GLuint inputTex, int w, int h);
};

}
