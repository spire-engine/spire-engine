#ifndef LINUX_VIRTUAL_KEYS_H
#define LINUX_VIRTUAL_KEYS_H

namespace GameEngine
{
    enum class VirtualKeys
    {
        LMouse = 0x01,
        RMouse = 0x02,
        MMouse = 0x04,
        Backspace = 0x08,
        Tab = 0x09,
        Return = 0x0D,
        Shift = 0x10,
        Ctrl = 0x11,
        Alt = 0x12,
        Capslock = 0x14,
        PageUp = 0x21,
        PageDown = 0x22,
        End = 0x23,
        Space = 0x20,
        Home = 0x24,
        Left = 0x25,
        Up = 0x26,
        Right = 0x27,
        Down = 0x28,
        Delete = 0x2E,
        F1 = 0x70,
        F2 = 0x71,
        F3 = 0x72,
        F4 = 0x73,
        F5 = 0x74,
        F6 = 0x75,
        F7 = 0x76,
        F8 = 0x77,
        F9 = 0x78,
        F10 = 0x79,
        F11 = 0x7A,
        F12 = 0x7B,
    };
}

#endif