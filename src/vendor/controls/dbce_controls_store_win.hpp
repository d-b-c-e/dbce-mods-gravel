// Windows explicit Save for ProfileDraft. No reader, enumeration, force or automatic writes.
// Same guarded replace/backup/readback sequence as managed ControlProfileDraft; active bytes stay immutable.
#pragma once
#ifndef _WIN32
#error This store is Windows only; use dbce_binding_editor.hpp for portable capture/draft logic.
#endif
#include "dbce_binding_editor.hpp"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <atomic>
#include <filesystem>
#include <limits>

namespace dbce { namespace controls { namespace edit {
struct SaveResult {bool saved{}, replaced{};std::filesystem::path backup;std::string message;};
namespace store_detail {
class Handle {
    HANDLE h_{INVALID_HANDLE_VALUE};
public:
    explicit Handle(HANDLE h):h_(h){}
    Handle(const Handle&)=delete;Handle& operator=(const Handle&)=delete;
    ~Handle(){if(h_!=INVALID_HANDLE_VALUE)CloseHandle(h_);}
    HANDLE get()const{return h_;}
    bool valid()const{return h_!=INVALID_HANDLE_VALUE;}
    void close(){if(valid()){CloseHandle(h_);h_=INVALID_HANDLE_VALUE;}}
};
inline void fail(const char* text){throw std::runtime_error(text);}
inline BY_HANDLE_FILE_INFORMATION info(HANDLE h) {
    BY_HANDLE_FILE_INFORMATION out{};
    if(!GetFileInformationByHandle(h,&out)||(out.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT))||out.nNumberOfLinks!=1)
        fail("Choose a regular unlinked controls file.");
    return out;
}
inline bool same_file(const BY_HANDLE_FILE_INFORMATION& a,const BY_HANDLE_FILE_INFORMATION& b) {
    return a.dwVolumeSerialNumber==b.dwVolumeSerialNumber&&a.nFileIndexHigh==b.nFileIndexHigh&&a.nFileIndexLow==b.nFileIndexLow;
}
inline void regular_parents(const std::filesystem::path& path) {
    for(auto p=path.parent_path();!p.empty();) {
        DWORD attr=GetFileAttributesW(p.c_str());
        if(attr==INVALID_FILE_ATTRIBUTES||!(attr&FILE_ATTRIBUTE_DIRECTORY)||(attr&FILE_ATTRIBUTE_REPARSE_POINT))fail("Controls parent folder is missing or linked.");
        auto parent=p.parent_path();if(parent==p)break;p=parent;
    }
}
inline std::string read(HANDLE h) {
    LARGE_INTEGER size{};if(!GetFileSizeEx(h,&size)||size.QuadPart<1||size.QuadPart>1024*1024)fail("Controls file exceeds the supported size.");
    LARGE_INTEGER zero{};if(!SetFilePointerEx(h,zero,nullptr,FILE_BEGIN))fail("Cannot seek controls.");
    std::string bytes(static_cast<size_t>(size.QuadPart),'\0');DWORD got{};
    if(!ReadFile(h,bytes.data(),static_cast<DWORD>(bytes.size()),&got,nullptr)||got!=bytes.size())fail("Cannot read complete controls.");
    char extra{};if(!ReadFile(h,&extra,1,&got,nullptr)||got)fail("Controls changed size while reading.");
    return bytes;
}
inline Handle open_read(const std::filesystem::path& path) {
    return Handle(CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr));
}
inline std::string read_path(const std::filesystem::path& path) {
    auto h=open_read(path);if(!h.valid())fail("Cannot open controls for verification.");(void)info(h.get());return read(h.get());
}
inline std::wstring unique_suffix() {
    static std::atomic<unsigned long long> serial{0};LARGE_INTEGER now{};
    if(!QueryPerformanceCounter(&now))fail("Cannot generate an explicit-save name.");
    return std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(now.QuadPart)+L"-"+std::to_wstring(++serial);
}
inline bool matches(const std::filesystem::path& path,const BY_HANDLE_FILE_INFORMATION& expected) {
    try{auto h=open_read(path);return h.valid()&&same_file(info(h.get()),expected);}catch(...){return false;}
}
}
inline SaveResult save(ProfileDraft& draft,const std::filesystem::path& path,const std::string& revision) {
    SaveResult result;result.message="No binding changes to save.";
    if(!draft.has_draft())return result;
    std::filesystem::path temp;std::optional<BY_HANDLE_FILE_INFORMATION> temp_identity;
    try {
        if(!path.is_absolute()||path.lexically_normal()!=path)store_detail::fail("Use the fixed absolute normalized controls path.");
        store_detail::regular_parents(path);
        auto source=store_detail::open_read(path);if(!source.valid())store_detail::fail("The existing controls file cannot be opened.");
        auto original_identity=store_detail::info(source.get());
        std::string expected=draft.saved_bytes();
        if(store_detail::read(source.get())!=expected)store_detail::fail("Controls changed outside the editor. Restart to reread them; draft kept.");
        std::string next=draft.preview(revision);
        auto suffix=store_detail::unique_suffix();temp=path;temp+=L".bindings-"+suffix+L".tmp";
        result.backup=path;result.backup+=L".bindings-backup-"+suffix;
        if(GetFileAttributesW(result.backup.c_str())!=INVALID_FILE_ATTRIBUTES)store_detail::fail("Save backup name already exists.");
        store_detail::Handle output(CreateFileW(temp.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr));
        if(!output.valid())store_detail::fail("Cannot create a new controls temporary file.");
        temp_identity=store_detail::info(output.get());DWORD written{};
        if(!WriteFile(output.get(),next.data(),static_cast<DWORD>(next.size()),&written,nullptr)||written!=next.size()||!FlushFileBuffers(output.get()))
            store_detail::fail("Cannot durably write the binding draft.");
        output.close();
        store_detail::regular_parents(path);
        if(!store_detail::matches(path,original_identity)||store_detail::read(source.get())!=expected||store_detail::read_path(path)!=expected)
            store_detail::fail("Controls changed before Save. Nothing replaced; draft kept.");
        if(!temp_identity||!store_detail::matches(temp,*temp_identity)||store_detail::read_path(temp)!=next)
            store_detail::fail("Binding temporary file changed. Nothing replaced; draft kept.");
        if(!ReplaceFileW(path.c_str(),temp.c_str(),result.backup.c_str(),0,nullptr,nullptr))store_detail::fail("Controls replacement failed. Draft kept.");
        result.replaced=true;
        if(store_detail::read_path(path)!=next||store_detail::read_path(result.backup)!=expected)
            store_detail::fail("Controls replacement did not verify. Backup and draft kept; restart before editing again.");
        result.saved=draft.accept_saved(expected,next,result.message);
    } catch(const std::exception& e) {result.message=std::string("Bindings not saved: ")+e.what();}
    // Only remove our same retained temporary-file identity; preserve any replacement or ambiguous path.
    if(temp_identity&&store_detail::matches(temp,*temp_identity))DeleteFileW(temp.c_str());
    return result;
}
}}} // namespace dbce::controls::edit
