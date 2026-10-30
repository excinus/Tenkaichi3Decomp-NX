// The Switch setup's window (docs/port/switch_plan.md, "The setup"): Tenkaichi3Decomp-switch-setup, in each release's
// zip (Windows, Linux, macOS) beside the Switch's files. The player chooses where the game data comes from (the disc image, or the PC version installed
// before) and the SD card, presses Install, and watches the steps (work.cpp). The look and the drawing are the PC
// setup's (port/setup/setup.cpp: SDL3 and Dear ImGui, SDL's GPU layer or OpenGL).
//
//   build:  port/switch/setup/build.sh win|linux|mac  ->  port/build/Tenkaichi3Decomp-switch-<system>.zip
//   test:   Tenkaichi3Decomp-switch-setup --install (--iso <file> [--eu <file>] | --pc <folder>) --sd <folder>: the
//           same work without a window, printing the event lines; BT3_NXSETUP_FILES=<folder> stands for the setup's
//           own folder (the Switch's files), BT3_NXSETUP_NOCHECK=1 skips the disc's checksums (a made-up image)
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <mutex>
#include <string>
#include <vector>
#include <SDL3/SDL.h>
#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_sdlgpu3.h"
#include "imgui_impl_opengl3.h"
#include "imgui_impl_opengl3_loader.h"
#include "../../setup/native.h"
#include "work.h"

#define TITLE "Tenkaichi3Decomp for Nintendo Switch"

enum Page { PAGE_PICK, PAGE_RUN, PAGE_DONE, PAGE_FAILED };
enum State { PENDING, RUNNING, OK, SKIPPED, FAILED };
struct StepRow { std::string title, detail; State state = PENDING; Uint64 start = 0, end = 0; };

static const struct { const char *title, *help; } kHelp[] = {
    {"Switch files", "The game and its launcher, onto the SD card"},
    {"Disc image", "Your disc image is read and checked"},
    {"PC installation", "Your PC version's game data is found"},
    {"Game data", "Copied to the SD card, the voices as one file (a few minutes)"},
    {"European languages", "Spanish, German, French and Italian, plus the opening soundtrack"}};

static SDL_Window *sWindow;
static SDL_GPUDevice *sDevice;
static SDL_GLContext sGl;
static bool sUseGl;
static Page sPage = PAGE_PICK;
static int sFrom;                                  // 0: the disc image, 1: the PC version
static char sIso[1024], sIsoEu[1024], sPc[1024], sSd[1024];
static std::string sError;
static bool sDone;
static std::vector<StepRow> sRows;
static std::mutex sDialogLock;
static std::string sDialogResult;
static int sDialogFor = -1;                        // the field the dialog fills: 0 iso, 1 eu, 2 pc, 3 sd
static bool sQuit, sRunning, sHasFiles;
static Uint64 sRunStart, sRunEnd;
static const ImVec4 kAccent(0.90f, 0.0f, 0.07f, 1.0f), kGreen(0.36f, 0.80f, 0.45f, 1.0f), kRed(0.93f, 0.36f, 0.33f, 1.0f),
    kDim(0.60f, 0.63f, 0.70f, 1.0f), kWarn(0.96f, 0.72f, 0.30f, 1.0f);

static bool file_exists(const char *path) {
    SDL_PathInfo info;
    return path[0] != '\0' && SDL_GetPathInfo(path, &info) && info.type == SDL_PATHTYPE_FILE;
}

static const char *step_help(const std::string &title) {
    for (auto &h : kHelp) {
        if (title == h.title) {
            return h.help;
        }
    }
    return "";
}

// the PC setup's style, in the Switch's red
static void style() {
    ImGuiStyle &st = ImGui::GetStyle();
    ImGui::StyleColorsDark();
    st.WindowRounding = 0.0f;
    st.FrameRounding = 6.0f;
    st.GrabRounding = 6.0f;
    st.WindowPadding = ImVec2(28.0f, 22.0f);
    st.FramePadding = ImVec2(12.0f, 8.0f);
    st.ItemSpacing = ImVec2(10.0f, 10.0f);
    st.WindowBorderSize = 0.0f;
    ImVec4 *c = st.Colors;
    c[ImGuiCol_WindowBg] = ImVec4(0.07f, 0.08f, 0.11f, 1.0f);
    c[ImGuiCol_FrameBg] = ImVec4(0.15f, 0.17f, 0.23f, 1.0f);
    c[ImGuiCol_FrameBgHovered] = ImVec4(0.21f, 0.24f, 0.32f, 1.0f);
    c[ImGuiCol_FrameBgActive] = ImVec4(0.26f, 0.29f, 0.38f, 1.0f);
    c[ImGuiCol_Button] = ImVec4(0.17f, 0.20f, 0.27f, 1.0f);
    c[ImGuiCol_ButtonHovered] = ImVec4(0.90f, 0.0f, 0.07f, 0.55f);
    c[ImGuiCol_ButtonActive] = kAccent;
    c[ImGuiCol_CheckMark] = ImVec4(1.0f, 0.35f, 0.38f, 1.0f);
    c[ImGuiCol_PlotHistogram] = kAccent;
    c[ImGuiCol_TextSelectedBg] = ImVec4(0.90f, 0.0f, 0.07f, 0.35f);
}

// ------------------------------------------------------------------------------------------------------ the work

static void reset_rows() {
    std::vector<std::string> titles = {"Switch files", sFrom ? "PC installation" : "Disc image", "Game data"};
    if (!sFrom && sIsoEu[0] != '\0') {
        titles.push_back("European languages");
    }
    sRows.assign(titles.size(), StepRow());
    for (size_t i = 0; i < titles.size(); i++) {
        sRows[i].title = titles[i];
    }
}

static void start_install() {
    NxJob job;
    job.fromPc = sFrom == 1;
    job.iso = sIso;
    job.eu = sIsoEu;
    job.pc = sPc;
    job.sd = sSd;
    reset_rows();
    sError.clear();
    sDone = false;
    sRunStart = SDL_GetTicks();
    sRunning = true;
    NxSetup_Start(job);
    sPage = PAGE_RUN;
}

static StepRow *current() {
    for (auto &r : sRows) {
        if (r.state == RUNNING) {
            return &r;
        }
    }
    return NULL;
}

static std::string unescape(const std::string &s) {
    std::string out;
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == '\\' && i + 1 < s.size() && s[i + 1] == 'n') {
            out += '\n';
            i++;
        } else {
            out += s[i];
        }
    }
    return out;
}

static void event(const std::string &line) {
    if (line.empty() || line[0] != '@') {
        return;
    }
    size_t sp = line.find(' ');
    std::string kind = line.substr(1, sp == std::string::npos ? std::string::npos : sp - 1);
    std::string text = sp == std::string::npos ? "" : unescape(line.substr(sp + 1));
    StepRow *cur = current();

    if (kind == "step") {
        int i = atoi(text.c_str());
        if (i >= 1 && i <= (int)sRows.size()) {
            sRows[i - 1].state = RUNNING;
            sRows[i - 1].start = SDL_GetTicks();
            sRows[i - 1].detail.clear();
        }
    } else if (cur != NULL && kind == "note") {
        cur->detail = text;
    } else if (cur != NULL && (kind == "ok" || kind == "skip" || kind == "fail")) {
        cur->state = kind == "ok" ? OK : kind == "skip" ? SKIPPED : FAILED;
        cur->end = SDL_GetTicks();
        cur->detail = kind == "fail" ? "" : text;
    } else if (kind == "stopped") {
        sError = text;
    } else if (kind == "done") {
        sDone = true;
    }
}

static void pump_install() {
    if (!sRunning) {
        return;
    }
    std::string line;
    bool ended = !Native_Running(); // asked before the queue is emptied: nothing is left behind
    while (Native_Poll(line)) {
        event(line);
    }
    if (ended) {
        sRunning = false;
        sRunEnd = SDL_GetTicks();
        for (auto &r : sRows) {
            if (r.state == RUNNING) {
                r.state = FAILED;
                r.end = sRunEnd;
            }
        }
        if (sDone) {
            sPage = PAGE_DONE;
        } else {
            if (sError.empty()) {
                sError = "The setup stopped unexpectedly.";
            }
            sPage = PAGE_FAILED;
        }
    }
}

// --------------------------------------------------------------------------------------------------------- pages

static void SDLCALL dialog_done(void *userdata, const char *const *files, int filter) {
    (void)filter;
    std::lock_guard<std::mutex> lock(sDialogLock);
    if (files != NULL && files[0] != NULL) {
        sDialogResult = files[0];
        sDialogFor = (int)(intptr_t)userdata;
    }
}

static void heading(const char *title, const char *sub) {
    ImGui::PushFont(NULL, 30.0f);
    ImGui::TextUnformatted(title);
    ImGui::PopFont();
    ImGui::PushStyleColor(ImGuiCol_Text, kDim);
    ImGui::TextWrapped("%s", sub);
    ImGui::PopStyleColor();
    ImGui::Dummy(ImVec2(0.0f, 6.0f));
}

static bool big_button(const char *label, bool primary, bool enabled = true) {
    bool pressed;
    if (primary) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.90f, 0.0f, 0.07f, 0.90f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1.0f, 0.18f, 0.24f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
    }
    ImGui::BeginDisabled(!enabled);
    pressed = ImGui::Button(label, ImVec2(150.0f, 40.0f));
    ImGui::EndDisabled();
    if (primary) {
        ImGui::PopStyleColor(3);
    }
    return pressed;
}

static void bottom_row(int buttons) {
    ImVec2 avail = ImGui::GetContentRegionAvail();
    float w = buttons * 150.0f + (buttons - 1) * ImGui::GetStyle().ItemSpacing.x;
    ImGui::SetCursorPos(ImVec2(ImGui::GetCursorPosX() + avail.x - w, ImGui::GetCursorPosY() + avail.y - 40.0f));
}

// A path field with its Browse button: a file (iso) or a folder.
static void path_field(const char *label, const char *id, const char *hint, char *buf, size_t size, int which, bool folder) {
    static const SDL_DialogFileFilter filters[] = {{"PS2 disc image", "iso;bin;img"}, {"All files", "*"}};
    char button[48];
    ImGui::TextUnformatted(label);
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 130.0f);
    ImGui::InputTextWithHint(id, hint, buf, size);
    ImGui::SameLine();
    snprintf(button, sizeof(button), "Browse...%s", id);
    if (ImGui::Button(button, ImVec2(120.0f, 0.0f))) {
        if (folder) {
            SDL_ShowOpenFolderDialog(dialog_done, (void *)(intptr_t)which, sWindow, NULL, false);
        } else {
            SDL_ShowOpenFileDialog(dialog_done, (void *)(intptr_t)which, sWindow, filters, 2, NULL, false);
        }
    }
}

static void note(const ImVec4 &col, const char *text) {
    ImGui::PushStyleColor(ImGuiCol_Text, col);
    ImGui::TextWrapped("%s", text);
    ImGui::PopStyleColor();
}

static void page_pick() {
    bool ok = true;

    heading(TITLE, "This puts the game on your Switch's SD card, with the game's data from your own PlayStation 2 disc.");
    if (!sHasFiles) {
        note(kRed, "The game's files are not next to this program. Unpack the whole Tenkaichi3Decomp-switch zip into a folder, then start the setup from there.");
        ok = false;
    }
    ImGui::TextUnformatted("The game's data comes from");
    ImGui::RadioButton("my disc image", &sFrom, 0);
    ImGui::SameLine(0.0f, 24.0f);
    ImGui::RadioButton("my PC version of Tenkaichi3Decomp", &sFrom, 1);
    ImGui::Dummy(ImVec2(0.0f, 4.0f));
    if (sFrom == 0) {
        path_field("Disc image (USA release, SLUS-21678)", "##iso", "your .iso file: drop it on this window, or browse", sIso, sizeof(sIso), 0, false);
        if (sIso[0] != '\0' && !file_exists(sIso)) {
            note(kRed, "That file does not exist.");
            ok = false;
        } else if (sIso[0] == '\0') {
            ok = false;
        }
        path_field("European disc image (optional)", "##isoeu", "the European release (SLES-54945): Spanish, German, French, Italian",
                   sIsoEu, sizeof(sIsoEu), 1, false);
        if (sIsoEu[0] != '\0' && !file_exists(sIsoEu)) {
            note(kRed, "That file does not exist.");
            ok = false;
        }
    } else {
        path_field("PC version's folder", "##pc", "the folder of your PC installation of Tenkaichi3Decomp", sPc, sizeof(sPc), 2, true);
        if (sPc[0] != '\0' && !NxSetup_PcValid(sPc)) {
            note(kRed, "No game data there: choose the folder of your PC installation (the one with a gamedata folder in it).");
            ok = false;
        } else if (sPc[0] == '\0') {
            ok = false;
        }
    }
    ImGui::Dummy(ImVec2(0.0f, 4.0f));
#if defined(_WIN32)
    const char *sdHint = "the SD card's drive, such as E:\\";
#elif defined(__APPLE__)
    const char *sdHint = "the SD card, such as /Volumes/SWITCH SD";
#else
    const char *sdHint = "the SD card, such as /media/you/SWITCH SD";
#endif
    path_field("SD card", "##sd", sdHint, sSd, sizeof(sSd), 3, true);
    {
        bool warn = false;
        std::string text = NxSetup_CardNote(sSd, warn);
        if (!text.empty()) {
            note(warn ? kWarn : kDim, text.c_str());
        }
        if (sSd[0] == '\0' || (warn && text == "That folder does not exist.")) {
            ok = false;
        }
    }
    bottom_row(2);
    if (big_button("Close", false)) {
        sQuit = true;
    }
    ImGui::SameLine();
    if (big_button("Install", true, ok)) {
        start_install();
    }
}

static void state_icon(State st, float t) {
    ImDrawList *dl = ImGui::GetWindowDrawList();
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImVec2 c(p.x + 11.0f, p.y + 11.0f);

    if (st == OK || st == SKIPPED) {
        ImU32 col = ImGui::GetColorU32(kGreen);
        dl->AddCircle(c, 10.0f, col, 32, 1.6f);
        dl->AddLine(ImVec2(c.x - 5.0f, c.y), ImVec2(c.x - 1.5f, c.y + 4.0f), col, 2.0f);
        dl->AddLine(ImVec2(c.x - 1.5f, c.y + 4.0f), ImVec2(c.x + 5.5f, c.y - 4.0f), col, 2.0f);
    } else if (st == FAILED) {
        ImU32 col = ImGui::GetColorU32(kRed);
        dl->AddCircle(c, 10.0f, col, 32, 1.6f);
        dl->AddLine(ImVec2(c.x - 4.0f, c.y - 4.0f), ImVec2(c.x + 4.0f, c.y + 4.0f), col, 2.0f);
        dl->AddLine(ImVec2(c.x - 4.0f, c.y + 4.0f), ImVec2(c.x + 4.0f, c.y - 4.0f), col, 2.0f);
    } else if (st == RUNNING) {
        float a = t * 5.0f;
        dl->AddCircle(c, 10.0f, ImGui::GetColorU32(ImVec4(1.0f, 1.0f, 1.0f, 0.12f)), 32, 2.0f);
        dl->PathArcTo(c, 10.0f, a, a + 1.9f, 24);
        dl->PathStroke(ImGui::GetColorU32(kAccent), 0, 2.4f);
    } else {
        dl->AddCircle(c, 10.0f, ImGui::GetColorU32(ImVec4(1.0f, 1.0f, 1.0f, 0.18f)), 32, 1.4f);
    }
    ImGui::Dummy(ImVec2(22.0f, 22.0f));
}

static void step_list() {
    float t = (float)SDL_GetTicks() / 1000.0f;
    for (size_t i = 0; i < sRows.size(); i++) {
        StepRow &r = sRows[i];
        char clock[32] = "";
        state_icon(r.state, t);
        ImGui::SameLine(0.0f, 14.0f);
        ImGui::BeginGroup();
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(10.0f, 1.0f));
        ImGui::TextColored(r.state == PENDING ? kDim : ImGui::GetStyleColorVec4(ImGuiCol_Text), "%s", r.title.c_str());
        ImGui::TextColored(kDim, "%s", !r.detail.empty() ? r.detail.c_str() : step_help(r.title));
        ImGui::PopStyleVar();
        ImGui::EndGroup();
        if (r.state == RUNNING) {
            snprintf(clock, sizeof(clock), "%d s", (int)((SDL_GetTicks() - r.start) / 1000));
        } else if ((r.state == OK || r.state == FAILED) && r.end - r.start >= 2000) {
            snprintf(clock, sizeof(clock), "%d s", (int)((r.end - r.start) / 1000));
        }
        if (clock[0] != '\0') {
            ImGui::SameLine(ImGui::GetWindowWidth() - ImGui::GetStyle().WindowPadding.x - ImGui::CalcTextSize(clock).x);
            ImGui::TextColored(kDim, "%s", clock);
        }
    }
}

static void page_run() {
    int done = 0;
    heading("Installing", "Leave the SD card in until this is finished. If it is interrupted, Install continues from where it stopped.");
    step_list();
    for (auto &r : sRows) {
        done += r.state == OK || r.state == SKIPPED;
    }
    ImGui::Dummy(ImVec2(0.0f, 4.0f));
    ImGui::ProgressBar((float)done / (float)(sRows.empty() ? 1 : sRows.size()), ImVec2(-FLT_MIN, 8.0f), "");
    bottom_row(1);
    if (big_button("Cancel", false)) {
        Native_Cancel();
    }
}

static void page_done() {
    char sub[128];
    snprintf(sub, sizeof(sub), "The game is on the SD card.  (%d min %d s)", (int)((sRunEnd - sRunStart) / 60000), (int)((sRunEnd - sRunStart) / 1000 % 60));
    heading("Ready to play", sub);
    step_list();
    ImGui::Dummy(ImVec2(0.0f, 6.0f));
    ImGui::TextWrapped("1. Put the SD card back in the Switch.");
    ImGui::TextWrapped("2. Hold R while starting any game: the homebrew menu opens. Start Tenkaichi3Decomp there.");
    ImGui::Dummy(ImVec2(0.0f, 2.0f));
    note(kDim, "Recommended: put the game on the HOME menu with a forwarder made by sphaira. sys-clk is needed for full speed "
               "(the HATS pack has it). In the game, + and - together open the settings.");
    bottom_row(1);
    if (big_button("Close", true)) {
        sQuit = true;
    }
}

static void page_failed() {
    heading("Setup stopped", "Nothing on your PC was changed. What is already on the SD card is kept: Install continues from where it stopped.");
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.93f, 0.36f, 0.33f, 0.10f));
    ImGui::BeginChild("why", ImVec2(0.0f, 150.0f), ImGuiChildFlags_AlwaysUseWindowPadding);
    ImGui::TextWrapped("%s", sError.c_str());
    ImGui::EndChild();
    ImGui::PopStyleColor();
    bottom_row(2);
    if (big_button("Close", false)) {
        sQuit = true;
    }
    ImGui::SameLine();
    if (big_button("Back", true)) {
        sPage = PAGE_PICK;
    }
}

static void build_ui() {
    ImGuiIO &io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::Begin("setup", NULL, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
    switch (sPage) {
    case PAGE_PICK: page_pick(); break;
    case PAGE_RUN: page_run(); break;
    case PAGE_DONE: page_done(); break;
    case PAGE_FAILED: page_failed(); break;
    }
    ImGui::End();
}

// ---------------------------------------------------------------------------------------------------- the window

#ifdef _WIN32
extern "C" {
__declspec(dllimport) wchar_t *__stdcall GetCommandLineW(void);
__declspec(dllimport) wchar_t **__stdcall CommandLineToArgvW(const wchar_t *line, int *count);
__declspec(dllimport) int __stdcall WideCharToMultiByte(unsigned page, unsigned long flags, const wchar_t *wide, int wideLen, char *out, int outLen,
                                                        const char *def, int *usedDef);
__declspec(dllimport) void *__stdcall LocalFree(void *mem);
}

// the arguments in UTF-8 (as the PC setup: a path with letters outside the system's code page)
static char **utf8_args(int *count) {
    int n = 0;
    wchar_t **wide = CommandLineToArgvW(GetCommandLineW(), &n);
    char **out = wide != NULL ? (char **)SDL_calloc((size_t)n + 1, sizeof(char *)) : NULL;
    for (int i = 0; out != NULL && i < n; i++) {
        int len = WideCharToMultiByte(65001, 0, wide[i], -1, NULL, 0, NULL, NULL);
        out[i] = (char *)SDL_calloc((size_t)(len > 0 ? len : 1), 1);
        if (out[i] == NULL) {
            out = NULL;
            break;
        }
        if (len > 0) {
            WideCharToMultiByte(65001, 0, wide[i], -1, out[i], len, NULL, NULL);
        }
    }
    if (wide != NULL) {
        LocalFree(wide);
    }
    *count = n;
    return out;
}
#endif

// Without a window: --install (--iso <file> [--eu <file>] | --pc <folder>) --sd <folder>; prints the event lines.
static int install_here(int argc, char **argv) {
    NxJob job;
    std::string line;
    bool ok = false;
    job.fromPc = false;
    for (int i = 2; i + 1 < argc; i += 2) {
        std::string k = argv[i];
        if (k == "--iso") {
            job.iso = argv[i + 1];
        } else if (k == "--eu") {
            job.eu = argv[i + 1];
        } else if (k == "--pc") {
            job.pc = argv[i + 1];
            job.fromPc = true;
        } else if (k == "--sd") {
            job.sd = argv[i + 1];
        }
    }
    if (job.sd.empty() || (job.iso.empty() && job.pc.empty())) {
        fprintf(stderr, "usage: Tenkaichi3Decomp-switch-setup --install (--iso <file> [--eu <file>] | --pc <folder>) --sd <folder>\n");
        return 2;
    }
    SDL_Init(0);
    NxSetup_Start(job);
    for (bool last = false; !last;) {
        last = !Native_Running();
        while (Native_Poll(line)) {
            printf("%s\n", line.c_str());
            fflush(stdout);
            ok = ok || line.rfind("@done", 0) == 0;
        }
        SDL_Delay(50);
    }
    Native_Join();
    return ok ? 0 : 1;
}

int main(int argc, char **argv) {
    static const char *fonts[] = {
        "C:\\Windows\\Fonts\\segoeui.ttf", "/usr/share/fonts/TTF/DejaVuSans.ttf", "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/noto/NotoSans-Regular.ttf", "/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf",
        "/usr/share/fonts/liberation/LiberationSans-Regular.ttf", "/System/Library/Fonts/Supplemental/Arial.ttf",
        "/System/Library/Fonts/Geneva.ttf",
    };
    ImGui_ImplSDLGPU3_InitInfo info;

#ifdef _WIN32
    {
        int count = 0;
        char **args = utf8_args(&count);
        if (args != NULL) {
            argc = count;
            argv = args;
        }
    }
#endif
    if (argc > 1 && strcmp(argv[1], "--install") == 0) {
        return install_here(argc, argv);
    }
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        fprintf(stderr, "switch-setup: %s\n", SDL_GetError());
        return 1;
    }
    const char *glsl = "#version 330";
    {
        const char *api = getenv("BT3_GPU_API");
        bool wantGl = api != NULL && (strcmp(api, "gl") == 0 || strcmp(api, "opengl") == 0);
        std::string why;
        if (!wantGl) {
            sWindow = SDL_CreateWindow(TITLE " - Setup", 760, 600, SDL_WINDOW_HIGH_PIXEL_DENSITY);
            sDevice = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_DXIL | SDL_GPU_SHADERFORMAT_MSL, false, NULL);
            if (sWindow == NULL || sDevice == NULL || !SDL_ClaimWindowForGPUDevice(sDevice, sWindow)) {
                why = SDL_GetError();
                if (sDevice != NULL) {
                    SDL_DestroyGPUDevice(sDevice);
                    sDevice = NULL;
                }
                if (sWindow != NULL) {
                    SDL_DestroyWindow(sWindow);
                    sWindow = NULL;
                }
            }
        }
        if (sDevice == NULL) {
            for (int attempt = 0; attempt < 2 && sGl == NULL; attempt++) {
                SDL_GL_ResetAttributes();
                SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
                SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, attempt == 0 ? 3 : 0);
                SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, attempt == 0 ? SDL_GL_CONTEXT_PROFILE_CORE : 0);
                glsl = attempt == 0 ? "#version 330" : "#version 130";
                SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
                sWindow = SDL_CreateWindow(TITLE " - Setup", 760, 600, SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_OPENGL);
                sGl = sWindow != NULL ? SDL_GL_CreateContext(sWindow) : NULL;
                if (sGl == NULL && sWindow != NULL) {
                    SDL_DestroyWindow(sWindow);
                    sWindow = NULL;
                }
            }
            if (sGl == NULL) {
                fprintf(stderr, "switch-setup: no window: %s%s%s\n", why.c_str(), why.empty() ? "" : "; OpenGL: ", SDL_GetError());
                return 1;
            }
            SDL_GL_MakeCurrent(sWindow, sGl);
            SDL_GL_SetSwapInterval(1);
            sUseGl = true;
        } else {
            SDL_SetGPUSwapchainParameters(sDevice, sWindow, SDL_GPU_SWAPCHAINCOMPOSITION_SDR, SDL_GPU_PRESENTMODE_VSYNC);
        }
    }
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.IniFilename = NULL;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    style();
    for (size_t i = 0; i < sizeof(fonts) / sizeof(fonts[0]); i++) {
        if (file_exists(fonts[i])) {
            io.Fonts->AddFontFromFileTTF(fonts[i], 18.0f);
            break;
        }
    }
    if (sUseGl) {
        ImGui_ImplSDL3_InitForOpenGL(sWindow, sGl);
        ImGui_ImplOpenGL3_Init(glsl);
    } else {
        ImGui_ImplSDL3_InitForSDLGPU(sWindow);
        info.Device = sDevice;
        info.ColorTargetFormat = SDL_GetGPUSwapchainTextureFormat(sDevice, sWindow);
        info.MSAASamples = SDL_GPU_SAMPLECOUNT_1;
        ImGui_ImplSDLGPU3_Init(&info);
    }
    sHasFiles = NxSetup_HasFiles();
    while (!sQuit) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            ImGui_ImplSDL3_ProcessEvent(&ev);
            if (ev.type == SDL_EVENT_QUIT || ev.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) {
                if (!sRunning) {
                    sQuit = true;
                }
            } else if (ev.type == SDL_EVENT_DROP_FILE && sPage == PAGE_PICK && ev.drop.data != NULL) {
                // a file: the disc image (the European one when the USA one is there); a folder: the PC version's
                // when that is asked for and empty, else the card
                SDL_PathInfo pi;
                bool dir = SDL_GetPathInfo(ev.drop.data, &pi) && pi.type == SDL_PATHTYPE_DIRECTORY;
                if (!dir) {
                    sFrom = 0;
                    SDL_strlcpy(file_exists(sIso) && strcmp(sIso, ev.drop.data) != 0 ? sIsoEu : sIso, ev.drop.data, sizeof(sIso));
                } else if (sFrom == 1 && sPc[0] == '\0') {
                    SDL_strlcpy(sPc, ev.drop.data, sizeof(sPc));
                } else {
                    SDL_strlcpy(sSd, ev.drop.data, sizeof(sSd));
                }
            }
        }
        {
            std::lock_guard<std::mutex> lock(sDialogLock);
            if (sDialogFor >= 0) {
                char *fields[] = {sIso, sIsoEu, sPc, sSd};
                SDL_strlcpy(fields[sDialogFor], sDialogResult.c_str(), sizeof(sIso));
                sDialogFor = -1;
            }
        }
        pump_install();
        if (sUseGl) {
            ImGui_ImplOpenGL3_NewFrame();
        } else {
            ImGui_ImplSDLGPU3_NewFrame();
        }
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();
        build_ui();
        ImGui::Render();
        ImDrawData *dd = ImGui::GetDrawData();
        if (sUseGl) {
            int pw = 0, ph = 0;
            SDL_GetWindowSizeInPixels(sWindow, &pw, &ph);
            glViewport(0, 0, pw, ph);
            glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);
            ImGui_ImplOpenGL3_RenderDrawData(dd);
            SDL_GL_SwapWindow(sWindow);
            continue;
        }
        SDL_GPUCommandBuffer *cmd = SDL_AcquireGPUCommandBuffer(sDevice);
        SDL_GPUTexture *swap = NULL;
        Uint32 w = 0, h = 0;
        if (SDL_WaitAndAcquireGPUSwapchainTexture(cmd, sWindow, &swap, &w, &h) && swap != NULL) {
            SDL_GPUColorTargetInfo target;
            ImGui_ImplSDLGPU3_PrepareDrawData(dd, cmd);
            SDL_zero(target);
            target.texture = swap;
            target.load_op = SDL_GPU_LOADOP_CLEAR;
            target.store_op = SDL_GPU_STOREOP_STORE;
            SDL_GPURenderPass *pass = SDL_BeginGPURenderPass(cmd, &target, 1, NULL);
            ImGui_ImplSDLGPU3_RenderDrawData(dd, cmd, pass);
            SDL_EndGPURenderPass(pass);
        }
        SDL_SubmitGPUCommandBuffer(cmd);
    }
    Native_Join();
    if (sUseGl) {
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplSDL3_Shutdown();
        ImGui::DestroyContext();
        SDL_GL_DestroyContext(sGl);
        SDL_DestroyWindow(sWindow);
        SDL_Quit();
        return 0;
    }
    SDL_WaitForGPUIdle(sDevice);
    ImGui_ImplSDL3_Shutdown();
    ImGui_ImplSDLGPU3_Shutdown();
    ImGui::DestroyContext();
    SDL_ReleaseWindowFromGPUDevice(sDevice, sWindow);
    SDL_DestroyGPUDevice(sDevice);
    SDL_DestroyWindow(sWindow);
    SDL_Quit();
    return 0;
}
