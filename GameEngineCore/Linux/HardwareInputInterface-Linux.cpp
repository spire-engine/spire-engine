#if defined(__linux__)

#include "HardwareInputInterface.h"
#include "CoreLib/Basic.h"
#include "SystemWindow-Linux.h"
#include "OsApplicationContext-Linux.h"

namespace GameEngine
{
	using namespace CoreLib::Basic;

	// Defined in OS-Linux.cpp
    LinuxApplicationContext* GetLinuxApplicationContext();

	class LinuxHardwareInputInterface : public HardwareInputInterface
	{
	private:
		LinuxSystemWindow* sysWindow = nullptr;
	public:
		LinuxHardwareInputInterface(WindowHandle window)
		{
			auto context = GetLinuxApplicationContext();
			context->systemWindows.TryGetValue(window.window, sysWindow);
		}
		virtual KeyStateQueryResult QueryKeyState(wchar_t key) override
		{
			auto context = GetLinuxApplicationContext();
			KeyStateQueryResult rs = {};
			if (key < KeyStateTableSize)
			{
				rs.IsDown = context->keyStates[key] != KeyState::Released;
				rs.HasPressed = context->keyStates[key] == KeyState::Hold;
			}
			return rs;
		}

		virtual void QueryCursorPosition(int& x, int& y) override
		{
			x = sysWindow->cursorX;
			y = sysWindow->cursorY;
		}

		virtual void SetCursorPosition(int x, int y) override
		{
			
		}

		virtual void SetCursorVisiblity(bool visible) override
		{
		}
	};

	HardwareInputInterface* CreateHardwareInputInterface(WindowHandle window)
	{
		return new LinuxHardwareInputInterface(window);
	}
}
#endif