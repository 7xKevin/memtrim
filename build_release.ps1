$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$dist = Join-Path $root "dist"
$appDir = Join-Path $dist "app"
$websiteDownloads = Join-Path $root "website\\downloads"
$vsVars = "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
$iscc = "C:\Users\miner\AppData\Local\Programs\Inno Setup 6\ISCC.exe"

New-Item -ItemType Directory -Force -Path $dist, $appDir, $websiteDownloads | Out-Null

if (-not (Test-Path $vsVars)) {
    throw "MSVC toolchain not found at $vsVars"
}

cmd.exe /c "`"$vsVars`" >nul && cl /nologo /std:c++17 /O2 /EHsc /DUNICODE /D_UNICODE mem_trim.cpp user32.lib gdi32.lib psapi.lib dwmapi.lib advapi32.lib dxgi.lib gdiplus.lib shell32.lib /link /SUBSYSTEM:WINDOWS /OUT:dist\\app\\MemTrimLite.exe"

Copy-Item (Join-Path $root "mem_trim.ico") (Join-Path $appDir "mem_trim.ico") -Force

$portableZip = Join-Path $dist "MemTrimLite-Portable.zip"
if (Test-Path $portableZip) {
    Remove-Item $portableZip -Force
}
Compress-Archive -Path (Join-Path $appDir "*") -DestinationPath $portableZip -Force

if (-not (Test-Path $iscc)) {
    throw "Inno Setup compiler not found at $iscc"
}

& $iscc (Join-Path $root "installer\\MemTrimLite.iss")

Copy-Item (Join-Path $dist "MemTrimLite-Setup.exe") (Join-Path $websiteDownloads "MemTrimLite-Setup.exe") -Force
Copy-Item $portableZip (Join-Path $websiteDownloads "MemTrimLite-Portable.zip") -Force

Write-Host "Release files created in $dist"
