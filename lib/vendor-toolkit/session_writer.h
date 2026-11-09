// dbce.wheel.session v1, Windows/C++17. No device or game dependencies.
// Single owner thread; synchronous writes belong on an observation/worker
// thread, NEVER in a physics/input/FFB hook. This does not schedule samples.
// An interrupted/failed writer has no completion footer. Never overwrite data.
#pragma once
#include <windows.h>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <locale>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>

namespace dbce { namespace session {
using Text = std::map<std::string, std::string>;
using Channels = std::map<std::string, double>;
struct Metadata { std::string game, pluginVersion, toolkitVersion; Text properties, channelUnits; };
struct Limits { uint64_t bytes = 64 * 1024 * 1024; size_t recordBytes = 256 * 1024; double seconds = 600; };

// Metadata/channel names are deliberately ASCII in this initial native API.
// Values allow ASCII controls, escaped below. Paths use Win32 UTF-16 separately.
inline bool ascii(const std::string& s, size_t max, bool name = false) {
    if (s.size() > max || (name && s.find_first_not_of(" \r\n\t\v\f") == std::string::npos)) return false;
    for (unsigned char c : s) if (c >= 128 || c == 0) return false;
    return true;
}
inline std::string quote(const std::string& s) {
    std::string r = "\"";
    for (unsigned char c : s) {
        if (c == '\"' || c == '\\') { r += '\\'; r += char(c); }
        else if (c < 32) { const char* hex = "0123456789abcdef"; r += "\\u00"; r += hex[c >> 4]; r += hex[c & 15]; }
        else r += char(c);
    }
    return r + "\"";
}
inline std::ostringstream jsonStream() {
    std::ostringstream s; s.imbue(std::locale::classic()); s << std::setprecision(17); return s;
}
inline std::string dictionary(const Text& values) {
    if (values.size() > 1024) throw std::invalid_argument("metadata entries");
    std::string s = "{";
    for (const auto& v : values) {
        if (!ascii(v.first, 256, true) || !ascii(v.second, 4096)) throw std::invalid_argument("metadata text");
        if (s.size() > 1) s += ',';
        s += quote(v.first) + ':' + quote(v.second);
    }
    return s + '}';
}
class Writer {
    HANDLE file_ = INVALID_HANDLE_VALUE;
    Limits limits_;
    uint64_t bytes_ = 0, samples_ = 0;
    double last_ = 0, lastSample_ = -1;
    bool finished_ = false;
    std::string reason_;
    bool write(const std::string& line, bool count = true) {
        if (file_ == INVALID_HANDLE_VALUE) return false;
        DWORD written = 0;
        const std::string data = line + '\n';
        if (!WriteFile(file_, data.data(), DWORD(data.size()), &written, nullptr) || written != data.size()) {
            reason_ = "ioError"; abandon(); return false;
        }
        if (count) bytes_ += data.size();
        return true;
    }
    bool footer(const char* reason) {
        auto s = jsonStream();
        bool complete = std::string(reason) == "stopped";
        s << "{\"kind\":\"footer\",\"footer\":{\"elapsedSeconds\":" << last_
          << ",\"completed\":" << (complete ? "true" : "false") << ",\"stopReason\":" << quote(reason)
          << ",\"counts\":{\"acceptedSamples\":" << samples_ << ",\"acceptedMarkers\":0,\"writtenSamples\":" << samples_
          << ",\"writtenMarkers\":0,\"droppedSamples\":0,\"droppedMarkers\":0,\"queueFullCount\":0,\"contentionCount\":0,\"invalidCount\":0,\"limitCount\":"
          << (complete ? 0 : 1) << ",\"errorCount\":0,\"rejectedAfterStop\":0,\"dataBytesWritten\":" << bytes_ << "}}}";
        bool ok = write(s.str(), false);
        if (ok && !FlushFileBuffers(file_)) { reason_ = "flushError"; ok = false; }
        finished_ = ok; if (ok) reason_ = reason;
        abandon(); return ok;
    }
public:
    Writer() = default;
    Writer(const Writer&) = delete;
    Writer& operator=(const Writer&) = delete;
    ~Writer() { abandon(); } // Destruction never claims completion.
    bool active() const { return file_ != INVALID_HANDLE_VALUE; }
    bool completed() const { return finished_ && reason_ == "stopped"; }
    const std::string& reason() const { return reason_; }
    void abandon() { if (active()) { CloseHandle(file_); file_ = INVALID_HANDLE_VALUE; } }
    bool open(const std::wstring& path, const Metadata& m, Limits limits = {}) {
        if (active() || bytes_ || finished_) return false; // one file per object
        if (limits.bytes < 16384 || limits.bytes > 1024ull * 1024 * 1024 || limits.recordBytes < 1024 ||
            limits.recordBytes > 1024 * 1024 || !std::isfinite(limits.seconds) || limits.seconds <= 0 || limits.seconds > 86400)
            throw std::invalid_argument("recording limits");
        if (!ascii(m.game, 256, true) || !ascii(m.pluginVersion, 256, true) || !ascii(m.toolkitVersion, 256, true))
            throw std::invalid_argument("metadata identity");
        SYSTEMTIME now; GetSystemTime(&now); char utc[40];
        snprintf(utc, sizeof utc, "%04u-%02u-%02uT%02u:%02u:%02u.%03u0000Z", now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond, now.wMilliseconds);
        std::string header = "{\"kind\":\"metadata\",\"schema\":\"dbce.wheel.session\",\"version\":1,\"metadata\":{\"game\":" + quote(m.game) +
            ",\"pluginVersion\":" + quote(m.pluginVersion) + ",\"toolkitVersion\":" + quote(m.toolkitVersion) + ",\"startedUtc\":" + quote(utc) +
            ",\"properties\":" + dictionary(m.properties) + ",\"channelUnits\":" + dictionary(m.channelUnits) + "}}";
        if (header.size() + 1 > limits.recordBytes || header.size() + 1 + 8192 > limits.bytes) throw std::invalid_argument("metadata size");
        limits_ = limits;
        file_ = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (!active()) { reason_ = "openError"; return false; }
        return write(header);
    }
    bool sample(double elapsed, const Channels& channels) {
        if (!active()) return false;
        // Invalid observations make the file incomplete, rather than pretending
        // the signal was zero or silently skipping it in a qualified recording.
        if (!std::isfinite(elapsed) || elapsed < 0 || elapsed <= lastSample_ || channels.empty() || channels.size() > 1024) {
            reason_ = "invalidSample"; abandon(); return false;
        }
        if (elapsed > limits_.seconds) { footer("durationLimit"); return false; }
        auto s = jsonStream();
        s << "{\"kind\":\"sample\",\"sample\":{\"sequence\":" << samples_ << ",\"elapsedSeconds\":" << elapsed << ",\"channels\":{";
        bool first = true;
        for (const auto& c : channels) {
            if (!ascii(c.first, 256, true) || !std::isfinite(c.second)) { reason_ = "invalidSample"; abandon(); return false; }
            if (!first) s << ',';
            first = false; s << quote(c.first) << ':' << c.second;
        }
        s << "}}}";
        auto line = s.str();
        if (line.size() + 1 > limits_.recordBytes) { footer("recordSizeLimit"); return false; }
        if (bytes_ + line.size() + 1 + 8192 > limits_.bytes) { footer("fileSizeLimit"); return false; }
        if (!write(line)) return false;
        ++samples_; last_ = lastSample_ = elapsed; return true;
    }
    bool stop() { return active() && footer("stopped"); }
};
}} // dbce::session
