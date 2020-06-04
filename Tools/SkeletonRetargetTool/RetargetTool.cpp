#include "Engine.h"
#include "CoreLib/WinForm/WinForm.h"
#include "CoreLib/WinForm/WinApp.h"
#include "CoreLib/LibUI/LibUI.h"
#include "OS.h"

using namespace CoreLib::WinForm;
using namespace GameEngine;

void InitUI(SystemWindow * window)
{
	window->SetText("Skeleton Retargeting Tool");
}

String RemoveQuote(String dir)
{
	if (dir.StartsWith("\""))
		return dir.SubString(1, dir.Length() - 2);
	return dir;
}

const char * levelSrc = R"(
Camera
{
	name "FreeCam"
	orientation[-0.044 0.000 0.000]
	position[18.77 108.36 280.13]
	znear 5.0
	zfar 8000.0
	fov 60.0
}

ArcBallCameraController
{
	name "CamControl"
	TargetCameraName "FreeCam"

	Radius 500.00
	Center[0.0 50.0 0.0]
	Alpha 1.57
	Beta 0.439
	NeedAlt true
}

DirectionalLight
{
	Name "DirectionalLight0"
	CastShadow true
	RenderCustomDepth false
	LocalTransform [ 0.89646453 -0.43586737 -0.079817578 0 0.42636201 0.79940009 0.42329049 0 -0.1206923 -0.41349608 0.90247124 0 26.584183 343.18619 312.37769 1 ]
	Mobility 1
	EnableShadows 2
	Radius 0
	ShadowDistance 2500
	NumShadowCascades 4
	TransitionFactor 0.80000001
	Color [ 3 3 3 ]
	Ambient 0.2
}

EnvMap
{
	name "envMap"
	LocalTransform[1 0 0 0   0 1 0 0    0 0 1 0    0 5000 0 1]
	Radius 2000000.0
	TintColor [0.5 0.4 0.3]
}

ToneMapping
{
	name "tonemapping"
	Exposure 2
	MinLuminancePercentile 0.4
	MaxLuminancePercentile 0.90000004
	MinLuminance 1
	MaxLuminance 5
	AdaptSpeedUp 1.5
	AdaptSpeedDown 3.5
	ColorLUT ""
}
Atmosphere
{
	Name "atmosphere"
	CastShadow true
	RenderCustomDepth false
	LocalTransform [ 1 0 0 0 0 1 0 0 0 0 1 0 0 0 0 1 ]
	SunDir [ -0.48795006 0.78072011 0.39036006 ]
	AtmosphericFogScaleFactor 0.5
}
AmbientLight
{
    name "ambientLight"
    Ambient [0.4 0.3 0.3]
}

SkeletonRetargetVisualizer
{
    name "retargetEditor"
}
)";

void RegisterRetargetActor();

int __stdcall wWinMain(
	HINSTANCE /*hInstance*/,
	HINSTANCE /*hPrevInstance*/,
	LPWSTR     /*lpCmdLine*/,
	int       /*nCmdShow*/
)
{
	Application::Init();
	try
	{
		EngineInitArguments args;
		auto & appParams = args.LaunchParams;
		int w = 1920;
		int h = 1080;
#ifdef _WIN32
        args.API = RenderAPI::D3D12;
#else
        args.API = RenderAPI::Vulkan;
#endif
		args.GpuId = 0;
		args.Width = w;
		args.Height = h;
		args.NoConsole = true;
		CommandLineParser parser(Application::GetCommandLine());
		if (parser.OptionExists("-vk"))
			args.API = RenderAPI::Vulkan;
		if (parser.OptionExists("-dir"))
			args.GameDirectory = RemoveQuote(parser.GetOptionValue("-dir"));
		if (parser.OptionExists("-enginedir"))
			args.EngineDirectory = RemoveQuote(parser.GetOptionValue("-enginedir"));
		args.RecompileShaders = false;
		{
			Engine::Instance()->Init(args);
			RegisterRetargetActor();
			InitUI(Engine::Instance()->GetMainWindow());
			Engine::Instance()->LoadLevelFromText(levelSrc);
			
			Engine::Run();
			Engine::Destroy();
		}
	}
	catch (...)
	{
	}
	Application::Dispose();
}