#include "../../src/recording.cpp"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>

Config g_cfg; InputState g_input; FfbState g_ffb; FmodState g_fmod; Ue4State g_ue4;
void logf(const char*, ...) {}

int main(int argc, char** argv) {
    assert(argc==3); std::string mode=argv[1];
    std::wstring root(argv[2],argv[2]+strlen(argv[2]));
    SetEnvironmentVariableW(L"LOCALAPPDATA",root.c_str());
    const std::wstring id=L"abcdef0123456789abcdef0123456789";
    auto result=root+L"/Dbce/StagePlayback/Gravel/"+id;
    std::filesystem::create_directories(result);
    const auto req=beside(L"milestone_recording.request.ini");
    if(mode!="none") {
        FILETIME ft; GetSystemTimeAsFileTime(&ft); ULARGE_INTEGER stamp; stamp.LowPart=ft.dwLowDateTime; stamp.HighPart=ft.dwHighDateTime;
        uint64_t expires=mode=="expired" ? 1 : stamp.QuadPart+600000000;
        std::ofstream out(req.c_str()); out<<"[session]\nid=abcdef0123456789abcdef0123456789\nseconds=1\nexpiresFileTimeUtc="<<expires<<"\n"; out.close();
    }
    recordingInit();
    if(mode=="none") { assert(!recordingMuted()); std::cout<<"PASS no request\n"; return 0; }
    assert(recordingMuted());
    if(mode=="expired") { assert(g_finished && std::filesystem::exists(req)); std::filesystem::remove(req); std::cout<<"PASS expired fail closed\n"; return 0; }
    assert(!std::filesystem::exists(req) && std::filesystem::exists(result+L"/claimed-request.ini"));
    recordingPoll(); // no observations: no invented force/audio values
    g_ue4.live=true; g_ue4.available=1; g_ue4.rpm=0;
    g_ffb.constant=.61f; g_ffb.observedAt=GetTickCount64(); g_ffb.summaryAt[0]=GetTickCount64();
    g_fmod.rpm=0; g_fmod.observedAt[0]=GetTickCount64();
    Sleep(2); recordingPoll();
    // Advance only the test clock; production duration is real monotonic time.
    g_begin.QuadPart -= g_frequency.QuadPart*2;
    recordingPoll();
    assert(g_finished && g_writer->completed() && recordingMuted());
    std::cout<<"PASS actual request claim, availability and bounded capture: "<<std::string(result.begin(),result.end())<<"\n";
}
