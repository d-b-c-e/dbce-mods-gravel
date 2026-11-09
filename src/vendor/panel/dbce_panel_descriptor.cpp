// Strict reader of the F6 settings descriptor (see dbce_panel_descriptor.h). A small JSON parser (RFC 8259 values,
// UTF-8 text, \u escapes including surrogate pairs) feeds a validator that knows the fixed page set and row kinds.
#include "dbce_panel_descriptor.h"
#include "dbce_mod_file.h"
#include "dbce_store.h"
#include <charconv>
#include <windows.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <utility>

namespace dbce { namespace panel {

namespace {

struct Json {
    enum Type { Null, Bool, Number, String, Array, Object } type = Null;
    bool b = false;
    double n = 0;
    std::string s;
    std::vector<Json> a;
    std::vector<std::pair<std::string, Json>> o;
    const Json *get(const char *key) const
    {
        for (const auto &kv : o) if (kv.first == key) return &kv.second;
        return nullptr;
    }
};

class Parser {
public:
    Parser(const char *text, size_t length, std::string &error) : p_(text), end_(text + length), error_(error) {}
    bool document(Json &out)
    {
        if (end_ - p_ >= 3 && (unsigned char)p_[0] == 0xEF && (unsigned char)p_[1] == 0xBB && (unsigned char)p_[2] == 0xBF) p_ += 3;
        if (!value(out, 0)) return false;
        ws();
        return p_ == end_ || fail("text after the JSON value");
    }

private:
    const char *p_, *end_;
    std::string &error_;
    const char *start_ = p_;

    bool fail(const char *what)
    {
        char b[96];
        sprintf_s(b, "JSON: %s at byte %d", what, (int)(p_ - start_));
        error_ = b;
        return false;
    }
    void ws() { while (p_ < end_ && (*p_ == ' ' || *p_ == '\t' || *p_ == '\n' || *p_ == '\r')) ++p_; }
    bool lit(const char *word)
    {
        size_t n = strlen(word);
        if ((size_t)(end_ - p_) < n || memcmp(p_, word, n)) return false;
        p_ += n;
        return true;
    }
    static void utf8(std::string &out, unsigned cp)
    {
        if (cp < 0x80) out += (char)cp;
        else if (cp < 0x800) { out += (char)(0xC0 | (cp >> 6)); out += (char)(0x80 | (cp & 0x3F)); }
        else if (cp < 0x10000) { out += (char)(0xE0 | (cp >> 12)); out += (char)(0x80 | ((cp >> 6) & 0x3F)); out += (char)(0x80 | (cp & 0x3F)); }
        else { out += (char)(0xF0 | (cp >> 18)); out += (char)(0x80 | ((cp >> 12) & 0x3F)); out += (char)(0x80 | ((cp >> 6) & 0x3F)); out += (char)(0x80 | (cp & 0x3F)); }
    }
    bool hex4(unsigned &v)
    {
        if (end_ - p_ < 4) return false;
        v = 0;
        for (int i = 0; i < 4; ++i) {
            char c = *p_++;
            v <<= 4;
            if (c >= '0' && c <= '9') v |= (unsigned)(c - '0');
            else if (c >= 'a' && c <= 'f') v |= (unsigned)(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') v |= (unsigned)(c - 'A' + 10);
            else return false;
        }
        return true;
    }
    bool string(std::string &out)
    {
        ++p_;   // opening quote
        while (p_ < end_ && *p_ != '"') {
            unsigned char c = (unsigned char)*p_;
            if (c < 0x20) return fail("control character in a string");
            if (c != '\\') {
                // Copy one UTF-8 sequence, checking its shape (no overlongs or surrogates checked beyond the lead).
                int n = c < 0x80 ? 1 : (c & 0xE0) == 0xC0 && c >= 0xC2 ? 2 : (c & 0xF0) == 0xE0 ? 3 : (c & 0xF8) == 0xF0 && c <= 0xF4 ? 4 : 0;
                if (!n || end_ - p_ < n) return fail("invalid UTF-8");
                for (int i = 1; i < n; ++i) if (((unsigned char)p_[i] & 0xC0) != 0x80) return fail("invalid UTF-8");
                out.append(p_, (size_t)n);
                p_ += n;
                continue;
            }
            if (++p_ == end_) break;
            char e = *p_++;
            switch (e) {
            case '"': out += '"'; break;
            case '\\': out += '\\'; break;
            case '/': out += '/'; break;
            case 'b': out += '\b'; break;
            case 'f': out += '\f'; break;
            case 'n': out += '\n'; break;
            case 'r': out += '\r'; break;
            case 't': out += '\t'; break;
            case 'u': {
                unsigned cp;
                if (!hex4(cp)) return fail("bad \\u escape");
                if (cp >= 0xD800 && cp < 0xDC00) {
                    unsigned lo;
                    if (end_ - p_ < 2 || p_[0] != '\\' || p_[1] != 'u') return fail("unpaired surrogate");
                    p_ += 2;
                    if (!hex4(lo) || lo < 0xDC00 || lo > 0xDFFF) return fail("unpaired surrogate");
                    cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                } else if (cp >= 0xDC00 && cp < 0xE000) return fail("unpaired surrogate");
                utf8(out, cp);
                break;
            }
            default: return fail("bad escape");
            }
        }
        if (p_ == end_) return fail("unterminated string");
        ++p_;
        return true;
    }
    bool number(double &out)
    {
        const char *s = p_;
        if (p_ < end_ && *p_ == '-') ++p_;
        if (p_ == end_ || *p_ < '0' || *p_ > '9') return fail("bad number");
        if (*p_ == '0') ++p_; else while (p_ < end_ && *p_ >= '0' && *p_ <= '9') ++p_;
        if (p_ < end_ && *p_ == '.') {
            ++p_;
            if (p_ == end_ || *p_ < '0' || *p_ > '9') return fail("bad number");
            while (p_ < end_ && *p_ >= '0' && *p_ <= '9') ++p_;
        }
        if (p_ < end_ && (*p_ == 'e' || *p_ == 'E')) {
            ++p_;
            if (p_ < end_ && (*p_ == '+' || *p_ == '-')) ++p_;
            if (p_ == end_ || *p_ < '0' || *p_ > '9') return fail("bad number");
            while (p_ < end_ && *p_ >= '0' && *p_ <= '9') ++p_;
        }
        std::string t(s, (size_t)(p_ - s));
        out = strtod(t.c_str(), nullptr);
        return isfinite(out) || fail("number out of range");
    }
    bool value(Json &out, int depth)
    {
        if (depth > 16) return fail("nested too deeply");
        ws();
        if (p_ == end_) return fail("unexpected end");
        char c = *p_;
        if (c == '{') {
            out.type = Json::Object;
            ++p_; ws();
            if (p_ < end_ && *p_ == '}') { ++p_; return true; }
            for (;;) {
                ws();
                if (p_ == end_ || *p_ != '"') return fail("expected a member name");
                std::string key;
                if (!string(key)) return false;
                for (const auto &kv : out.o) if (kv.first == key) return fail(("duplicate member '" + key + "'").c_str());
                ws();
                if (p_ == end_ || *p_ != ':') return fail("expected ':'");
                ++p_;
                out.o.emplace_back(key, Json());
                if (!value(out.o.back().second, depth + 1)) return false;
                ws();
                if (p_ < end_ && *p_ == ',') { ++p_; continue; }
                if (p_ < end_ && *p_ == '}') { ++p_; return true; }
                return fail("expected ',' or '}'");
            }
        }
        if (c == '[') {
            out.type = Json::Array;
            ++p_; ws();
            if (p_ < end_ && *p_ == ']') { ++p_; return true; }
            for (;;) {
                out.a.emplace_back();
                if (!value(out.a.back(), depth + 1)) return false;
                ws();
                if (p_ < end_ && *p_ == ',') { ++p_; continue; }
                if (p_ < end_ && *p_ == ']') { ++p_; return true; }
                return fail("expected ',' or ']'");
            }
        }
        if (c == '"') { out.type = Json::String; return string(out.s); }
        if (lit("true")) { out.type = Json::Bool; out.b = true; return true; }
        if (lit("false")) { out.type = Json::Bool; out.b = false; return true; }
        if (lit("null")) { out.type = Json::Null; return true; }
        out.type = Json::Number;
        return number(out.n);
    }
};

// ---- validation -----------------------------------------------------------------------------------------------
struct Checker {
    std::string &error;
    bool fail(const std::string &what) { error = what; return false; }

    bool str(const Json &row, const char *key, std::string &out, const std::string &where, bool required)
    {
        const Json *v = row.get(key);
        if (!v) return !required || fail(where + ": missing '" + key + "'");
        if (v->type != Json::String) return fail(where + ": '" + key + "' must be a string");
        out = v->s;
        return true;
    }
    bool num(const Json &row, const char *key, double &out, const std::string &where)
    {
        const Json *v = row.get(key);
        if (!v) return true;
        if (v->type != Json::Number) return fail(where + ": '" + key + "' must be a number");
        out = v->n;
        return true;
    }
    static int words(const std::string &s)
    {
        int n = 0;
        bool in = false;
        for (char c : s) {
            const bool space = c == ' ' || c == '\t' || c == '\n';
            if (!space && !in) ++n;
            in = !space;
        }
        return n;
    }
    static bool idOk(const std::string &id)
    {
        if (id.empty() || id.size() > 63) return false;
        for (char c : id)
            if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-')) return false;
        return true;
    }

    bool row(const Json &j, StdPage page, Row &r)
    {
        std::string where = std::string("page '") + StdPageId(page) + "' row";
        if (j.type != Json::Object) return fail(where + ": must be an object");
        static const char *const kFields[] = {"id", "kind", "label", "help", "level", "apply", "choices", "min", "max",
                                              "default", "unit", "decimals", "text", "role", "store"};
        if (!str(j, "id", r.id, where, true)) return false;
        if (!idOk(r.id)) return fail(where + ": bad id '" + r.id + "'");
        where = "row '" + r.id + "'";
        for (const auto &kv : j.o) {
            bool known = false;
            for (const char *f : kFields) known = known || kv.first == f;
            if (!known) return fail(where + ": unknown field '" + kv.first + "'");
        }
        r.page = page;
        std::string kind, level = "simple", apply = "live";
        if (!str(j, "kind", kind, where, true)) return false;
        static const char *const kKinds[] = {"toggle", "choice", "percent", "slider", "number", "binding", "action", "status"};
        int k = -1;
        for (int i = 0; i < 8; ++i) if (kind == kKinds[i]) k = i;
        if (k < 0) return fail(where + ": unknown kind '" + kind + "'");
        r.kind = (RowKind)k;
        if (!str(j, "label", r.label, where, r.kind != RowKind::Status)) return false;
        if (r.kind != RowKind::Status && r.label.empty()) return fail(where + ": empty label");
        if (!str(j, "help", r.help, where, false) || !str(j, "unit", r.unit, where, false) || !str(j, "text", r.text, where, false)) return false;
        if (words(r.help) > 12) return fail(where + ": help is over 12 words");
        if (!str(j, "role", r.role, where, false)) return false;
        if (const Json *st = j.get("store")) {
            if (st->type != Json::Object) return fail(where + ": 'store' must be an object");
            for (const auto &kv : st->o)
                if (kv.first != "file" && kv.first != "section" && kv.first != "key" && kv.first != "on" && kv.first != "off" && kv.first != "scale")
                    return fail(where + ": unknown store field '" + kv.first + "'");
            r.hasStore = true;
            if (!str(*st, "file", r.storeFile, where, false) || !str(*st, "section", r.storeSection, where, false) ||
                !str(*st, "key", r.storeKey, where, true) || !str(*st, "on", r.storeOn, where, false) ||
                !str(*st, "off", r.storeOff, where, false) || !num(*st, "scale", r.storeScale, where)) return false;
            if (!(r.storeScale > 0)) return fail(where + ": store scale must be above 0");
        }
        if (!str(j, "level", level, where, false) || !str(j, "apply", apply, where, false)) return false;
        if (level == "simple") r.level = RowLevel::Simple;
        else if (level == "advanced") r.level = RowLevel::Advanced;
        else if (level == "developer") r.level = RowLevel::Developer;
        else return fail(where + ": unknown level '" + level + "'");
        if (apply == "live") r.nextStart = false;
        else if (apply == "next-start") r.nextStart = true;
        else return fail(where + ": unknown apply '" + apply + "'");
        if (r.nextStart && (r.kind == RowKind::Action || r.kind == RowKind::Status)) return fail(where + ": an action or status cannot be next-start");

        const Json *def = j.get("default");
        const bool numeric = r.kind == RowKind::Percent || r.kind == RowKind::Slider || r.kind == RowKind::Number;
        if (j.get("choices") && r.kind != RowKind::Choice) return fail(where + ": only a choice has choices");
        if ((j.get("min") || j.get("max")) && !numeric) return fail(where + ": only percent, slider and number have a range");
        if (j.get("decimals") && r.kind != RowKind::Number) return fail(where + ": only a number has decimals");
        if (j.get("unit") && r.kind != RowKind::Slider && r.kind != RowKind::Number) return fail(where + ": only a slider or number has a unit");
        if (j.get("text") && r.kind != RowKind::Status && r.kind != RowKind::Binding) return fail(where + ": only a status or binding has text");
        if (def && (r.kind == RowKind::Status || r.kind == RowKind::Action || r.kind == RowKind::Binding))
            return fail(where + ": a " + kind + " has no default");
        switch (r.kind) {
        case RowKind::Toggle:
            if (def && def->type != Json::Bool) return fail(where + ": a toggle's default is true or false");
            r.defaultValue = def && def->b ? 1 : 0;
            break;
        case RowKind::Choice: {
            const Json *c = j.get("choices");
            if (!c || c->type != Json::Array || c->a.size() < 2 || c->a.size() > 8) return fail(where + ": a choice needs 2-8 choices");
            for (const Json &e : c->a) {
                if (e.type != Json::String || e.s.empty()) return fail(where + ": choices are non-empty strings");
                for (const std::string &seen : r.choices) if (seen == e.s) return fail(where + ": duplicate choice '" + e.s + "'");
                r.choices.push_back(e.s);
            }
            if (def) {
                int index = -1;
                if (def->type == Json::String) { for (size_t i = 0; i < r.choices.size(); ++i) if (r.choices[i] == def->s) index = (int)i; }
                else if (def->type == Json::Number && def->n == floor(def->n)) index = (int)def->n;
                if (index < 0 || index >= (int)r.choices.size()) return fail(where + ": default is not one of the choices");
                r.defaultValue = index;
            }
            break;
        }
        case RowKind::Percent: case RowKind::Slider: case RowKind::Number: {
            if (r.kind == RowKind::Percent) { r.minValue = 0; r.maxValue = 100; }
            else if (!j.get("min") || !j.get("max")) return fail(where + ": a " + kind + " needs min and max");
            if (!num(j, "min", r.minValue, where) || !num(j, "max", r.maxValue, where)) return false;
            if (!(r.minValue < r.maxValue)) return fail(where + ": min must be below max");
            r.defaultValue = r.minValue;
            if (def && def->type != Json::Number) return fail(where + ": default must be a number");
            if (def) r.defaultValue = def->n;
            if (r.defaultValue < r.minValue || r.defaultValue > r.maxValue) return fail(where + ": default is outside min-max");
            if (r.kind != RowKind::Number) {
                if (r.minValue != floor(r.minValue) || r.maxValue != floor(r.maxValue) || r.defaultValue != floor(r.defaultValue))
                    return fail(where + ": a " + kind + " uses whole numbers");
            }
            double d = 0;
            if (!num(j, "decimals", d, where)) return false;
            if (d < 0 || d > 3 || d != floor(d)) return fail(where + ": decimals is 0-3");
            r.decimals = (int)d;
            break;
        }
        default: break;
        }
        return true;
    }
};

} // namespace

bool ParseDescriptor(const char *json, size_t length, Descriptor &out, std::string &error)
{
    out = Descriptor();
    error.clear();
    Json root;
    Parser parser(json ? json : "", json ? length : 0, error);
    if (!parser.document(root)) return false;
    Checker ck{error};
    const Json *s = &root;
    if (root.type == Json::Object && root.get("settings")) s = root.get("settings");
    if (s->type != Json::Object) return ck.fail("the settings block must be an object");
    for (const auto &kv : s->o) {
        static const char *const kTop[] = {"product", "version", "modPage", "keys", "config", "format", "pages"};
        bool known = false;
        for (const char *f : kTop) known = known || kv.first == f;
        if (!known) return ck.fail("settings: unknown field '" + kv.first + "'");
    }
    Descriptor d;
    if (!ck.str(*s, "product", d.product, "settings", true) || !ck.str(*s, "version", d.version, "settings", false) ||
        !ck.str(*s, "modPage", d.modPage, "settings", false) || !ck.str(*s, "keys", d.keys, "settings", false) ||
        !ck.str(*s, "config", d.config, "settings", false)) return false;
    if (s->get("format")) {
        if (!ck.str(*s, "format", d.format, "settings", true)) return false;
        if (d.format != "ini" && d.format != "xml") return ck.fail("settings: format is ini or xml");
    }
    if (d.product.empty()) return ck.fail("settings: empty product");
    const Json *pages = s->get("pages");
    if (!pages || pages->type != Json::Object) return ck.fail("settings: 'pages' must be an object");
    for (const auto &kv : pages->o) {
        int page = -1;
        for (int p = 0; p < (int)StdPage::Developer; ++p) if (kv.first == StdPageId((StdPage)p)) page = p;
        if (page < 0) return ck.fail("settings: unknown page '" + kv.first + "'");
        if (kv.second.type != Json::Array) return ck.fail("page '" + kv.first + "' must be an array of rows");
        int controls = 0, statuses = 0;
        for (const Json &j : kv.second.a) {
            Row r;
            if (!ck.row(j, (StdPage)page, r)) return false;
            for (const Row &seen : d.rows) if (seen.id == r.id) return ck.fail("row '" + r.id + "': duplicate id");
            if (r.level == RowLevel::Simple) (RowIsControl(r.kind) ? controls : statuses)++;
            d.rows.push_back(std::move(r));
        }
        if (controls > 5) return ck.fail("page '" + kv.first + "': more than five simple controls");
        if (statuses > 2) return ck.fail("page '" + kv.first + "': more than two simple status lines");
    }
    for (Row &r : d.rows) if (r.hasStore && r.storeFile.empty()) r.storeFile = d.config;
    out = std::move(d);
    return true;
}

bool LoadDescriptor(const wchar_t *path, Descriptor &out, std::string &error)
{
    out = Descriptor();
    HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) { error = "descriptor: cannot open the file"; return false; }
    LARGE_INTEGER size;
    std::string text;
    bool ok = GetFileSizeEx(f, &size) && size.QuadPart < (1 << 20);
    if (ok) {
        text.resize((size_t)size.QuadPart);
        DWORD got = 0;
        ok = text.empty() || (ReadFile(f, &text[0], (DWORD)text.size(), &got, nullptr) && got == text.size());
    }
    CloseHandle(f);
    if (!ok) { error = "descriptor: cannot read the file (or it is over 1 MB)"; return false; }
    return ParseDescriptor(text.data(), text.size(), out, error);
}

// ---- the whole dbce-mod.json (dbce_mod_file.h) ----------------------------------------------------------------
namespace {

std::string S(const Json *o, const char *key)
{
    const Json *v = o ? o->get(key) : nullptr;
    return v && v->type == Json::String ? v->s : std::string();
}
double N(const Json *o, const char *key, double fallback)
{
    const Json *v = o ? o->get(key) : nullptr;
    return v && v->type == Json::Number ? v->n : fallback;
}
bool B(const Json *o, const char *key)
{
    const Json *v = o ? o->get(key) : nullptr;
    return v && v->type == Json::Bool && v->b;
}
const Json *Obj(const Json *o, const char *key)
{
    const Json *v = o ? o->get(key) : nullptr;
    return v && v->type == Json::Object ? v : nullptr;
}
const std::vector<Json> &Arr(const Json *o, const char *key)
{
    static const std::vector<Json> empty;
    const Json *v = o ? o->get(key) : nullptr;
    return v && v->type == Json::Array ? v->a : empty;
}
std::vector<std::string> Strs(const Json *o, const char *key)
{
    std::vector<std::string> out;
    for (const Json &e : Arr(o, key)) out.push_back(e.s);
    return out;
}
std::vector<ModFromTo> FromTos(const Json *o, const char *key)
{
    std::vector<ModFromTo> out;
    for (const Json &e : Arr(o, key)) out.push_back({S(&e, "from"), S(&e, "to")});
    return out;
}

struct Dumper {
    std::string out;
    void s(const std::string &p, const std::string &v)
    {
        out += p; out += '=';
        for (char c : v) {
            if (c == '\\') out += "\\\\";
            else if (c == '\r') out += "\\r";
            else if (c == '\n') out += "\\n";
            else out += c;
        }
        out += '\n';
    }
    void b(const std::string &p, bool v) { out += p + (v ? "=true\n" : "=false\n"); }
    void n(const std::string &p, double v)
    {
        char buf[64];
        auto r = std::to_chars(buf, buf + sizeof buf, v);
        out += p + "=" + std::string(buf, r.ptr) + "\n";
    }
    void null(const std::string &p) { out += p + "=null\n"; }
    void list(const std::string &p, const std::vector<std::string> &v)
    {
        n(p + ".count", (double)v.size());
        for (size_t k = 0; k < v.size(); ++k) s(p + "[" + std::to_string(k) + "]", v[k]);
    }
    void fromTo(const std::string &p, const std::vector<ModFromTo> &v)
    {
        n(p + ".count", (double)v.size());
        for (size_t k = 0; k < v.size(); ++k) { s(p + "[" + std::to_string(k) + "].from", v[k].from); s(p + "[" + std::to_string(k) + "].to", v[k].to); }
    }
};

} // namespace

bool ParseModFile(const char *json, size_t length, ModFile &out, std::string &error)
{
    out = ModFile();
    error.clear();
    Json root;
    Parser parser(json ? json : "", json ? length : 0, error);
    if (!parser.document(root)) return false;
    if (root.type != Json::Object) { error = "dbce-mod.json must be an object"; return false; }
    ModFile m;
    m.schema = S(&root, "schema");
    if (m.schema != "dbce.mod@1") { error = "not a dbce-mod.json (schema dbce.mod@1)"; return false; }
    m.id = S(&root, "id"); m.name = S(&root, "name"); m.version = S(&root, "version");
    if (m.id.empty() || m.name.empty() || m.version.empty()) { error = "dbce-mod.json needs id, name and version"; return false; }
    m.steamAppId = N(&root, "steamAppId", 0); m.folder = S(&root, "folder"); m.exe = Strs(&root, "exe"); m.repository = S(&root, "repository");
    const Json *log = Obj(&root, "log");
    m.logFile = S(log, "file"); m.logStart = S(log, "start");

    const Json *pk = Obj(&root, "package");
    m.kind = S(pk, "kind");
    if (m.kind.empty()) { error = "dbce-mod.json needs a package kind"; return false; }
    if (const Json *loader = Obj(pk, "loader")) {
        m.hasLoader = true;
        m.loaderName = S(loader, "name"); m.loaderVersion = S(loader, "version"); m.loaderBundle = S(loader, "bundle");
    }
    m.payload = FromTos(pk, "payload"); m.optional = FromTos(pk, "optional"); m.conflicts = Strs(pk, "conflicts"); m.seedConfig = FromTos(pk, "seedConfig");
    for (const Json &b : Arr(pk, "knownBuilds")) m.knownBuilds.push_back({S(&b, "file"), S(&b, "sha256"), S(&b, "label")});
    m.strict = B(pk, "strict");
    if (!S(pk, "uiHost").empty()) m.uiHost = S(pk, "uiHost");

    if (Obj(&root, "settings")) {
        m.hasSettings = true;
        if (!ParseDescriptor(json, length, m.settings, error)) return false;
    }
    if (const Json *c = Obj(&root, "controls")) {
        m.hasControls = true;
        m.controlsStore = S(c, "store"); m.controlsProfile = S(c, "profile"); m.controlsSettings = S(c, "settings");
        if (const Json *oi = Obj(c, "optIn")) {
            m.hasOptIn = true;
            m.optInSection = S(oi, "section"); m.optInKey = S(oi, "key"); m.optInValue = S(oi, "value"); m.optInDefault = S(oi, "default");
            m.optInAppliesAfterRestart = B(oi, "appliesAfterRestart");
        }
        m.delivered = Strs(c, "delivered");
        if (const Json *un = Obj(c, "unsupported")) for (const auto &kv : un->o) m.unsupported.emplace_back(kv.first, kv.second.s);
        for (const Json &x : Arr(c, "conditions")) {
            ModCondition k;
            k.actions = Strs(&x, "actions"); k.transmission = S(&x, "transmission"); k.settingsKey = S(&x, "settingsKey");
            k.settingsValues = Strs(&x, "settingsValues"); k.settingsDefault = S(&x, "settingsDefault"); k.note = S(&x, "note");
            m.conditions.push_back(k);
        }
        for (const Json &g : Arr(c, "gameControls")) m.gameControls.push_back({S(&g, "id"), S(&g, "kind"), S(&g, "label"), S(&g, "group")});
    }
    for (const Json &x : Arr(&root, "cameras"))
        m.cameras.push_back({S(&x, "camera"), S(&x, "start"), S(&x, "file"), S(&x, "section"), S(&x, "key"), S(&x, "realistic"), S(&x, "game")});
    if (const Json *t = Obj(&root, "selfTest")) {
        m.hasSelfTest = true;
        m.selfTestFolder = S(t, "folder"); m.selfTestSteps = Strs(t, "steps");
        for (const Json &p : Arr(t, "probes")) m.probes.push_back({S(&p, "id"), S(&p, "type"), S(&p, "label")});
    }
    if (const Json *tt = Obj(&root, "test")) {
        m.hasTest = true;
        if (const Json *la = Obj(tt, "launch")) {
            m.hasLaunch = true;
            m.launchSteamAppId = (long)N(la, "steamAppId", 0); m.launchExe = S(la, "exe"); m.launchArgs = Strs(la, "args");
        }
        m.protect = Strs(tt, "protect");
        if (const Json *ts = Obj(tt, "testSave")) { m.hasTestSave = true; m.testSaveFrom = S(ts, "from"); m.testSaveTo = S(ts, "to"); }
        for (const Json &s : Arr(tt, "states")) m.states.push_back({S(&s, "state"), S(&s, "field"), S(&s, "is"), S(&s, "text"), S(&s, "picture")});
        if (const Json *en = Obj(tt, "entry")) { m.hasEntry = true; m.entryCommand = S(en, "command"); m.entryRoute = S(en, "route"); }
        for (const Json &r : Arr(tt, "route")) {
            ModRouteStep step;
            step.key = S(&r, "key"); step.input = S(&r, "input"); step.waitState = S(&r, "waitState"); step.waitText = S(&r, "waitText");
            step.timeout = (long)N(&r, "timeout", 0);
            m.route.push_back(step);
        }
        if (const Json *qu = Obj(tt, "quit")) { m.hasQuit = true; m.quitCommand = S(qu, "command"); m.quitKey = S(qu, "key"); m.quitInput = S(qu, "input"); }
    }
    if (const Json *i = Obj(&root, "installed")) {
        m.hasInstalled = true;
        m.installedVersion = S(i, "version"); m.commit = S(i, "commit"); m.installedUtc = S(i, "installedUtc");
        m.loaderInstalledByUs = B(i, "loaderInstalledByUs"); m.backups = S(i, "backups");
        for (const Json &f : Arr(i, "files")) m.files.push_back({S(&f, "path"), S(&f, "sha256")});
        if (const Json *fl = Obj(i, "flags")) for (const auto &kv : fl->o) m.flags.emplace_back(kv.first, kv.second.type == Json::Bool && kv.second.b);
    }
    out = std::move(m);
    return true;
}

bool LoadModFile(const wchar_t *path, ModFile &out, std::string &error)
{
    out = ModFile();
    HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) { error = "dbce-mod.json: cannot open the file"; return false; }
    LARGE_INTEGER size;
    std::string text;
    bool ok = GetFileSizeEx(f, &size) && size.QuadPart < (1 << 20);
    if (ok) {
        text.resize((size_t)size.QuadPart);
        DWORD got = 0;
        ok = text.empty() || (ReadFile(f, &text[0], (DWORD)text.size(), &got, nullptr) && got == text.size());
    }
    CloseHandle(f);
    if (!ok) { error = "dbce-mod.json: cannot read the file (or it is over 1 MB)"; return false; }
    return ParseModFile(text.data(), text.size(), out, error);
}

std::string DumpModFile(const ModFile &m)
{
    static const char *const kKinds[] = {"toggle", "choice", "percent", "slider", "number", "binding", "action", "status"};
    static const char *const kLevels[] = {"simple", "advanced", "developer"};
    Dumper w;
    w.s("schema", m.schema); w.s("id", m.id); w.s("name", m.name); w.s("version", m.version);
    w.n("steamAppId", m.steamAppId); w.s("folder", m.folder); w.list("exe", m.exe); w.s("repository", m.repository);
    w.s("log.file", m.logFile); w.s("log.start", m.logStart);
    w.s("package.kind", m.kind);
    if (!m.hasLoader) w.null("package.loader");
    else { w.s("package.loader.name", m.loaderName); w.s("package.loader.version", m.loaderVersion); w.s("package.loader.bundle", m.loaderBundle); }
    w.fromTo("package.payload", m.payload); w.fromTo("package.optional", m.optional); w.list("package.conflicts", m.conflicts);
    w.fromTo("package.seedConfig", m.seedConfig);
    w.n("package.knownBuilds.count", (double)m.knownBuilds.size());
    for (size_t k = 0; k < m.knownBuilds.size(); ++k) {
        const std::string q = "package.knownBuilds[" + std::to_string(k) + "]";
        w.s(q + ".file", m.knownBuilds[k].file); w.s(q + ".sha256", m.knownBuilds[k].sha256); w.s(q + ".label", m.knownBuilds[k].label);
    }
    w.b("package.strict", m.strict); w.s("package.uiHost", m.uiHost);
    if (!m.hasSettings) w.null("settings");
    else {
        const Descriptor &d = m.settings;
        w.s("settings.product", d.product); w.s("settings.version", d.version); w.s("settings.modPage", d.modPage); w.s("settings.keys", d.keys);
        w.s("settings.config", d.config); w.s("settings.format", d.format);
        w.n("settings.rows.count", (double)d.rows.size());
        for (size_t k = 0; k < d.rows.size(); ++k) {
            const Row &r = d.rows[k];
            const std::string q = "settings.rows[" + std::to_string(k) + "]";
            w.s(q + ".page", StdPageId(r.page)); w.s(q + ".id", r.id); w.s(q + ".kind", kKinds[(int)r.kind]); w.s(q + ".label", r.label);
            w.s(q + ".help", r.help); w.s(q + ".level", kLevels[(int)r.level]); w.s(q + ".apply", r.nextStart ? "next-start" : "live");
            w.list(q + ".choices", r.choices);
            w.n(q + ".min", r.minValue); w.n(q + ".max", r.maxValue); w.n(q + ".default", r.defaultValue); w.s(q + ".unit", r.unit);
            w.n(q + ".decimals", r.decimals); w.s(q + ".text", r.text); w.s(q + ".role", r.role);
            if (!r.hasStore) w.null(q + ".store");
            else {
                w.s(q + ".store.file", r.storeFile); w.s(q + ".store.section", r.storeSection); w.s(q + ".store.key", r.storeKey);
                w.s(q + ".store.on", r.storeOn); w.s(q + ".store.off", r.storeOff); w.n(q + ".store.scale", r.storeScale);
            }
        }
    }
    if (!m.hasControls) w.null("controls");
    else {
        w.s("controls.store", m.controlsStore); w.s("controls.profile", m.controlsProfile); w.s("controls.settings", m.controlsSettings);
        if (!m.hasOptIn) w.null("controls.optIn");
        else {
            w.s("controls.optIn.section", m.optInSection); w.s("controls.optIn.key", m.optInKey); w.s("controls.optIn.value", m.optInValue);
            w.s("controls.optIn.default", m.optInDefault); w.b("controls.optIn.appliesAfterRestart", m.optInAppliesAfterRestart);
        }
        w.list("controls.delivered", m.delivered);
        w.n("controls.unsupported.count", (double)m.unsupported.size());
        for (size_t k = 0; k < m.unsupported.size(); ++k) {
            const std::string q = "controls.unsupported[" + std::to_string(k) + "]";
            w.s(q + ".action", m.unsupported[k].first); w.s(q + ".reason", m.unsupported[k].second);
        }
        w.n("controls.conditions.count", (double)m.conditions.size());
        for (size_t k = 0; k < m.conditions.size(); ++k) {
            const ModCondition &x = m.conditions[k];
            const std::string q = "controls.conditions[" + std::to_string(k) + "]";
            w.list(q + ".actions", x.actions); w.s(q + ".transmission", x.transmission); w.s(q + ".settingsKey", x.settingsKey);
            w.list(q + ".settingsValues", x.settingsValues); w.s(q + ".settingsDefault", x.settingsDefault); w.s(q + ".note", x.note);
        }
        w.n("controls.gameControls.count", (double)m.gameControls.size());
        for (size_t k = 0; k < m.gameControls.size(); ++k) {
            const std::string q = "controls.gameControls[" + std::to_string(k) + "]";
            w.s(q + ".id", m.gameControls[k].id); w.s(q + ".kind", m.gameControls[k].kind); w.s(q + ".label", m.gameControls[k].label);
            w.s(q + ".group", m.gameControls[k].group);
        }
    }
    w.n("cameras.count", (double)m.cameras.size());
    for (size_t k = 0; k < m.cameras.size(); ++k) {
        const ModCameraStart &c = m.cameras[k];
        const std::string q = "cameras[" + std::to_string(k) + "]";
        w.s(q + ".camera", c.camera); w.s(q + ".start", c.start); w.s(q + ".file", c.file); w.s(q + ".section", c.section);
        w.s(q + ".key", c.key); w.s(q + ".realistic", c.realistic); w.s(q + ".game", c.game);
    }
    if (!m.hasSelfTest) w.null("selfTest");
    else {
        w.s("selfTest.folder", m.selfTestFolder); w.list("selfTest.steps", m.selfTestSteps);
        w.n("selfTest.probes.count", (double)m.probes.size());
        for (size_t k = 0; k < m.probes.size(); ++k) {
            const std::string q = "selfTest.probes[" + std::to_string(k) + "]";
            w.s(q + ".id", m.probes[k].id); w.s(q + ".type", m.probes[k].type); w.s(q + ".label", m.probes[k].label);
        }
    }
    if (!m.hasTest) w.null("test");
    else {
        if (!m.hasLaunch) w.null("test.launch");
        else { w.n("test.launch.steamAppId", (double)m.launchSteamAppId); w.s("test.launch.exe", m.launchExe); w.list("test.launch.args", m.launchArgs); }
        w.list("test.protect", m.protect);
        if (!m.hasTestSave) w.null("test.testSave");
        else { w.s("test.testSave.from", m.testSaveFrom); w.s("test.testSave.to", m.testSaveTo); }
        w.n("test.states.count", (double)m.states.size());
        for (size_t k = 0; k < m.states.size(); ++k) {
            const std::string q = "test.states[" + std::to_string(k) + "]";
            w.s(q + ".state", m.states[k].state); w.s(q + ".field", m.states[k].field); w.s(q + ".is", m.states[k].is);
            w.s(q + ".text", m.states[k].text); w.s(q + ".picture", m.states[k].picture);
        }
        if (!m.hasEntry) w.null("test.entry");
        else { w.s("test.entry.command", m.entryCommand); w.s("test.entry.route", m.entryRoute); }
        w.n("test.route.count", (double)m.route.size());
        for (size_t k = 0; k < m.route.size(); ++k) {
            const std::string q = "test.route[" + std::to_string(k) + "]";
            w.s(q + ".key", m.route[k].key); w.s(q + ".input", m.route[k].input);
            w.s(q + ".waitState", m.route[k].waitState); w.s(q + ".waitText", m.route[k].waitText);
            w.n(q + ".timeout", (double)m.route[k].timeout);
        }
        if (!m.hasQuit) w.null("test.quit");
        else { w.s("test.quit.command", m.quitCommand); w.s("test.quit.key", m.quitKey); w.s("test.quit.input", m.quitInput); }
    }
    if (!m.hasInstalled) w.null("installed");
    else {
        w.s("installed.version", m.installedVersion); w.s("installed.commit", m.commit); w.s("installed.installedUtc", m.installedUtc);
        w.b("installed.loaderInstalledByUs", m.loaderInstalledByUs);
        w.n("installed.files.count", (double)m.files.size());
        for (size_t k = 0; k < m.files.size(); ++k) {
            const std::string q = "installed.files[" + std::to_string(k) + "]";
            w.s(q + ".path", m.files[k].from); w.s(q + ".sha256", m.files[k].to);
        }
        w.s("installed.backups", m.backups);
        w.n("installed.flags.count", (double)m.flags.size());
        for (size_t k = 0; k < m.flags.size(); ++k) {
            const std::string q = "installed.flags[" + std::to_string(k) + "]";
            w.s(q + ".name", m.flags[k].first); w.b(q + ".value", m.flags[k].second);
        }
    }
    return w.out;
}

// ---- the shared store (dbce_store.h, docs/STORE.md) -----------------------------------------------------------
namespace {

bool ReadSmallFile(const wchar_t *path, const char *what, std::string &text, std::string &error)
{
    HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) { error = std::string(what) + ": cannot open the file"; return false; }
    LARGE_INTEGER size;
    bool ok = GetFileSizeEx(f, &size) && size.QuadPart < (4 << 20);
    if (ok) {
        text.resize((size_t)size.QuadPart);
        DWORD got = 0;
        ok = text.empty() || (ReadFile(f, &text[0], (DWORD)text.size(), &got, nullptr) && got == text.size());
    }
    CloseHandle(f);
    if (!ok) error = std::string(what) + ": cannot read the file (or it is over 4 MB)";
    return ok;
}

bool ParseRoot(const char *json, size_t length, Json &root, const char *what, std::string &error)
{
    Parser parser(json ? json : "", json ? length : 0, error);
    if (!parser.document(root)) return false;
    if (root.type != Json::Object) { error = std::string(what) + " must be an object"; return false; }
    return true;
}

bool Screens(const Json *s, StoreScreens &out)
{
    out = StoreScreens();
    if (!s || s->type != Json::Object) return false;
    out.count = (int)N(s, "Count", out.count); out.widthPixels = (int)N(s, "WidthPixels", out.widthPixels); out.heightPixels = (int)N(s, "HeightPixels", out.heightPixels);
    const Json *sep = s->get("SeparateMonitors");
    if (sep && sep->type == Json::Bool) out.separateMonitors = sep->b;
    out.diagonalInches = N(s, "DiagonalInches", out.diagonalInches); out.aspectWidth = N(s, "AspectWidth", out.aspectWidth); out.aspectHeight = N(s, "AspectHeight", out.aspectHeight);
    out.eyeDistanceMm = N(s, "EyeDistanceMm", out.eyeDistanceMm); out.sideAngleDegrees = N(s, "SideAngleDegrees", out.sideAngleDegrees);
    out.bezelGapMm = N(s, "BezelGapMm", out.bezelGapMm); out.curveRadiusMm = N(s, "CurveRadiusMm", out.curveRadiusMm);
    const Json *m = s->get("MeasuredWidthMm");
    out.hasMeasuredWidth = m && m->type == Json::Number;
    if (out.hasMeasuredWidth) out.measuredWidthMm = m->n;
    return true;
}

void DumpScreens(Dumper &w, const std::string &p, bool has, const StoreScreens &s)
{
    if (!has) { w.null(p); return; }
    w.n(p + ".count", s.count); w.n(p + ".widthPixels", s.widthPixels); w.n(p + ".heightPixels", s.heightPixels); w.b(p + ".separateMonitors", s.separateMonitors);
    w.n(p + ".diagonalInches", s.diagonalInches); w.n(p + ".aspectWidth", s.aspectWidth); w.n(p + ".aspectHeight", s.aspectHeight);
    w.n(p + ".eyeDistanceMm", s.eyeDistanceMm); w.n(p + ".sideAngleDegrees", s.sideAngleDegrees); w.n(p + ".bezelGapMm", s.bezelGapMm);
    w.n(p + ".curveRadiusMm", s.curveRadiusMm);
    if (s.hasMeasuredWidth) w.n(p + ".measuredWidthMm", s.measuredWidthMm); else w.null(p + ".measuredWidthMm");
}

const StoreProfile *FindProfile(const Store &store, const std::string &id)
{
    for (const StoreProfile &p : store.profiles) if (p.id == id) return &p;
    return nullptr;
}

std::wstring LocalAppData()
{
    wchar_t path[MAX_PATH];
    DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", path, MAX_PATH);
    return n && n < MAX_PATH ? std::wstring(path, n) : std::wstring();
}

} // namespace

bool ParseStore(const char *json, size_t length, Store &out, std::string &error)
{
    out = Store();
    error.clear();
    Json root;
    if (!ParseRoot(json, length, root, "profiles.json", error)) return false;
    Store st;
    st.schema = (int)N(&root, "Schema", 0);
    if (st.schema != 2) { error = "not a store profiles.json (Schema 2)"; return false; }
    st.activeId = S(&root, "ActiveId");
    st.hasCockpit = Screens(root.get("Cockpit"), st.cockpit);
    const Json *profiles = root.get("Profiles");
    if (!profiles || profiles->type != Json::Array || profiles->a.empty()) { error = "profiles.json needs at least one profile"; return false; }
    for (const Json &e : profiles->a) {
        const Json *c = Obj(&e, "Profile");
        if (S(&e, "Id").empty() || !c) { error = "each profile needs an Id and a Profile"; return false; }
        if (FindProfile(st, S(&e, "Id"))) { error = "duplicate profile Id '" + S(&e, "Id") + "'"; return false; }
        StoreProfile p;
        p.id = S(&e, "Id"); p.name = S(c, "Name"); p.wheel = S(c, "Wheel"); p.shifter = S(c, "Shifter");
        if (!S(c, "Transmission").empty()) p.transmission = S(c, "Transmission");
        if (const Json *bindings = Obj(c, "Bindings"))
            for (const auto &kv : bindings->o) {
                const Json &b = kv.second;
                if (b.type != Json::Object) continue;
                StoreBinding x;
                x.action = kv.first; x.instanceGuid = S(&b, "InstanceGuid"); x.productGuid = S(&b, "ProductGuid"); x.deviceName = S(&b, "DeviceName");
                x.kind = S(&b, "Kind"); x.index = (int)N(&b, "Index", 0); x.direction = (int)N(&b, "Direction", 0); x.rest = (int)N(&b, "Rest", 0);
                x.minimum = (int)N(&b, "Minimum", 0); x.maximum = (int)N(&b, "Maximum", 65535);
                x.calibrated = B(&b, "Calibrated"); x.inverted = B(&b, "Inverted");
                static const char *const kCal[3] = {"CalLow", "CalCentre", "CalHigh"};
                for (int i = 0; i < 3; ++i) {
                    const Json *v = b.get(kCal[i]);
                    x.hasCal[i] = v && v->type == Json::Number;
                    if (x.hasCal[i]) x.cal[i] = (int)v->n;
                }
                p.bindings.push_back(x);
            }
        if (const Json *rig = Obj(&e, "Rig")) {
            p.ffbStrength = (int)N(rig, "FfbStrength", 50);
            const Json *on = rig->get("FfbOn");
            if (on && on->type == Json::Bool) p.ffbOn = on->b;
            p.hasScreens = Screens(rig->get("Screens"), p.screens);
        }
        st.profiles.push_back(p);
    }
    if (const Json *games = Obj(&root, "Games"))
        for (const auto &kv : games->o)
            if (kv.second.type == Json::Object) st.games.emplace_back(kv.first, S(&kv.second, "Profile"));
    out = st;
    return true;
}

bool LoadStore(const wchar_t *path, Store &out, std::string &error)
{
    out = Store();
    std::string text;
    return ReadSmallFile(path, "profiles.json", text, error) && ParseStore(text.data(), text.size(), out, error);
}

StoreChoice StoreFor(const Store &store, const std::string &gameId)
{
    StoreChoice c;
    for (const auto &g : store.games) if (g.first == gameId) c.profile = FindProfile(store, g.second);
    if (!c.profile) c.profile = FindProfile(store, store.activeId);
    if (!c.profile && !store.profiles.empty()) c.profile = &store.profiles[0];
    if (c.profile && c.profile->hasScreens) { c.screens = &c.profile->screens; c.screensFrom = "profile"; }
    else if (store.hasCockpit) { c.screens = &store.cockpit; c.screensFrom = "cockpit"; }
    return c;
}

std::string DumpStore(const Store &st)
{
    Dumper w;
    w.n("schema", st.schema); w.s("activeId", st.activeId);
    DumpScreens(w, "cockpit", st.hasCockpit, st.cockpit);
    w.n("profiles.count", (double)st.profiles.size());
    for (size_t k = 0; k < st.profiles.size(); ++k) {
        const StoreProfile &p = st.profiles[k];
        const std::string q = "profiles[" + std::to_string(k) + "]";
        w.s(q + ".id", p.id); w.s(q + ".name", p.name); w.s(q + ".transmission", p.transmission); w.s(q + ".wheel", p.wheel); w.s(q + ".shifter", p.shifter);
        w.n(q + ".ffbStrength", p.ffbStrength); w.b(q + ".ffbOn", p.ffbOn);
        DumpScreens(w, q + ".screens", p.hasScreens, p.screens);
        w.n(q + ".bindings.count", (double)p.bindings.size());
        for (size_t i = 0; i < p.bindings.size(); ++i) {
            const StoreBinding &b = p.bindings[i];
            const std::string r = q + ".bindings[" + std::to_string(i) + "]";
            w.s(r + ".action", b.action); w.s(r + ".instanceGuid", b.instanceGuid); w.s(r + ".productGuid", b.productGuid); w.s(r + ".deviceName", b.deviceName);
            w.s(r + ".kind", b.kind); w.n(r + ".index", b.index); w.n(r + ".direction", b.direction); w.n(r + ".rest", b.rest);
            w.n(r + ".minimum", b.minimum); w.n(r + ".maximum", b.maximum); w.b(r + ".calibrated", b.calibrated); w.b(r + ".inverted", b.inverted);
            static const char *const kCal[3] = {".calLow", ".calCentre", ".calHigh"};
            for (int c = 0; c < 3; ++c) { if (b.hasCal[c]) w.n(r + kCal[c], b.cal[c]); else w.null(r + kCal[c]); }
        }
    }
    w.n("games.count", (double)st.games.size());
    for (size_t k = 0; k < st.games.size(); ++k) {
        w.s("games[" + std::to_string(k) + "].id", st.games[k].first); w.s("games[" + std::to_string(k) + "].profile", st.games[k].second);
    }
    std::vector<std::string> ids;
    for (const auto &g : st.games) ids.push_back(g.first);
    ids.push_back("unassigned-game");
    for (const std::string &id : ids) {
        const StoreChoice c = StoreFor(st, id);
        w.s("for[" + id + "].profile", c.profile ? c.profile->id : std::string()); w.s("for[" + id + "].screens", c.screensFrom);
    }
    return w.out;
}

bool ParseRuntimeSettings(const char *json, size_t length, RuntimeSettings &out, std::string &error)
{
    out = RuntimeSettings();
    error.clear();
    Json root;
    if (!ParseRoot(json, length, root, "runtime settings", error)) return false;
    RuntimeSettings r;
    r.schema = (int)N(&root, "Schema", 0);
    if (r.schema != 1) { error = "not a store runtime file (Schema 1)"; return false; }
    r.game = S(&root, "Game");
    if (r.game.empty()) { error = "a runtime file needs its Game"; return false; }
    r.profile = S(&root, "Profile");
    if (const Json *values = Obj(&root, "Values"))
        for (const auto &kv : values->o) {
            RuntimeValue v;
            v.key = kv.first;
            if (kv.second.type == Json::Number) { v.type = RuntimeValue::Number; v.number = kv.second.n; }
            else if (kv.second.type == Json::Bool) { v.type = RuntimeValue::Bool; v.flag = kv.second.b; }
            else if (kv.second.type == Json::String) { v.type = RuntimeValue::String; v.text = kv.second.s; }
            else continue;
            r.values.push_back(v);
        }
    out = r;
    return true;
}

bool LoadRuntimeSettings(const wchar_t *path, RuntimeSettings &out, std::string &error)
{
    out = RuntimeSettings();
    std::string text;
    return ReadSmallFile(path, "runtime settings", text, error) && ParseRuntimeSettings(text.data(), text.size(), out, error);
}

std::string DumpRuntimeSettings(const RuntimeSettings &r)
{
    Dumper w;
    w.n("schema", r.schema); w.s("game", r.game); w.s("profile", r.profile);
    w.n("values.count", (double)r.values.size());
    for (size_t k = 0; k < r.values.size(); ++k) {
        const RuntimeValue &v = r.values[k];
        const std::string q = "values[" + std::to_string(k) + "]";
        w.s(q + ".key", v.key);
        if (v.type == RuntimeValue::Number) w.n(q + ".number", v.number);
        else if (v.type == RuntimeValue::Bool) w.b(q + ".bool", v.flag);
        else w.s(q + ".string", v.text);
    }
    return w.out;
}

std::wstring StoreFolder()
{
    const std::wstring base = LocalAppData();
    return base.empty() ? base : base + L"\\Wheelkit\\store";
}
std::wstring StoreProfilesPath()
{
    const std::wstring dir = StoreFolder();
    return dir.empty() ? dir : dir + L"\\profiles.json";
}
std::wstring StoreRuntimePath(const wchar_t *gameId)
{
    const std::wstring dir = StoreFolder();
    return dir.empty() || !gameId || !*gameId ? std::wstring() : dir + L"\\runtime\\" + gameId + L".json";
}

}} // namespace dbce::panel
