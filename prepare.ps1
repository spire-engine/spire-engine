Add-Type -assembly "system.io.compression.filesystem"
[Net.ServicePointManager]::SecurityProtocol = "tls12, tls11, tls"
if (-NOT ((Test-Path '.\ExternalLibs\FbxSDK\x64\debug\libfbxsdk.lib') -AND (Test-Path '.\ExternalLibs\FbxSDK\x64\release\libfbxsdk.lib') -AND (Test-Path '.\ExternalLibs\FbxSDK\x86\release\libfbxsdk.lib')  -AND (Test-Path '.\ExternalLibs\FbxSDK\x86\debug\libfbxsdk.lib')))
{
    "Preparing dependencies..."
    "Downloading Autodesk FBX SDK..."
    Invoke-WebRequest -Uri 'https://github.com/csyonghe/SpireMiniEngineExtBinaries/raw/master/fbxsdk.zip' -OutFile 'binaries.zip'
    if (Test-Path 'ExternalLibs')
    {
        Remove-Item -Recurse -Force 'ExternalLibs'
    }
    New-Item -ItemType directory -Path "ExternalLibs" | Out-Null
    New-Item -ItemType directory -Path "ExternalLibs\FbxSDK" | Out-Null
    [System.IO.Compression.ZipFile]::ExtractToDirectory("binaries.zip", '.\ExternalLibs\FbxSDK\')  | Out-Null
    Remove-Item 'binaries.zip'  | Out-Null
}

if (-NOT ((Test-path '.\ExternalLibs\Slang\') -AND (Test-Path "x64\Debug\slang.dll") -AND (Test-Path "x64\Release\slang.dll")))
{
    "Downloading Slang..."
    New-Item -ItemType directory -Path .\ExternalLibs\Slang\ | Out-Null
    Invoke-WebRequest -Uri "https://github.com/shader-slang/slang/releases/download/v0.15.12/slang-0.15.12-win64.zip" -OutFile 'slang-x64.zip'
    Expand-Archive -Path "slang-x64.zip" -DestinationPath '.\ExternalLibs\Slang\' -Force
    Remove-Item 'slang-x64.zip'
  
    if (-not (Test-path '.\x64\Debug')) {
        New-Item .\x64\Debug -ItemType Directory | Out-Null
    }
    if (-not (Test-path '.\x64\Release')) {
        New-Item .\x64\Release -ItemType Directory | Out-Null
    }
    
    Copy-Item -Path ".\ExternalLibs\Slang\bin\windows-x64\release\slang.dll" "x64\Debug\slang.dll"
    Copy-Item -Path ".\ExternalLibs\Slang\bin\windows-x64\release\slang.dll" "x64\Release\slang.dll"
    Copy-Item -Path ".\ExternalLibs\Slang\bin\windows-x64\release\slang-glslang.dll" "x64\Debug\slang-glslang.dll"
    Copy-Item -Path ".\ExternalLibs\Slang\bin\windows-x64\release\slang-glslang.dll" "x64\Release\slang-glslang.dll"
    "Done preparing solution for build."
}