Add-Type -assembly "system.io.compression.filesystem"
[Net.ServicePointManager]::SecurityProtocol = "tls12, tls11, tls"
if (-NOT ((Test-Path '.\ExternalLibs\FbxSDK\x64\debug\libfbxsdk.lib') -AND (Test-Path '.\ExternalLibs\FbxSDK\x64\release\libfbxsdk.lib') -AND (Test-Path '.\ExternalLibs\FbxSDK\x86\release\libfbxsdk.lib')  -AND (Test-Path '.\ExternalLibs\FbxSDK\x86\debug\libfbxsdk.lib'))) {
"Required binaries not found, downloading..."
(New-Object Net.WebClient).DownloadFile('https://github.com/csyonghe/SpireMiniEngineExtBinaries/raw/master/binaries.zip', 'binaries.zip')
if (Test-Path 'ExternalLibs') {
Remove-Item -Recurse -Force 'ExternalLibs'
}
[System.IO.Compression.ZipFile]::ExtractToDirectory("binaries.zip", '.\')
Remove-Item 'binaries.zip'
}
if (Test-path '.\ExternalLibs\Slang\') {
    Remove-Item -Recurse -Force '.\ExternalLibs\Slang\'
}
New-Item -ItemType directory -Path .\ExternalLibs\Slang\
$slangVersion='0.12.4'
(New-Object Net.WebClient).DownloadFile("https://github.com/shader-slang/slang/releases/download/v$slangVersion/slang-$slangVersion-win64.zip", 'slang-x64.zip')
Expand-Archive -Path "slang-x64.zip" -DestinationPath '.\ExternalLibs\Slang\' -Force
Remove-Item 'slang-x64.zip'
(New-Object Net.WebClient).DownloadFile("https://github.com/shader-slang/slang/releases/download/v$slangVersion/slang-$slangVersion-win32.zip", 'slang-32.zip')
Expand-Archive -Path "slang-32.zip" -DestinationPath '.\ExternalLibs\Slang\' -Force
Remove-Item 'slang-32.zip'
if (-not (Test-path '.\x64\Debug')) {
    New-Item .\x64\Debug -ItemType Directory
}
if (-not (Test-path '.\x64\Release')) {
    New-Item .\x64\Release -ItemType Directory
}
if (-not (Test-path '.\Debug')) {
    New-Item .\Debug -ItemType Directory
}
if (-not (Test-path '.\Release')) {
    New-Item .\Release -ItemType Directory
}
Copy-Item -Path ".\ExternalLibs\Slang\bin\windows-x64\release\slang.dll" "x64\Debug\slang.dll"
Copy-Item -Path ".\ExternalLibs\Slang\bin\windows-x64\release\slang.dll" "x64\Release\slang.dll"
Copy-Item -Path ".\ExternalLibs\Slang\bin\windows-x64\release\slang-glslang.dll" "x64\Debug\slang-glslang.dll"
Copy-Item -Path ".\ExternalLibs\Slang\bin\windows-x64\release\slang-glslang.dll" "x64\Release\slang-glslang.dll"
Copy-Item -Path ".\ExternalLibs\Slang\bin\windows-x86\release\slang.dll" "Debug\slang.dll"
Copy-Item -Path ".\ExternalLibs\Slang\bin\windows-x86\release\slang.dll" "Release\slang.dll"
Copy-Item -Path ".\ExternalLibs\Slang\bin\windows-x86\release\slang-glslang.dll" "Debug\slang-glslang.dll"
Copy-Item -Path ".\ExternalLibs\Slang\bin\windows-x86\release\slang-glslang.dll" "Release\slang-glslang.dll"
"Done."
cmd /c pause | out-null