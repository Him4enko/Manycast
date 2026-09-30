# Checks every locale file against data/locale/en-US.ini:
#   - exactly the same key set (no missing and no extra translations)
#   - every value is quoted (OBS truncates unquoted values at the first space)
#   - the %1..%9 placeholders of the reference file are all present
#
#   powershell -ExecutionPolicy Bypass -File tests\check-locales.ps1

$ErrorActionPreference = "Stop"

$localeDir = Resolve-Path "$PSScriptRoot\..\data\locale"
$referencePath = Join-Path $localeDir "en-US.ini"

function Read-Locale([string] $Path) {
    $text = [Text.Encoding]::UTF8.GetString([IO.File]::ReadAllBytes($Path))
    $entries = @{}
    foreach ($line in ($text -split "`r?`n")) {
        if ($line -match '^\s*([^=;#]+?)\s*=(.*)$') {
            $entries[$Matches[1]] = $Matches[2]
        }
    }
    return $entries
}

function Get-Placeholders([string] $Value) {
    return @([regex]::Matches($Value, '%[0-9]') | ForEach-Object { $_.Value } | Sort-Object -Unique)
}

$reference = Read-Locale $referencePath
Write-Host "reference: en-US.ini ($($reference.Count) keys)"

$problems = 0

foreach ($file in (Get-ChildItem $localeDir -Filter "*.ini" | Where-Object { $_.Name -ne "en-US.ini" } | Sort-Object Name)) {
    $locale = Read-Locale $file.FullName
    $issues = @()

    foreach ($key in $reference.Keys) {
        if (-not $locale.ContainsKey($key)) {
            $issues += "missing key: $key"
            continue
        }
        $value = $locale[$key]
        if (-not $value.StartsWith('"')) {
            $issues += "unquoted value: $key"
        }
        $expected = (Get-Placeholders $reference[$key]) -join ' '
        $actual = (Get-Placeholders $value) -join ' '
        if ($expected -ne $actual) {
            $issues += "placeholder mismatch: $key (expected [$expected], got [$actual])"
        }
    }

    foreach ($key in $locale.Keys) {
        if (-not $reference.ContainsKey($key)) {
            $issues += "unused key: $key"
        }
    }

    if ($issues.Count -gt 0) {
        Write-Host "FAIL $($file.Name)"
        foreach ($issue in $issues) {
            Write-Host "      $issue"
        }
        $problems += $issues.Count
    } else {
        Write-Host "ok   $($file.Name) ($($locale.Count) keys)"
    }
}

if ($problems -gt 0) {
    Write-Host "locale check FAILED ($problems problem(s))"
    exit 1
}

Write-Host "locale check passed"
exit 0
