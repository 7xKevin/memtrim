$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$dist = Join-Path $root "dist"
$appDir = Join-Path $dist "app"
$websiteDownloads = Join-Path $root "website\\downloads"
$vsVars = "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
$iscc = "C:\Users\miner\AppData\Local\Programs\Inno Setup 6\ISCC.exe"
$resourceObject = Join-Path $dist "mem_trim.res"

Set-Location $root

function Find-SignTool {
    if ($env:MEMTRIM_SIGNTOOL -and (Test-Path $env:MEMTRIM_SIGNTOOL)) {
        return $env:MEMTRIM_SIGNTOOL
    }

    $kitsRoot = "C:\Program Files (x86)\Windows Kits\10\bin"
    if (-not (Test-Path $kitsRoot)) {
        return $null
    }

    return Get-ChildItem -Path $kitsRoot -Filter signtool.exe -Recurse -ErrorAction SilentlyContinue |
        Sort-Object FullName -Descending |
        Select-Object -First 1 -ExpandProperty FullName
}

function Sign-FileIfConfigured([string]$Path) {
    if (-not $env:MEMTRIM_SIGN_PFX -or -not $env:MEMTRIM_SIGN_PASSWORD) {
        return
    }

    $signTool = Find-SignTool
    if (-not $signTool) {
        throw "Signing requested, but signtool.exe was not found."
    }

    & $signTool sign /fd SHA256 /f $env:MEMTRIM_SIGN_PFX /p $env:MEMTRIM_SIGN_PASSWORD /tr http://timestamp.digicert.com /td SHA256 $Path
}

New-Item -ItemType Directory -Force -Path $dist, $appDir, $websiteDownloads | Out-Null

if (-not (Test-Path $vsVars)) {
    throw "MSVC toolchain not found at $vsVars"
}

cmd.exe /c "`"$vsVars`" >nul && rc /nologo /fo `"$resourceObject`" mem_trim.rc && cl /nologo /std:c++17 /O2 /EHsc /DUNICODE /D_UNICODE mem_trim.cpp `"$resourceObject`" user32.lib gdi32.lib psapi.lib dwmapi.lib advapi32.lib dxgi.lib gdiplus.lib shell32.lib /link /SUBSYSTEM:WINDOWS /OUT:dist\\app\\MemTrimLite.exe"
if ($LASTEXITCODE -ne 0) {
    throw "MSVC build failed with exit code $LASTEXITCODE"
}

Copy-Item (Join-Path $root "mem_trim.ico") (Join-Path $appDir "mem_trim.ico") -Force
Sign-FileIfConfigured (Join-Path $appDir "MemTrimLite.exe")

$portableZip = Join-Path $dist "MemTrimLite-Portable.zip"
if (Test-Path $portableZip) {
    Remove-Item $portableZip -Force
}
Compress-Archive -Path (Join-Path $appDir "*") -DestinationPath $portableZip -Force

if (-not (Test-Path $iscc)) {
    throw "Inno Setup compiler not found at $iscc"
}

& $iscc (Join-Path $root "installer\\MemTrimLite.iss")
if ($LASTEXITCODE -ne 0) {
    throw "Inno Setup build failed with exit code $LASTEXITCODE"
}
Sign-FileIfConfigured (Join-Path $dist "MemTrimLite-Setup.exe")

Copy-Item (Join-Path $dist "MemTrimLite-Setup.exe") (Join-Path $websiteDownloads "MemTrimLite-Setup.exe") -Force
Copy-Item $portableZip (Join-Path $websiteDownloads "MemTrimLite-Portable.zip") -Force

Write-Host "Release files created in $dist"
