# Smoke test for the built plugin: loads manycast.dll and verifies that the OBS
# module entry points are exported and callable. The DLL imports obs.dll,
# obs-frontend-api.dll and Qt6, so every directory that provides them is added to
# the search path - in CI that is the .deps directory of the template.
#
#   powershell -ExecutionPolicy Bypass -File tests\smoke-plugin.ps1
#   powershell -ExecutionPolicy Bypass -File tests\smoke-plugin.ps1 -ObsBin <dir> [-ObsBin <dir2>]

param(
    [string] $Dll = "",
    [string[]] $ObsBin = @()
)

$ErrorActionPreference = "Stop"

$projectRoot = Resolve-Path "$PSScriptRoot\.."

if ([string]::IsNullOrEmpty($Dll)) {
    $candidates = @(
        "$projectRoot\build_x64\RelWithDebInfo\manycast.dll",
        "$projectRoot\build_x64\Debug\manycast.dll",
        "$projectRoot\build_x64\Release\manycast.dll"
    )
    $Dll = $candidates | Where-Object { Test-Path $_ } | Select-Object -First 1
}

if (-not $Dll -or -not (Test-Path $Dll)) {
    Write-Host "SKIP: the plugin is not built yet ($Dll)"
    exit 0
}

$searchDirs = @()
$searchDirs += $ObsBin
if ($env:LR_OBS_BIN) { $searchDirs += $env:LR_OBS_BIN }
$searchDirs += @(
    "$projectRoot\.deps\bin\64bit",
    "D:\SteamLibrary\steamapps\common\OBS Studio\bin\64bit",
    "C:\Program Files\obs-studio\bin\64bit",
    "${env:ProgramFiles}\obs-studio\bin\64bit"
)
if (Test-Path "$projectRoot\.deps") {
    $searchDirs += Get-ChildItem "$projectRoot\.deps" -Directory -Filter "obs-deps-*" -ErrorAction SilentlyContinue |
        ForEach-Object { Join-Path $_.FullName "bin" }
}
$searchDirs = @($searchDirs | Where-Object { $_ -and (Test-Path $_) } | Select-Object -Unique)

if ($searchDirs.Count -eq 0) {
    Write-Host "SKIP: no directory with the OBS and Qt libraries was found, pass -ObsBin <dir>"
    exit 0
}

Write-Host "plugin: $Dll"
foreach ($dir in $searchDirs) {
    Write-Host "libs:   $dir"
}

Add-Type -TypeDefinition @"
using System;
using System.Runtime.InteropServices;

public static class LrSmoke
{
    [DllImport("kernel32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
    public static extern bool SetDllDirectoryW(string path);

    [DllImport("kernel32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
    public static extern IntPtr LoadLibraryW(string file);

    [DllImport("kernel32.dll", SetLastError = true)]
    public static extern IntPtr GetProcAddress(IntPtr module, string name);

    [DllImport("kernel32.dll", SetLastError = true)]
    public static extern bool FreeLibrary(IntPtr module);

    public delegate uint VerFn();

    public static uint GetVersion(IntPtr address)
    {
        VerFn fn = (VerFn)Marshal.GetDelegateForFunctionPointer(address, typeof(VerFn));
        return fn();
    }
}
"@

$failed = $false

$env:PATH = ($searchDirs -join ";") + ";" + $env:PATH
[void][LrSmoke]::SetDllDirectoryW($searchDirs[0])

$module = [LrSmoke]::LoadLibraryW((Resolve-Path $Dll).Path)
if ($module -eq [IntPtr]::Zero) {
    $code = [Runtime.InteropServices.Marshal]::GetLastWin32Error()
    Write-Host "FAIL: could not load the plugin (win32 error $code)"
    exit 1
}

try {
    $exports = @("obs_module_load", "obs_module_unload", "obs_module_ver", "obs_module_set_pointer")
    foreach ($name in $exports) {
        $address = [LrSmoke]::GetProcAddress($module, $name)
        if ($address -eq [IntPtr]::Zero) {
            Write-Host "FAIL: export '$name' is missing"
            $failed = $true
        } else {
            Write-Host "ok:   export '$name'"
        }
    }

    $verAddress = [LrSmoke]::GetProcAddress($module, "obs_module_ver")
    if ($verAddress -ne [IntPtr]::Zero) {
        $version = [LrSmoke]::GetVersion($verAddress)
        $major = ($version -shr 24) -band 0xFF
        $minor = ($version -shr 16) -band 0xFF
        $patch = $version -band 0xFFFF
        Write-Host "ok:   obs_module_ver() = $major.$minor.$patch"
        if ($major -eq 0) {
            Write-Host "FAIL: obs_module_ver() returned zero"
            $failed = $true
        }
    }
} finally {
    [void][LrSmoke]::FreeLibrary($module)
}

if ($failed) {
    Write-Host "smoke test FAILED"
    exit 1
}

Write-Host "smoke test passed"
exit 0
