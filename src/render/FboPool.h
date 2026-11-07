#pragma once

#include "render/Fbo.h"

#include <memory>
#include <vector>

namespace bsl {

class FboPool
{
public:
    class Lease
    {
    public:
        Lease() = default;
        Lease(FboPool* pool, Fbo* fbo) : pool_(pool), fbo_(fbo) {}
        ~Lease() { reset(); }
        Lease(const Lease&) = delete;
        Lease& operator=(const Lease&) = delete;
        Lease(Lease&& o) noexcept : pool_(o.pool_), fbo_(o.fbo_)
        {
            o.pool_ = nullptr;
            o.fbo_ = nullptr;
        }
        Lease& operator=(Lease&& o) noexcept
        {
            if (this != &o) {
                reset();
                pool_ = o.pool_;
                fbo_ = o.fbo_;
                o.pool_ = nullptr;
                o.fbo_ = nullptr;
            }
            return *this;
        }
        void reset()
        {
            if (pool_ && fbo_)
                pool_->release(fbo_);
            pool_ = nullptr;
            fbo_ = nullptr;
        }
        Fbo* get() const { return fbo_; }
        Fbo& operator*() const { return *fbo_; }
        Fbo* operator->() const { return fbo_; }
        explicit operator bool() const { return fbo_ != nullptr; }

    private:
        FboPool* pool_ = nullptr;
        Fbo* fbo_ = nullptr;
    };

    FboPool() = default;
    FboPool(const FboPool&) = delete;
    FboPool& operator=(const FboPool&) = delete;

    Lease acquire(int width, int height);
    void release(Fbo* fbo);
    void endFrame(int maxIdleFrames = 240);
    void clear();

    size_t size() const { return entries_.size(); }
    size_t inUse() const;
    size_t created() const { return created_; }

private:
    struct Entry
    {
        std::unique_ptr<Fbo> fbo;
        bool used = false;
        int idle = 0;
    };
    std::vector<Entry> entries_;
    size_t created_ = 0;
};

}
