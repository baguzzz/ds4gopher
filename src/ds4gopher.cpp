// =====================================================================
//  DS4Gopher - Steam-like Controller Mapper untuk DualShock 4
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
#include <vector>

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
static bool g_useZForRightStick = false;

// Map untuk tombol controller
static std::map<std::string, int> g_btnIdx = {
    {"Cross", 1}, {"Circle", 2}, {"Square", 0}, {"Triangle", 3},
    {"L1", 4}, {"R1", 5}, {"L2", 6}, {"R2", 7},
    {"Share", 8}, {"Options", 9}, {"L3", 10}, {"R3", 11}, {"PS", 12},
    {"Touchpad", 13},
    {"DpadUp", -1}, {"DpadDown", -1}, {"DpadLeft", -1}, {"DpadRight", -1} // D-Pad ditangani khusus
};

// Struktur untuk menampung aksi (Bisa Keyboard, Mouse, atau Combo)
struct ActionBinding {
    std::vector<WORD> vkCodes; // Kode tombol keyboard (bisa lebih dari 1 untuk combo)
    int mouseAction;           // 0 = None, 1 = Left, 2 = Right, 3 = Middle
};

// Map utama untuk config [Bindings]
static std::map<std::string, ActionBinding> g_bindings;

// Analog Functions
static std::map<std::string, std::string> g_analogFuncs = {
    {"LeftStick", "Mouse"}, {"RightStick", "Scroll"}
};

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
        printf("[i] config.ini tidak ditemukan, pakai default (Mouse & Keyboard).\n");
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

        if (section == "Bindings") {
            ActionBinding act;
            act.mouseAction = 0;
            
            if (val == "MouseLeft") act.mouseAction = 1;
            else if (val == "MouseRight") act.mouseAction = 2;
            else if (val == "MouseMiddle") act.mouseAction = 3;
            else if (val == "None") act.mouseAction = -1; // Disabled
            else if (val.find("Key:") == 0) {
                // Parsing format "Key:0x20+0x43" (Space + C)
                std::string keysStr = val.substr(4);
                std::stringstream ss(keysStr);
                std::string segment;
                while (std::getline(ss, segment, '+')) {
                    try { act.vkCodes.push_back((WORD)std::stoi(segment, nullptr, 0)); } catch (...) {}
                }
            }
            g_bindings[key] = act;
        } else if (section == "Analog") {
            g_analogFuncs[key] = val;
        } else if (section == "General") {
            if (key == "Deadzone") { try { g_deadzone = std::stoi(val); } catch (...) {} }
            else if (key == "Speed1") { try { g_speeds[0] = std::stoi(val); } catch (...) {} }
            else if (key == "Speed2") { try { g_speeds[1] = std::stoi(val); } catch (...) {} }
            else if (key == "Speed3") { try { g_speeds[2] = std::stoi(val); } catch (...) {} }
        }
    }
    printf("[+] Config loaded.\n");
}

static bool IsButtonPressed(const std::string& btnName) {
    if (btnName == "DpadUp") return (g_js.rgdwPOV[0] != 0xFFFFFFFF && (g_js.rgdwPOV[0] > 31500 || g_js.rgdwPOV[0] <= 4500));
    if (btnName == "DpadDown") return (g_js.rgdwPOV[0] != 0xFFFFFFFF && (g_js.rgdwPOV[0] > 13500 && g_js.rgdwPOV[0] <= 22500));
    if (btnName == "DpadLeft") return (g_js.rgdwPOV[0] != 0xFFFFFFFF && (g_js.rgdwPOV[0] > 22500 && g_js.rgdwPOV[0] <= 31500));
    if (btnName == "DpadRight") return (g_js.rgdwPOV[0] != 0xFFFFFFFF && (g_js.rgdwPOV[0] > 4500 && g_js.rgdwPOV[0] <= 13500));

    auto it = g_btnIdx.find(btnName);
    if (it == g_btnIdx.end() || it->second < 0) return false;
    return (g_js.rgbButtons[it->second] & 0x80) != 0;
}

// -------------------- Input Senders --------------------
static void MoveMouse(int dx, int dy) {
    if (dx == 0 && dy == 0) return;
    INPUT in = {}; in.type = INPUT_MOUSE; in.mi.dx = dx; in.mi.dy = dy; in.mi.dwFlags = MOUSEEVENTF_MOVE;
    SendInput(1, &in, sizeof(INPUT));
}

static void ScrollMouse(int amount) {
    if (amount == 0) return;
    INPUT in = {}; in.type = INPUT_MOUSE; in.mi.mouseData = (DWORD)amount; in.mi.dwFlags = MOUSEEVENTF_WHEEL;
    SendInput(1, &in, sizeof(INPUT));
}

static void MouseClick(DWORD flag) {
    INPUT in = {}; in.type = INPUT_MOUSE; in.mi.dwFlags = flag;
    SendInput(1, &in, sizeof(INPUT));
}

static void KeyAction(WORD vk, bool down) {
    if (vk == 0) return;
    INPUT in = {}; in.type = INPUT_KEYBOARD; in.ki.wVk = vk; in.ki.dwFlags = down ? 0 : KEYEVENTF_KEYUP;
    SendInput(1, &in, sizeof(INPUT));
}

static void ExecuteBinding(const std::string& btnName, bool isPressed, const ActionBinding& act) {
    static std::map<std::string, bool> lastStates;
    bool lastState = lastStates[btnName];

    if (isPressed && !lastState) { // Tombol Baru Ditekan
        if (act.mouseAction == 1) MouseClick(MOUSEEVENTF_LEFTDOWN);
        else if (act.mouseAction == 2) MouseClick(MOUSEEVENTF_RIGHTDOWN);
        else if (act.mouseAction == 3) MouseClick(MOUSEEVENTF_MIDDLEDOWN);
        else {
            for (WORD vk : act.vkCodes) KeyAction(vk, true);
        }
    } else if (!isPressed && lastState) { // Tombol Dilepas
        if (act.mouseAction == 1) MouseClick(MOUSEEVENTF_LEFTUP);
        else if (act.mouseAction == 2) MouseClick(MOUSEEVENTF_RIGHTUP);
        else if (act.mouseAction == 3) MouseClick(MOUSEEVENTF_MIDDLEUP);
        else {
            // Lepas kebalikan dari urutan tekan (untuk combo)
            for (auto it = act.vkCodes.rbegin(); it != act.vkCodes.rend(); ++it) KeyAction(*it, false);
        }
    }
    lastStates[btnName] = isPressed;
}

static float NormStick(LONG v) {
    if (std::abs(v) < g_deadzone) return 0.0f;
    float s = (v > 0 ? 1.0f : -1.0f) * (float)(std::abs(v) - g_deadzone) / (32767.0f - g_deadzone);
    if (s > 1.0f) s = 1.0f; if (s < -1.0f) s = -1.0f;
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
    range.diph.dwSize = sizeof(DIPROPRANGE); range.diph.dwHeaderSize = sizeof(DIPROPHEADER);
    range.diph.dwHow = DIPH_BYOFFSET; range.lMin = -32768; range.lMax = 32767;
    range.diph.dwObj = DIJOFS_X;  g_pDev->SetProperty(DIPROP_RANGE, &range.diph);
    range.diph.dwObj = DIJOFS_Y;  g_pDev->SetProperty(DIPROP_RANGE, &range.diph);
    range.diph.dwObj = DIJOFS_RX; g_pDev->SetProperty(DIPROP_RANGE, &range.diph);
    range.diph.dwObj = DIJOFS_RY; g_pDev->SetProperty(DIPROP_RANGE, &range.diph);
    range.diph.dwObj = DIJOFS_Z;  g_pDev->SetProperty(DIPROP_RANGE, &range.diph);
    range.diph.dwObj = DIJOFS_RZ; g_pDev->SetProperty(DIPROP_RANGE, &range.diph);

    g_pDev->Acquire();
    Sleep(50); g_pDev->Poll();
    if (SUCCEEDED(g_pDev->GetDeviceState(sizeof(DIJOYSTATE2), &g_js))) {
        neutralLX = g_js.lX; neutralLY = g_js.lY;
        if (std::abs(g_js.lRx) < 1000 && std::abs(g_js.lRy) < 1000) {
            g_useZForRightStick = true; neutralRX = g_js.lZ; neutralRY = g_js.lRz;
        } else {
            g_useZForRightStick = false; neutralRX = g_js.lRx; neutralRY = g_js.lRy;
        }
    }
    return true;
}

static void ToggleConsole() {
    HWND h = GetConsoleWindow(); if (!h) return;
    if (g_consoleHidden) { ShowWindow(h, SW_SHOW); SetForegroundWindow(h); g_consoleHidden = false; }
    else { ShowWindow(h, SW_HIDE); g_consoleHidden = true; }
}

// -------------------- Main Loop --------------------
static void ProcessFrame() {
    if (FAILED(g_pDev->Poll())) { g_pDev->Acquire(); return; }
    if (FAILED(g_pDev->GetDeviceState(sizeof(DIJOYSTATE2), &g_js))) { g_pDev->Acquire(); return; }

    bool isConsoleFocused = (GetForegroundWindow() == GetConsoleWindow());
    
    // Cek tombol Options untuk sembunyikan console (tetap default)
    static bool lastHide = false;
    bool curHide = IsButtonPressed("Options");
    if (curHide && !lastHide && !isConsoleFocused) ToggleConsole();
    lastHide = curHide;

    if (isConsoleFocused && !g_consoleHidden) return;

    // Analog
    float lx = NormStick(g_js.lX - neutralLX);
    float ly = NormStick(g_js.lY - neutralLY);
    float rx = NormStick((g_useZForRightStick ? g_js.lZ : g_js.lRx) - neutralRX);
    float ry = NormStick((g_useZForRightStick ? g_js.lRz : g_js.lRy) - neutralRY);
    int speed = g_speeds[g_speedIdx];

    if (g_analogFuncs["LeftStick"] == "Mouse") MoveMouse((int)(lx * std::abs(lx) * speed), (int)(ly * std::abs(ly) * speed));
    else if (g_analogFuncs["LeftStick"] == "Scroll" && std::abs(ly) > 0.2f) ScrollMouse((int)(ly * 120.0f));

    if (g_analogFuncs["RightStick"] == "Mouse") MoveMouse((int)(rx * std::abs(rx) * speed), (int)(ry * std::abs(ry) * speed));
    else if (g_analogFuncs["RightStick"] == "Scroll" && std::abs(ry) > 0.2f) ScrollMouse((int)(ry * 120.0f));

    // Eksekusi semua tombol yang ada di [Bindings]
    for (const auto& pair : g_bindings) {
        ExecuteBinding(pair.first, IsButtonPressed(pair.first), pair.second);
    }
}

int main(int argc, char** argv) {
    SetConsoleOutputCP(CP_UTF8);
    printf("=====================================================\n");
    printf("  DS4Gopher - Steam-like Controller Mapper\n");
    printf("=====================================================\n");

    LoadConfig("config.ini");
    if (!InitDirectInput()) { printf("Tekan Enter untuk keluar...\n"); getchar(); return 1; }

    printf("[i] Ctrl+C untuk keluar.\n");
    while (g_running) { ProcessFrame(); Sleep(16); }

    if (g_pDev) { g_pDev->Unacquire(); g_pDev->Release(); }
    if (g_pDI)  g_pDI->Release();
    return 0;
}