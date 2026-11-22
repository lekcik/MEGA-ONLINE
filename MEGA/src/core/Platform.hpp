#pragma once

#include <cstdint>

namespace mega::platform {

enum class Key : uint16_t {
    Unknown = 0,
    W, A, S, D, E, F, G, H, I, M, N, Q, R, T, V, X, Y, Z, C, B,
    Num1, Num2, Num3,
    Space, LShift, LCtrl, Tab, Enter, Escape, Backspace,
    Up, Down, Left, Right,
    F1, F5,
    LeftBracket, RightBracket, Period, Comma,
    Count
};

enum class EventType : uint8_t {
    Quit = 0,
    WindowResized,
    FocusLost,
    FocusGained,
    KeyDown,
    KeyUp,
    TextInput,
    MouseMove,
    MouseButtonDown,
    MouseButtonUp
};

enum class MouseButton : uint8_t {
    None = 0,
    Left = 1,
    Right = 2,
    Middle = 3
};

struct Event {
    EventType type = EventType::Quit;
    int width = 0;
    int height = 0;
    Key key = Key::Unknown;
    bool ctrl = false;
    bool repeat = false;
    char textChar = 0;
    float x = 0.0f;
    float y = 0.0f;
    float xrel = 0.0f;
    float yrel = 0.0f;
    MouseButton button = MouseButton::None;
};

struct Window;
struct AudioOutput;

Window* createWindow(const char* title, int width, int height);
void destroyWindow(Window* win);
void swapBuffers(Window* win);
void setSwapInterval(int interval);
void setRelativeMouseMode(Window* win, bool enabled);
bool pollEvent(Window* win, Event& out);
bool isKeyDown(const Window* win, Key k);
void* glGetProcAddress(const char* name);
uint64_t getTicksMs();

using AudioCallback = void (*)(void* userdata, float* outStereo, int frames);
AudioOutput* startAudioOutput(int sampleRate, AudioCallback cb, void* userdata);
void stopAudioOutput(AudioOutput* audio);

} // namespace mega::platform
