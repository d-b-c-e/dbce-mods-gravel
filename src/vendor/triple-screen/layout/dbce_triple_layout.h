/* Strict reader for the v1 triple-screen layout plus per-panel views at unit near distance.
 *
 * Header-only C99 and C++. No allocation, I/O, locale, engine, graphics or platform dependency.
 * Geometry mirrors native/geometry/dbce_triple_geometry.hpp and the managed core, which remain the
 * conformance oracle. See docs/native-layout.md for units, conventions and policy.
 * Every function returns a status; outputs stay unchanged unless it is DBCE_TRIPLE_OK. */
#ifndef DBCE_TRIPLE_LAYOUT_H
#define DBCE_TRIPLE_LAYOUT_H

#include <float.h>
#include <math.h>
#include <stddef.h>
#include <string.h>

#if defined(__cplusplus) || (defined(__STDC_VERSION__) && __STDC_VERSION__ >= 199901L)
#define DBCE_TRIPLE_FN static inline
#else
#define DBCE_TRIPLE_FN static __inline
#endif

#define DBCE_TRIPLE_LAYOUT_API_VERSION 1
#define DBCE_TRIPLE_LAYOUT_SCHEMA_VERSION 1
#define DBCE_TRIPLE_LAYOUT_MAX_BYTES 65536
#define DBCE_TRIPLE_LAYOUT_MAX_DEPTH 16

typedef enum {
    DBCE_TRIPLE_OK = 0,
    DBCE_TRIPLE_E_ARGUMENT = 1,           /* null pointer */
    DBCE_TRIPLE_E_EMPTY = 2,
    DBCE_TRIPLE_E_TOO_LARGE = 3,          /* more than DBCE_TRIPLE_LAYOUT_MAX_BYTES */
    DBCE_TRIPLE_E_NOT_UTF8 = 4,
    DBCE_TRIPLE_E_JSON = 5,               /* syntax, comment, nesting depth or trailing content */
    DBCE_TRIPLE_E_VERSION = 6,            /* not an object whose "schemaVersion" is 1 */
    DBCE_TRIPLE_E_UNKNOWN_PROPERTY = 7,
    DBCE_TRIPLE_E_DUPLICATE_PROPERTY = 8,
    DBCE_TRIPLE_E_MISSING_PROPERTY = 9,
    DBCE_TRIPLE_E_VALUE = 10,             /* wrong type, range or enumeration value */
    DBCE_TRIPLE_E_RIG = 11,               /* Error::invalid_input: nonfinite or out-of-range rig */
    DBCE_TRIPLE_E_DEGENERATE_SURFACE = 12,
    DBCE_TRIPLE_E_EYE_BEHIND_SURFACE = 13,
    DBCE_TRIPLE_E_DEGENERATE_FRUSTUM = 14
} dbce_triple_status;

typedef enum {
    DBCE_TRIPLE_OUTPUT_NVIDIA_SURROUND = 1,
    DBCE_TRIPLE_OUTPUT_BORDERLESS_SPAN = 2,
    DBCE_TRIPLE_OUTPUT_SEPARATE_DISPLAYS = 3
} dbce_triple_output_mode;

/* contracts/triple-screen-layout.schema.json version 1 as parsed. Absent optional values are 0. */
typedef struct {
    int schema_version;                         /* always 1 */
    int native_width_px, native_height_px;      /* one panel */
    double physical_width_mm, physical_height_mm; /* one panel's visible active area */
    int has_curve_radius;                       /* 0 when absent or null */
    double curve_radius_mm;                     /* recorded only: v1 models each panel as one plane */
    double bezel_width_mm;
    double eye_distance_mm;                     /* eye to the center-panel plane, along its normal */
    double eye_height_above_panel_center_mm;
    double left_yaw_degrees, right_yaw_degrees; /* [0,90): side panel turned toward the viewer */
    dbce_triple_output_mode output_mode;
    int combined_width_px, combined_height_px;  /* both 0 when absent or null */
} dbce_triple_layout_v1;

/* Same fields and meaning as dbce::triple_screen::Rig and the managed TripleRigDefinition. */
typedef struct {
    double panel_width_mm, panel_height_mm, eye_distance_mm;
    double left_angle_degrees, right_angle_degrees, eye_height_above_center_mm, bezel_gap_mm;
} dbce_triple_rig;

/* One panel seen from the eye at the origin, in right-handed physical millimeters: +X right, +Y up,
 * center panel at -Z. axis_forward points from the viewer into the panel. left/right/bottom/top are
 * dimensionless frustum bounds at unit near distance; the engine multiplies them by its own near. */
typedef struct {
    double lower_left_mm[3], lower_right_mm[3], upper_left_mm[3];
    double axis_right[3], axis_up[3], axis_forward[3];
    double left, right, bottom, top;
} dbce_triple_view;

/* ---- Implementation details, not part of the API. ---- */

typedef struct { double x, y, z; } dbce_triple_detail_v3;
typedef struct { const unsigned char *begin, *p, *end; } dbce_triple_detail_cursor;

DBCE_TRIPLE_FN int dbce_triple_detail_finite(double v) { return v >= -DBL_MAX && v <= DBL_MAX; }
DBCE_TRIPLE_FN dbce_triple_detail_v3 dbce_triple_detail_v(double x, double y, double z)
{ dbce_triple_detail_v3 r; r.x = x; r.y = y; r.z = z; return r; }
DBCE_TRIPLE_FN dbce_triple_detail_v3 dbce_triple_detail_add(dbce_triple_detail_v3 a, dbce_triple_detail_v3 b)
{ return dbce_triple_detail_v(a.x + b.x, a.y + b.y, a.z + b.z); }
DBCE_TRIPLE_FN dbce_triple_detail_v3 dbce_triple_detail_sub(dbce_triple_detail_v3 a, dbce_triple_detail_v3 b)
{ return dbce_triple_detail_v(a.x - b.x, a.y - b.y, a.z - b.z); }
DBCE_TRIPLE_FN dbce_triple_detail_v3 dbce_triple_detail_mul(dbce_triple_detail_v3 a, double s)
{ return dbce_triple_detail_v(a.x * s, a.y * s, a.z * s); }
DBCE_TRIPLE_FN double dbce_triple_detail_dot(dbce_triple_detail_v3 a, dbce_triple_detail_v3 b)
{ return a.x * b.x + a.y * b.y + a.z * b.z; }
DBCE_TRIPLE_FN dbce_triple_detail_v3 dbce_triple_detail_cross(dbce_triple_detail_v3 a, dbce_triple_detail_v3 b)
{ return dbce_triple_detail_v(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x); }
DBCE_TRIPLE_FN int dbce_triple_detail_v3_finite(dbce_triple_detail_v3 a)
{ return dbce_triple_detail_finite(a.x) && dbce_triple_detail_finite(a.y) && dbce_triple_detail_finite(a.z); }
DBCE_TRIPLE_FN void dbce_triple_detail_store(double out[3], dbce_triple_detail_v3 a)
{ out[0] = a.x; out[1] = a.y; out[2] = a.z; }

/* Managed Vector3d.Normalized: divide by the length. */
DBCE_TRIPLE_FN int dbce_triple_detail_unit(dbce_triple_detail_v3 a, double epsilon, dbce_triple_detail_v3 *out)
{
    const double length = sqrt(dbce_triple_detail_dot(a, a));
    if (!dbce_triple_detail_finite(length) || !(length > epsilon)) return 0;
    *out = dbce_triple_detail_v(a.x / length, a.y / length, a.z / length);
    return dbce_triple_detail_v3_finite(*out);
}

/* Builds and checks one panel the way TripleRigBuilder.CreateSurface and project() do. */
DBCE_TRIPLE_FN dbce_triple_status dbce_triple_detail_view(dbce_triple_detail_v3 center, dbce_triple_detail_v3 right,
                                                          double width, double height, dbce_triple_view *out)
{
    const dbce_triple_detail_v3 half_right = dbce_triple_detail_mul(right, width / 2.0);
    const dbce_triple_detail_v3 half_up = dbce_triple_detail_v(0.0, height / 2.0, 0.0);
    const dbce_triple_detail_v3 ll = dbce_triple_detail_sub(dbce_triple_detail_sub(center, half_right), half_up);
    const dbce_triple_detail_v3 lr = dbce_triple_detail_sub(dbce_triple_detail_add(center, half_right), half_up);
    const dbce_triple_detail_v3 ul = dbce_triple_detail_add(dbce_triple_detail_sub(center, half_right), half_up);
    dbce_triple_detail_v3 axis_right, axis_up, normal;
    double distance, l, r, b, t;
    if (!dbce_triple_detail_v3_finite(ll) || !dbce_triple_detail_v3_finite(lr) || !dbce_triple_detail_v3_finite(ul))
        return DBCE_TRIPLE_E_RIG;
    if (!dbce_triple_detail_unit(dbce_triple_detail_sub(lr, ll), 1e-9, &axis_right) ||
        !dbce_triple_detail_unit(dbce_triple_detail_sub(ul, ll), 1e-9, &axis_up) ||
        fabs(dbce_triple_detail_dot(axis_right, axis_up)) > 1e-6 ||
        !dbce_triple_detail_unit(dbce_triple_detail_cross(axis_right, axis_up), 1e-12, &normal))
        return DBCE_TRIPLE_E_DEGENERATE_SURFACE;
    /* The eye is the origin, so each corner is also the eye-to-corner vector. */
    distance = -dbce_triple_detail_dot(ll, normal);
    if (!dbce_triple_detail_finite(distance)) return DBCE_TRIPLE_E_RIG;
    if (!(distance > 1e-9)) return DBCE_TRIPLE_E_EYE_BEHIND_SURFACE;
    l = dbce_triple_detail_dot(axis_right, ll) / distance;
    r = dbce_triple_detail_dot(axis_right, lr) / distance;
    b = dbce_triple_detail_dot(axis_up, ll) / distance;
    t = dbce_triple_detail_dot(axis_up, ul) / distance;
    if (!dbce_triple_detail_finite(l) || !dbce_triple_detail_finite(r) || !dbce_triple_detail_finite(b) ||
        !dbce_triple_detail_finite(t) || !dbce_triple_detail_finite(r - l) || !dbce_triple_detail_finite(t - b) ||
        !(r - l > 1e-12) || !(t - b > 1e-12))
        return DBCE_TRIPLE_E_DEGENERATE_FRUSTUM;
    dbce_triple_detail_store(out->lower_left_mm, ll);
    dbce_triple_detail_store(out->lower_right_mm, lr);
    dbce_triple_detail_store(out->upper_left_mm, ul);
    dbce_triple_detail_store(out->axis_right, axis_right);
    dbce_triple_detail_store(out->axis_up, axis_up);
    dbce_triple_detail_store(out->axis_forward, dbce_triple_detail_mul(normal, -1.0));
    out->left = l; out->right = r; out->bottom = b; out->top = t;
    return DBCE_TRIPLE_OK;
}

DBCE_TRIPLE_FN int dbce_triple_detail_utf8(const unsigned char *p, const unsigned char *end, const unsigned char **bad)
{
    while (p < end) {
        const unsigned char c = *p;
        unsigned char low = 0x80, high = 0xBF;
        size_t count, i;
        if (c < 0x80) { ++p; continue; }
        if (c >= 0xC2 && c <= 0xDF) count = 1;
        else if (c == 0xE0) { count = 2; low = 0xA0; }
        else if ((c >= 0xE1 && c <= 0xEC) || c == 0xEE || c == 0xEF) count = 2;
        else if (c == 0xED) { count = 2; high = 0x9F; }
        else if (c == 0xF0) { count = 3; low = 0x90; }
        else if (c >= 0xF1 && c <= 0xF3) count = 3;
        else if (c == 0xF4) { count = 3; high = 0x8F; }
        else { *bad = p; return 0; }
        if ((size_t)(end - p) <= count || p[1] < low || p[1] > high) { *bad = p; return 0; }
        for (i = 2; i <= count; ++i)
            if (p[i] < 0x80 || p[i] > 0xBF) { *bad = p; return 0; }
        p += count + 1;
    }
    return 1;
}

DBCE_TRIPLE_FN void dbce_triple_detail_ws(dbce_triple_detail_cursor *c)
{
    while (c->p < c->end && (*c->p == ' ' || *c->p == '\t' || *c->p == '\n' || *c->p == '\r')) ++c->p;
}

DBCE_TRIPLE_FN int dbce_triple_detail_literal(dbce_triple_detail_cursor *c, const char *word)
{
    const size_t length = strlen(word);
    if ((size_t)(c->end - c->p) < length || memcmp(c->p, word, length) != 0) return 0;
    c->p += length;
    return 1;
}

/* At '"'. Sets [*first, *last) to the raw contents and *escaped when it contains an escape. */
DBCE_TRIPLE_FN int dbce_triple_detail_string(dbce_triple_detail_cursor *c, const unsigned char **first,
                                             const unsigned char **last, int *escaped)
{
    *escaped = 0;
    *first = ++c->p;
    while (c->p < c->end) {
        unsigned char ch = *c->p;
        if (ch == '"') { *last = c->p++; return 1; }
        if (ch < 0x20) return 0;
        if (ch == '\\') {
            *escaped = 1;
            if (++c->p >= c->end) return 0;
            ch = *c->p;
            if (ch == 'u') {
                int i;
                for (i = 0; i < 4; ++i) {
                    if (++c->p >= c->end) return 0;
                    ch = *c->p;
                    if (!((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f') || (ch >= 'A' && ch <= 'F'))) return 0;
                }
            } else if (ch != '"' && ch != '\\' && ch != '/' && ch != 'b' && ch != 'f' && ch != 'n' &&
                       ch != 'r' && ch != 't') {
                return 0;
            }
        }
        ++c->p;
    }
    return 0;
}

DBCE_TRIPLE_FN int dbce_triple_detail_digits(dbce_triple_detail_cursor *c)
{
    const unsigned char *start = c->p;
    while (c->p < c->end && *c->p >= '0' && *c->p <= '9') ++c->p;
    return c->p > start;
}

/* RFC 8259 number grammar only. */
DBCE_TRIPLE_FN int dbce_triple_detail_number_token(dbce_triple_detail_cursor *c)
{
    if (c->p < c->end && *c->p == '-') ++c->p;
    if (c->p < c->end && *c->p == '0') ++c->p;
    else if (!(c->p < c->end && *c->p >= '1' && *c->p <= '9') || !dbce_triple_detail_digits(c)) return 0;
    if (c->p < c->end && *c->p == '.') { ++c->p; if (!dbce_triple_detail_digits(c)) return 0; }
    if (c->p < c->end && (*c->p == 'e' || *c->p == 'E')) {
        ++c->p;
        if (c->p < c->end && (*c->p == '+' || *c->p == '-')) ++c->p;
        if (!dbce_triple_detail_digits(c)) return 0;
    }
    return 1;
}

/* Locale-independent conversion of a validated number token. Exact (one rounding) for up to
 * 2^53 significant value and |decimal exponent| <= 22; otherwise within a few ulps. */
DBCE_TRIPLE_FN double dbce_triple_detail_number_value(const unsigned char *p, const unsigned char *end)
{
    unsigned long long mantissa = 0;
    long exponent = 0, written = 0, i;
    int negative = 0, digits = 0, exponent_negative = 0;
    double value, scale = 1.0;
    if (*p == '-') { negative = 1; ++p; }
    for (; p < end && *p >= '0' && *p <= '9'; ++p) {
        if (digits < 19) { mantissa = mantissa * 10u + (unsigned)(*p - '0'); if (mantissa) ++digits; }
        else ++exponent;
    }
    if (p < end && *p == '.') {
        for (++p; p < end && *p >= '0' && *p <= '9'; ++p) {
            if (digits < 19) { mantissa = mantissa * 10u + (unsigned)(*p - '0'); if (mantissa) ++digits; --exponent; }
        }
    }
    if (p < end && (*p == 'e' || *p == 'E')) {
        ++p;
        if (*p == '+' || *p == '-') { exponent_negative = *p == '-'; ++p; }
        for (; p < end; ++p) if (written < 100000) written = written * 10 + (*p - '0');
        exponent += exponent_negative ? -written : written;
    }
    value = (double)mantissa;
    if (mantissa == 0 || exponent < -400) return 0.0;
    if (exponent > 400) exponent = 400; /* still overflows to infinity below, at run time */
    for (; exponent > 22; exponent -= 22) value *= 1e22;
    for (; exponent < -22; exponent += 22) value /= 1e22;
    for (i = 0; i < (exponent < 0 ? -exponent : exponent); ++i) scale *= 10.0; /* exact up to 1e22 */
    value = exponent < 0 ? value / scale : value * scale;
    return negative ? -value : value;
}

/* Generic validation of one JSON value; containers nest at most DBCE_TRIPLE_LAYOUT_MAX_DEPTH deep. */
DBCE_TRIPLE_FN int dbce_triple_detail_skip(dbce_triple_detail_cursor *c, int depth)
{
    const unsigned char *first, *last;
    int escaped;
    dbce_triple_detail_ws(c);
    if (c->p >= c->end) return 0;
    if (*c->p == '{' || *c->p == '[') {
        const unsigned char close = (unsigned char)(*c->p == '{' ? '}' : ']');
        if (depth >= DBCE_TRIPLE_LAYOUT_MAX_DEPTH) return 0;
        ++c->p;
        dbce_triple_detail_ws(c);
        if (c->p < c->end && *c->p == close) { ++c->p; return 1; }
        for (;;) {
            if (close == '}') {
                dbce_triple_detail_ws(c);
                if (c->p >= c->end || *c->p != '"' || !dbce_triple_detail_string(c, &first, &last, &escaped)) return 0;
                dbce_triple_detail_ws(c);
                if (c->p >= c->end || *c->p != ':') return 0;
                ++c->p;
            }
            if (!dbce_triple_detail_skip(c, depth + 1)) return 0;
            dbce_triple_detail_ws(c);
            if (c->p >= c->end) return 0;
            if (*c->p == close) { ++c->p; return 1; }
            if (*c->p != ',') return 0;
            ++c->p;
        }
    }
    if (*c->p == '"') return dbce_triple_detail_string(c, &first, &last, &escaped);
    if (*c->p == 't') return dbce_triple_detail_literal(c, "true");
    if (*c->p == 'f') return dbce_triple_detail_literal(c, "false");
    if (*c->p == 'n') return dbce_triple_detail_literal(c, "null");
    return dbce_triple_detail_number_token(c);
}

DBCE_TRIPLE_FN int dbce_triple_detail_name(const unsigned char *first, const unsigned char *last, int escaped,
                                           const char *name)
{
    const size_t length = strlen(name);
    return !escaped && (size_t)(last - first) == length && memcmp(first, name, length) == 0;
}

/* Strict pass over syntax-validated text: the next member name, or 0 after the closing brace. */
DBCE_TRIPLE_FN int dbce_triple_detail_member(dbce_triple_detail_cursor *c, const unsigned char **first,
                                             const unsigned char **last, int *escaped, size_t *offset)
{
    dbce_triple_detail_ws(c);
    if (*c->p == ',') { ++c->p; dbce_triple_detail_ws(c); }
    if (*c->p == '}') { ++c->p; return 0; }
    *offset = (size_t)(c->p - c->begin);
    dbce_triple_detail_string(c, first, last, escaped);
    dbce_triple_detail_ws(c);
    ++c->p; /* ':' */
    dbce_triple_detail_ws(c);
    return 1;
}

/* A finite number, or null when allowed. */
DBCE_TRIPLE_FN int dbce_triple_detail_read_number(dbce_triple_detail_cursor *c, int allow_null, int *is_null, double *value)
{
    const unsigned char *start = c->p;
    *is_null = 0;
    if (*c->p == 'n') {
        if (!allow_null) return 0;
        c->p += 4;
        *is_null = 1;
        return 1;
    }
    if (*c->p != '-' && !(*c->p >= '0' && *c->p <= '9')) return 0;
    dbce_triple_detail_number_token(c);
    *value = dbce_triple_detail_number_value(start, c->p);
    return dbce_triple_detail_finite(*value);
}

DBCE_TRIPLE_FN int dbce_triple_detail_pixels(double value, int *out)
{
    if (!(value >= 1.0 && value <= 2147483647.0) || floor(value) != value) return 0;
    *out = (int)value;
    return 1;
}

DBCE_TRIPLE_FN dbce_triple_status dbce_triple_detail_panel(dbce_triple_detail_cursor *c, dbce_triple_layout_v1 *l, size_t *error)
{
    const size_t start = (size_t)(c->p - c->begin);
    const unsigned char *first, *last;
    unsigned seen = 0, bit = 0;
    size_t key = 0;
    int escaped, is_null, ok = 0;
    double v = 0.0;
    if (*c->p != '{') { *error = start; return DBCE_TRIPLE_E_VALUE; }
    ++c->p;
    while (dbce_triple_detail_member(c, &first, &last, &escaped, &key)) {
        const size_t at = (size_t)(c->p - c->begin);
        if (dbce_triple_detail_name(first, last, escaped, "count")) bit = 1;
        else if (dbce_triple_detail_name(first, last, escaped, "nativeWidthPx")) bit = 2;
        else if (dbce_triple_detail_name(first, last, escaped, "nativeHeightPx")) bit = 4;
        else if (dbce_triple_detail_name(first, last, escaped, "physicalWidthMm")) bit = 8;
        else if (dbce_triple_detail_name(first, last, escaped, "physicalHeightMm")) bit = 16;
        else if (dbce_triple_detail_name(first, last, escaped, "curveRadiusMm")) bit = 32;
        else if (dbce_triple_detail_name(first, last, escaped, "bezelWidthMm")) bit = 64;
        else { *error = key; return DBCE_TRIPLE_E_UNKNOWN_PROPERTY; }
        if (seen & bit) { *error = key; return DBCE_TRIPLE_E_DUPLICATE_PROPERTY; }
        seen |= bit;
        if (!dbce_triple_detail_read_number(c, bit == 32, &is_null, &v)) { *error = at; return DBCE_TRIPLE_E_VALUE; }
        switch (bit) {
        case 1: ok = v == 3.0; break;
        case 2: ok = dbce_triple_detail_pixels(v, &l->native_width_px); break;
        case 4: ok = dbce_triple_detail_pixels(v, &l->native_height_px); break;
        case 8: ok = v > 0.0; l->physical_width_mm = v; break;
        case 16: ok = v > 0.0; l->physical_height_mm = v; break;
        case 32: ok = is_null || v > 0.0; l->has_curve_radius = !is_null; l->curve_radius_mm = is_null ? 0.0 : v; break;
        default: ok = v >= 0.0; l->bezel_width_mm = v; break;
        }
        if (!ok) { *error = at; return DBCE_TRIPLE_E_VALUE; }
    }
    if ((seen & 31u) != 31u) { *error = start; return DBCE_TRIPLE_E_MISSING_PROPERTY; }
    return DBCE_TRIPLE_OK;
}

DBCE_TRIPLE_FN dbce_triple_status dbce_triple_detail_geometry(dbce_triple_detail_cursor *c, dbce_triple_layout_v1 *l, size_t *error)
{
    const size_t start = (size_t)(c->p - c->begin);
    const unsigned char *first, *last;
    unsigned seen = 0, bit = 0;
    size_t key = 0;
    int escaped, is_null, ok = 0;
    double v = 0.0;
    if (*c->p != '{') { *error = start; return DBCE_TRIPLE_E_VALUE; }
    ++c->p;
    while (dbce_triple_detail_member(c, &first, &last, &escaped, &key)) {
        const size_t at = (size_t)(c->p - c->begin);
        if (dbce_triple_detail_name(first, last, escaped, "eyeDistanceMm")) bit = 1;
        else if (dbce_triple_detail_name(first, last, escaped, "leftYawDegrees")) bit = 2;
        else if (dbce_triple_detail_name(first, last, escaped, "rightYawDegrees")) bit = 4;
        else if (dbce_triple_detail_name(first, last, escaped, "eyeHeightAbovePanelCenterMm")) bit = 8;
        else { *error = key; return DBCE_TRIPLE_E_UNKNOWN_PROPERTY; }
        if (seen & bit) { *error = key; return DBCE_TRIPLE_E_DUPLICATE_PROPERTY; }
        seen |= bit;
        if (!dbce_triple_detail_read_number(c, 0, &is_null, &v)) { *error = at; return DBCE_TRIPLE_E_VALUE; }
        switch (bit) {
        case 1: ok = v > 0.0; l->eye_distance_mm = v; break;
        case 2: ok = v >= 0.0 && v < 90.0; l->left_yaw_degrees = v; break;
        case 4: ok = v >= 0.0 && v < 90.0; l->right_yaw_degrees = v; break;
        default: ok = 1; l->eye_height_above_panel_center_mm = v; break;
        }
        if (!ok) { *error = at; return DBCE_TRIPLE_E_VALUE; }
    }
    if ((seen & 7u) != 7u) { *error = start; return DBCE_TRIPLE_E_MISSING_PROPERTY; }
    return DBCE_TRIPLE_OK;
}

DBCE_TRIPLE_FN dbce_triple_status dbce_triple_detail_output(dbce_triple_detail_cursor *c, dbce_triple_layout_v1 *l, size_t *error)
{
    const size_t start = (size_t)(c->p - c->begin);
    const unsigned char *first, *last, *value_first, *value_last;
    unsigned seen = 0, bit = 0;
    size_t key = 0;
    int escaped, is_null, ok = 0;
    double v = 0.0;
    if (*c->p != '{') { *error = start; return DBCE_TRIPLE_E_VALUE; }
    ++c->p;
    while (dbce_triple_detail_member(c, &first, &last, &escaped, &key)) {
        const size_t at = (size_t)(c->p - c->begin);
        if (dbce_triple_detail_name(first, last, escaped, "mode")) bit = 1;
        else if (dbce_triple_detail_name(first, last, escaped, "combinedWidthPx")) bit = 2;
        else if (dbce_triple_detail_name(first, last, escaped, "combinedHeightPx")) bit = 4;
        else { *error = key; return DBCE_TRIPLE_E_UNKNOWN_PROPERTY; }
        if (seen & bit) { *error = key; return DBCE_TRIPLE_E_DUPLICATE_PROPERTY; }
        seen |= bit;
        if (bit == 1) {
            if (*c->p != '"') { *error = at; return DBCE_TRIPLE_E_VALUE; }
            dbce_triple_detail_string(c, &value_first, &value_last, &escaped);
            ok = 1;
            if (dbce_triple_detail_name(value_first, value_last, escaped, "nvidia-surround"))
                l->output_mode = DBCE_TRIPLE_OUTPUT_NVIDIA_SURROUND;
            else if (dbce_triple_detail_name(value_first, value_last, escaped, "borderless-span"))
                l->output_mode = DBCE_TRIPLE_OUTPUT_BORDERLESS_SPAN;
            else if (dbce_triple_detail_name(value_first, value_last, escaped, "separate-displays"))
                l->output_mode = DBCE_TRIPLE_OUTPUT_SEPARATE_DISPLAYS;
            else ok = 0;
        } else {
            int *target = bit == 2 ? &l->combined_width_px : &l->combined_height_px;
            if (!dbce_triple_detail_read_number(c, 1, &is_null, &v)) { *error = at; return DBCE_TRIPLE_E_VALUE; }
            ok = is_null || dbce_triple_detail_pixels(v, target);
            if (is_null) *target = 0;
        }
        if (!ok) { *error = at; return DBCE_TRIPLE_E_VALUE; }
    }
    if (!(seen & 1u)) { *error = start; return DBCE_TRIPLE_E_MISSING_PROPERTY; }
    /* As the managed reader: combined width and height are both given or both omitted. */
    if ((l->combined_width_px == 0) != (l->combined_height_px == 0)) { *error = start; return DBCE_TRIPLE_E_VALUE; }
    return DBCE_TRIPLE_OK;
}

/* ---- API ---- */

/* Parses one complete v1 desired-layout document from UTF-8 bytes (a leading BOM is skipped).
 * Rejects comments, duplicate or unknown properties at every level, property names written with
 * escapes, wrong JSON types, out-of-range values and trailing content. A document whose root
 * "schemaVersion" is not 1 returns DBCE_TRIPLE_E_VERSION whatever else it contains. error_offset
 * (optional) receives the byte offset of the failing token, or 0 on success. */
DBCE_TRIPLE_FN dbce_triple_status dbce_triple_layout_parse(const void *utf8, size_t length,
                                                           dbce_triple_layout_v1 *out, size_t *error_offset)
{
    dbce_triple_detail_cursor c;
    dbce_triple_layout_v1 l;
    const unsigned char *first, *last, *bad = 0, *version_first = 0, *version_last = 0;
    unsigned seen = 0, bit = 0;
    size_t error = 0, key = 0;
    int escaped, is_null;
    double v = 0.0;
    dbce_triple_status status = DBCE_TRIPLE_OK;
    if (error_offset) *error_offset = 0;
    if (!utf8 || !out) return DBCE_TRIPLE_E_ARGUMENT;
    if (length == 0) return DBCE_TRIPLE_E_EMPTY;
    if (length > DBCE_TRIPLE_LAYOUT_MAX_BYTES) return DBCE_TRIPLE_E_TOO_LARGE;
    c.begin = c.p = (const unsigned char *)utf8;
    c.end = c.begin + length;
    if (length >= 3 && c.p[0] == 0xEF && c.p[1] == 0xBB && c.p[2] == 0xBF) c.p += 3;
    if (!dbce_triple_detail_utf8(c.p, c.end, &bad)) {
        if (error_offset) *error_offset = (size_t)(bad - c.begin);
        return DBCE_TRIPLE_E_NOT_UTF8;
    }

    /* Pass 1: the whole text is JSON; find the root schemaVersion before judging anything else. */
    {
        dbce_triple_detail_cursor g = c;
        int object;
        dbce_triple_detail_ws(&g);
        object = g.p < g.end && *g.p == '{';
        if (object) {
            ++g.p;
            dbce_triple_detail_ws(&g);
            if (g.p < g.end && *g.p == '}') { ++g.p; object = 2; }
            else for (;;) {
                dbce_triple_detail_ws(&g);
                if (g.p >= g.end || *g.p != '"' || !dbce_triple_detail_string(&g, &first, &last, &escaped)) break;
                dbce_triple_detail_ws(&g);
                if (g.p >= g.end || *g.p != ':') break;
                ++g.p;
                dbce_triple_detail_ws(&g);
                if (dbce_triple_detail_name(first, last, escaped, "schemaVersion")) {
                    version_first = g.p;
                    if (g.p < g.end && (*g.p == '-' || (*g.p >= '0' && *g.p <= '9')) && dbce_triple_detail_number_token(&g))
                        version_last = g.p;
                    else version_last = 0;
                    g.p = version_first;
                }
                if (!dbce_triple_detail_skip(&g, 1)) break;
                dbce_triple_detail_ws(&g);
                if (g.p < g.end && *g.p == ',') { ++g.p; continue; }
                if (g.p < g.end && *g.p == '}') { ++g.p; object = 2; }
                break;
            }
            if (object != 2) {
                if (error_offset) *error_offset = (size_t)(g.p - c.begin);
                return DBCE_TRIPLE_E_JSON;
            }
        } else if (!dbce_triple_detail_skip(&g, 0)) {
            if (error_offset) *error_offset = (size_t)(g.p - c.begin);
            return DBCE_TRIPLE_E_JSON;
        }
        dbce_triple_detail_ws(&g);
        if (g.p != g.end) {
            if (error_offset) *error_offset = (size_t)(g.p - c.begin);
            return DBCE_TRIPLE_E_JSON;
        }
        if (!object || !version_last || dbce_triple_detail_number_value(version_first, version_last) != 1.0) {
            if (error_offset && version_first) *error_offset = (size_t)(version_first - c.begin);
            return DBCE_TRIPLE_E_VERSION;
        }
    }

    /* Pass 2: strict v1 members. */
    memset(&l, 0, sizeof l);
    l.schema_version = DBCE_TRIPLE_LAYOUT_SCHEMA_VERSION;
    dbce_triple_detail_ws(&c);
    ++c.p;
    while (status == DBCE_TRIPLE_OK && dbce_triple_detail_member(&c, &first, &last, &escaped, &key)) {
        if (dbce_triple_detail_name(first, last, escaped, "schemaVersion")) bit = 1;
        else if (dbce_triple_detail_name(first, last, escaped, "panel")) bit = 2;
        else if (dbce_triple_detail_name(first, last, escaped, "geometry")) bit = 4;
        else if (dbce_triple_detail_name(first, last, escaped, "output")) bit = 8;
        else { error = key; status = DBCE_TRIPLE_E_UNKNOWN_PROPERTY; break; }
        if (seen & bit) { error = key; status = DBCE_TRIPLE_E_DUPLICATE_PROPERTY; break; }
        seen |= bit;
        if (bit == 1) dbce_triple_detail_read_number(&c, 0, &is_null, &v); /* checked in pass 1 */
        else if (bit == 2) status = dbce_triple_detail_panel(&c, &l, &error);
        else if (bit == 4) status = dbce_triple_detail_geometry(&c, &l, &error);
        else status = dbce_triple_detail_output(&c, &l, &error);
    }
    if (status == DBCE_TRIPLE_OK && seen != 15u) { error = 0; status = DBCE_TRIPLE_E_MISSING_PROPERTY; }
    if (status != DBCE_TRIPLE_OK) {
        if (error_offset) *error_offset = error;
        return status;
    }
    *out = l;
    return DBCE_TRIPLE_OK;
}

/* Copies the measured rig from a parsed layout. Whether the bezel gap skips the scene behind the
 * bezels is adapter policy: pass apply_bezel_gap = 0 to ignore bezel_width_mm. */
DBCE_TRIPLE_FN dbce_triple_status dbce_triple_rig_from_layout(const dbce_triple_layout_v1 *layout, int apply_bezel_gap,
                                                              dbce_triple_rig *out)
{
    dbce_triple_rig r;
    if (!layout || !out) return DBCE_TRIPLE_E_ARGUMENT;
    r.panel_width_mm = layout->physical_width_mm;
    r.panel_height_mm = layout->physical_height_mm;
    r.eye_distance_mm = layout->eye_distance_mm;
    r.left_angle_degrees = layout->left_yaw_degrees;
    r.right_angle_degrees = layout->right_yaw_degrees;
    r.eye_height_above_center_mm = layout->eye_height_above_panel_center_mm;
    r.bezel_gap_mm = apply_bezel_gap ? layout->bezel_width_mm : 0.0;
    *out = r;
    return DBCE_TRIPLE_OK;
}

/* Left, center and right panel views for the eye at the origin, as build_rig() followed by
 * project(panel, origin, 1, far) would give them. */
DBCE_TRIPLE_FN dbce_triple_status dbce_triple_rig_views(const dbce_triple_rig *rig, dbce_triple_view out[3])
{
    const double pi = 3.141592653589793238462643383279502884;
    dbce_triple_view views[3];
    dbce_triple_detail_v3 left_right, right_right, hinge;
    double y, z, w, h, offset, left_radians, right_radians;
    dbce_triple_status status;
    if (!rig || !out) return DBCE_TRIPLE_E_ARGUMENT;
    w = rig->panel_width_mm;
    h = rig->panel_height_mm;
    if (!dbce_triple_detail_finite(w) || !dbce_triple_detail_finite(h) || !dbce_triple_detail_finite(rig->eye_distance_mm) ||
        !dbce_triple_detail_finite(rig->left_angle_degrees) || !dbce_triple_detail_finite(rig->right_angle_degrees) ||
        !dbce_triple_detail_finite(rig->eye_height_above_center_mm) || !dbce_triple_detail_finite(rig->bezel_gap_mm) ||
        !(w > 0.0) || !(h > 0.0) || !(rig->eye_distance_mm > 0.0) ||
        !(rig->left_angle_degrees >= 0.0 && rig->left_angle_degrees < 90.0) ||
        !(rig->right_angle_degrees >= 0.0 && rig->right_angle_degrees < 90.0) || !(rig->bezel_gap_mm >= 0.0))
        return DBCE_TRIPLE_E_RIG;
    y = -rig->eye_height_above_center_mm;
    z = -rig->eye_distance_mm;
    offset = rig->bezel_gap_mm + w / 2.0;
    left_radians = rig->left_angle_degrees * pi / 180.0;
    right_radians = rig->right_angle_degrees * pi / 180.0;
    left_right = dbce_triple_detail_v(cos(left_radians), 0.0, -sin(left_radians));
    right_right = dbce_triple_detail_v(cos(right_radians), 0.0, sin(right_radians));
    /* Each side panel starts one bezel gap beyond its hinge, along its own surface. */
    hinge = dbce_triple_detail_v(-w / 2.0, y, z);
    status = dbce_triple_detail_view(dbce_triple_detail_sub(hinge, dbce_triple_detail_mul(left_right, offset)),
                                     left_right, w, h, &views[0]);
    if (status != DBCE_TRIPLE_OK) return status;
    status = dbce_triple_detail_view(dbce_triple_detail_v(0.0, y, z), dbce_triple_detail_v(1.0, 0.0, 0.0), w, h, &views[1]);
    if (status != DBCE_TRIPLE_OK) return status;
    hinge = dbce_triple_detail_v(w / 2.0, y, z);
    status = dbce_triple_detail_view(dbce_triple_detail_add(hinge, dbce_triple_detail_mul(right_right, offset)),
                                     right_right, w, h, &views[2]);
    if (status != DBCE_TRIPLE_OK) return status;
    out[0] = views[0];
    out[1] = views[1];
    out[2] = views[2];
    return DBCE_TRIPLE_OK;
}

#endif
