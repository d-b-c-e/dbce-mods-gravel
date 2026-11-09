// STD-023 frame-rate summary for the shared F6 panel: ten-second windows of frame times from the overlay's present
// hook. Pure statistics (tested by test_frame_stats.cpp): a fixed buffer, one sort per window, no allocation per frame.
#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdio>

namespace dbce { namespace panel {

struct FrameSummary {
    bool valid = false;
    int frames = 0;
    double avgFps = 0;      // frames / window time
    double lowFps = 0;      // 1% low: the mean of the slowest 1% of frame times, as fps
    double worstMs = 0;     // the slowest frame
};

class FrameWindow {
public:
    static const int kMax = 8192;                 // 10 s at up to 819 fps; a full buffer closes the window early
    static constexpr double kWindowMs = 10000.0;
    static constexpr double kGapMs = 5000.0;      // a longer frame: paused, minimised or loading; the window restarts

    // One frame time in ms; true when a window closed and Last() changed.
    bool Add(double ms)
    {
        if (!(ms > 0) || ms > kGapMs) { n_ = 0; sum_ = 0; return false; }
        ms_[n_++] = ms;
        sum_ += ms;
        if (sum_ < kWindowMs && n_ < kMax) return false;
        std::sort(ms_, ms_ + n_);
        const int slow = (std::max)(1, n_ / 100);
        double slowSum = 0;
        for (int i = n_ - slow; i < n_; ++i) slowSum += ms_[i];
        last_.valid = true;
        last_.frames = n_;
        last_.avgFps = n_ * 1000.0 / sum_;
        last_.lowFps = slow * 1000.0 / slowSum;
        last_.worstMs = ms_[n_ - 1];
        n_ = 0;
        sum_ = 0;
        return true;
    }
    const FrameSummary &Last() const { return last_; }

private:
    double ms_[kMax];
    int n_ = 0;
    double sum_ = 0;
    FrameSummary last_;
};

// The readout's line: the three numbers, then what the display is. `display` (the mod's own words, for example
// "Surround span, one wide view") wins over `triples` (-1 not reported, 0 off, 1 on), so a mod whose wide window is
// one planar view never reports "triples on". Truncated to `size`, never overrun.
inline void FormatFrameLine(char *out, size_t size, const FrameSummary &s, const char *display, int triples)
{
    if (!out || !size) return;
    char suffix[96] = "";
    if (display && *display) snprintf(suffix, sizeof suffix, "; %s", display);
    else if (triples >= 0) snprintf(suffix, sizeof suffix, "%s", triples ? "; triples on" : "; triples off");
    snprintf(out, size, "%.0f fps average, 1%% low %.0f fps, worst frame %.1f ms%s", s.avgFps, s.lowFps, s.worstMs, suffix);
}

}} // namespace dbce::panel
