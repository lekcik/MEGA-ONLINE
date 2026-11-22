#include "core/Platform.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <deque>
#include <iostream>
#include <thread>
#include <vector>

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
  #define NOMINMAX
#endif
#include <windows.h>
#include <windowsx.h>
#include <mmsystem.h>

namespace mega::platform {

namespace {

using PFN_wglCreateContextAttribsARB = HGLRC (WINAPI*)(HDC, HGLRC, const int*);
using PFN_wglSwapIntervalEXT = BOOL (WINAPI*)(int);

constexpr int WGL_CONTEXT_MAJOR_VERSION_ARB = 0x2091;
constexpr int WGL_CONTEXT_MINOR_VERSION_ARB = 0x2092;
constexpr int WGL_CONTEXT_PROFILE_MASK_ARB  = 0x9126;
constexpr int WGL_CONTEXT_CORE_PROFILE_BIT_ARB = 0x00000001;

Key mapWinVk(WPARAM vk) {
    switch (vk) {
        case 'W': return Key::W;
        case 'A': return Key::A;
        case 'S': return Key::S;
        case 'D': return Key::D;
        case 'E': return Key::E;
        case 'F': return Key::F;
        case 'G': return Key::G;
        case 'H': return Key::H;
        case 'I': return Key::I;
        case 'M': return Key::M;
        case 'N': return Key::N;
        case 'Q': return Key::Q;
        case 'R': return Key::R;
        case 'T': return Key::T;
        case 'V': return Key::V;
        case 'X': return Key::X;
        case 'Y': return Key::Y;
        case 'Z': return Key::Z;
        case 'C': return Key::C;
        case 'B': return Key::B;
        case '1': return Key::Num1;
        case '2': return Key::Num2;
        case '3': return Key::Num3;
        case VK_SPACE:   return Key::Space;
        case VK_SHIFT:
        case VK_LSHIFT:
        case VK_RSHIFT:  return Key::LShift;
        case VK_CONTROL:
        case VK_LCONTROL:
        case VK_RCONTROL:return Key::LCtrl;
        case VK_TAB:     return Key::Tab;
        case VK_RETURN:  return Key::Enter;
        case VK_ESCAPE:  return Key::Escape;
        case VK_BACK:    return Key::Backspace;
        case VK_UP:      return Key::Up;
        case VK_DOWN:    return Key::Down;
        case VK_LEFT:    return Key::Left;
        case VK_RIGHT:   return Key::Right;
        case VK_F1:      return Key::F1;
        case VK_F5:      return Key::F5;
        case VK_OEM_4:   return Key::LeftBracket;
        case VK_OEM_6:   return Key::RightBracket;
        case VK_OEM_PERIOD: return Key::Period;
        case VK_OEM_COMMA:  return Key::Comma;
        default:         return Key::Unknown;
    }
}

HMODULE g_opengl32Dll = nullptr;
PFN_wglSwapIntervalEXT g_wglSwapInterval = nullptr;

} // namespace

struct Window {
    HWND hwnd = nullptr;
    HDC hdc = nullptr;
    HGLRC hglrc = nullptr;
    int width = 1280;
    int height = 720;
    bool relativeMouse = false;
    bool cursorHidden = false;
    bool focused = true;
    bool keyDown[static_cast<size_t>(Key::Count)]{};
    std::deque<Event> queue;
    std::vector<uint8_t> rawBuf;
};

static void updateClipAndCursor(Window* win) {
    if (!win || !win->hwnd) return;
    if (win->relativeMouse && win->focused) {
        if (!win->cursorHidden) {
            while (ShowCursor(FALSE) >= 0) {}
            win->cursorHidden = true;
        }
        RECT rc{};
        GetClientRect(win->hwnd, &rc);
        POINT tl{rc.left, rc.top};
        POINT br{rc.right, rc.bottom};
        ClientToScreen(win->hwnd, &tl);
        ClientToScreen(win->hwnd, &br);
        RECT screenRc{tl.x, tl.y, br.x, br.y};
        ClipCursor(&screenRc);
        SetCursorPos((tl.x + br.x) / 2, (tl.y + br.y) / 2);
    } else {
        ClipCursor(nullptr);
        if (win->cursorHidden) {
            while (ShowCursor(TRUE) < 0) {}
            win->cursorHidden = false;
        }
    }
}

static LRESULT CALLBACK megaWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    auto* win = reinterpret_cast<Window*>(GetWindowLongPtrA(hwnd, GWLP_USERDATA));
    if (!win) return DefWindowProcA(hwnd, msg, wParam, lParam);

    switch (msg) {
        case WM_CLOSE: {
            Event ev{};
            ev.type = EventType::Quit;
            win->queue.push_back(ev);
            return 0;
        }
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        case WM_SIZE: {
            win->width = std::max(1, static_cast<int>(LOWORD(lParam)));
            win->height = std::max(1, static_cast<int>(HIWORD(lParam)));
            if (win->relativeMouse && win->focused) updateClipAndCursor(win);
            Event ev{};
            ev.type = EventType::WindowResized;
            ev.width = win->width;
            ev.height = win->height;
            win->queue.push_back(ev);
            return 0;
        }
        case WM_KILLFOCUS: {
            win->focused = false;
            std::fill(std::begin(win->keyDown), std::end(win->keyDown), false);
            updateClipAndCursor(win);
            Event ev{};
            ev.type = EventType::FocusLost;
            win->queue.push_back(ev);
            return 0;
        }
        case WM_SETFOCUS: {
            win->focused = true;
            std::fill(std::begin(win->keyDown), std::end(win->keyDown), false);
            updateClipAndCursor(win);
            Event ev{};
            ev.type = EventType::FocusGained;
            win->queue.push_back(ev);
            return 0;
        }
        case WM_SYSCOMMAND:
            if ((wParam & 0xFFF0) == SC_KEYMENU) return 0;
            break;
        case WM_KEYDOWN:
        case WM_SYSKEYDOWN: {
            Key k = mapWinVk(wParam);
            bool repeat = (lParam & (1LL << 30)) != 0;
            if (k != Key::Unknown) win->keyDown[static_cast<size_t>(k)] = true;
            Event ev{};
            ev.type = EventType::KeyDown;
            ev.key = k;
            ev.ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
            ev.repeat = repeat;
            win->queue.push_back(ev);
            if (msg == WM_SYSKEYDOWN && wParam == VK_F4) break;
            return 0;
        }
        case WM_KEYUP:
        case WM_SYSKEYUP: {
            Key k = mapWinVk(wParam);
            if (k != Key::Unknown) win->keyDown[static_cast<size_t>(k)] = false;
            Event ev{};
            ev.type = EventType::KeyUp;
            ev.key = k;
            ev.ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
            win->queue.push_back(ev);
            return 0;
        }
        case WM_CHAR: {
            if (wParam >= 32 && wParam < 127) {
                Event ev{};
                ev.type = EventType::TextInput;
                ev.textChar = static_cast<char>(wParam);
                win->queue.push_back(ev);
            }
            return 0;
        }
        case WM_INPUT: {
            if (!win->relativeMouse || !win->focused) break;
            UINT sz = 0;
            GetRawInputData(reinterpret_cast<HRAWINPUT>(lParam), RID_INPUT, nullptr, &sz, sizeof(RAWINPUTHEADER));
            if (sz > 0) {
                if (win->rawBuf.size() < sz) win->rawBuf.resize(sz);
                if (GetRawInputData(reinterpret_cast<HRAWINPUT>(lParam), RID_INPUT, win->rawBuf.data(), &sz, sizeof(RAWINPUTHEADER)) == sz) {
                    const auto* raw = reinterpret_cast<const RAWINPUT*>(win->rawBuf.data());
                    if (raw->header.dwType == RIM_TYPEMOUSE && (raw->data.mouse.usFlags & MOUSE_MOVE_ABSOLUTE) == 0) {
                        LONG dx = raw->data.mouse.lLastX;
                        LONG dy = raw->data.mouse.lLastY;
                        if (dx != 0 || dy != 0) {
                            Event ev{};
                            ev.type = EventType::MouseMove;
                            ev.xrel = static_cast<float>(dx);
                            ev.yrel = static_cast<float>(dy);
                            win->queue.push_back(ev);
                        }
                    }
                }
            }
            RECT rc{};
            GetClientRect(win->hwnd, &rc);
            POINT center{(rc.right - rc.left) / 2, (rc.bottom - rc.top) / 2};
            ClientToScreen(win->hwnd, &center);
            POINT cur{};
            GetCursorPos(&cur);
            if (cur.x != center.x || cur.y != center.y) {
                SetCursorPos(center.x, center.y);
            }
            return 0;
        }
        case WM_MOUSEMOVE: {
            if (!win->relativeMouse) {
                Event ev{};
                ev.type = EventType::MouseMove;
                ev.x = static_cast<float>(GET_X_LPARAM(lParam));
                ev.y = static_cast<float>(GET_Y_LPARAM(lParam));
                win->queue.push_back(ev);
            }
            return 0;
        }
        case WM_LBUTTONDOWN:
        case WM_RBUTTONDOWN:
        case WM_MBUTTONDOWN: {
            SetCapture(hwnd);
            Event ev{};
            ev.type = EventType::MouseButtonDown;
            ev.button = (msg == WM_LBUTTONDOWN) ? MouseButton::Left :
                        (msg == WM_RBUTTONDOWN ? MouseButton::Right : MouseButton::Middle);
            ev.x = static_cast<float>(GET_X_LPARAM(lParam));
            ev.y = static_cast<float>(GET_Y_LPARAM(lParam));
            win->queue.push_back(ev);
            return 0;
        }
        case WM_LBUTTONUP:
        case WM_RBUTTONUP:
        case WM_MBUTTONUP: {
            ReleaseCapture();
            Event ev{};
            ev.type = EventType::MouseButtonUp;
            ev.button = (msg == WM_LBUTTONUP) ? MouseButton::Left :
                        (msg == WM_RBUTTONUP ? MouseButton::Right : MouseButton::Middle);
            ev.x = static_cast<float>(GET_X_LPARAM(lParam));
            ev.y = static_cast<float>(GET_Y_LPARAM(lParam));
            win->queue.push_back(ev);
            return 0;
        }
    }
    return DefWindowProcA(hwnd, msg, wParam, lParam);
}

Window* createWindow(const char* title, int width, int height) {
    HINSTANCE hInst = GetModuleHandleA(nullptr);
    const char* clsName = "MEGA_GL33_WINDOW";

    WNDCLASSA wc{};
    wc.style = CS_OWNDC | CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = megaWndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = clsName;
    RegisterClassA(&wc);

    RECT rc{0, 0, width, height};
    DWORD style = WS_OVERLAPPEDWINDOW | WS_VISIBLE;
    AdjustWindowRect(&rc, style, FALSE);

    HWND hwnd = CreateWindowExA(
        0, clsName, title, style,
        CW_USEDEFAULT, CW_USEDEFAULT,
        rc.right - rc.left, rc.bottom - rc.top,
        nullptr, nullptr, hInst, nullptr
    );
    if (!hwnd) return nullptr;

    auto* win = new Window{};
    win->hwnd = hwnd;
    win->width = width;
    win->height = height;
    SetWindowLongPtrA(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(win));

    win->hdc = GetDC(hwnd);
    PIXELFORMATDESCRIPTOR pfd{};
    pfd.nSize = sizeof(pfd);
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 32;
    pfd.cDepthBits = 24;
    pfd.cStencilBits = 8;
    pfd.iLayerType = PFD_MAIN_PLANE;

    int pf = ChoosePixelFormat(win->hdc, &pfd);
    if (!pf || !SetPixelFormat(win->hdc, pf, &pfd)) {
        destroyWindow(win);
        return nullptr;
    }

    HGLRC tmpCtx = wglCreateContext(win->hdc);
    if (!tmpCtx || !wglMakeCurrent(win->hdc, tmpCtx)) {
        if (tmpCtx) wglDeleteContext(tmpCtx);
        destroyWindow(win);
        return nullptr;
    }

    auto createAttribs = reinterpret_cast<PFN_wglCreateContextAttribsARB>(
        reinterpret_cast<void*>(wglGetProcAddress("wglCreateContextAttribsARB")));
    g_wglSwapInterval = reinterpret_cast<PFN_wglSwapIntervalEXT>(
        reinterpret_cast<void*>(wglGetProcAddress("wglSwapIntervalEXT")));

    if (createAttribs) {
        const int attribs[] = {
            WGL_CONTEXT_MAJOR_VERSION_ARB, 3,
            WGL_CONTEXT_MINOR_VERSION_ARB, 3,
            WGL_CONTEXT_PROFILE_MASK_ARB, WGL_CONTEXT_CORE_PROFILE_BIT_ARB,
            0
        };
        HGLRC coreCtx = createAttribs(win->hdc, nullptr, attribs);
        if (coreCtx && wglMakeCurrent(win->hdc, coreCtx)) {
            wglDeleteContext(tmpCtx);
            win->hglrc = coreCtx;
        } else {
            if (coreCtx) wglDeleteContext(coreCtx);
            win->hglrc = tmpCtx;
        }
    } else {
        win->hglrc = tmpCtx;
    }

    RAWINPUTDEVICE rid{};
    rid.usUsagePage = 0x01;
    rid.usUsage = 0x02;
    rid.dwFlags = 0;
    rid.hwndTarget = hwnd;
    RegisterRawInputDevices(&rid, 1, sizeof(rid));

    return win;
}

void destroyWindow(Window* win) {
    if (!win) return;
    win->relativeMouse = false;
    updateClipAndCursor(win);
    if (win->hglrc) {
        wglMakeCurrent(nullptr, nullptr);
        wglDeleteContext(win->hglrc);
    }
    if (win->hdc && win->hwnd) ReleaseDC(win->hwnd, win->hdc);
    if (win->hwnd) DestroyWindow(win->hwnd);
    delete win;
}

void swapBuffers(Window* win) {
    if (win && win->hdc) SwapBuffers(win->hdc);
}

void setSwapInterval(int interval) {
    if (g_wglSwapInterval) g_wglSwapInterval(interval);
}

void setRelativeMouseMode(Window* win, bool enabled) {
    if (!win) return;
    win->relativeMouse = enabled;
    updateClipAndCursor(win);
}

bool pollEvent(Window* win, Event& out) {
    if (!win) return false;
    MSG msg{};
    while (PeekMessageA(&msg, nullptr, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) {
            Event q{};
            q.type = EventType::Quit;
            win->queue.push_back(q);
        } else {
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }
    }
    if (win->queue.empty()) return false;
    out = win->queue.front();
    win->queue.pop_front();
    return true;
}

bool isKeyDown(const Window* win, Key k) {
    if (!win || k >= Key::Count) return false;
    return win->keyDown[static_cast<size_t>(k)];
}

void* glGetProcAddress(const char* name) {
    void* p = reinterpret_cast<void*>(wglGetProcAddress(name));
    intptr_t iv = reinterpret_cast<intptr_t>(p);
    if (iv == 0 || iv == 1 || iv == 2 || iv == 3 || iv == -1) {
        if (!g_opengl32Dll) g_opengl32Dll = LoadLibraryA("opengl32.dll");
        if (g_opengl32Dll) p = reinterpret_cast<void*>(GetProcAddress(g_opengl32Dll, name));
    }
    return p;
}

uint64_t getTicksMs() {
    static const auto t0 = std::chrono::steady_clock::now();
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - t0).count());
}

struct AudioOutput {
    HWAVEOUT hwo = nullptr;
    HANDLE evt = nullptr;
    std::atomic<bool> stopFlag{false};
    std::thread worker;
};

AudioOutput* startAudioOutput(int sampleRate, AudioCallback cb, void* userdata) {
    if (!cb) return nullptr;
    auto* ao = new AudioOutput{};
    ao->evt = CreateEventA(nullptr, FALSE, FALSE, nullptr);

    WAVEFORMATEX wfx{};
    wfx.wFormatTag = WAVE_FORMAT_PCM;
    wfx.nChannels = 2;
    wfx.nSamplesPerSec = static_cast<DWORD>(sampleRate);
    wfx.wBitsPerSample = 16;
    wfx.nBlockAlign = 4;
    wfx.nAvgBytesPerSec = static_cast<DWORD>(sampleRate) * 4;

    if (waveOutOpen(&ao->hwo, WAVE_MAPPER, &wfx, reinterpret_cast<DWORD_PTR>(ao->evt),
                    0, CALLBACK_EVENT) != MMSYSERR_NOERROR) {
        CloseHandle(ao->evt);
        delete ao;
        return nullptr;
    }

    ao->worker = std::thread([ao, cb, userdata]() {
        constexpr int NUM_BUFS = 3;
        constexpr int FRAMES = 1024;
        std::vector<int16_t> pcm[NUM_BUFS];
        std::vector<float> flt(FRAMES * 2, 0.0f);
        WAVEHDR hdrs[NUM_BUFS]{};

        for (int i = 0; i < NUM_BUFS; ++i) {
            pcm[i].assign(FRAMES * 2, 0);
            hdrs[i].lpData = reinterpret_cast<LPSTR>(pcm[i].data());
            hdrs[i].dwBufferLength = static_cast<DWORD>(pcm[i].size() * sizeof(int16_t));
            waveOutPrepareHeader(ao->hwo, &hdrs[i], sizeof(WAVEHDR));
            cb(userdata, flt.data(), FRAMES);
            for (int s = 0; s < FRAMES * 2; ++s) {
                float v = std::clamp(flt[static_cast<size_t>(s)], -1.0f, 1.0f);
                pcm[i][static_cast<size_t>(s)] = static_cast<int16_t>(v * 32767.0f);
            }
            waveOutWrite(ao->hwo, &hdrs[i], sizeof(WAVEHDR));
        }

        while (!ao->stopFlag.load(std::memory_order_relaxed)) {
            WaitForSingleObject(ao->evt, 50);
            for (int i = 0; i < NUM_BUFS; ++i) {
                if (hdrs[i].dwFlags & WHDR_DONE) {
                    std::fill(flt.begin(), flt.end(), 0.0f);
                    cb(userdata, flt.data(), FRAMES);
                    for (int s = 0; s < FRAMES * 2; ++s) {
                        float v = std::clamp(flt[static_cast<size_t>(s)], -1.0f, 1.0f);
                        pcm[i][static_cast<size_t>(s)] = static_cast<int16_t>(v * 32767.0f);
                    }
                    waveOutWrite(ao->hwo, &hdrs[i], sizeof(WAVEHDR));
                }
            }
        }

        waveOutReset(ao->hwo);
        for (int i = 0; i < NUM_BUFS; ++i) {
            waveOutUnprepareHeader(ao->hwo, &hdrs[i], sizeof(WAVEHDR));
        }
    });

    return ao;
}

void stopAudioOutput(AudioOutput* audio) {
    if (!audio) return;
    audio->stopFlag.store(true, std::memory_order_relaxed);
    if (audio->evt) SetEvent(audio->evt);
    if (audio->worker.joinable()) audio->worker.join();
    if (audio->hwo) waveOutClose(audio->hwo);
    if (audio->evt) CloseHandle(audio->evt);
    delete audio;
}

} // namespace mega::platform

#else // POSIX / Linux (Zero-dev-header dlopen X11 + GLX + ALSA)

#include <cctype>
#include <dlfcn.h>

namespace mega::platform {

namespace {

using XID = unsigned long;
using XWindow = XID;
using Colormap = XID;
using Cursor = XID;
using Pixmap = XID;
using Atom = unsigned long;
using Time = unsigned long;
using KeySym = unsigned long;
using Bool = int;
using Status = int;
struct Display;
struct Visual;

struct XVisualInfo {
    Visual* visual;
    unsigned long visualid;
    int screen;
    int depth;
    int c_class;
    unsigned long red_mask;
    unsigned long green_mask;
    unsigned long blue_mask;
    int colormap_size;
    int bits_per_rgb;
};

struct XSetWindowAttributes {
    Pixmap background_pixmap;
    unsigned long background_pixel;
    Pixmap border_pixmap;
    unsigned long border_pixel;
    int bit_gravity;
    int win_gravity;
    int backing_store;
    unsigned long backing_planes;
    unsigned long backing_pixel;
    Bool save_under;
    long event_mask;
    long do_not_propagate_mask;
    Bool override_redirect;
    Colormap colormap;
    Cursor cursor;
};

struct XColor {
    unsigned long pixel;
    unsigned short red, green, blue;
    char flags;
    char pad;
};

struct XEventPadded {
    int type;
    union {
        struct { int type; unsigned long serial; Bool send_event; Display* display; XWindow window; } xany;
        struct { int type; unsigned long serial; Bool send_event; Display* display; XWindow window; XWindow root; XWindow subwindow; Time time; int x, y; int x_root, y_root; unsigned int state; unsigned int keycode; Bool same_screen; } xkey;
        struct { int type; unsigned long serial; Bool send_event; Display* display; XWindow window; XWindow root; XWindow subwindow; Time time; int x, y; int x_root, y_root; unsigned int state; unsigned int button; Bool same_screen; } xbutton;
        struct { int type; unsigned long serial; Bool send_event; Display* display; XWindow window; XWindow root; XWindow subwindow; Time time; int x, y; int x_root, y_root; unsigned int state; char is_hint; Bool same_screen; } xmotion;
        struct { int type; unsigned long serial; Bool send_event; Display* display; XWindow window; int x, y; int width, height; int border_width; XWindow above; Bool override_redirect; } xconfigure;
        struct { int type; unsigned long serial; Bool send_event; Display* display; XWindow window; Atom message_type; int format; union { char b[20]; short s[10]; long l[5]; } data; } xclient;
        long pad[24];
    };
};

constexpr int X_KeyPress        = 2;
constexpr int X_KeyRelease      = 3;
constexpr int X_ButtonPress     = 4;
constexpr int X_ButtonRelease   = 5;
constexpr int X_MotionNotify    = 6;
constexpr int X_FocusIn         = 9;
constexpr int X_FocusOut        = 10;
constexpr int X_ConfigureNotify = 22;
constexpr int X_ClientMessage   = 33;

constexpr long MaskKeyPress        = 1L << 0;
constexpr long MaskKeyRelease      = 1L << 1;
constexpr long MaskButtonPress     = 1L << 2;
constexpr long MaskButtonRelease   = 1L << 3;
constexpr long MaskPointerMotion   = 1L << 6;
constexpr long MaskStructureNotify = 1L << 17;
constexpr long MaskFocusChange     = 1L << 21;

constexpr unsigned long CWBorderPixel = 1L << 3;
constexpr unsigned long CWEventMask   = 1L << 11;
constexpr unsigned long CWColormap    = 1L << 13;

using GLXContext = void*;
using GLXFBConfig = void*;

struct X11Api {
    void* libX11 = nullptr;
    void* libGL = nullptr;

    Display* (*XOpenDisplay)(const char*) = nullptr;
    int (*XCloseDisplay)(Display*) = nullptr;
    int (*XDefaultScreen)(Display*) = nullptr;
    XWindow (*XRootWindow)(Display*, int) = nullptr;
    Colormap (*XCreateColormap)(Display*, XWindow, Visual*, int) = nullptr;
    XWindow (*XCreateWindow)(Display*, XWindow, int, int, unsigned int, unsigned int, unsigned int, int, unsigned int, Visual*, unsigned long, XSetWindowAttributes*) = nullptr;
    int (*XStoreName)(Display*, XWindow, const char*) = nullptr;
    int (*XMapWindow)(Display*, XWindow) = nullptr;
    int (*XDestroyWindow)(Display*, XWindow) = nullptr;
    Atom (*XInternAtom)(Display*, const char*, Bool) = nullptr;
    Status (*XSetWMProtocols)(Display*, XWindow, Atom*, int) = nullptr;
    int (*XPending)(Display*) = nullptr;
    int (*XNextEvent)(Display*, XEventPadded*) = nullptr;
    int (*XPeekEvent)(Display*, XEventPadded*) = nullptr;
    int (*XEventsQueued)(Display*, int) = nullptr;
    int (*XLookupString)(void*, char*, int, KeySym*, void*) = nullptr;
    int (*XWarpPointer)(Display*, XWindow, XWindow, int, int, unsigned int, unsigned int, int, int) = nullptr;
    Pixmap (*XCreateBitmapFromData)(Display*, XID, const char*, unsigned int, unsigned int) = nullptr;
    Cursor (*XCreatePixmapCursor)(Display*, Pixmap, Pixmap, XColor*, XColor*, unsigned int, unsigned int) = nullptr;
    int (*XFreePixmap)(Display*, Pixmap) = nullptr;
    int (*XFreeCursor)(Display*, Cursor) = nullptr;
    int (*XDefineCursor)(Display*, XWindow, Cursor) = nullptr;
    int (*XUndefineCursor)(Display*, XWindow) = nullptr;
    int (*XFlush)(Display*) = nullptr;
    int (*XFree)(void*) = nullptr;

    XVisualInfo* (*glXChooseVisual)(Display*, int, int*) = nullptr;
    GLXFBConfig* (*glXChooseFBConfig)(Display*, int, const int*, int*) = nullptr;
    XVisualInfo* (*glXGetVisualFromFBConfig)(Display*, GLXFBConfig) = nullptr;
    GLXContext (*glXCreateContext)(Display*, XVisualInfo*, GLXContext, Bool) = nullptr;
    Bool (*glXMakeCurrent)(Display*, XID, GLXContext) = nullptr;
    void (*glXSwapBuffers)(Display*, XID) = nullptr;
    void (*glXDestroyContext)(Display*, GLXContext) = nullptr;
    void* (*glXGetProcAddressARB)(const unsigned char*) = nullptr;

    template <typename T>
    bool sym(void* lib, T& fn, const char* name) {
        fn = reinterpret_cast<T>(dlsym(lib, name));
        return fn != nullptr;
    }

    bool load() {
        libX11 = dlopen("libX11.so.6", RTLD_LAZY | RTLD_GLOBAL);
        if (!libX11) libX11 = dlopen("libX11.so", RTLD_LAZY | RTLD_GLOBAL);
        libGL = dlopen("libGL.so.1", RTLD_LAZY | RTLD_GLOBAL);
        if (!libGL) libGL = dlopen("libGL.so", RTLD_LAZY | RTLD_GLOBAL);
        if (!libX11 || !libGL) return false;

        bool ok = true;
        ok &= sym(libX11, XOpenDisplay, "XOpenDisplay");
        ok &= sym(libX11, XCloseDisplay, "XCloseDisplay");
        ok &= sym(libX11, XDefaultScreen, "XDefaultScreen");
        ok &= sym(libX11, XRootWindow, "XRootWindow");
        ok &= sym(libX11, XCreateColormap, "XCreateColormap");
        ok &= sym(libX11, XCreateWindow, "XCreateWindow");
        ok &= sym(libX11, XStoreName, "XStoreName");
        ok &= sym(libX11, XMapWindow, "XMapWindow");
        ok &= sym(libX11, XDestroyWindow, "XDestroyWindow");
        ok &= sym(libX11, XInternAtom, "XInternAtom");
        ok &= sym(libX11, XSetWMProtocols, "XSetWMProtocols");
        ok &= sym(libX11, XPending, "XPending");
        ok &= sym(libX11, XNextEvent, "XNextEvent");
        ok &= sym(libX11, XPeekEvent, "XPeekEvent");
        ok &= sym(libX11, XEventsQueued, "XEventsQueued");
        ok &= sym(libX11, XLookupString, "XLookupString");
        ok &= sym(libX11, XWarpPointer, "XWarpPointer");
        ok &= sym(libX11, XCreateBitmapFromData, "XCreateBitmapFromData");
        ok &= sym(libX11, XCreatePixmapCursor, "XCreatePixmapCursor");
        ok &= sym(libX11, XFreePixmap, "XFreePixmap");
        ok &= sym(libX11, XFreeCursor, "XFreeCursor");
        ok &= sym(libX11, XDefineCursor, "XDefineCursor");
        ok &= sym(libX11, XUndefineCursor, "XUndefineCursor");
        ok &= sym(libX11, XFlush, "XFlush");
        ok &= sym(libX11, XFree, "XFree");

        ok &= sym(libGL, glXChooseVisual, "glXChooseVisual");
        sym(libGL, glXChooseFBConfig, "glXChooseFBConfig");
        sym(libGL, glXGetVisualFromFBConfig, "glXGetVisualFromFBConfig");
        ok &= sym(libGL, glXCreateContext, "glXCreateContext");
        ok &= sym(libGL, glXMakeCurrent, "glXMakeCurrent");
        ok &= sym(libGL, glXSwapBuffers, "glXSwapBuffers");
        ok &= sym(libGL, glXDestroyContext, "glXDestroyContext");
        glXGetProcAddressARB = reinterpret_cast<void* (*)(const unsigned char*)>(
            dlsym(libGL, "glXGetProcAddressARB"));
        if (!glXGetProcAddressARB) {
            glXGetProcAddressARB = reinterpret_cast<void* (*)(const unsigned char*)>(
                dlsym(libGL, "glXGetProcAddress"));
        }
        return ok;
    }
};

X11Api g_x11;
Display* g_activeDpy = nullptr;
XWindow g_activeWin = 0;

Key mapKeySym(KeySym ks) {
    if (ks >= 'A' && ks <= 'Z') ks = ks - 'A' + 'a';
    switch (ks) {
        case 'w': return Key::W;
        case 'a': return Key::A;
        case 's': return Key::S;
        case 'd': return Key::D;
        case 'e': return Key::E;
        case 'f': return Key::F;
        case 'g': return Key::G;
        case 'h': return Key::H;
        case 'i': return Key::I;
        case 'm': return Key::M;
        case 'n': return Key::N;
        case 'q': return Key::Q;
        case 'r': return Key::R;
        case 't': return Key::T;
        case 'v': return Key::V;
        case 'x': return Key::X;
        case 'y': return Key::Y;
        case 'z': return Key::Z;
        case 'c': return Key::C;
        case 'b': return Key::B;
        case '1': return Key::Num1;
        case '2': return Key::Num2;
        case '3': return Key::Num3;
        case ' ': return Key::Space;
        case '[': return Key::LeftBracket;
        case ']': return Key::RightBracket;
        case '.': return Key::Period;
        case ',': return Key::Comma;
        case 0xFF08: return Key::Backspace;
        case 0xFF09: return Key::Tab;
        case 0xFF0D:
        case 0xFF8D: return Key::Enter;
        case 0xFF1B: return Key::Escape;
        case 0xFF51: return Key::Left;
        case 0xFF52: return Key::Up;
        case 0xFF53: return Key::Right;
        case 0xFF54: return Key::Down;
        case 0xFFBE: return Key::F1;
        case 0xFFC2: return Key::F5;
        case 0xFFE1:
        case 0xFFE2: return Key::LShift;
        case 0xFFE3:
        case 0xFFE4: return Key::LCtrl;
        default:     return Key::Unknown;
    }
}

} // namespace

struct Window {
    Display* dpy = nullptr;
    XWindow xwin = 0;
    Colormap cmap = 0;
    GLXContext glx = nullptr;
    Atom wmDelete = 0;
    Cursor hiddenCursor = 0;
    int width = 1280;
    int height = 720;
    bool relativeMouse = false;
    bool focused = true;
    bool skipNextCenter = false;
    bool keyDown[static_cast<size_t>(Key::Count)]{};
};

Window* createWindow(const char* title, int width, int height) {
    if (!g_x11.load()) {
        std::cerr << "Failed to load libX11.so.6 / libGL.so.1\n";
        return nullptr;
    }
    Display* dpy = g_x11.XOpenDisplay(nullptr);
    if (!dpy) {
        std::cerr << "XOpenDisplay failed\n";
        return nullptr;
    }

    int screen = g_x11.XDefaultScreen(dpy);
    XWindow root = g_x11.XRootWindow(dpy, screen);

    XVisualInfo* vi = nullptr;
    GLXFBConfig chosenFbc = nullptr;
    if (g_x11.glXChooseFBConfig && g_x11.glXGetVisualFromFBConfig) {
        int fbAttribs[] = {
            0x8010 /*GLX_X_RENDERABLE*/, 1,
            0x8011 /*GLX_DRAWABLE_TYPE*/, 1 /*GLX_WINDOW_BIT*/,
            0x8012 /*GLX_RENDER_TYPE*/, 1 /*GLX_RGBA_BIT*/,
            5 /*GLX_DOUBLEBUFFER*/, 1,
            8 /*GLX_RED_SIZE*/, 8,
            9 /*GLX_GREEN_SIZE*/, 8,
            10 /*GLX_BLUE_SIZE*/, 8,
            12 /*GLX_DEPTH_SIZE*/, 24,
            0
        };
        int count = 0;
        GLXFBConfig* fbcs = g_x11.glXChooseFBConfig(dpy, screen, fbAttribs, &count);
        if (fbcs && count > 0) {
            chosenFbc = fbcs[0];
            vi = g_x11.glXGetVisualFromFBConfig(dpy, chosenFbc);
            g_x11.XFree(fbcs);
        }
    }
    if (!vi) {
        int visAttribs[] = {4 /*GLX_RGBA*/, 12 /*GLX_DEPTH_SIZE*/, 24, 5 /*GLX_DOUBLEBUFFER*/, 0};
        vi = g_x11.glXChooseVisual(dpy, screen, visAttribs);
    }
    if (!vi) {
        g_x11.XCloseDisplay(dpy);
        return nullptr;
    }

    Colormap cmap = g_x11.XCreateColormap(dpy, root, vi->visual, 0 /*AllocNone*/);
    XSetWindowAttributes swa{};
    swa.colormap = cmap;
    swa.border_pixel = 0;
    swa.event_mask = MaskKeyPress | MaskKeyRelease | MaskButtonPress | MaskButtonRelease |
                     MaskPointerMotion | MaskStructureNotify | MaskFocusChange;

    XWindow xwin = g_x11.XCreateWindow(
        dpy, root, 0, 0, static_cast<unsigned int>(width), static_cast<unsigned int>(height),
        0, vi->depth, 1 /*InputOutput*/, vi->visual,
        CWBorderPixel | CWColormap | CWEventMask, &swa);

    g_x11.XStoreName(dpy, xwin, title);
    Atom wmDelete = g_x11.XInternAtom(dpy, "WM_DELETE_WINDOW", 0);
    g_x11.XSetWMProtocols(dpy, xwin, &wmDelete, 1);
    g_x11.XMapWindow(dpy, xwin);
    g_x11.XFlush(dpy);

    GLXContext ctx = nullptr;
    using PFN_glXCreateContextAttribsARB = GLXContext (*)(Display*, GLXFBConfig, GLXContext, Bool, const int*);
    auto createAttribs = g_x11.glXGetProcAddressARB
        ? reinterpret_cast<PFN_glXCreateContextAttribsARB>(
              g_x11.glXGetProcAddressARB(reinterpret_cast<const unsigned char*>("glXCreateContextAttribsARB")))
        : nullptr;
    if (createAttribs && chosenFbc) {
        int ctxAttribs[] = {
            0x2091 /*GLX_CONTEXT_MAJOR_VERSION_ARB*/, 3,
            0x2092 /*GLX_CONTEXT_MINOR_VERSION_ARB*/, 3,
            0x9126 /*GLX_CONTEXT_PROFILE_MASK_ARB*/, 1 /*GLX_CONTEXT_CORE_PROFILE_BIT_ARB*/,
            0
        };
        ctx = createAttribs(dpy, chosenFbc, nullptr, 1, ctxAttribs);
    }
    if (!ctx) ctx = g_x11.glXCreateContext(dpy, vi, nullptr, 1);
    g_x11.XFree(vi);
    if (!ctx || !g_x11.glXMakeCurrent(dpy, xwin, ctx)) {
        if (ctx) g_x11.glXDestroyContext(dpy, ctx);
        g_x11.XDestroyWindow(dpy, xwin);
        g_x11.XCloseDisplay(dpy);
        return nullptr;
    }

    char zeroBits[1] = {0};
    Pixmap bm = g_x11.XCreateBitmapFromData(dpy, xwin, zeroBits, 1, 1);
    XColor dummy{};
    Cursor hiddenCur = g_x11.XCreatePixmapCursor(dpy, bm, bm, &dummy, &dummy, 0, 0);
    g_x11.XFreePixmap(dpy, bm);

    auto* win = new Window{};
    win->dpy = dpy;
    win->xwin = xwin;
    win->cmap = cmap;
    win->glx = ctx;
    win->wmDelete = wmDelete;
    win->hiddenCursor = hiddenCur;
    win->width = width;
    win->height = height;
    g_activeDpy = dpy;
    g_activeWin = xwin;
    return win;
}

void destroyWindow(Window* win) {
    if (!win) return;
    if (win->dpy) {
        g_x11.glXMakeCurrent(win->dpy, 0, nullptr);
        if (win->glx) g_x11.glXDestroyContext(win->dpy, win->glx);
        if (win->hiddenCursor) g_x11.XFreeCursor(win->dpy, win->hiddenCursor);
        if (win->xwin) g_x11.XDestroyWindow(win->dpy, win->xwin);
        g_x11.XCloseDisplay(win->dpy);
    }
    g_activeDpy = nullptr;
    g_activeWin = 0;
    delete win;
}

void swapBuffers(Window* win) {
    if (win && win->dpy && win->xwin) g_x11.glXSwapBuffers(win->dpy, win->xwin);
}

void setSwapInterval(int interval) {
    if (!g_x11.glXGetProcAddressARB || !g_activeDpy || !g_activeWin) return;
    using PFN_glXSwapIntervalEXT = void (*)(Display*, XID, int);
    auto swapExt = reinterpret_cast<PFN_glXSwapIntervalEXT>(
        g_x11.glXGetProcAddressARB(reinterpret_cast<const unsigned char*>("glXSwapIntervalEXT")));
    if (swapExt) {
        swapExt(g_activeDpy, g_activeWin, interval);
        return;
    }
    using PFN_glXSwapIntervalMESA = int (*)(unsigned int);
    auto swapMesa = reinterpret_cast<PFN_glXSwapIntervalMESA>(
        g_x11.glXGetProcAddressARB(reinterpret_cast<const unsigned char*>("glXSwapIntervalMESA")));
    if (swapMesa) swapMesa(static_cast<unsigned int>(interval));
}

void setRelativeMouseMode(Window* win, bool enabled) {
    if (!win || !win->dpy || !win->xwin) return;
    win->relativeMouse = enabled;
    if (enabled) {
        g_x11.XDefineCursor(win->dpy, win->xwin, win->hiddenCursor);
        win->skipNextCenter = true;
        g_x11.XWarpPointer(win->dpy, 0, win->xwin, 0, 0, 0, 0, win->width / 2, win->height / 2);
    } else {
        g_x11.XUndefineCursor(win->dpy, win->xwin);
    }
    g_x11.XFlush(win->dpy);
}

bool pollEvent(Window* win, Event& out) {
    if (!win || !win->dpy) return false;
    while (g_x11.XPending(win->dpy) > 0) {
        XEventPadded xev{};
        g_x11.XNextEvent(win->dpy, &xev);

        if (xev.type == X_ClientMessage && static_cast<Atom>(xev.xclient.data.l[0]) == win->wmDelete) {
            out = Event{};
            out.type = EventType::Quit;
            return true;
        }
        if (xev.type == X_ConfigureNotify) {
            if (xev.xconfigure.width != win->width || xev.xconfigure.height != win->height) {
                win->width = std::max(1, xev.xconfigure.width);
                win->height = std::max(1, xev.xconfigure.height);
                out = Event{};
                out.type = EventType::WindowResized;
                out.width = win->width;
                out.height = win->height;
                return true;
            }
            continue;
        }
        if (xev.type == X_FocusOut) {
            win->focused = false;
            std::fill(std::begin(win->keyDown), std::end(win->keyDown), false);
            out = Event{};
            out.type = EventType::FocusLost;
            return true;
        }
        if (xev.type == X_FocusIn) {
            win->focused = true;
            std::fill(std::begin(win->keyDown), std::end(win->keyDown), false);
            out = Event{};
            out.type = EventType::FocusGained;
            return true;
        }
        if (xev.type == X_KeyPress || xev.type == X_KeyRelease) {
            bool isRepeat = false;
            if (xev.type == X_KeyRelease && g_x11.XEventsQueued(win->dpy, 1 /*QueuedAfterReading*/) > 0) {
                XEventPadded nextEv{};
                g_x11.XPeekEvent(win->dpy, &nextEv);
                if (nextEv.type == X_KeyPress && nextEv.xkey.time == xev.xkey.time &&
                    nextEv.xkey.keycode == xev.xkey.keycode) {
                    continue; // Skip synthetic X11 auto-repeat KeyRelease
                }
            }
            char textBuf[16]{};
            KeySym ks = 0;
            int len = g_x11.XLookupString(&xev.xkey, textBuf, sizeof(textBuf) - 1, &ks, nullptr);
            Key k = mapKeySym(ks);
            if (k != Key::Unknown) {
                if (xev.type == X_KeyPress && win->keyDown[static_cast<size_t>(k)]) isRepeat = true;
                win->keyDown[static_cast<size_t>(k)] = (xev.type == X_KeyPress);
            }

            out = Event{};
            out.type = (xev.type == X_KeyPress) ? EventType::KeyDown : EventType::KeyUp;
            out.key = k;
            out.repeat = isRepeat;
            out.ctrl = (xev.xkey.state & 4u) != 0;
            if (xev.type == X_KeyPress && len == 1 && textBuf[0] >= 32 && textBuf[0] < 127) {
                out.textChar = textBuf[0];
            }
            return true;
        }
        if (xev.type == X_MotionNotify) {
            if (win->relativeMouse && win->focused) {
                int cx = win->width / 2;
                int cy = win->height / 2;
                if (xev.xmotion.x == cx && xev.xmotion.y == cy) {
                    win->skipNextCenter = false;
                    continue;
                }
                int dx = xev.xmotion.x - cx;
                int dy = xev.xmotion.y - cy;
                g_x11.XWarpPointer(win->dpy, 0, win->xwin, 0, 0, 0, 0, cx, cy);
                g_x11.XFlush(win->dpy);
                out = Event{};
                out.type = EventType::MouseMove;
                out.xrel = static_cast<float>(dx);
                out.yrel = static_cast<float>(dy);
                return true;
            } else {
                out = Event{};
                out.type = EventType::MouseMove;
                out.x = static_cast<float>(xev.xmotion.x);
                out.y = static_cast<float>(xev.xmotion.y);
                return true;
            }
        }
        if (xev.type == X_ButtonPress || xev.type == X_ButtonRelease) {
            MouseButton mb = MouseButton::None;
            if (xev.xbutton.button == 1) mb = MouseButton::Left;
            else if (xev.xbutton.button == 3) mb = MouseButton::Right;
            else if (xev.xbutton.button == 2) mb = MouseButton::Middle;
            if (mb == MouseButton::None) continue;
            out = Event{};
            out.type = (xev.type == X_ButtonPress) ? EventType::MouseButtonDown : EventType::MouseButtonUp;
            out.button = mb;
            out.x = static_cast<float>(xev.xbutton.x);
            out.y = static_cast<float>(xev.xbutton.y);
            return true;
        }
    }
    return false;
}

bool isKeyDown(const Window* win, Key k) {
    if (!win || k >= Key::Count) return false;
    return win->keyDown[static_cast<size_t>(k)];
}

void* glGetProcAddress(const char* name) {
    if (g_x11.glXGetProcAddressARB) {
        void* p = g_x11.glXGetProcAddressARB(reinterpret_cast<const unsigned char*>(name));
        if (p) return p;
    }
    return g_x11.libGL ? dlsym(g_x11.libGL, name) : nullptr;
}

uint64_t getTicksMs() {
    static const auto t0 = std::chrono::steady_clock::now();
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - t0).count());
}

struct AudioOutput {
    void* libAsound = nullptr;
    void* pcm = nullptr;
    std::atomic<bool> stopFlag{false};
    std::thread worker;
};

AudioOutput* startAudioOutput(int sampleRate, AudioCallback cb, void* userdata) {
    if (!cb) return nullptr;
    void* lib = dlopen("libasound.so.2", RTLD_LAZY);
    if (!lib) return nullptr;

    auto pcmOpen = reinterpret_cast<int (*)(void**, const char*, int, int)>(dlsym(lib, "snd_pcm_open"));
    auto pcmSetParams = reinterpret_cast<int (*)(void*, int, int, unsigned int, unsigned int, int, unsigned int)>(dlsym(lib, "snd_pcm_set_params"));
    auto pcmWritei = reinterpret_cast<long (*)(void*, const void*, unsigned long)>(dlsym(lib, "snd_pcm_writei"));
    auto pcmRecover = reinterpret_cast<int (*)(void*, int, int)>(dlsym(lib, "snd_pcm_recover"));
    auto pcmClose = reinterpret_cast<int (*)(void*)>(dlsym(lib, "snd_pcm_close"));
    if (!pcmOpen || !pcmSetParams || !pcmWritei || !pcmRecover || !pcmClose) {
        dlclose(lib);
        return nullptr;
    }

    void* pcm = nullptr;
    if (pcmOpen(&pcm, "default", 0 /*PLAYBACK*/, 0) < 0) {
        dlclose(lib);
        return nullptr;
    }
    // SND_PCM_FORMAT_S16_LE = 2, SND_PCM_ACCESS_RW_INTERLEAVED = 3
    if (pcmSetParams(pcm, 2, 3, 2, static_cast<unsigned int>(sampleRate), 1, 50000) < 0) {
        pcmClose(pcm);
        dlclose(lib);
        return nullptr;
    }

    auto* ao = new AudioOutput{};
    ao->libAsound = lib;
    ao->pcm = pcm;
    ao->worker = std::thread([ao, cb, userdata, pcmWritei, pcmRecover]() {
        constexpr int FRAMES = 512;
        std::vector<float> flt(FRAMES * 2, 0.0f);
        std::vector<int16_t> pcmBuf(FRAMES * 2, 0);
        while (!ao->stopFlag.load(std::memory_order_relaxed)) {
            std::fill(flt.begin(), flt.end(), 0.0f);
            cb(userdata, flt.data(), FRAMES);
            for (int i = 0; i < FRAMES * 2; ++i) {
                float v = std::clamp(flt[static_cast<size_t>(i)], -1.0f, 1.0f);
                pcmBuf[static_cast<size_t>(i)] = static_cast<int16_t>(v * 32767.0f);
            }
            long r = pcmWritei(ao->pcm, pcmBuf.data(), FRAMES);
            if (r < 0) pcmRecover(ao->pcm, static_cast<int>(r), 1);
        }
    });
    return ao;
}

void stopAudioOutput(AudioOutput* audio) {
    if (!audio) return;
    audio->stopFlag.store(true, std::memory_order_relaxed);
    if (audio->worker.joinable()) audio->worker.join();
    if (audio->libAsound) {
        if (audio->pcm) {
            auto pcmClose = reinterpret_cast<int (*)(void*)>(dlsym(audio->libAsound, "snd_pcm_close"));
            if (pcmClose) pcmClose(audio->pcm);
        }
        dlclose(audio->libAsound);
    }
    delete audio;
}

} // namespace mega::platform

#endif
