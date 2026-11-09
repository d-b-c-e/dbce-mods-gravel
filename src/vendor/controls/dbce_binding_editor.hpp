// Shared native binding capture and explicit-save draft. C++17, no device/OS/UI/force calls.
// Mirrors dotnet/Dbce.Wheel.Input BindingCapture{,Flow}, BindingEditorPolicy and ControlProfileDraft.
// The host supplies only accepted ordinary Core snapshots; this code never ticks or opens a reader.
#pragma once
#include "dbce_controls.hpp"
#include <algorithm>
#include <array>
#include <bitset>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>

namespace dbce { namespace controls { namespace edit {
inline bool valid_utf8(const std::string& text);
using ActionKindPolicy = std::function<bool(const std::string&, Kind)>;
struct RawSample {
    std::array<int32_t, 8> axes{};
    std::array<bool, 128> buttons{};
    std::array<int32_t, 4> hats{{-1,-1,-1,-1}};
};
struct AcceptedObservation {
    std::string instance, product, name, identity_key;
    uint64_t epoch{}, sequence{};
    double seen{};                         // same injected local clock as observe/begin/refresh
    RawSample sample;
    uint32_t axis_mask{}, range_mask{}, hat_mask{};
    std::bitset<128> button_mask;
    std::array<int32_t,8> minimum{}, maximum{};
};
struct CaptureResult { std::string action; Binding binding; };
inline bool same_identity(const AcceptedObservation& a, const AcceptedObservation& b) {
    return a.instance==b.instance && a.product==b.product && a.identity_key==b.identity_key && a.epoch==b.epoch;
}
inline bool fresh(const AcceptedObservation& o, double now) {
    return std::isfinite(now) && std::isfinite(o.seen) && o.seen<=now && now-o.seen<=0.25;
}
inline bool nonzero_guid(const std::string& s) {
    return detail::guid(s)==s && s!="{00000000-0000-0000-0000-000000000000}";
}
inline bool ranges_valid(const AcceptedObservation& o) {
    if ((o.axis_mask|o.range_mask)>255 || o.hat_mask>15 || (o.range_mask&~o.axis_mask)) return false;
    for(size_t i=0;i<8;++i) if((o.axis_mask&o.range_mask)&(1u<<i))
        if(o.maximum[i]<=o.minimum[i] || o.sample.axes[i]<o.minimum[i] || o.sample.axes[i]>o.maximum[i]) return false;
    for(size_t i=0;i<4;++i) if(o.hat_mask&(1u<<i))
        if(o.sample.hats[i]!=-1 && (o.sample.hats[i]<0 || o.sample.hats[i]>=36000)) return false;
    return true;
}
// Explicit selection never silently becomes another device, including after a reconnect/host replacement.
enum class SelectionMode { Saved, Explicit, Unavailable };
class DeviceSelection {
    SelectionMode mode_{SelectionMode::Saved};
    uintptr_t host_{};
    std::optional<AcceptedObservation> selected_;
public:
    SelectionMode mode() const { return mode_; }
    const std::optional<AcceptedObservation>& selected() const { return selected_; }
    void select_saved() { mode_=SelectionMode::Saved; selected_.reset(); }
    bool select_device(uintptr_t host, const AcceptedObservation& o, double now) {
        if(!host || !fresh(o,now) || !nonzero_guid(o.instance) || !nonzero_guid(o.product)) return false;
        host_=host;selected_=o;mode_=SelectionMode::Explicit;return true;
    }
    void reconcile(uintptr_t host, const std::vector<AcceptedObservation>& live, double now) {
        if(mode_==SelectionMode::Explicit) {
            bool found=host && host==host_ && selected_ && std::any_of(live.begin(),live.end(),[&](const auto& o){return same_identity(*selected_,o)&&fresh(o,now);});
            if(!found) mode_=SelectionMode::Unavailable;
        }
        host_=host;
    }
};
class CaptureFlow {
    ActionKindPolicy policy_;
    std::map<std::string,AcceptedObservation> devices_, watermarks_;
    std::optional<AcceptedObservation> chosen_, baseline_, previous_;
    std::optional<CaptureResult> result_;
    std::string action_, message_;
    double start_{}, deadline_{}, result_time_{};
public:
    explicit CaptureFlow(ActionKindPolicy policy):policy_(std::move(policy)) {
        if(!policy_) throw std::invalid_argument("An action/kind policy is required.");
    }
    const std::string& action() const { return action_; }
    const std::string& message() const { return message_; }
    std::vector<AcceptedObservation> devices(double now) const {
        std::vector<AcceptedObservation> rows;
        for(const auto& p:devices_) if(fresh(p.second,now)) rows.push_back(p.second);
        return rows;
    }
    void cancel(const std::string& reason="Binding canceled. The prior draft was kept.") {
        action_.clear();chosen_.reset();baseline_.reset();previous_.reset();result_.reset();message_=reason;
    }
    void remove(const std::string& instance,const std::string& why) {
        devices_.erase(instance);
        if(chosen_ && chosen_->instance==instance) cancel(why);
    }
    void reader_stopped() { devices_.clear();cancel("Capture reader stopped. The prior draft was kept."); }
    void refresh(double now,bool can_edit) {
        if(result_ && (!can_edit || !std::isfinite(now) || now<result_time_ || now-result_time_>0.25)) result_.reset();
        if(action_.empty()) return;
        auto it=chosen_?devices_.find(chosen_->instance):devices_.end();
        if(!can_edit) cancel("Capture canceled because the settings lost eligibility. The prior draft was kept.");
        else if(!std::isfinite(now) || it==devices_.end() || !fresh(it->second,now) || !same_identity(*chosen_,it->second))
            cancel("Capture device disconnected or changed. The prior draft was kept.");
        else if(now>=deadline_) cancel("Capture timed out. The prior draft was kept.");
    }
    bool begin(const std::string& action,const std::string& instance,double now,bool can_edit) {
        cancel();
        auto it=devices_.find(instance);
        if(!can_edit || it==devices_.end() || !fresh(it->second,now) || shapeOf(action)==Shape::Unknown) {
            message_="Choose a current capture device while settings are editable.";return false;
        }
        bool any=policy_(action,Kind::Axis)||policy_(action,Kind::Button)||policy_(action,Kind::Hat);
        if(!any) {message_="This action is unavailable in the current consumer.";return false;}
        if(shapeOf(action)==Shape::Centred) {
            bool usable=false;
            for(size_t i=0;i<8;++i) if((it->second.axis_mask&it->second.range_mask)&(1u<<i))
                usable=usable || (int64_t(it->second.maximum[i])-it->second.minimum[i]>=64);
            if(!usable) {message_="This device has no qualified steering axis range; use standalone setup.";return false;}
        }
        action_=action;chosen_=it->second;start_=now;deadline_=now+(shapeOf(action)==Shape::Digital?10:11);
        message_="Release buttons and hats. Keep an axis released or centered for one second, then move one control.";
        return true;
    }
    bool observe(const AcceptedObservation& o,double now,bool can_edit) {
        if(!nonzero_guid(o.instance)||!nonzero_guid(o.product)||o.identity_key.empty()||o.identity_key.size()>32768||!valid_utf8(o.name)||!valid_utf8(o.identity_key)||!o.epoch||!o.sequence||!fresh(o,now)||!ranges_valid(o)) {
            remove(o.instance,"Capture refused an invalid or stale observation. The prior draft was kept.");return false;
        }
        auto old=watermarks_.find(o.instance);
        if(old!=watermarks_.end()) {
            if(chosen_ && chosen_->instance==o.instance && !same_identity(*chosen_,o)) cancel("Capture device changed. The prior draft was kept.");
            if(o.epoch<old->second.epoch || (o.epoch==old->second.epoch&&o.sequence<=old->second.sequence)) {refresh(now,can_edit);return false;}
            if(o.epoch==old->second.epoch && (o.product!=old->second.product || o.identity_key!=old->second.identity_key ||
                o.axis_mask!=old->second.axis_mask || o.range_mask!=old->second.range_mask || o.hat_mask!=old->second.hat_mask ||
                o.button_mask!=old->second.button_mask || o.minimum!=old->second.minimum || o.maximum!=old->second.maximum)) {
                remove(o.instance,"Capture metadata changed without a new epoch. The prior draft was kept.");return false;
            }
        } else if(watermarks_.size()>=64) {message_="Capture device table exceeds its bound.";return false;}
        watermarks_[o.instance]=o;devices_[o.instance]=o;refresh(now,can_edit);
        if(action_.empty()||!chosen_||chosen_->instance!=o.instance) return true;
        bool analog=shapeOf(action_)!=Shape::Digital;
        if(analog&&now-start_<1) return true;
        if(!baseline_) {baseline_=o;previous_=o;message_="Listening for one control; held buttons must be released first.";return true;}
        struct Change {Kind kind;int index;int direction;int32_t rest;};
        std::vector<Change> changes;
        if(analog) for(size_t i=0;i<8;++i) if((o.axis_mask&o.range_mask)&(1u<<i)) {
            int64_t delta=int64_t(o.sample.axes[i])-baseline_->sample.axes[i];
            int64_t threshold=std::max<int64_t>(1,(int64_t(o.maximum[i])-o.minimum[i])*12000/65535);
            if(std::abs(delta)>threshold) changes.push_back({Kind::Axis,static_cast<int>(i),delta>0?1:-1,baseline_->sample.axes[i]});
        }
        for(size_t i=0;i<128;++i) if(o.button_mask[i]&&!previous_->sample.buttons[i]&&o.sample.buttons[i])
            changes.push_back({Kind::Button,static_cast<int>(i),1,0});
        for(size_t i=0;i<4;++i) if((o.hat_mask&(1u<<i))&&previous_->sample.hats[i]<0&&o.sample.hats[i]>=0)
            changes.push_back({Kind::Hat,static_cast<int>(i),o.sample.hats[i],-1});
        previous_=o;
        if(changes.size()!=1) return true;
        auto c=changes.front();
        if(!policy_(action_,c.kind)) {cancel("That control kind is unsupported for this action. The prior draft was kept.");return true;}
        Binding b;b.kind=c.kind;b.index=c.index;b.angle=c.kind==Kind::Hat?c.direction:-1;
        b.dev=o.instance;b.prod=o.product;b.name=o.name;
        if(c.kind==Kind::Axis) {b.min=o.minimum[static_cast<size_t>(c.index)];b.max=o.maximum[static_cast<size_t>(c.index)];b.rest=c.rest;b.travel=c.direction;b.hasRange=b.hasRest=true;}
        Binding checked;std::string why;
        if(!parse(format(b),checked,why,1)||!(why=fits(action_,checked)).empty()) {cancel("Capture refused: "+why+". The prior draft was kept.");return true;}
        CaptureResult captured{action_,checked};cancel();result_=std::move(captured);result_time_=now;
        message_="A binding candidate is ready for draft validation. Active input is unchanged.";return true;
    }
    std::optional<CaptureResult> take_result(double now,bool can_edit) {refresh(now,can_edit);auto out=std::move(result_);result_.reset();return out;}
};

inline bool valid_utf8(const std::string& text) {
    for(size_t i=0;i<text.size();) {
        auto c=static_cast<unsigned char>(text[i++]);
        if(c<128)continue;
        uint32_t value{};size_t count{};uint32_t least{};
        if(c>=0xC2&&c<=0xDF){value=c&31u;count=1;least=0x80;}
        else if(c>=0xE0&&c<=0xEF){value=c&15u;count=2;least=0x800;}
        else if(c>=0xF0&&c<=0xF4){value=c&7u;count=3;least=0x10000;}else return false;
        if(count>text.size()-i)return false;
        for(size_t n=0;n<count;++n){auto part=static_cast<unsigned char>(text[i++]);if((part&0xC0)!=0x80)return false;value=(value<<6)|(part&63u);}
        if(value<least||value>0x10FFFF||(value>=0xD800&&value<=0xDFFF))return false;
    }
    return true;
}

// Strict editing envelope around the existing canonical binding parser. Unknown lines are retained verbatim.
class ProfileDraft {
    struct Line {std::string body,eol;};
    struct Document {
        std::string bom;
        std::vector<Line> lines;
        std::map<std::string,size_t> positions;
        std::map<std::string,Binding> bindings;
        size_t end{};
        std::string eol{"\n"};
    };
    std::string active_,saved_;
    ActionKindPolicy policy_;
    bool require_steering_{};
    std::map<std::string,std::optional<Binding>> changes_;
    static std::string lower(std::string s) {for(auto& c:s)c=detail::lower(c);return s;}
    static Document document(const std::string& text) {
        if(text.empty()||text.size()>1024*1024||text.find('\0')!=std::string::npos||!valid_utf8(text)) throw std::invalid_argument("Invalid shared profile size or encoding.");
        Document d;size_t at=0;
        if(text.compare(0,3,"\xEF\xBB\xBF")==0){d.bom=text.substr(0,3);at=3;}
        bool first_eol=true;
        while(at<text.size()) {size_t n=text.find('\n',at);size_t stop=n==std::string::npos?text.size():n;
            bool cr=stop>at&&text[stop-1]=='\r';std::string eol=n==std::string::npos?"":cr?"\r\n":"\n";
            d.lines.push_back({text.substr(at,stop-at-(cr?1:0)),eol});
            if(first_eol&&!eol.empty()){d.eol=eol;first_eol=false;}if(n==std::string::npos)break;at=n+1;}
        bool inside=false,found=false;std::set<std::string> keys;std::string schema,transmission="Automatic";size_t bytes=0;
        d.end=d.lines.size();
        for(size_t i=0;i<d.lines.size();++i) {
            std::string t=trim(d.lines[i].body);
            if(!t.empty()&&t.front()=='[') {
                if(t.back()!=']')throw std::invalid_argument("Invalid shared profile section header.");
                if(inside)d.end=i;
                inside=lower(t)=="[controls]";
                if(inside&&found)throw std::invalid_argument("Repeated Controls section.");
                if(inside){found=true;bytes=d.lines[i].body.size()+d.lines[i].eol.size();}
                continue;
            }
            if(!inside)continue;
            bytes+=d.lines[i].body.size()+d.lines[i].eol.size();
            if(bytes>64*1024)throw std::invalid_argument("Controls section exceeds 64 KiB.");
            if(t.empty()||t.front()==';'||t.front()=='#')continue;
            size_t eq=t.find('=');if(eq==std::string::npos||eq==0)throw std::invalid_argument("Invalid Controls key/value.");
            auto key=trim(t.substr(0,eq)),value=trim(t.substr(eq+1));
            if(!keys.insert(lower(key)).second)throw std::invalid_argument("Repeated Controls key.");
            d.positions[key]=i;
            if(key=="Schema")schema=value;
            else if(key=="Transmission")transmission=value;
            else if(shapeOf(key)!=Shape::Unknown && !value.empty()) {
                Binding b;std::string why;
                if(!parse(value,b,why,1)||!fits(key,b).empty()||!nonzero_guid(b.dev)||!nonzero_guid(b.prod)||format(b)!=value)
                    throw std::invalid_argument("Cannot losslessly import binding: "+key);
                d.bindings[key]=b;
            }
        }
        if(!found||schema!="1")throw std::invalid_argument("This editor requires Controls Schema 1.");
        if(transmission!="Automatic"&&transmission!="Sequential"&&transmission!="H-pattern")throw std::invalid_argument("Unsupported transmission metadata.");
        return d;
    }
public:
    explicit ProfileDraft(std::string bytes,ActionKindPolicy policy,bool require_steering=false):active_(bytes),saved_(std::move(bytes)),policy_(std::move(policy)),require_steering_(require_steering) {
        if(!policy_)throw std::invalid_argument("An action/kind policy is required.");(void)document(saved_);
    }
    const std::string& active_bytes() const {return active_;}
    const std::string& saved_bytes() const {return saved_;}
    bool has_draft() const {return !changes_.empty();}
    bool saved_pending_restart() const {return saved_!=active_;}
    std::optional<Binding> binding(const std::string& action) const {
        auto changed=changes_.find(action);if(changed!=changes_.end())return changed->second;
        auto d=document(saved_);auto it=d.bindings.find(action);return it==d.bindings.end()?std::nullopt:std::optional<Binding>(it->second);
    }
    bool set(const std::string& action,std::optional<Binding> value,std::string& reason) {
        reason.clear();
        bool editable=policy_(action,Kind::Axis)||policy_(action,Kind::Button)||policy_(action,Kind::Hat);
        if(shapeOf(action)==Shape::Unknown||!editable){reason="This action is not editable.";return false;}
        if(value) {
            Binding parsed;std::string why;
            if(!policy_(action,value->kind)||!parse(format(*value),parsed,why,1)||!fits(action,parsed).empty()||!nonzero_guid(parsed.dev)||!nonzero_guid(parsed.prod)) {
                reason="The captured binding is unsupported or invalid.";return false;
            }
            value=parsed;
        }
        auto d=document(saved_);auto old=d.bindings.find(action);
        if((!value&&old==d.bindings.end()&&d.positions.count(action))||(value&&old!=d.bindings.end()&&format(*value)==format(old->second)))changes_.erase(action);
        else changes_[action]=std::move(value);
        return true;
    }
    void discard(){changes_.clear();}
    std::string preview(const std::string& revision) const {
        if(revision.empty()||revision.size()>128||std::any_of(revision.begin(),revision.end(),[](unsigned char c){return c<33||c>126;}))
            throw std::invalid_argument("Use a bounded printable ASCII save revision.");
        auto d=document(saved_);std::map<size_t,std::optional<std::string>> replacements;std::vector<std::string> added;
        for(const auto& p:changes_) {
            auto line=std::optional<std::string>(p.first+" = "+(p.second?format(*p.second):""));
            auto at=d.positions.find(p.first);
            if(at!=d.positions.end())replacements[at->second]=line;else if(line)added.push_back(*line);
        }
        auto rev=d.positions.find("Revision");
        if(rev!=d.positions.end())replacements[rev->second]="Revision = "+revision;else added.push_back("Revision = "+revision);
        std::vector<Line> output;
        for(size_t i=0;i<=d.lines.size();++i) {
            if(i==d.end&&!added.empty()) {
                bool final_empty=i==d.lines.size()&&!d.lines.empty()&&d.lines.back().eol.empty();
                if(!output.empty()&&output.back().eol.empty())output.back().eol=d.eol;
                for(size_t n=0;n<added.size();++n)output.push_back({added[n],final_empty&&n+1==added.size()?"":d.eol});
            }
            if(i==d.lines.size())break;
            auto changed=replacements.find(i);
            if(changed==replacements.end())output.push_back(d.lines[i]);
            else if(changed->second)output.push_back({*changed->second,d.lines[i].eol});
        }
        std::string text=d.bom;for(const auto& l:output)text+=l.body+l.eol;
        auto checked=document(text);
        if(require_steering_&&!checked.bindings.count("steer"))throw std::invalid_argument("A usable steering binding is required.");
        return text;
    }
    // Called only after the store checked the exact old bytes and replaced/read back the prepared bytes.
    bool accept_saved(const std::string& expected,const std::string& next,std::string& reason) {
        if(expected!=saved_){reason="Draft source changed; restart before editing again.";return false;}
        (void)document(next);saved_=next;changes_.clear();reason="Bindings saved. Restart to use them; active input is unchanged.";return true;
    }
};
}}} // namespace dbce::controls::edit
