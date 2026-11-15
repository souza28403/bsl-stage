#include "media/ControlPanel.h"

#include "platform/OpenUrl.h"
#include "platform/Unavailable.h"

namespace bsl::media {

bool openUrlInBrowser(const std::string& url, std::string* error)
{
    if (url.rfind("http://127.0.0.1:", 0) != 0 && url.rfind("http://localhost:", 0) != 0) {
        if (error)
            *error = "endereÃ§o nÃ£o permitido";
        return false;
    }
    if (!bslplat::openUrl(url)) {
        if (error)
            *error = "nÃ£o foi poss¨ªvel abrir o navegador padrÃ£o";
        return false;
    }
    return true;
}

bool openUrlInOwnWindow(const std::string&, const std::string&, std::string* error)
{
    if (error)
        *error = bslplat::unavailableText("Janela pr¨®pria do painel");
    return false;
}

void closeOwnControlWindow() {}

unsigned long ownControlWindowPid()
{
    return 0ul;
}

}
