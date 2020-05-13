#if defined(__linux__)

#include "HardwareInputInterface.h"
#include "CoreLib/Basic.h"

namespace GameEngine
{
	using namespace CoreLib::Basic;

	class LinuxHardwareInputInterface : public HardwareInputInterface
	{
	private:
	public:
		LinuxHardwareInputInterface(WindowHandle window)
		{
		}
		virtual KeyStateQueryResult QueryKeyState(wchar_t key) override
		{
			KeyStateQueryResult rs;
			rs.IsDown = false;
			rs.HasPressed = false;
			return rs;
		}

		virtual void QueryCursorPosition(int& x, int& y) override
		{
			
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