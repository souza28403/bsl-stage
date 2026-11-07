#include "fx/IsfReload.h"

namespace bsl::fx {

IsfReloadResult isfReload(std::unique_ptr<IsfRenderer>& renderer, const std::optional<IsfEntry>& entry, bool glReady,
                          bool wantEffect, IsfReloadState& state)
{
    if (!entry)
        return IsfReloadResult::Unchanged;
    if (!entry->doc) {
        if (renderer) {
            state.failedDoc.reset();
            state.error = entry->error;
        }
        return IsfReloadResult::Unchanged;
    }
    if (entry->isEffect != wantEffect) {
        if (renderer) {
            state.failedDoc = entry->doc;
            state.error = wantEffect ? "O arquivo deixou de ter a entrada inputImage (virou gerador)."
                                     : "O arquivo passou a ter a entrada inputImage (virou efeito).";
        }
        return IsfReloadResult::Unchanged;
    }
    if (renderer && renderer->docPtr() == entry->doc) {
        if (!state.failedDoc)
            state.error.clear();
        return IsfReloadResult::Unchanged;
    }
    if (state.failedDoc == entry->doc)
        return IsfReloadResult::Unchanged;

    auto fresh = std::make_unique<IsfRenderer>(entry->doc, entry->file.parent_path());
    if (renderer && renderer->ready() && glReady && !fresh->compile()) {
        state.failedDoc = entry->doc;
        state.error = fresh->error();
        return IsfReloadResult::Unchanged;
    }
    renderer = std::move(fresh);
    state.failedDoc.reset();
    state.error.clear();
    return IsfReloadResult::Replaced;
}

std::string isfReloadMessage(const IsfReloadState& state)
{
    if (state.error.empty())
        return {};
    return "O arquivo foi alterado e tem erro; a versão anterior continua rodando.\n" + state.error;
}

}
