#include "ffgl/FfglCatalog.h"

namespace bsl::ffgl {

std::string pluginDisplayName(const PluginInfo& info)
{
    return info.name.empty() ? info.fileName : info.name;
}

Catalog& Catalog::instance()
{
    static Catalog* c = new Catalog();
    return *c;
}

Catalog::~Catalog() = default;

void Catalog::shutdown() {}

void Catalog::configure(std::vector<Folder> folders)
{
    std::lock_guard lk(mutex_);
    folders_ = std::move(folders);
}

std::vector<Folder> Catalog::folders() const
{
    std::lock_guard lk(mutex_);
    return folders_;
}

void Catalog::setCachePath(std::filesystem::path p)
{
    std::lock_guard lk(mutex_);
    cachePath_ = std::move(p);
}

void Catalog::clearCache() {}

std::string Catalog::scanStatus() const
{
    return "Plugins FFGL nÃ£o estÃ£o dispon¨ªveis nesta plataforma.";
}

bool Catalog::waitIdle(unsigned)
{
    return true;
}

void Catalog::rescan() {}

void Catalog::retry(const std::string&) {}

std::vector<EntryPtr> Catalog::entries() const
{
    return {};
}

EntryPtr Catalog::find(const std::string&) const
{
    return nullptr;
}

std::vector<BlockedPlugin> Catalog::blocked() const
{
    return {};
}

void Catalog::markSessionFault(const std::string&, const std::string&) {}

bool Catalog::sessionBlocked(const std::string&, std::string*) const
{
    return false;
}

}
