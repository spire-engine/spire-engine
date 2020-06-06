#include "CoreLib/Basic.h"
#include "CoreLib/LibIO.h"
#include "CoreLib/Imaging/Bitmap.h"
#include <Windows.h>

using namespace CoreLib;

bool appveyorMode = false;

void StartProcess(String commandLine)
{
    STARTUPINFO si;
    PROCESS_INFORMATION pi;

    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    ZeroMemory(&pi, sizeof(pi));

    CreateProcess(
        nullptr, (wchar_t *)commandLine.ToWString(), nullptr, nullptr, 0, 0, nullptr, nullptr, &si, &pi);

    // Wait until child process exits.
    WaitForSingleObject(pi.hProcess, INFINITE);

    // Close process and thread handles.
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
}

void Success(String testName)
{
    printf("SUCCESS: %S\n", testName.ToWString());
    if (appveyorMode)
    {
        StartProcess("appveyor AddTest \"" + testName +
                     "\" -FileName \"test\" -Framework \"SpireEngineTest\" -Outcome \"Passed\"");
    }
}

void Fail(String testName)
{
    printf("FAIL: %S\n", testName.ToWString());
    if (appveyorMode)
    {
        StartProcess("appveyor AddTest \"" + testName +
                     "\" -FileName \"test\" -Framework \"SpireEngineTest\" -Outcome \"Failed\"");
    }
}

int wmain(int argc, const wchar_t** argv)
{
    String binDir = "";
    String slnDir = "";
    for (int i = 0; i < argc; i++)
    {
        if (String::FromWString(argv[i]) == "-bindir")
        {
            binDir = String::FromWString(argv[i + 1]);
            i++;
        }
        else if (String::FromWString(argv[i]) == "-slndir")
        {
            slnDir = String::FromWString(argv[i + 1]);
            i++;
        }
        else if (String::FromWString(argv[i]) == "-appveyor")
        {
            appveyorMode = true;
        }
    }

    auto exePath = IO::Path::Combine(binDir, "GameEngine.exe");
    StringBuilder cmdBuilder;
    cmdBuilder << exePath << " "
               << "-dir \"" << IO::Path::Combine(slnDir, "ExampleGame") << "\" -enginedir \""
               << IO::Path::Combine(slnDir, "EngineContent")
               << "\" -level \"level0.level\""
               << " -runforframes 1"
               << " -recdir \"\""
               << " -width 1280 -height 720 -forcedpi 96"
               << " -d3dwarp -headless";

    StartProcess(cmdBuilder.ToString());

    try
    {
        CoreLib::Imaging::Bitmap expected =
            CoreLib::Imaging::Bitmap(IO::Path::Combine(slnDir, "ExampleGame", "level0-expected.bmp"));
        CoreLib::Imaging::Bitmap actual =
            CoreLib::Imaging::Bitmap("0.bmp");
        if (actual.GetWidth() == expected.GetWidth() && actual.GetHeight() == expected.GetHeight())
        {
            int diffCount = 0;
            for (int i = 0; i < actual.GetWidth() * actual.GetHeight() * 4; i++)
            {
                if (actual.GetPixels()[i] != expected.GetPixels()[i])
                {
                    diffCount++;
                    if (diffCount > 200)
                    {
                        Fail("Smoke");
                        return -1;
                    }
                }
            }
            Success("Smoke");
        }
        else
        {
            Fail("Smoke");
            return -1;
        }
    }
    catch (IO::IOException)
    {
        Fail("Smoke");
        return -1;
    }
    return 0;
}