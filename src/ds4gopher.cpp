// =====================================================================
//  DS4Gopher - Gopher360-style utility untuk DualShock 4 via DirectInput
// =====================================================================
#define DIRECTINPUT_VERSION 0x0800
#include <windows.h>
#include <dinput.h>
#include <cstdio>
#include <cstdint>
#include <cmath>
#include <cstring>
#include <fstream>
#include <string>
#include <sstream>
#include <map>

#pragma comment(lib, "dinput8.lib")
#pragma comment(lib, "dxguid.lib")
#pragma comment(lib, "user32.lib")

// -------------------- State Global --------------------
static LPDIRECTINPUT8        g_pDI  = nullptr;
static LPDIRECTINPUTDEVICE8  g_pDev = nullptr;
static DIJOYSTATE2           g_js   = {};
static bool                  g_running       = true;
static bool                  g_consoleHidden = false;
static bool                  g_debug         = false;
static int                   g_speedIdx      = 1;
static int                   g_speeds[3]     = { 6, 14, 28 };
static int                   g_deadzone      = 8000;

static LONG neutralLX = 0, neutralLY = 0, neutralRX = 0, neutralRY = 0;

// -------------------- Config Maps --------------------
static std::map<std::string, int> g_btnIdx = {
    {"Cross", 1}, {"Circle", 2}, {"Square", 0}, {"Triangle", 3},
    {"L1", 4}, {"R1", 5}, {"L2", 6}, {"R2", 7},
    {"Share", 8}, {"Options", 9}, {"L3", 10}, {"R3", 11}, {"PS", 12},
    {"Touchpad", 13}
};

static std::map<std::string, std::string> g_actions = {
    {"MouseLeft",   "L1"}, {"MouseRight",  "R1"}, {"MouseMiddle", "L3"},
    {"Cross",       "Cross"}, {"Circle",    "Circle"}, {"Triangle", "Triangle"},
    {"Square",      "Square"}, {"Share",    "Share"}, {"Options", "Options"},
    {"R3",          "R3"}, {"L2", "L2"}, {"R2", "R2"},
    {"SpeedCycle",  "L3+R3"}, {"Disable", "Options+Share"}
};

// Map untuk Custom Keyboard Keys
static std::map<std::string, int> g_customKeys;

// -------------------- Helper --------------------
static std::string Trim(const std::string& s) {
    auto a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    auto b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

static void LoadConfig(const char* path) {
    std::ifstream f(path);
    if (!f) {
        printf("[i] config.ini tidak ditemukan, pakai default.\n");
        return;
    }
    printf("[i] Membaca %s\n", path);

    std::string line, section;
    while (std::getline(f, line)) {
        auto hash = line.find('#');
        if (hash != std::string::npos) line = line.substr(0, hash);
        auto semi = line.find(';');
        if (semi != std::string::npos) line = line.substr(0, semi);
        line = Trim(line);
        if (line.empty()) continue;

        if (line.front() == '[' && line.back() == ']') {
            section = line.substr(1, line.size() - 2);
            continue;
        }

        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = Trim(line.substr(0, eq));
        std::string val = Trim(line.substr(eq + 1));

        if (section == "ButtonIndices") {
            try { g_btnIdx[key] = std::stoi(val); } catch (...) {}
        } else if (section == "Actions") {
            g_actions[key] = val;
        } else if (section == "CustomKeys") {
            // Membaca hex atau desimal (misal: 0x41 atau 65)
            try { g_customKeys[key] = std::stoi(val, nullptr, 0); } catch (...) {}
        } else if (section == "General") {
            if      (key == "Deadzone")        { try { g_deadzone = std::stoi(val); } catch (...) {} }
            else if (key == "Speed1")          { try { g_speeds[0] = std::stoi(val); } catch (...) {} }
            else if (key == "Speed2")          { try { g_speeds[1] = std::stoi(val); } catch (...) {} }
            else if (key == "Speed3")          { try { g_speeds[2] = std::stoi(val); } catch (...) {} }
        }
    }
    printf("[+] Config loaded.\n");
}

static int BtnOf(const std::string& action) {
    auto it = g_actions.find(action);
    if (it == g_actions.end()) return -1;
    auto bit = g_btnIdx.find(it->second);
    if (bit == g_btnIdx.end()) return -1;
    return bit->second;
}

static bool IsButtonPressed(const std::string& btnName) {
    auto it = g_btnIdx.find(btnName);
    if (it == g_btnIdx.end()) return false;
    int idx = it->second;
    if (idx < 0 || idx >= 128) return false;
    return (g_js.rgbButtons[idx] & 0x80) != 0;
}

static bool IsCombo(const std::string& action) {
    auto it = g_actions.find(action);
    if (it == g_actions.end()) return false;
    const std::string& v = it->second;
    size_t plus = v.find('+');
    if (plus == std::string::npos) return false;
    std::string a = v.substr(0, plus);
    std::string b = v.substr(plus + 1);
    auto ai = g_btnIdx.find(a);
    auto bi = g_btnIdx.find(b);
    if (ai == g_btnIdx.end() || bi == g_btnIdx.end()) return false;
    return (g_js.rgbButtons[ai->second] & 0x80) && (g_js.rgbButtons[bi->second] & 0x80);
}

static bool IsActionActive(const std::string& action) {
    auto it = g_actions.find(action);
    if (it == g_actions.end()) return false;
    if (it->second.find('+') != std::string::npos) return IsCombo(action);
    int idx = BtnOf(action);
    if (idx < 0 || idx >= 128) return false;
    return (g_js.rgbButtons[idx] & 0x80) != 0;
}

// -------------------- Input Send --------------------
static void MoveMouse(int dx, int dy) {
    if (dx == 0 && dy == 0) return;
    INPUT in = {};
    in.type = INPUT_MOUSE;
    in.mi.dx = dx; in.mi.dy = dy;
    in.mi.dwFlags = MOUSEEVENTF_MOVE;
    SendInput(1, &in, sizeof(INPUT));
}

static void ScrollMouse(int amount) {
    if (amount == 0) return;
    INPUT in = {};
    in.type = INPUT_MOUSE;
    in.mi.mouseData = (DWORD)amount;
    in.mi.dwFlags = MOUSEEVENTF_WHEEL;
    SendInput(1, &in, sizeof(INPUT));
}

static void MouseClick(DWORD flag) {
    INPUT in = {};
    in.type = INPUT_MOUSE;
    in.mi.dwFlags = flag;
    SendInput(1, &in, sizeof(INPUT));
}

static void KeyAction(WORD vk, bool down) {
    if (vk == 0) return;
    INPUT in = {};
    in.type = INPUT_KEYBOARD;
    in.ki.wVk = vk;
    in.ki.dwFlags = down ? 0 : KEYEVENTF_KEYUP;
    SendInput(1, &in, sizeof(INPUT));
}

static float NormStick(LONG v) {
    if (std::abs(v) < g_deadzone) return 0.0f;
    float s = (v > 0 ? 1.0f : -1.0f) * (float)(std::abs(v) - g_deadzone) / (32767.0f - g_deadzone);
    if (s >  1.0f) s =  1.0f;
    if (s < -1.0f) s = -1.0f;
    return s;
}

// -------------------- DirectInput Init --------------------
static BOOL CALLBACK EnumCallback(LPCDIDEVICEINSTANCE pdidi, LPVOID) {
    if (FAILED(g_pDI->CreateDevice(pdidi->guidInstance, &g_pDev, nullptr))) return DIENUM_CONTINUE;
    return DIENUM_STOP;
}

static bool InitDirectInput() {
    if (FAILED(DirectInput8Create(GetModuleHandle(nullptr), DIRECTINPUT_VERSION, IID_IDirectInput8, (void**)&g_pDI, nullptr))) return false;
    if (FAILED(g_pDI->EnumDevices(DI8DEVCLASS_GAMECTRL, EnumCallback, nullptr, DIEDFL_ATTACHEDONLY)) || !g_pDev) return false;
    if (FAILED(g_pDev->SetDataFormat(&c_dfDIJoystick2))) return false;
    if (FAILED(g_pDev->SetCooperativeLevel(GetConsoleWindow(), DISCL_NONEXCLUSIVE | DISCL_BACKGROUND))) return false;

    DIPROPRANGE range;
    range.diph.dwSize = sizeof(DIPROPRANGE);
    range.diph.dwHeaderSize = sizeof(DIPROPHEADER);
    range.diph.dwHow = DIPH_BYOFFSET;
    range.lMin = -32768; range.lMax = 32767;

    range.diph.dwObj = DIJOFS_X;  g_pDev->SetProperty(DIPROP_RANGE, &range.diph);
    range.diph.dwObj = DIJOFS_Y;  g_pDev->SetProperty(DIPROP_RANGE, &range.diph);
    range.diph.dwObj = DIJOFS_RX; g_pDev->SetProperty(DIPROP_RANGE, &range.diph);
    range.diph.dwObj = DIJOFS_RY; g_pDev->SetProperty(DIPROP_RANGE, &range.diph);

    g_pDev->Acquire();
    Sleep(50);
    g_pDev->Poll();
    if (SUCCEEDED(g_pDev->GetDeviceState(sizeof(DIJOYSTATE2), &g_js))) {
        neutralLX = g_js.lX; neutralLY = g_js.lY;
        neutralRX = g_js.lRx; neutralRY = g_js.lRy;
    }
    return true;
}

static void ToggleConsole() {
    HWND h = GetConsoleWindow();
    if (!h) return;
    if (g_consoleHidden) { ShowWindow(h, SW_SHOW); SetForegroundWindow(h); g_consoleHidden = false; }
    else { ShowWindow(h, SW_HIDE); g_consoleHidden = true; }
}

// -------------------- Main Loop --------------------
static void ProcessFrame() {
    if (FAILED(g_pDev->Poll())) { g_pDev->Acquire(); return; }
    if (FAILED(g_pDev->GetDeviceState(sizeof(DIJOYSTATE2), &g_js))) { g_pDev->Acquire(); return; }

    bool isConsoleFocused = (GetForegroundWindow() == GetConsoleWindow());

    static bool lastHide = false;
    bool curHide = IsActionActive("Options");
    if (curHide && !lastHide) ToggleConsole();
    lastHide = curHide;

    if (isConsoleFocused && !g_consoleHidden) return;

    float lx = NormStick(g_js.lX - neutralLX);
    float ly = NormStick(g_js.lY - neutralLY);
    int speed = g_speeds[g_speedIdx];
    MoveMouse((int)(lx * std::abs(lx) * speed), (int)(ly * std::abs(ly) * speed));

    float ry = NormStick(g_js.lRy - neutralRY);
    if (std::abs(ry) > 0.2f) ScrollMouse((int)(ry * 120.0f));

    // --- Aksi Bawaan ---
    static bool lastL = false, lastR = false, lastM = false;
    bool curL = IsActionActive("MouseLeft"); bool curR = IsActionActive("MouseRight"); bool curM = IsActionActive("MouseMiddle");
    if (curL && !lastL) MouseClick(MOUSEEVENTF_LEFTDOWN); if (!curL && lastL) MouseClick(MOUSEEVENTF_LEFTUP);
    if (curR && !lastR) MouseClick(MOUSEEVENTF_RIGHTDOWN); if (!curR && lastR) MouseClick(MOUSEEVENTF_RIGHTUP);
    if (curM && !lastM) MouseClick(MOUSEEVENTF_MIDDLEDOWN); if (!curM && lastM) MouseClick(MOUSEEVENTF_MIDDLEUP);
    lastL = curL; lastR = curR; lastM = curM;

    static bool lastCross = false; bool curCross = IsActionActive("Cross");
    if (curCross && !lastCross) MouseClick(MOUSEEVENTF_LEFTDOWN); if (!curCross && lastCross) MouseClick(MOUSEEVENTF_LEFTUP);
    lastCross = curCross;

    static bool lastEnter = false; bool curEnter = IsActionActive("Circle");
    if (curEnter != lastEnter) KeyAction(VK_RETURN, curEnter); lastEnter = curEnter;

    static bool lastEsc = false; bool curEsc = IsActionActive("Share");
    if (curEsc != lastEsc) KeyAction(VK_ESCAPE, curEsc); lastEsc = curEsc;

    static bool lastF2 = false; bool curF2 = IsActionActive("R3");
    if (curF2 != lastF2) KeyAction(VK_F2, curF2); lastF2 = curF2;

    static bool lastVk = false; bool curVk = IsActionActive("Triangle");
    if (curVk != lastVk) KeyAction(VK_F24, curVk); lastVk = curVk;

    static bool lastL2 = false, lastR2 = false;
    bool l2On = IsActionActive("L2"); bool r2On = IsActionActive("R2");
    if (l2On != lastL2) KeyAction(VK_SPACE, l2On); if (r2On != lastR2) KeyAction(VK_BACK, r2On);
    lastL2 = l2On; lastR2 = r2On;

    DWORD pov = g_js.rgdwPOV[0];
    static bool upD=false,dnD=false,lfD=false,rtD=false;
    bool up=false,dn=false,lf=false,rt=false;
    if (pov != 0xFFFFFFFF) {
        if (pov > 31500 || pov <= 4500)  up = true;
        if (pov > 4500  && pov <= 13500) rt = true;
        if (pov > 13500 && pov <= 22500) dn = true;
        if (pov > 22500 && pov <= 31500) lf = true;
    }
    if (up != upD) { KeyAction(VK_UP, up); upD = up; }
    if (dn != dnD) { KeyAction(VK_DOWN, dn); dnD = dn; }
    if (lf != lfD) { KeyAction(VK_LEFT, lf); lfD = lf; }
    if (rt != rtD) { KeyAction(VK_RIGHT, rt); rtD = rt; }

    static bool lastSpeedCombo = false; bool speedCombo = IsActionActive("SpeedCycle");
    if (speedCombo && !lastSpeedCombo) {
        g_speedIdx = (g_speedIdx + 1) % 3;
        printf("[i] Kecepatan: level %d (speed=%d)\n", g_speedIdx + 1, g_speeds[g_speedIdx]);
    }
    lastSpeedCombo = speedCombo;

    static bool lastDisable = false; static bool disabled = false;
    bool disableCombo = IsActionActive("Disable");
    if (disableCombo && !lastDisable) {
        disabled = !disabled;
        printf("[i] Gopher %s\n", disabled ? "DIMATIKAN" : "DIAKTIFKAN");
    }
    lastDisable = disableCombo;

    if (disabled) return;

    // ====================================================================
    // FITUR BARU: KUSTOM TOMBOL KEYBOARD BEBAS
    // ====================================================================
    for (const auto& pair : g_customKeys) {
        std::string btnName = pair.first;
        int vkCode = pair.second;
        bool isPressed = IsButtonPressed(btnName);
        
        static std::map<std::string, bool> lastCustomStates;
        if (isPressed && !lastCustomStates[btnName]) {
            KeyAction(vkCode, true);  // Tombol Ditekan
        }
        if (!isPressed && lastCustomStates[btnName]) {
            KeyAction(vkCode, false); // Tombol Dilepas
        }
        lastCustomStates[btnName] = isPressed;
    }
}

int main(int argc, char** argv) {
    SetConsoleOutputCP(CP_UTF8);
    for (int i = 1; i < argc; i++) {
        if (std::string(argv[i]) == "--debug") g_debug = true;
    }

    printf("=====================================================\n");
    printf("  DS4Gopher - Gopher360-style untuk DualShock 4\n");
    printf("=====================================================\n");

    LoadConfig("config.ini");
    if (!InitDirectInput()) { printf("Tekan Enter untuk keluar...\n"); getchar(); return 1; }

    printf("[i] Ctrl+C untuk keluar.\n");

    while (g_running) {
        ProcessFrame();
        Sleep(16);
    }

    if (g_pDev) { g_pDev->Unacquire(); g_pDev->Release(); }
    if (g_pDI)  g_pDI->Release();
    return 0;
}
