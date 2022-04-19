
#include "ocr.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <clocale>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "backend/backend.h"
#include "backend/screenshot.h"
#include "backend/screenshot_error.h"
#include "error.h"
#include "geometry.h"
#include "geometry_c.h"
#include "img.h"
#include "ocr_engine/ocr_engine.h"
#include "progress_tracker.h"
#include "timing.h"


static dpso::backend::Backend* backend;


// Tesseract versions before 4.1.0 only work with "C" locale. Since we
// run Tesseract in a separate thread, it's possible neither to call
// setlocale() from the background thread nor let the user to change
// the locale while OCR is active.
//
// When the locale is changed and restored is documented in
// dpsoOcrQueueJob().

static std::vector<const void*> localeRefHolders;
static std::string lastLocale;
static bool localeChanged;


static auto localeRefHoldersLowerBound(const void* refHolder)
    // In GCC 4.8, vectors' insert(), erase(), etc. only accept
    // non-const iterators.
    -> std::vector<const void*>::iterator
{
    return std::lower_bound(
        localeRefHolders.begin(), localeRefHolders.end(), refHolder);
}


static void setCLocale(const void* refHolder)
{
    const auto iter = localeRefHoldersLowerBound(refHolder);
    if (iter != localeRefHolders.end() && *iter == refHolder)
        return;

    localeRefHolders.insert(iter, refHolder);

    if (localeRefHolders.size() > 1 || localeChanged)
        return;

    if (const auto* locale = std::setlocale(LC_ALL, nullptr)) {
        lastLocale = locale;
        std::setlocale(LC_ALL, "C");
        localeChanged = true;
    }
}


static void restoreLocale(const void* refHolder)
{
    const auto iter = localeRefHoldersLowerBound(refHolder);
    if (iter == localeRefHolders.end() || *iter != refHolder)
        return;

    localeRefHolders.erase(iter);

    if (!localeRefHolders.empty() || !localeChanged)
        return;

    std::setlocale(LC_ALL, lastLocale.c_str());
    localeChanged = false;
}


namespace {


// The public API gives a sorted list of language codes, while in
// OcrEngine they may be in arbitrary order. A Lang at the language
// index from public API refers to the state of the engine's language
// at Lang::langIdx.
struct Lang {
    int langIdx;
    bool isActive;
};


// 19 characters for ISO 8601 (YYYY-MM-DD HH:MM:SS) + null.
using Timestamp = std::array<char, 20>;


struct Job {
    std::unique_ptr<dpso::backend::Screenshot> screenshot;
    std::vector<int> langIndices;
    dpso::OcrFeatures ocrFeatures;
    Timestamp timestamp;
};


struct JobResult {
    dpso::OcrResult ocrResult;
    Timestamp timestamp;
};


// Link between main and background threads.
struct Link {
    mutable std::mutex lock;

    std::queue<Job> jobQueue;
    bool jobActive;

    bool waitingForResults;
    DpsoOcrProgressCallback waitingProgressCallback;
    void* waitingUserData;

    DpsoOcrProgress progress;

    std::vector<JobResult> results;

    bool terminateJobs;
    bool terminateThread;

    void reset()
    {
        clearJobQueue();
        jobActive = false;

        waitingForResults = false;

        progress = {};

        results.clear();

        terminateJobs = false;
        terminateThread = false;
    }

    void clearJobQueue()
    {
        // We don't use = {} with std::queue for compatibility
        // with older GCC versions where explicit default
        // constructors are not fixed. See P0935R0.
        jobQueue = std::queue<Job>();
    }

    bool jobsPending() const
    {
        return !jobQueue.empty() || jobActive;
    }
};


#define LINK_LOCK(L) std::lock_guard<std::mutex> guard(L.lock)


}


struct DpsoOcr {
    std::unique_ptr<dpso::OcrEngine> engine;

    std::vector<Lang> langs;
    int numActiveLangs;

    Link link;
    std::thread thread;

    std::vector<std::uint8_t> imgBuffers[3];
    bool dumpDebugImage;

    std::vector<JobResult> fetchedResults;
    std::vector<DpsoOcrJobResult> returnResults;
};


static void cacheLangs(DpsoOcr& ocr)
{
    ocr.langs.clear();
    ocr.langs.reserve(ocr.engine->getNumLangs());

    for (int i = 0; i < ocr.engine->getNumLangs(); ++i)
        ocr.langs.push_back({i, false});

    std::sort(
        ocr.langs.begin(), ocr.langs.end(),
        [&](const Lang& a, const Lang& b)
        {
            return std::strcmp(
                ocr.engine->getLangCode(a.langIdx),
                ocr.engine->getLangCode(b.langIdx)) < 0;
        });
}


static void threadLoop(DpsoOcr* ocr);


struct DpsoOcr* dpsoOcrCreate()
{
    // We don't use OcrUPtr here because dpsoOcrDelete() expects
    // a joinable thread.
    std::unique_ptr<DpsoOcr> ocr{new DpsoOcr{}};

    setCLocale(ocr.get());
    try {
        ocr->engine = dpso::OcrEngine::create();
    } catch (dpso::OcrEngineError& e) {
        dpsoSetError("Can't create OCR engine: %s", e.what());
        restoreLocale(ocr.get());
        return nullptr;
    }
    restoreLocale(ocr.get());

    cacheLangs(*ocr);

    ocr->thread = std::thread(threadLoop, ocr.get());

    const auto* dumpDebugImageEnvVar = std::getenv(
        "DPSO_DUMP_DEBUG_IMAGE");
    ocr->dumpDebugImage = (
        dumpDebugImageEnvVar
        && std::strcmp(dumpDebugImageEnvVar, "0") != 0);

    return ocr.release();
}


void dpsoOcrDelete(struct DpsoOcr* ocr)
{
    if (!ocr)
        return;

    dpsoOcrTerminateJobs(ocr);

    {
        LINK_LOCK(ocr->link);
        ocr->link.terminateThread = true;
    }
    assert(ocr->thread.joinable());
    ocr->thread.join();

    delete ocr;
}


int dpsoOcrGetNumLangs(const struct DpsoOcr* ocr)
{
    return ocr ? ocr->langs.size() : 0;
}


const char* dpsoOcrGetLangCode(const struct DpsoOcr* ocr, int langIdx)
{
    if (!ocr
            || langIdx < 0
            || static_cast<std::size_t>(langIdx) >= ocr->langs.size())
        return "";

    return ocr->engine->getLangCode(ocr->langs[langIdx].langIdx);
}


const char* dpsoOcrGetDefaultLangCode(const struct DpsoOcr* ocr)
{
    return ocr ? ocr->engine->getDefaultLangCode() : "";
}


const char* dpsoOcrGetLangName(
    const struct DpsoOcr* ocr, const char* langCode)
{
    return ocr ? ocr->engine->getLangName(langCode) : nullptr;
}


int dpsoOcrGetLangIdx(const struct DpsoOcr* ocr, const char* langCode)
{
    if (!ocr)
        return -1;

    const auto iter = std::lower_bound(
        ocr->langs.begin(), ocr->langs.end(), langCode,
        [&](const Lang& lang, const char* langCode)
        {
            return std::strcmp(
                ocr->engine->getLangCode(lang.langIdx), langCode) < 0;
        });

    if (iter != ocr->langs.end()
            && std::strcmp(
                ocr->engine->getLangCode(iter->langIdx),
                langCode) == 0)
        return iter - ocr->langs.begin();

    return -1;
}


int dpsoOcrGetLangIsActive(const struct DpsoOcr* ocr, int langIdx)
{
    if (!ocr
            || langIdx < 0
            || static_cast<std::size_t>(langIdx) >= ocr->langs.size())
        return false;

    return ocr->langs[langIdx].isActive;
}


void dpsoOcrSetLangIsActive(
    struct DpsoOcr* ocr, int langIdx, int newIsActive)
{
    if (!ocr
            || langIdx < 0
            || static_cast<std::size_t>(langIdx) >= ocr->langs.size()
            || newIsActive == ocr->langs[langIdx].isActive)
        return;

    ocr->langs[langIdx].isActive = newIsActive;

    if (newIsActive)
        ++ocr->numActiveLangs;
    else
        --ocr->numActiveLangs;
}


int dpsoOcrGetNumActiveLangs(const struct DpsoOcr* ocr)
{
    return ocr ? ocr->numActiveLangs : 0;
}


static std::vector<int> getActiveLangIndices(const DpsoOcr& ocr)
{
    std::vector<int> result;
    result.reserve(ocr.numActiveLangs);

    for (const auto& lang : ocr.langs)
        if (lang.isActive)
            result.push_back(lang.langIdx);

    return result;
}


static Timestamp createTimestamp()
{
    // We are still targeting old GCC versions, so double braces are
    // needed to avoid -Wmissing-field-initializers.
    Timestamp timestamp{{}};

    const auto time = std::time(nullptr);
    if (const auto* tm = std::localtime(&time))
        if (std::strftime(
                timestamp.data(), timestamp.size(),
                "%Y-%m-%d %H:%M:%S", tm) == 0)
            timestamp[0] = 0;

    return timestamp;
}


static dpso::OcrImage prepareScreenshot(
    DpsoOcr& ocr,
    const dpso::backend::Screenshot& screenshot,
    dpso::ProgressTracker& progressTracker)
{
    const int imageScale = 4;
    const auto bufferW = screenshot.getWidth() * imageScale;
    const auto bufferH = screenshot.getHeight() * imageScale;
    const auto bufferPitch = bufferW;

    for (auto& buffer : ocr.imgBuffers)
        buffer.resize(bufferH * bufferPitch);

    START_TIMING(screenshotGetData);
    screenshot.getGrayscaleData(&ocr.imgBuffers[0][0], bufferPitch);
    END_TIMING(
        screenshotGetData,
        "screenshot.getGrayscaleData (%ix%i px)",
        screenshot.getWidth(), screenshot.getHeight());

    START_TIMING(imageResizing);
    dpso::img::resize(
        &ocr.imgBuffers[0][0],
        screenshot.getWidth(), screenshot.getHeight(), bufferPitch,
        &ocr.imgBuffers[1][0],
        bufferW, bufferH, bufferPitch,
        &progressTracker);
    END_TIMING(
        imageResizing,
        "Image resizing (%ix%i px -> %ix%i px, x%i)",
        screenshot.getWidth(), screenshot.getHeight(),
        bufferW, bufferH,
        imageScale);

    const int unsharpMaskRadius = 10;

    START_TIMING(unsharpMasking);
    dpso::img::unsharpMask(
        &ocr.imgBuffers[1][0], bufferPitch,
        &ocr.imgBuffers[0][0], bufferPitch,
        &ocr.imgBuffers[2][0], bufferPitch,
        bufferW, bufferH,
        unsharpMaskRadius,
        1.0f,
        &progressTracker);
    END_TIMING(
        unsharpMasking,
        "Unsharp masking (radius=%i, %ix%i px)",
        unsharpMaskRadius, bufferW, bufferH);

    return {&ocr.imgBuffers[0][0], bufferW, bufferH, bufferPitch};
}


namespace {


struct OcrProgressCallbackData {
    DpsoOcr& ocr;
    dpso::ProgressTracker& progressTracker;
};


}


static bool ocrProgressCallback(int progress, void* userData)
{
    auto* data = static_cast<OcrProgressCallbackData*>(userData);
    assert(data);
    data->progressTracker.update(progress / 100.0f);

    auto& link = data->ocr.link;

    bool waitingForResults;
    {
        LINK_LOCK(link);
        waitingForResults = link.waitingForResults;
    }

    if (waitingForResults && link.waitingProgressCallback)
        link.waitingProgressCallback(link.waitingUserData);

    LINK_LOCK(link);
    return !link.terminateJobs;
}


static void progressTrackerFn(float progress, void* userData)
{
    auto* ocr = static_cast<DpsoOcr*>(userData);
    assert(ocr);

    LINK_LOCK(ocr->link);
    ocr->link.progress.curJobProgress = progress * 100;
}


static void processJob(DpsoOcr& ocr, const Job& job)
{
    assert(job.screenshot);
    assert(!job.langIndices.empty());

    // There are 3 progress jobs: resizing and unsharp masking in
    // prepareScreenshot() and OCR.
    dpso::ProgressTracker progressTracker(3, progressTrackerFn, &ocr);
    progressTracker.start();

    const auto ocrImage = prepareScreenshot(
        ocr, *job.screenshot, progressTracker);

    if (ocr.dumpDebugImage)
        dpso::img::savePgm(
            "dpso_debug.pgm",
            ocrImage.data,
            ocrImage.width, ocrImage.height, ocrImage.pitch);

    progressTracker.advanceJob();

    OcrProgressCallbackData callbackData{ocr, progressTracker};
    auto ocrResult = ocr.engine->recognize(
        ocrImage, job.langIndices, job.ocrFeatures,
        ocrProgressCallback, &callbackData);

    progressTracker.finish();

    LINK_LOCK(ocr.link);
    if (ocr.link.terminateJobs)
        return;

    ocr.link.results.push_back({std::move(ocrResult), job.timestamp});
}


static void threadLoop(DpsoOcr* ocr)
{
    while (true) {
        Job job;

        {
            LINK_LOCK(ocr->link);

            if (ocr->link.terminateThread)
                break;

            if (!ocr->link.jobQueue.empty()) {
                job = std::move(ocr->link.jobQueue.front());
                assert(job.screenshot);
                ocr->link.jobQueue.pop();

                ocr->link.jobActive = true;

                // Although processJob() will reset result to zero,
                // this should also be done before incrementing
                // curJob so we don't return the progress of the
                // previous job from dpsoOcrGetProgress() before
                // the new one starts.
                ocr->link.progress.curJobProgress = 0;

                ++ocr->link.progress.curJob;
            } else if (ocr->link.jobActive) {
                ocr->link.jobActive = false;
                ocr->link.progress = {};
            }
        }

        if (!job.screenshot) {
            std::this_thread::sleep_for(
                std::chrono::milliseconds(1000 / 60));
            continue;
        }

        processJob(*ocr, job);
    }
}


int dpsoOcrQueueJob(
    struct DpsoOcr* ocr, const struct DpsoOcrJobArgs* jobArgs)
{
    if (!backend) {
        dpsoSetError("Library is not initialized");
        return false;
    }

    if (!ocr) {
        dpsoSetError("ocr is null");
        return false;
    }

    if (!jobArgs) {
        dpsoSetError("jobArgs is null");
        return false;
    }

    if (dpsoRectIsEmpty(&jobArgs->screenRect)) {
        dpsoSetError("jobArgs->screenRect is empty");
        return false;
    }

    if (ocr->numActiveLangs == 0) {
        dpsoSetError("No active languages");
        return false;
    }

    START_TIMING(takeScreenshot);

    std::unique_ptr<dpso::backend::Screenshot> screenshot;
    try {
        screenshot = backend->takeScreenshot(
            dpso::Rect{jobArgs->screenRect});
    } catch (dpso::backend::ScreenshotError& e) {
        dpsoSetError("Can't take screenshot: %s", e.what());
        return false;
    }

    assert(screenshot);

    END_TIMING(
        takeScreenshot,
        "Take screenshot (%ix%i px)",
        screenshot->getWidth(),
        screenshot->getHeight());

    dpso::OcrFeatures ocrFeatures{};
    if (jobArgs->flags & dpsoOcrJobTextSegmentation)
        ocrFeatures |= dpso::ocrFeatureTextSegmentation;

    Job job{
        std::move(screenshot),
        getActiveLangIndices(*ocr),
        ocrFeatures,
        createTimestamp()
    };

    setCLocale(ocr);

    LINK_LOCK(ocr->link);

    ocr->link.jobQueue.push(std::move(job));

    ++ocr->link.progress.totalJobs;

    return true;
}


int dpsoOcrProgressEqual(
    const struct DpsoOcrProgress* a, const struct DpsoOcrProgress* b)
{
    return (
        a && b
        && a->curJobProgress == b->curJobProgress
        && a->curJob == b->curJob
        && a->totalJobs == b->totalJobs);
}


void dpsoOcrGetProgress(
    const struct DpsoOcr* ocr, struct DpsoOcrProgress* progress)
{
    if (!ocr || !progress)
        return;

    LINK_LOCK(ocr->link);
    *progress = ocr->link.progress;
}


int dpsoOcrGetJobsPending(const struct DpsoOcr* ocr)
{
    if (!ocr)
        return false;

    LINK_LOCK(ocr->link);
    return ocr->link.jobsPending();
}


void dpsoOcrFetchResults(
    struct DpsoOcr* ocr, struct DpsoOcrJobResults* results)
{
    if (!ocr || !results)
        return;

    LINK_LOCK(ocr->link);

    ocr->fetchedResults.clear();
    ocr->fetchedResults.swap(ocr->link.results);

    ocr->returnResults.clear();
    ocr->returnResults.reserve(ocr->fetchedResults.size());
    for (const auto& result : ocr->fetchedResults)
        ocr->returnResults.push_back(
            {result.ocrResult.getText(),
                result.ocrResult.getTextLen(),
                result.timestamp.data()});

    results->items = ocr->returnResults.data();
    results->numItems = ocr->returnResults.size();

    if (!ocr->link.jobsPending())
        restoreLocale(ocr);
}


static void waitJobsToFinish(const DpsoOcr& ocr)
{
    while (true) {
        {
            LINK_LOCK(ocr.link);
            if (!ocr.link.jobsPending())
                break;
        }

        std::this_thread::sleep_for(
            std::chrono::milliseconds(10));
    }
}


void dpsoOcrWaitJobsToComplete(
    struct DpsoOcr* ocr,
    DpsoOcrProgressCallback progressCallback,
    void* userData)
{
    if (!ocr)
        return;

    {
        LINK_LOCK(ocr->link);
        ocr->link.waitingForResults = true;
        ocr->link.waitingProgressCallback = progressCallback;
        ocr->link.waitingUserData = userData;
    }

    waitJobsToFinish(*ocr);

    LINK_LOCK(ocr->link);
    ocr->link.waitingForResults = false;

    if (ocr->link.terminateJobs) {
        // dpsoOcrTerminateJobs() was called from the status callback
        // during waiting.
        ocr->link.results.clear();

        ocr->link.terminateJobs = false;
    }

    restoreLocale(ocr);
}


void dpsoOcrTerminateJobs(struct DpsoOcr* ocr)
{
    if (!ocr)
        return;

    {
        LINK_LOCK(ocr->link);

        ocr->link.clearJobQueue();
        ocr->link.terminateJobs = true;

        if (ocr->link.waitingForResults)
            // dpsoOcrWaitJobsToComplete() will set terminateJobs to
            // false.
            return;
    }

    waitJobsToFinish(*ocr);

    // Locking here is not actually necessary since the idling
    // background thread doesn't access variables we use below.
    LINK_LOCK(ocr->link);

    ocr->link.results.clear();

    ocr->link.terminateJobs = false;

    restoreLocale(ocr);
}


namespace dpso {
namespace ocr {


void init(dpso::backend::Backend& backend)
{
    ::backend = &backend;
}


void shutdown()
{
    ::backend = nullptr;
}


}
}
