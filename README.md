# SpireEngine




SpireEngine is a Vulkan-based, cross-platform mini game engine that uses Slang as its shader compiler.

## Build Status
| Platform | Build Status |
|:--------:|:------------:|
| Windows | [![Build status](https://ci.appveyor.com/api/projects/status/cde7o78cyqxbaius?svg=true)](https://ci.appveyor.com/project/csyonghe/spire-engine) |
| Linux | [![Build Status](https://travis-ci.com/spire-engine/engine.svg?branch=master)](https://travis-ci.com/spire-engine/engine) |

![](https://github.com/csyonghe/SpireMiniEngineExtBinaries/blob/master/screenshot1.png)

## Build
SpireEngine currently runs on both Windows and Linux.
### Windows
- Run "prepare.ps1" script, which downloads the Autodesk FBX SDK binaries required for building ModelImporter.
- Open "GameEngine.sln" in Visual Studio 2019.
- Build the solution. 
### Linux
SpireEngine does not depend on any additional libraries except Xlib on Linux platforms. Just run `make` in the project root directory.
```
make -j16
```
The makefile script will automatically check and install Xlib if it does not exist on the system.

SpireEngine can be built by both `g++` and `clang++`. You can use the `CXX` environment variable to explicitly specify which compiler to use, for example:
```
CXX=clang++ make -j16
```

## Run
To run SpireEngine, you need to download addtional assets. Run the `get_examplegame.ps1` script on Windows, or the `get_examplegame.sh` script on Linux. After the assets have been downloaded, follow these steps to run engine:

### Windows
- In Visual Studio, set GameEngine as start-up project.
- Right click GameEngine project and set the following start-up arguments in Debug settings:
```
-enginedir "$(SolutionDir)EngineContent" -dir "$(SolutionDir)ExampleGame" -level "level0.level"
```
- Run.

### Linux
```
build/linux-x86-64/release/GameEngine -enginedir EngineContent -dir ExampleGame -level "level0.level"
```

## Editor Mode
To run the engine in editor mode, pass `-editor` command line argument when launching GameEngine.

## Render Video Output
SpireEngine supports rendering to video files. You can use the following command line arguments:
- `-reclen <time_in_seconds>`: specifies the length of video output, in seconds.
- `-recdir <mp4_filename_or_directory>`: specifies the location of the ouput video. If a *.mp4 filename is provided, SpireEngine will directly encode the resulting video as an H.264 video file. If a directory name is provided, SpireEngine will output individual images for each frame to the directory.

## Headless Mode
If you need to run SpireEngine in a non-desktop environment, you can pass the `-headless` argument to start without a window. This can be useful when rendering videos on a server through a console interface.
