// Pedal rest shaping (STD-033, docs/controls-contract.md). Some games' pedal filters read only the device's nominal end
// as released (V-Rally 4's MaxDefaultStick / InvertMaxDefaultStick), so a pedal that rests inside its range (the owner's
// brake rests at 1743 of 0..65535) reads slightly pressed at rest. A proxy that sees GetDeviceState can replace that
// axis's sample with the one the game's own filter reads as the contract's normalized value (normalize() in
// dbce_controls.hpp): released at the captured rest, full at the captured full end, linear between.
// Header-only, no Windows calls; test_shape.cpp checks it on x64 and x86. Opt-in per game: an empty table changes nothing.
#pragma once
#include "dbce_controls.hpp"
#include "dbce_inject_table.hpp"   // StateView
#include <atomic>
#include <cmath>
#include <deque>
#include <string>
#include <vector>

namespace dbce { namespace controls {

// Does the pedal rise from its minimum? Schema 2 `cal=released..full` gives the direction (full above released); without
// it, the captured travel does. A native-config writer that picks the game's filter from this (V-Rally's vr_controls.hpp
// uses the travel rule today) must use the same cal-aware rule before its consumer advertises ini-controls-2 (STD-037).
inline bool risesFromMin(const Binding &b) { return b.cal.size() == 2 ? b.cal[1] > b.cal[0] : b.travel > 0; }

// The end the game's pedal filter reads as released: the minimum when the pedal rises from it and the player did not
// invert the pedal, else the maximum. V-Rally's adapter picks its filter by the same rule (vr_controls.hpp).
inline bool releasedAtMin(const Binding &b) { return risesFromMin(b) != b.inverted; }

// A pedal needs a shape when the game's end-anchored filter differs from the contract (otherwise shapePedal() is the
// identity). Schema 1: when its rest is not the end its travel starts from. Schema 2 `cal`: unless the measured released
// and full are exactly the device's two ends (a calibration that stops short of an end needs the shape).
inline bool needsShape(const std::string &action, const Binding &b)
{
    if (b.kind != Kind::Axis || shapeOf(action) != Shape::Pedal) return false;
    if (b.cal.size() == 2) {
        const bool rises = risesFromMin(b);
        return b.cal[0] != (rises ? b.min : b.max) || b.cal[1] != (rises ? b.max : b.min);
    }
    return b.rest != (b.travel > 0 ? b.min : b.max);
}

// A sample in the range the game set on the axis (gmin..gmax) -> the sample the game's filter reads as the contract's
// normalized value. Widened arithmetic throughout; rounded half away from zero; clamped to gmin..gmax. Assumes
// fits(action, b) == "" (ShapeTable::add checks it).
inline long shapePedal(const std::string &action, const Binding &b, long raw, long gmin, long gmax)
{
    if (gmax <= gmin) return raw;
    const long captured = rescale(raw, gmin, gmax, b.min, b.max);
    const double n = normalize(action, b, captured);
    const double t = releasedAtMin(b) ? n : 1.0 - n;
    const double r = std::round(double(gmin) + t * (double(gmax) - double(gmin)));
    if (r <= double(gmin)) return gmin;
    if (r >= double(gmax)) return gmax;
    return (long)r;
}

struct PedalShape {
    std::string instance;   // the binding's dev, lowercase "{...}" as parse() stores it
    int axis = 0;           // DIJOYSTATE axis 0-7 (X Y Z Rx Ry Rz Slider0 Slider1)
    std::string action;
    Binding binding;
};

struct ShapeResult {
    int shaped = 0;
    int unavailable = 0;   // a shape this device needs that could not be applied: that axis is left as read, and the
                           // caller must report it (never as a shaped/calibrated value)
};

// Why a required shape was not applied to a read.
enum class ShapeMiss { None = 0, Range = 1, Format = 2, NoAxes = 3 };
inline const char *missText(ShapeMiss m)
{
    return m == ShapeMiss::Range ? "the game's range for the axis is unreadable or not increasing"
         : m == ShapeMiss::Format ? "the game reads the device in a non-standard format"
         : m == ShapeMiss::NoAxes ? "the read has no axes" : "none";
}

// What the reads so far show for one requested shape (requested is not observed: a shape counts as applied only once a
// read of its device was shaped).
struct ShapeState {
    long shaped = 0, unavailable = 0;
    ShapeMiss lastMiss = ShapeMiss::None;
};

// Built once per process (from the profile at game start), then only read; the observation counters are atomic. A hook
// publishes it by setting a pointer once, after it is complete, and never replaces or frees it while the process can
// still read devices. Not copyable.
class ShapeTable {
public:
    ShapeTable() = default;
    ShapeTable(const ShapeTable &) = delete;
    ShapeTable &operator=(const ShapeTable &) = delete;

    // A parsed pedal axis binding that needs a shape; anything else is refused with a reason (nothing added).
    bool add(const std::string &action, const Binding &b, std::string &why)
    {
        why.clear();
        if (b.kind != Kind::Axis || shapeOf(action) != Shape::Pedal) { why = "not a pedal axis"; return false; }
        if (b.dev.empty() || detail::guid(b.dev) != b.dev) { why = "bad-dev"; return false; }   // normalized, as parse() stores it
        if (b.prod.empty() || detail::guid(b.prod) != b.prod) { why = "bad-prod"; return false; }
        if (b.travel != 1 && b.travel != -1) { why = "bad-travel"; return false; }
        if (b.index < 0 || b.index > 7) { why = "bad-index"; return false; }
        if (b.max <= b.min || b.rest < b.min || b.rest > b.max) { why = "rest outside the range"; return false; }
        const std::string f = fits(action, b);
        if (!f.empty()) { why = f; return false; }
        if (!needsShape(action, b)) { why = "rest at the travel's start: no shape needed"; return false; }
        for (auto &s : shapes_)
            if (s.instance == b.dev && s.axis == b.index) { why = "this axis is already shaped (" + s.action + ")"; return false; }
        shapes_.push_back({b.dev, (int)b.index, action, b});
        obs_.emplace_back();
        return true;
    }
    bool any() const { return !shapes_.empty(); }
    const std::vector<PedalShape> &shapes() const { return shapes_; }
    int countFor(const std::string &instance) const
    {
        int n = 0;
        for (auto &s : shapes_) n += s.instance == instance ? 1 : 0;
        return n;
    }
    ShapeState state(size_t i) const
    {
        ShapeState st;
        if (i >= obs_.size()) return st;
        st.shaped = obs_[i].shaped.load();
        st.unavailable = obs_[i].unavailable.load();
        st.lastMiss = (ShapeMiss)obs_[i].lastMiss.load();
        return st;
    }
    // A device the game created whose identity the hook could not establish (no reported instance, or its device table was
    // full): any shape it needs cannot be applied. Counted once per device, reported beside the per-shape states.
    void noteUnidentified() const { unidentified_.fetch_add(1); }
    long unidentified() const { return unidentified_.load(); }

    // Shapes this device's axes in a just-read standard state (c_dfDIJoystick/2 only: the caller checks the format).
    // range(axis, min, max) reports the range the game set on the axis (DIPROP_RANGE). A range that cannot be read or is
    // not increasing is never assumed: that axis is left as read and counted unavailable. first(shape, miss, min, max) is
    // called the first time each shape is applied (miss None) and the first time each kind of miss happens to it.
    template <class Range, class First>
    ShapeResult apply(const std::string &instance, StateView &v, Range range, First first) const
    {
        ShapeResult r;
        for (size_t i = 0; i < shapes_.size(); ++i) {
            const PedalShape &s = shapes_[i];
            if (s.instance != instance) continue;
            long mn = 0, mx = 0;
            ShapeMiss miss = ShapeMiss::None;
            if (!v.axes) miss = ShapeMiss::NoAxes;
            else if (!range(s.axis, mn, mx) || mx <= mn) miss = ShapeMiss::Range;
            if (miss != ShapeMiss::None) { ++r.unavailable; missed(i, miss, first, mn, mx); continue; }
            v.axes[s.axis] = shapePedal(s.action, s.binding, v.axes[s.axis], mn, mx);
            ++r.shaped;
            if (obs_[i].shaped.fetch_add(1) == 0) first(s, ShapeMiss::None, mn, mx);
        }
        return r;
    }
    template <class Range>
    ShapeResult apply(const std::string &instance, StateView &v, Range range) const
    {
        return apply(instance, v, range, [](const PedalShape &, ShapeMiss, long, long) {});
    }
    // Every shape of this device missed for one reason (a read the hook cannot shape at all, e.g. a custom format).
    template <class First>
    ShapeResult missAll(const std::string &instance, ShapeMiss miss, First first) const
    {
        ShapeResult r;
        for (size_t i = 0; i < shapes_.size(); ++i)
            if (shapes_[i].instance == instance) { ++r.unavailable; missed(i, miss, first, 0, 0); }
        return r;
    }

private:
    struct Obs {
        std::atomic<long> shaped{0}, unavailable{0};
        std::atomic<int> lastMiss{0}, missSeen{0};   // missSeen: one bit per ShapeMiss already reported
    };
    template <class First>
    void missed(size_t i, ShapeMiss miss, First &first, long mn, long mx) const
    {
        obs_[i].unavailable.fetch_add(1);
        obs_[i].lastMiss.store((int)miss);
        const int bit = 1 << (int)miss;
        if (!(obs_[i].missSeen.fetch_or(bit) & bit)) first(shapes_[i], miss, mn, mx);
    }
    std::vector<PedalShape> shapes_;
    mutable std::deque<Obs> obs_;   // parallel to shapes_; a deque never moves its elements (atomics cannot move)
    mutable std::atomic<long> unidentified_{0};
};

// One line per requested shape for a status page: corrected (observed), requested but not seen yet, or unavailable.
inline std::vector<std::string> describe(const ShapeTable &t)
{
    std::vector<std::string> out;
    for (size_t i = 0; i < t.shapes().size(); ++i) {
        const PedalShape &s = t.shapes()[i];
        const ShapeState st = t.state(i);
        std::string line = s.action + " rest " + std::to_string(s.binding.rest) + ": ";
        if (st.unavailable && !st.shaped) line += std::string("NOT corrected (") + missText(st.lastMiss) + "); the raw value is used";
        else if (st.unavailable) line += "corrected, but some reads were not (" + std::string(missText(st.lastMiss)) + ")";
        else if (st.shaped) line += "corrected";
        else line += "requested; not seen yet (the game has not read that device)";
        out.push_back(line);
    }
    if (t.unidentified()) out.push_back(std::to_string(t.unidentified()) + " device(s) the mod could not identify (no reported instance, or its device table was full): any rest correction they need is not applied");
    return out;
}
}} // namespace dbce::controls
