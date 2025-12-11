#include <algorithm>
#include <atomic>
#include <cassert>
#include <map>
#include <utility>

#include <format>
#include <zest/math/math_utils.h>
#include <zest/string/murmur_hash.h>

#include <zest/file/serializer.h>
#include <pico_zest/time/pico_profiler.h>

#include <iostream>
#include <pico/stdlib.h>

using namespace std::chrono;
using namespace Zest;

// Initial prototypes used:
// https://gist.github.com/CedricGuillemet/fc82e7a9b2f703ff1ebf
// https://github.com/arnaud-jamin/imgui/blob/master/profiler.cpp
//
// This one is better at spans that cover frame boundaries, uses cross platform cpp libraries
// instead of OS specific ones.
// No claims made about performance, but in practice has been very useful in finding bugs
// Has a summary view and support for zoom/pan, CTRL+select range or click in the summary graphs
// There is an optional single 'Region' which I use for audio frame profiling; you can only have one
// The profile macros can pick a unique/nice color for a given name.
// There is a LOCK_GUARD wrapper around a mutex, for tracking lock times
// Requires some helper code from my zest library (https://github.com/Rezonality/Zest) for:
// - NVec/NRect
// - murmur hash (for text to color)
// - dpi
// - fmt for text formatting
// I typically use this as a 'one shot, collect frames' debugger.  I hit the Pause/Resume button at interesting spots and collect a bunch of frames.
// You have to pause to navigate/inspect.
// All memory allocation is up-front; change the 'Max' values below to collect more frames
// The profiler just 'stops' when the memory is full.  It can be restarted/stopped.
// I pulled this together over the space of a weekend, it could be tidier here and there, but it works great ;)
namespace Zest
{

namespace Profiler
{

namespace
{
PicoMutex gMutex;

ProfileSettings settings;

Zest::timer gTimer;
bool gPaused = true;
bool gRequestPause = false;
bool gRestarting = true;
bool gDumped = false;

std::atomic<uint64_t> gProfilerGeneration = 0;
thread_local int gThreadIndexTLS = -1;
thread_local uint64_t gGenerationTLS = -1;
std::vector<glm::vec4> DefaultColors;

ProfilerData gProfilerData;

} // namespace

void Reset();

// Optionally call this before doing any profiler calls to change the defaults
void SetProfileSettings(const ProfileSettings& s)
{
    settings = s;
}

#define NUM_DEFAULT_COLORS 16

void CalculateColors()
{
    double golden_ratio_conjugate = 0.618033988749895;
    double h = .85f;
    for (int i = 0; i < (int)NUM_DEFAULT_COLORS; i++)
    {
        h += golden_ratio_conjugate;
        h = std::fmod(h, 1.0);
        DefaultColors.emplace_back(HSVToRGB(float(h) * 360.0f, 0.6f, 200.0f));
    }
}

// Run Init every time a profile is started
void Init()
{
    CalculateColors();

    gProfilerData.threadData.resize(settings.MaxThreads);

    gProfilerGeneration++;

    for (uint32_t iZero = 0; iZero < settings.MaxThreads; iZero++)
    {
        ThreadData* threadData = &gProfilerData.threadData[iZero];
        threadData->initialized = iZero == 0;
        threadData->maxLevel = 0;
        threadData->minTime = std::numeric_limits<int64_t>::max();
        threadData->maxTime = 0;
        threadData->currentEntry = 0;
        threadData->name = std::string("Thread ") + std::to_string(iZero);
        threadData->entries.resize(settings.MaxEntriesPerThread);
        memset(threadData->entries.data(), 0, sizeof(ProfilerEntry) * threadData->entries.size());
        threadData->entryStack.resize(50);
        threadData->callStackDepth = 0;
    }

    gProfilerData.frameData.resize(settings.MaxFrames);
    gProfilerData.regionData.resize(settings.MaxRegions);
    for (auto& frame : gProfilerData.frameData)
    {
        frame.frameThreads.resize(settings.MaxThreads);
        frame.frameThreadCount = 0;
        for (auto& threadInfo : frame.frameThreads)
        {
            threadInfo.activeEntry = 0;
            threadInfo.threadIndex = 0;
        }
    }

    gThreadIndexTLS = 0;
    gGenerationTLS = 0;
    gProfilerData.threadData[0].initialized = true;
    gRestarting = true;
    gProfilerData.currentFrame = 0;
    gProfilerData.currentRegion = 0;
    gDumped = false;
    gProfilerData.maxFrameTime = duration_cast<nanoseconds>(milliseconds(30)).count();
    timer_start(gTimer);

    gPaused = false;
}

void InitThread()
{
    PicoLockGuard lock(gMutex);

    for (uint32_t iThread = 0; iThread < settings.MaxThreads; iThread++)
    {
        ThreadData* threadData = &gProfilerData.threadData[iThread];
        if (!threadData->initialized)
        {
            // use it
            gThreadIndexTLS = iThread;
            gGenerationTLS = gProfilerGeneration;
            threadData->currentEntry = 0;
            threadData->initialized = true;

            return;
        }
    }
    assert(false && "Every thread slots are used!");
}

void FinishThread()
{
    PicoLockGuard lock(gMutex);
    assert(gThreadIndexTLS > -1 && "Trying to finish an uninitilized thread.");
    gProfilerData.threadData[gThreadIndexTLS].initialized = false;
    gThreadIndexTLS = -1;
}

void Finish()
{
    gProfilerData.threadData.clear();
}

void SetPaused(bool pause)
{
    if (gPaused != pause)
    {
        gRequestPause = pause;
    }
}

bool DumpReady()
{
    return gPaused && !gDumped;
}

std::ostringstream Dump()
{
    std::ostringstream ss;
    if (!gDumped && gPaused)
    {
        std::map<uint64_t, std::string> stringMap;
        for (auto& thread : gProfilerData.threadData)
        {
            for (auto& entry : thread.entries)
            {
                if (entry.szSection != nullptr)
                {
                    stringMap[reinterpret_cast<uint64_t>(entry.szSection)] = std::string(entry.szSection);
                }
                if (entry.szFile != nullptr)
                {
                    stringMap[reinterpret_cast<uint64_t>(entry.szFile)] = std::string(entry.szFile);
                }
            }
        }

        for (auto& [ptr, str] : stringMap)
        {
            gProfilerData.stringPointers.push_back(ptr);
            gProfilerData.strings.push_back(str);
        }

        Zest::binary_writer writer(ss);
        serialize(writer, gProfilerData);

        gDumped = true;
    }
    return ss;
}

ThreadData* GetThreadData()
{
    if (gGenerationTLS != gProfilerGeneration.load())
    {
        gThreadIndexTLS = -1;
    }

    if (gThreadIndexTLS == -1)
    {
        InitThread();
    }

    return &gProfilerData.threadData[gThreadIndexTLS];
}

void HideThread()
{
    if (gPaused)
    {
        return;
    }
    ThreadData* threadData = GetThreadData();
    threadData->hidden = true;
}

void Reset()
{
    PicoLockGuard lock(gMutex);
    gThreadIndexTLS = -1;
    Init();
}

bool CheckEndState()
{
    if (gProfilerData.threadData[gThreadIndexTLS].currentEntry >= settings.MaxEntriesPerThread)
    {
        gPaused = true;
        gRequestPause = true;
    }

    if (gProfilerData.currentFrame >= settings.MaxFrames)
    {
        gPaused = true;
        gRequestPause = true;
    }

    if (gProfilerData.currentRegion >= settings.MaxRegions)
    {
        gPaused = true;
        gRequestPause = true;
    }
    return gPaused;
}

void PushSectionBase(const char* szSection, uint32_t color, const char* szFile, int line)
{
    if (gPaused)
    {
        return;
    }

    auto elapsed = timer_get_elapsed(gTimer).count();

    ThreadData* threadData = GetThreadData();
    if (CheckEndState())
    {
        return;
    }

    assert(threadData->callStackDepth < settings.MaxCallStack && "Might need to make call stack bigger!");

    // check again
    if (gPaused)
    {
        return;
    }

    // Write entry 0
    ProfilerEntry* profilerEntry = &threadData->entries[threadData->currentEntry];
    threadData->entryStack[threadData->callStackDepth] = threadData->currentEntry;

    if (threadData->callStackDepth > 0)
    {
        profilerEntry->parent = threadData->entryStack[threadData->callStackDepth - 1];
        assert(profilerEntry->parent < threadData->currentEntry);
    }
    else
    {
        profilerEntry->parent = 0xFFFFFFFF;
    }

    assert(szFile != NULL && "No file string specified");
    assert(szSection != NULL && "No section name specified");

    profilerEntry->color = color;
    profilerEntry->szFile = szFile;
    profilerEntry->szSection = szSection;
    profilerEntry->line = line;
    profilerEntry->startTime = elapsed;
    profilerEntry->endTime = std::numeric_limits<int64_t>::max();
    profilerEntry->level = threadData->callStackDepth;
    threadData->callStackDepth++;
    threadData->currentEntry++;

    threadData->maxLevel = std::max(threadData->maxLevel, threadData->callStackDepth);

    threadData->minTime = std::min(profilerEntry->startTime, threadData->minTime);
    threadData->maxTime = std::max(profilerEntry->startTime, threadData->maxTime);

    // New thread begin during frame
    if (threadData->currentEntry == 1)
    {
        // Add this thread to the current frame info
        if (gProfilerData.currentFrame > 0)
        {
            auto& frame = gProfilerData.frameData[gProfilerData.currentFrame - 1];
            auto& threadInfo = frame.frameThreads[frame.frameThreadCount];
            threadInfo.activeEntry = threadData->currentEntry - 1;
            threadInfo.threadIndex = gThreadIndexTLS;
            frame.frameThreadCount++;
        }
    }

    if (gRestarting)
    {
        gRestarting = false;
    }
}

void PopSection()
{
    if (gPaused)
    {
        return;
    }

    ThreadData* threadData = GetThreadData();
    if (CheckEndState())
    {
        return;
    }

    if (gRestarting || gPaused)
    {
        return;
    }

    if (threadData->callStackDepth <= 0)
    {
        return;
    }

    // Back to the last entry we wrote
    threadData->callStackDepth--;

    int entryIndex = threadData->entryStack[threadData->callStackDepth];
    ProfilerEntry* profilerEntry = &threadData->entries[entryIndex];

    assert(profilerEntry->szSection != nullptr);

    // store end time
    profilerEntry->endTime = timer_get_elapsed(gTimer).count();

    threadData->maxTime = std::max(profilerEntry->endTime, threadData->maxTime);
}

void NameThread(const char* pszName)
{
    if (gPaused)
    {
        return;
    }

    // Must get thread data to init the thread
    ThreadData* threadData = GetThreadData();
    threadData->name = pszName;
}

// You are allowed one secondary region - I use it for audio thread monitoring
void BeginRegion()
{
    if (gPaused)
    {
        return;
    }

    // Must get thread data to init the thread
    ThreadData* threadData = GetThreadData();
    if (CheckEndState())
    {
        return;
    }

    auto& region = gProfilerData.regionData[gProfilerData.currentRegion];
    region.startTime = timer_get_elapsed(gTimer).count();
}

void EndRegion()
{
    if (gPaused)
    {
        return;
    }

    ThreadData* threadData = GetThreadData();
    if (CheckEndState())
    {
        return;
    }

    auto& region = gProfilerData.regionData[gProfilerData.currentRegion];
    region.endTime = timer_get_elapsed(gTimer).count();

    //region.name = std::format("{:.2f}ms", float(timer_to_ms(nanoseconds(region.endTime - region.startTime))));

    gProfilerData.currentRegion++;
}

void NewFrame()
{
    if (gPaused)
    {
        return;
    }

    auto elapsed = timer_get_elapsed(gTimer).count();
    if (CheckEndState())
    {
        return;
    }

    auto& frame = gProfilerData.frameData[gProfilerData.currentFrame];
    for (uint32_t threadIndex = 0; threadIndex < settings.MaxThreads; threadIndex++)
    {
        auto& thread = gProfilerData.threadData[threadIndex];
        if (!thread.initialized)
        {
            continue;
        }
        if (thread.currentEntry > 0)
        {
            // Remember which entry was active for this thread
            auto& threadInfo = frame.frameThreads[frame.frameThreadCount];
            threadInfo.activeEntry = thread.currentEntry - 1;
            threadInfo.threadIndex = threadIndex;

            // Next thread
            frame.frameThreadCount++;
        }
    }

    frame.startTime = elapsed;
    if (gProfilerData.currentFrame > 0)
    {
        gProfilerData.frameData[gProfilerData.currentFrame - 1].endTime = frame.startTime;
        //gProfilerData.frameData[gProfilerData.currentFrame - 1].name = std::format("{:.2f}ms", float(timer_to_ms(nanoseconds(frame.startTime - gProfilerData.frameData[gProfilerData.currentFrame - 1].startTime))));
    }
    gProfilerData.currentFrame++;
}

const glm::vec4& ColorFromName(const char* pszName, const uint32_t len)
{
    const auto col = murmur_hash(pszName, len, 0);
    return DefaultColors[col % NUM_DEFAULT_COLORS];
}

#ifndef PICO_PROFILER


#endif

} // namespace Profiler
} // namespace Zest
