param([string]$ViewModel = '', [string]$World = '')
$ErrorActionPreference = 'Stop'
$out = Join-Path $PSScriptRoot '../build/tests'
New-Item -ItemType Directory -Force $out | Out-Null
Push-Location $out
try {
    foreach ($name in 'movement_test', 'weapon_test', 'surf_test') {
        & cl /nologo /O2 /EHsc /std:c++20 /W3 "$PSScriptRoot/$name.cpp" /Fe:"$name.exe"
        if ($LASTEXITCODE -ne 0) { throw "Compile failed: $name" }
    }
    & ./movement_test.exe
    if ($LASTEXITCODE -ne 0) { throw 'Movement tests failed' }
    if ($ViewModel) { & ./weapon_test.exe $ViewModel } else { & ./weapon_test.exe }
    if ($LASTEXITCODE -ne 0) { throw 'Weapon tests failed' }
    if ($World) {
        & ./surf_test.exe $World
        if ($LASTEXITCODE -ne 0) { throw 'Surf tests failed' }
    }
} finally { Pop-Location }
