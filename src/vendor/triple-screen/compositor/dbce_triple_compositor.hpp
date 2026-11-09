#pragma once
#include <array>
#include <cmath>
#include "../geometry/dbce_triple_geometry.hpp"

// C++17, graphics-independent warp of game-rendered source views onto three physical panels.
// See docs/native-compositor.md. Physical coordinates follow dbce_triple_geometry.hpp (RH, eye at the origin).
namespace dbce::triple_screen::compositor {
constexpr int max_sources = 4;

// One view the game rendered. Basis is orthonormal in physical coordinates; tangents bound the frustum at unit
// distance (left < right, bottom < top). The region is where that view sits in the source texture, top-left UV origin.
struct SourceView {
    Vec3 right, up, forward;
    double tan_left, tan_right, tan_bottom, tan_top;
    double region_u0, region_v0, region_u1, region_v1;
};
// Panels from build_rig. Each panel's width is the CHORD of a curved panel; curve_radius_mm 0 means flat.
// source_on (STD-041; C# CurvedPanelWarp.WithSourceOn, set with with_source_on): the game draws its views for
// source_panels, the same-size panels of a rig whose virtual eye is nearer or farther than the seat (the game's field
// of view, or a chosen one). The curve correction stays computed from the real eye: each point of the curved panel
// is seen from the eye on the panel's chord plane, and the same point of the matching source panel is sampled. A
// straight edge in the game's view then still looks straight from the seat; a warp built on the virtual rig bows it
// (owner report 2026-10-31, game field of view on 1500R). With flat panels it equals drawing on source_panels.
struct Layout {
    std::array<Surface,3> panels;
    double curve_radius_mm;
    bool source_on;
    std::array<Surface,3> source_panels;
};
struct Sample { bool covered; int source; double u, v; };   // texture UV, top-left origin

// A source turned about physical +Y by yaw (positive turns right), symmetric frustum from its horizontal FOV and the
// shape (width/height) of its region in pixels.
inline Error yawed_view(double yaw_degrees, double horizontal_fov_degrees, double region_aspect,
                        double u0, double v0, double u1, double v1, SourceView& out) noexcept {
    for (double v : {yaw_degrees,horizontal_fov_degrees,region_aspect,u0,v0,u1,v1}) if (!std::isfinite(v)) return Error::invalid_input;
    if (horizontal_fov_degrees<=0 || horizontal_fov_degrees>=180 || region_aspect<=0 || !(u0<u1) || !(v0<v1))
        return Error::invalid_input;
    const double y=yaw_degrees*detail::pi/180, tx=std::tan(horizontal_fov_degrees*detail::pi/360), ty=tx/region_aspect;
    out={{std::cos(y),0,std::sin(y)},{0,1,0},{std::sin(y),0,-std::cos(y)},-tx,tx,-ty,ty,u0,v0,u1,v1};
    return Error::none;
}

// Physical point at panel UV (lower-left origin, as eye_ray). Curved panels keep the chord endpoints of the flat
// panel and bow away from the viewer; pixels are spaced evenly along the arc.
inline Error panel_point(const Surface& s, double curve_radius_mm, double u, double v, Vec3& out) noexcept {
    if (!std::isfinite(u)||!std::isfinite(v)||!std::isfinite(curve_radius_mm)||u<0||u>1||v<0||v>1||curve_radius_mm<0)
        return Error::invalid_input;
    Vec3 right{},up{},normal{};
    const auto error=detail::basis(s,right,up,normal);
    if (error!=Error::none) return error;
    const double chord=std::sqrt(dot(s.lower_right-s.lower_left,s.lower_right-s.lower_left));
    const Vec3 lift=(s.upper_left-s.lower_left)*v;
    if (curve_radius_mm==0) { out=s.lower_left+right*(chord*u)+lift; return Error::none; }
    if (chord>2*curve_radius_mm) return Error::invalid_input;
    const double half=std::asin(chord/(2*curve_radius_mm)), phi=(2*u-1)*half;
    // cos(half)-cos(phi) written without cancellation for flat-ish panels.
    const double sag=-2*std::sin((half+phi)/2)*std::sin((half-phi)/2)*curve_radius_mm;
    out=s.lower_left+right*(chord/2)+right*(curve_radius_mm*std::sin(phi))+normal*sag+lift;
    return Error::none;
}

// The arc length of a curved panel whose chord is known (Wheelkit VisibleWidthMm is a chord).
inline Error arc_from_chord(double chord_mm, double curve_radius_mm, double& out) noexcept {
    if (!std::isfinite(chord_mm)||!std::isfinite(curve_radius_mm)||chord_mm<=0||curve_radius_mm<0) return Error::invalid_input;
    if (curve_radius_mm==0) { out=chord_mm; return Error::none; }
    if (chord_mm>2*curve_radius_mm) return Error::invalid_input;
    out=2*curve_radius_mm*std::asin(chord_mm/(2*curve_radius_mm)); return Error::none;
}

// The layout with its views drawn for the same-size `drawn` panels (Layout::source_on).
inline Error with_source_on(const Layout& l, const std::array<Surface,3>& drawn, Layout& out) noexcept {
    for (int i=0;i<3;++i) {
        Vec3 r{},u{},n{},dr{},du{},dn{};
        if (detail::basis(l.panels[i],r,u,n)!=Error::none||detail::basis(drawn[i],dr,du,dn)!=Error::none) return Error::invalid_input;
        const Vec3 w=l.panels[i].lower_right-l.panels[i].lower_left, h=l.panels[i].upper_left-l.panels[i].lower_left;
        const Vec3 dw=drawn[i].lower_right-drawn[i].lower_left, dh=drawn[i].upper_left-drawn[i].lower_left;
        if (std::fabs(std::sqrt(dot(w,w))-std::sqrt(dot(dw,dw)))>1e-6||std::fabs(std::sqrt(dot(h,h))-std::sqrt(dot(dh,dh)))>1e-6)
            return Error::invalid_input;   // the same panel sizes, as WithSourceOn
    }
    Layout r=l; r.source_on=true; r.source_panels=drawn; out=r; return Error::none;
}

// Where the warp samples for physical point p of `panel`: p itself, or with source_on the source panel's point at
// p's position on the panel's chord plane as seen from the eye (origin).
inline Error source_point(const Layout& l, int panel, const Vec3& p, Vec3& out) noexcept {
    if (panel<0||panel>2) return Error::invalid_input;
    if (!l.source_on) { out=p; return Error::none; }
    const Surface& s=l.panels[panel];
    const Surface& d=l.source_panels[panel];
    Vec3 right{},up{},normal{},dr{},du{},dn{};
    if (detail::basis(s,right,up,normal)!=Error::none||detail::basis(d,dr,du,dn)!=Error::none) return Error::invalid_input;
    const double plane=dot(s.lower_left,normal), depth=dot(p,normal);   // both negative with the eye in front
    if (!(plane<0)||!(depth<0)) return Error::eye_behind_surface;
    const Vec3 c=p*(plane/depth)-s.lower_left;
    out=d.lower_left+dr*dot(c,right)+du*dot(c,up);
    return Error::none;
}

// CPU reference for one panel pixel. Of the sources whose frustum holds the ray, the one holding it deepest inside
// horizontally (largest normalized distance to its left or right edge) wins, so overlapping views switch at a single
// vertical seam in the middle of their overlap.
inline Error sample(const Layout& l, const SourceView* sources, int count, int panel, double u, double v, Sample& out) noexcept {
    if (!sources||count<1||count>max_sources||panel<0||panel>2) return Error::invalid_input;
    Vec3 p{};
    auto error=panel_point(l.panels[panel],l.curve_radius_mm,u,v,p);
    if (error==Error::none) error=source_point(l,panel,p,p);
    if (error!=Error::none) return error;
    Sample best{false,-1,0,0};
    double best_margin=0;
    for (int i=0;i<count;++i) {
        const SourceView& s=sources[i];
        const double depth=dot(p,s.forward);
        if (!(depth>0)) continue;
        const double nu=(dot(p,s.right)/depth-s.tan_left)/(s.tan_right-s.tan_left);
        const double nv=(dot(p,s.up)/depth-s.tan_bottom)/(s.tan_top-s.tan_bottom);
        const double inside=std::fmin(std::fmin(nu,1-nu),std::fmin(nv,1-nv));
        if (!std::isfinite(inside)||inside<0) continue;
        const double margin=std::fmin(nu,1-nu);
        if (best.covered&&margin<=best_margin) continue;
        best={true,i,s.region_u0+nu*(s.region_u1-s.region_u0),s.region_v0+(1-nv)*(s.region_v1-s.region_v0)};
        best_margin=margin;
    }
    out=best; return Error::none;
}

// True when every point of a (grid+1)^2 lattice on each panel, edges included, reaches a source.
inline bool covers(const Layout& l, const SourceView* sources, int count, int grid = 32) noexcept {
    for (int p=0;p<3;++p) for (int i=0;i<=grid;++i) for (int j=0;j<=grid;++j) {
        Sample s{};
        if (sample(l,sources,count,p,(double)i/grid,(double)j/grid,s)!=Error::none||!s.covered) return false;
    }
    return true;
}

// Views for a game that can render `count` (2 or 3) equal views side by side in one texture, each region_aspect
// (width/height) in shape: yaws -y,+y (or -y,0,+y) and one horizontal FOV, chosen as the smallest FOV that covers
// every panel, so the game spends its pixels where the panels are. margin_degrees widens the result for safety.
inline Error plan_views(const Layout& l, int count, double region_aspect, double margin_degrees,
                        double& yaw_degrees, double& horizontal_fov_degrees, SourceView* out) noexcept {
    if ((count!=2&&count!=3)||!out||!std::isfinite(region_aspect)||region_aspect<=0||!std::isfinite(margin_degrees)||margin_degrees<0)
        return Error::invalid_input;
    auto views=[&](double yaw, double fov, SourceView* v) {
        for (int i=0;i<count;++i) {
            const double y=count==2?(i?yaw:-yaw):(i-1)*yaw;
            if (yawed_view(y,fov,region_aspect,(double)i/count,0,(double)(i+1)/count,1,v[i])!=Error::none) return false;
        }
        return true;
    };
    double best_yaw=0, best_fov=0;
    SourceView v[3]{};
    auto fit=[&](double yaw) {   // smallest covering FOV at this yaw, 0 if none
        if (!views(yaw,179,v)||!covers(l,v,count,48)) return 0.0;
        double lo=1, hi=179;
        while (hi-lo>0.01) { const double mid=(lo+hi)/2; if (views(yaw,mid,v)&&covers(l,v,count,48)) hi=mid; else lo=mid; }
        return hi;
    };
    auto consider=[&](double yaw) { const double f=fit(yaw); if (f>0&&(best_fov==0||f<best_fov)) best_fov=f, best_yaw=yaw; };
    for (double yaw=0;yaw<=88;yaw+=4) consider(yaw);   // coarse sweep, then refine around the best
    const double coarse=best_yaw;
    for (double yaw=coarse-4;yaw<=coarse+4;yaw+=0.5) if (yaw>=0&&yaw<=89) consider(yaw);
    if (best_fov==0) return Error::degenerate_frustum;
    // The search lattice is coarse; confirm on a fine one and widen until it holds.
    double fov=best_fov+margin_degrees;
    while (fov<179&&!(views(best_yaw,fov,v)&&covers(l,v,count,160))) fov+=0.05;
    if (fov>=179||!views(best_yaw,fov,out)) return Error::degenerate_frustum;
    yaw_degrees=best_yaw; horizontal_fov_degrees=fov; return Error::none;
}

// The three panels' own off-axis (Kooima) views, eye at the origin, each in its third of the source texture left to
// right: what a renderer that draws one chord frustum per panel (Sonic/ADRIFT triple.cpp) leaves in the back buffer.
// With curve_radius_mm 0 the warp of these views is the identity.
inline Error panel_views(const Layout& l, SourceView out[3]) noexcept {
    if (!out) return Error::invalid_input;
    SourceView views[3]{};
    for (int p=0;p<3;++p) {
        Projection pr{};
        const auto error=project(l.panels[p],{0,0,0},1,2,pr);
        if (error!=Error::none) return error;
        views[p]={pr.camera_right,pr.camera_up,pr.camera_forward,pr.left,pr.right,pr.bottom,pr.top,p/3.0,0,(p+1)/3.0,1};
    }
    for (int p=0;p<3;++p) out[p]=views[p];
    return Error::none;
}

// Shader constants (matches the cbuffer in dbce_triple_compositor_d3d11.hpp). Output rectangles are destination
// pixels (x, y, width, height), one per panel, left to right.
struct Constants {
    float panel_origin[3][4];   // lower left, w unused
    float panel_right[3][4];    // unit right, w = chord mm
    float panel_up[3][4];       // unit up, w = height mm
    float panel_normal[3][4];   // unit normal toward the viewer, w = half arc angle (0 flat)
    float output_rect[3][4];
    float source_right[max_sources][4];
    float source_up[max_sources][4];
    float source_forward[max_sources][4];
    float source_tan[max_sources][4];      // left, right, bottom, top
    float source_region[max_sources][4];   // u0, v0, u1, v1
    float misc[4];                         // radius mm, source count, crossfade band (0 = hard seam), fit panel + 1 (0 = warp)
    float tone[4];                         // exposure for a scene-linear (HDR) source, then Reinhard + sRGB; 0 = as is
    // Exposure match (set_exposure_match): views rendered with their own auto-exposure are scaled to their neighbour
    // toward the middle view, by the colour ratio where the two meet. x > 0 on, y mip level sampled, z maximum change.
    float match[4];
    // Layout::source_on: per panel the source panel's lower left (w 1 = on, 0 = sample the panel itself), unit right
    // and unit up.
    float drawn_origin[3][4];
    float drawn_right[3][4];
    float drawn_up[3][4];
};

// blend_band: where views overlap, crossfade over this normalized distance inside each view's edge (a fraction of
// its frustum width, e.g. 0.01); 0 switches views on a single seam, matching sample().
inline Error pack(const Layout& l, const SourceView* sources, int count, const float output_rects[3][4], Constants& out,
                  double blend_band = 0) noexcept {
    if (!sources||count<1||count>max_sources||!output_rects||!std::isfinite(l.curve_radius_mm)||l.curve_radius_mm<0||
        !std::isfinite(blend_band)||blend_band<0||blend_band>0.25)
        return Error::invalid_input;
    Constants c{};
    auto put=[](float* d, Vec3 v, double w) { d[0]=(float)v.x; d[1]=(float)v.y; d[2]=(float)v.z; d[3]=(float)w; };
    for (int i=0;i<3;++i) {
        const Surface& s=l.panels[i];
        Vec3 right{},up{},normal{};
        const auto error=detail::basis(s,right,up,normal);
        if (error!=Error::none) return error;
        const double chord=std::sqrt(dot(s.lower_right-s.lower_left,s.lower_right-s.lower_left));
        const double height=std::sqrt(dot(s.upper_left-s.lower_left,s.upper_left-s.lower_left));
        if (l.curve_radius_mm>0&&chord>2*l.curve_radius_mm) return Error::invalid_input;
        put(c.panel_origin[i],s.lower_left,0); put(c.panel_right[i],right,chord); put(c.panel_up[i],up,height);
        put(c.panel_normal[i],normal,l.curve_radius_mm>0?std::asin(chord/(2*l.curve_radius_mm)):0);
        for (int k=0;k<4;++k) { if (!std::isfinite(output_rects[i][k])) return Error::invalid_input; c.output_rect[i][k]=output_rects[i][k]; }
        if (!(output_rects[i][2]>0&&output_rects[i][3]>0)) return Error::invalid_input;
        if (l.source_on) {
            const Surface& d=l.source_panels[i];
            Vec3 dr{},du{},dn{};
            if (detail::basis(d,dr,du,dn)!=Error::none||!(dot(s.lower_left,normal)<0)) return Error::invalid_input;
            put(c.drawn_origin[i],d.lower_left,1); put(c.drawn_right[i],dr,0); put(c.drawn_up[i],du,0);
        }
    }
    for (int i=0;i<count;++i) {
        const SourceView& s=sources[i];
        if (!finite(s.right)||!finite(s.up)||!finite(s.forward)||!(s.tan_left<s.tan_right)||!(s.tan_bottom<s.tan_top)||
            !(s.region_u0<s.region_u1)||!(s.region_v0<s.region_v1)) return Error::invalid_input;
        // Orthonormal and right-handed (right x up points back at the viewer, against forward); a mirrored basis
        // would flip the view silently.
        const Vec3 back=cross(s.right,s.up);
        if (std::fabs(dot(s.right,s.right)-1)>1e-6||std::fabs(dot(s.up,s.up)-1)>1e-6||std::fabs(dot(s.right,s.up))>1e-6||
            std::fabs(dot(back,s.forward)+1)>1e-6) return Error::invalid_input;
        put(c.source_right[i],s.right,0); put(c.source_up[i],s.up,0); put(c.source_forward[i],s.forward,0);
        const double t[4]={s.tan_left,s.tan_right,s.tan_bottom,s.tan_top}, r[4]={s.region_u0,s.region_v0,s.region_u1,s.region_v1};
        for (int k=0;k<4;++k) { c.source_tan[i][k]=(float)t[k]; c.source_region[i][k]=(float)r[k]; }
    }
    c.misc[0]=(float)l.curve_radius_mm; c.misc[1]=(float)count; c.misc[2]=(float)blend_band;
    out=c; return Error::none;
}
// Fit mode: the whole source texture drawn into one panel's output rectangle (left 0, centre 1, right 2) and the other
// panels black, for a full-screen video or menu that would otherwise stretch across the row.
inline Error pack_fit(const float output_rects[3][4], int panel, Constants& out) noexcept {
    if (!output_rects||panel<0||panel>2) return Error::invalid_input;
    Constants c{};
    for (int i=0;i<3;++i) {
        for (int k=0;k<4;++k) { if (!std::isfinite(output_rects[i][k])) return Error::invalid_input; c.output_rect[i][k]=output_rects[i][k]; }
        if (!(output_rects[i][2]>0&&output_rects[i][3]>0)) return Error::invalid_input;
    }
    c.misc[3]=(float)(panel+1);
    out=c; return Error::none;
}
// Turns on exposure matching for a packed warp. mip: the source mip level sampled at the seams (each view a few dozen
// texels wide there; draw_in_place builds the chain); max_change: the largest gain change allowed, e.g. 0.35.
inline Error set_exposure_match(Constants& c, double mip, double max_change) noexcept {
    if (!std::isfinite(mip)||!std::isfinite(max_change)||mip<0||mip>14||max_change<=0||max_change>1) return Error::invalid_input;
    c.match[0]=1; c.match[1]=(float)mip; c.match[2]=(float)max_change; c.match[3]=0;
    return Error::none;
}
} // namespace dbce::triple_screen::compositor
