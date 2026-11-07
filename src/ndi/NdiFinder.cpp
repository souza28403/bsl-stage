#include "ndi/NdiFinder.h"

#include "app/Log.h"

#include <memory>

namespace bsl::ndi {

namespace {
std::mutex g_sharedMutex;
Finder* g_shared = nullptr;
}

Finder::Finder(Runtime& runtime) : rt_(runtime) {}

Finder::~Finder()
{
    stop();
}

Finder& Finder::shared()
{
    std::lock_guard lk(g_sharedMutex);
    if (!g_shared)
        g_shared = new Finder(Runtime::instance());
    return *g_shared;
}

void Finder::shutdownShared()
{
    Finder* f = nullptr;
    {
        std::lock_guard lk(g_sharedMutex);
        f = g_shared;
        g_shared = nullptr;
    }
    delete f;
}

void Finder::start()
{
    if (!rt_.available() || running_.load())
        return;
    if (thread_.joinable())
        thread_.join();
    stop_ = false;
    running_ = true;
    thread_ = std::thread([this] { loop(); });
}

void Finder::stop()
{
    stop_ = true;
    if (thread_.joinable())
        thread_.join();
    running_ = false;
}

std::vector<SourceInfo> Finder::sources() const
{
    std::lock_guard lk(mutex_);
    return sources_;
}

void Finder::loop()
{
    const NDIlib_v5_t* api = rt_.api();
    NDIlib_find_create_t settings{};
    settings.show_local_sources = true;
    NDIlib_find_instance_t find = api->find_create_v2(&settings);
    if (!find) {
        log::warn("NDI: não foi possível criar a busca de fontes");
        running_ = false;
        return;
    }
    while (!stop_.load()) {
        api->find_wait_for_sources(find, 400);
        uint32_t count = 0;
        const NDIlib_source_t* list = api->find_get_current_sources(find, &count);
        std::vector<SourceInfo> now;
        for (uint32_t i = 0; list && i < count; ++i) {
            SourceInfo s;
            s.name = list[i].p_ndi_name ? list[i].p_ndi_name : "";
            s.url = list[i].p_url_address ? list[i].p_url_address : "";
            if (!s.name.empty())
                now.push_back(std::move(s));
        }
        std::lock_guard lk(mutex_);
        if (now != sources_) {
            sources_ = std::move(now);
            ++generation_;
        }
    }
    api->find_destroy(find);
}

}
