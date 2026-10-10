# rocky-lite offline build check (Windows / PowerShell).
#
# Configures and builds the ROCKY_LITE configuration from scratch in a
# clean directory, with outbound network access made to fail fast, so a
# dependency lookup that unexpectedly tries to reach the network shows up
# as a build failure instead of silently succeeding. This is NOT a true
# network-namespace sandbox (that needs admin/firewall privileges this
# script doesn't assume) -- it is an approximation: point every common
# proxy environment variable at an address nothing is listening on, so
# any accidental HTTP(S) call (FetchContent download, CMake downloading a
# toolchain file, etc.) fails immediately with a connection error rather
# than quietly working because the machine happens to be online.
#
# Usage: powershell -File scripts/offline_build_check.ps1 [-BuildDir <path>] [-Config Release]

param(
    [string]$BuildDir = "$PSScriptRoot/../build_offline_check",
    [string]$Config = "Release"
)

$ErrorActionPreference = "Stop"
$RepoRoot = Resolve-Path "$PSScriptRoot/.."

Write-Host "== rocky-lite offline build check ==" -ForegroundColor Cyan
Write-Host "Repo:   $RepoRoot"
Write-Host "Build:  $BuildDir"

if (Test-Path $BuildDir) {
    Remove-Item -Recurse -Force $BuildDir
}
New-Item -ItemType Directory -Force -Path $BuildDir | Out-Null

# Make outbound network access fail fast rather than silently succeed.
$blackhole = "http://127.0.0.1:9"   # discard port; nothing should ever answer here
$env:HTTP_PROXY  = $blackhole
$env:HTTPS_PROXY = $blackhole
$env:http_proxy  = $blackhole
$env:https_proxy = $blackhole
$env:NO_PROXY    = ""
$env:no_proxy    = ""

try {
    Write-Host "`n-- Configuring (ROCKY_LITE=ON, no toolchain file) --" -ForegroundColor Cyan
    & cmake -S $RepoRoot -B $BuildDir -DCMAKE_BUILD_TYPE=$Config -DROCKY_LITE=ON
    if ($LASTEXITCODE -ne 0) { throw "CMake configure failed (exit $LASTEXITCODE)" }

    Write-Host "`n-- Building --" -ForegroundColor Cyan
    & cmake --build $BuildDir --config $Config --parallel
    if ($LASTEXITCODE -ne 0) { throw "CMake build failed (exit $LASTEXITCODE)" }

    Write-Host "`n-- Checking for vcpkg/system package-manager leakage --" -ForegroundColor Cyan
    $cache = Get-Content "$BuildDir/CMakeCache.txt" -ErrorAction SilentlyContinue
    $toolchainLine = $cache | Where-Object { $_ -match "CMAKE_TOOLCHAIN_FILE" }
    if ($toolchainLine -and $toolchainLine -match "vcpkg") {
        throw "CMAKE_TOOLCHAIN_FILE references vcpkg -- lite build must not need it: $toolchainLine"
    }

    Write-Host "`nOFFLINE BUILD CHECK PASSED" -ForegroundColor Green
}
finally {
    Remove-Item Env:HTTP_PROXY, Env:HTTPS_PROXY, Env:http_proxy, Env:https_proxy, Env:NO_PROXY, Env:no_proxy -ErrorAction SilentlyContinue
}
