#pragma once

#include "media/RemoteSource.h"

namespace bsl::media {

class WebSource : public RemoteSource
{
public:
    WebSource(int canvasWidth, int canvasHeight);
    ~WebSource() override;

    static std::vector<ParamDesc> describe(const ParamValues& values);
    static std::vector<ParamDesc> describeFile(const ParamValues& values);
    static std::string label(const ParamValues& values);
    static bool available();
    static std::string fileUrl(const std::filesystem::path& file);

    std::vector<ParamDesc> paramDescs() const override;
    bool isLive() const override { return true; }
    std::string pageTitle() const { return hostSnapshot().title; }
    bool hasPageSize() const { return width() > 0; }

protected:
    const char* kindName() const override { return "web"; }
    WorkerJson buildOpenBody() const override;
    void outputSize(int& w, int& h) const override;
    void onOpened() override;
    void onParamsChanged() override;
    void onCanvasResized(int w, int h) override;

    virtual WorkerJson paramsJson() const;
    virtual std::string currentUrl() const;

private:
    bool fileMode_ = false;
    std::string lastUrl_;
    WorkerJson lastParams_;
    int lastW_ = 0, lastH_ = 0;
};

}
