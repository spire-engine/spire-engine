#if defined(__linux__)

#include "CoreLib/Basic.h"
#include "CoreLib/LibUI/KeyCode.h"
#include <X11/keysym.h>
#include <X11/Xlib.h>

// Translates X11 key code values to Win32 Virtual Key values

using namespace CoreLib;

namespace GameEngine
{
    CoreLib::Dictionary<int, int> keyCodeMap;

    struct Win32KeyCode
    {
        int vKeyCode;
        int keySym;
    };

    Win32KeyCode keys[] =
    {
        {Keys::Left, XK_Left},
        {Keys::Up, XK_Up},
        {Keys::Down, XK_Down},
        {Keys::Right, XK_Right},
        {Keys::Escape, XK_Escape},
        {Keys::Return, XK_Return},
        {Keys::Space, XK_space},
        {Keys::Shift, XK_Shift_L},
        {Keys::Shift, XK_Shift_R},
        {Keys::Ctrl, XK_Control_L},
        {Keys::Ctrl, XK_Control_R},
        {Keys::Alt, XK_Alt_L},
        {Keys::Alt, XK_Alt_R},
        {Keys::Backspace, XK_BackSpace},
        {Keys::Delete, XK_Delete},
        {Keys::Home, XK_Home},
        {Keys::End, XK_End},
        {Keys::PageUp, XK_Page_Up},
        {Keys::PageDown, XK_Page_Down},
        {Keys::Insert, XK_Insert},
        {Keys::Tab, XK_Tab},
        {Keys::A, 0x41},
        {Keys::B, 0x42},
        {Keys::C, 0x43},
        {Keys::D, 0x44},
        {Keys::E, 0x45},
        {Keys::F, 0x46},
        {Keys::G, 0x47},
        {Keys::H, 0x48},
        {Keys::I, 0x49},
        {Keys::J, 0x4A},
        {Keys::K, 0x4B},
        {Keys::L, 0x4C},
        {Keys::M, 0x4D},
        {Keys::N, 0x4E},
        {Keys::O, 0x4F},
        {Keys::P, 0x50},
        {Keys::Q, 0x51},
        {Keys::R, 0x52},
        {Keys::S, 0x53},
        {Keys::T, 0x54},
        {Keys::U, 0x55},
        {Keys::V, 0x56},
        {Keys::W, 0x57},
        {Keys::X, 0x58},
        {Keys::Y, 0x59},
        {Keys::Z, 0x5A},
        {Keys::Semicolon, XK_semicolon},
        {Keys::Comma, XK_comma},
        {Keys::Dot, XK_period},
        {Keys::Slash, XK_slash},
        {Keys::Quote, XK_apostrophe},
        {Keys::LBracket, XK_bracketleft},
        {Keys::RBracket, XK_bracketright},
        {Keys::Backslash, XK_backslash},
        {Keys::Minus, XK_minus},
        {Keys::Plus, XK_equal},
        {Keys::Tilde, XK_asciitilde},
        {Keys::Key0, 0x30},
        {Keys::Key1, 0x31},
        {Keys::Key2, 0x32},
        {Keys::Key3, 0x33},
        {Keys::Key4, 0x34},
        {Keys::Key5, 0x35},
        {Keys::Key6, 0x36},
        {Keys::Key7, 0x37},
        {Keys::Key8, 0x38},
        {Keys::Key9, 0x39},
        {Keys::F1, XK_F1},
        {Keys::F2, XK_F2},
        {Keys::F3, XK_F3},
        {Keys::F4, XK_F4},
        {Keys::F5, XK_F5},
        {Keys::F6, XK_F6},
        {Keys::F7, XK_F7},
        {Keys::F8, XK_F8},
        {Keys::F9, XK_F9},
        {Keys::F10, XK_F10},
        {Keys::F11, XK_F11},
        {Keys::F12, XK_F12}
    };

    void InitKeyCodeTranslationTable(Display* display)
    {
        for (auto entry : keys)
        {
            auto systemKeyCode = XKeysymToKeycode(display, entry.keySym);
            keyCodeMap[systemKeyCode] = entry.vKeyCode;
        }
    }

    void FreeKeyCodeTranslationTable()
    {
        keyCodeMap = decltype(keyCodeMap)();
    }

    int TranslateKeyCode(int keyCode)
    {
        int result = 0;
        keyCodeMap.TryGetValue(keyCode, result);
        return result;
    }

    int GetKeyChar(int keyCode, int keyState)
    {
        bool shift = (keyState & ShiftMask) != 0;
        if (keyCode >= CoreLib::Keys::A && keyCode <= CoreLib::Keys::Z )
        {
            bool capslock = (keyState & LockMask) != 0;
            bool isCapital = capslock ^ shift;
            if (isCapital)
                return keyCode;
            else
                return keyCode + ('a'-'A');
        }
        else if (keyCode == CoreLib::Keys::Space)
        {
            return ' ';
        }
        else if (keyCode >= CoreLib::Keys::Key0 && keyCode <= CoreLib::Keys::Key9)
        {
            if (!shift)
                return keyCode;
            else
            {
                switch (keyCode)
                {
                    case CoreLib::Keys::Key0:
                        return ')';
                    case CoreLib::Keys::Key1:
                        return '!';
                    case CoreLib::Keys::Key2:
                        return '@';
                    case CoreLib::Keys::Key3:
                        return '#';
                    case CoreLib::Keys::Key4:
                        return '$';
                    case CoreLib::Keys::Key5:
                        return '%';
                    case CoreLib::Keys::Key6:
                        return '^';
                    case CoreLib::Keys::Key7:
                        return '&';
                    case CoreLib::Keys::Key8:
                        return '*';
                    case CoreLib::Keys::Key9:
                        return '(';
                }
            }
        }
        if (shift)
        {
            switch (keyCode)
            {
            case CoreLib::Keys::Semicolon:
                return ':';
            case CoreLib::Keys::Comma:
                return '<';
            case CoreLib::Keys::Dot:
                return '>';
            case CoreLib::Keys::Slash:
                return '?';
            case CoreLib::Keys::Quote:
                return '\"';
            case CoreLib::Keys::LBracket:
                return '{';
            case CoreLib::Keys::RBracket:
                return '}';
            case CoreLib::Keys::Backslash:
                return '|';
            case CoreLib::Keys::Minus:
                return '_';
            case CoreLib::Keys::Plus:
                return '+';
            case CoreLib::Keys::Tilde:
                return '~';
            default:
                return 0;
            }
        }
        else
        {
            switch (keyCode)
            {
            case CoreLib::Keys::Semicolon:
                return ';';
            case CoreLib::Keys::Comma:
                return ',';
            case CoreLib::Keys::Dot:
                return '.';
            case CoreLib::Keys::Slash:
                return '/';
            case CoreLib::Keys::Quote:
                return '\'';
            case CoreLib::Keys::LBracket:
                return '[';
            case CoreLib::Keys::RBracket:
                return ']';
            case CoreLib::Keys::Backslash:
                return '\\';
            case CoreLib::Keys::Minus:
                return '-';
            case CoreLib::Keys::Plus:
                return '=';
            case CoreLib::Keys::Tilde:
                return '`';
            default:
                return 0;
            }
        }
    }
} // namespace GameEngine

#endif