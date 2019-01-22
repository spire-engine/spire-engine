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
$slangVersion='0.11.18'
(New-Object Net.WebClient).DownloadFile("https://github.com/shader-slang/slang/releases/download/v$slangVersion/slang-$slangVersion-win64.zip", 'slang-x64.zip')
Expand-Archive -Path "slang-x64.zip" -DestinationPath '.\ExternalLibs\Slang\' -Force
Remove-Item 'slang-x64.zip'
(New-Object Net.WebClient).DownloadFile("https://github.com/shader-slang/slang/releases/download/v$slangVersion/slang-$slangVersion-win32.zip", 'slang-32.zip')
Expand-Archive -Path "slang-32.zip" -DestinationPath '.\ExternalLibs\Slang\' -Force
Remove-Item 'slang-32.zip'
"Done."
cmd /c pause | out-null