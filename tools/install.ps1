param(
    [Parameter(Mandatory)][string]$GothicPath,
    [string]$BuildPath = (Join-Path $PSScriptRoot '../build/plugin')
)
$ErrorActionPreference = 'Stop'
if (Get-Process Gothic, GothicMod -ErrorAction SilentlyContinue) { throw 'Close Gothic before installing.' }
$game = (Resolve-Path -LiteralPath $GothicPath).Path
if (!(Test-Path -LiteralPath (Join-Path $game 'System/Gothic.exe'))) { throw 'Not a Gothic installation.' }
$files = @(
    (Join-Path $BuildPath 'GothicCS.dll'),
    (Join-Path $BuildPath 'dependencies/union/union-api/UnionAPI.dll')
)
foreach ($file in $files) { if (!(Test-Path -LiteralPath $file)) { throw "Build output missing: $file" } }
foreach ($name in 'GothicCS.vdf', 'GothicSkate.vdf', 'GothicMetin.vdf') {
    if (Test-Path -LiteralPath (Join-Path $game "Data/$name")) {
        throw "Disable Data/$name before installing loose plugin DLLs."
    }
}
$dest = Join-Path $game 'System/Autorun'
New-Item -ItemType Directory -Force $dest | Out-Null
foreach ($file in $files) {
    $target = Join-Path $dest (Split-Path $file -Leaf)
    if (Test-Path -LiteralPath $target) {
        $backup = "$target.backup-$(Get-Date -Format 'yyyyMMddHHmmssfff')"
        Copy-Item -LiteralPath $target -Destination $backup -ErrorAction Stop
    }
    Copy-Item -LiteralPath $file -Destination $target -Force
}
Write-Host 'Installed GothicCS and UnionAPI in System/Autorun. Start Gothic and press Insert in game.'
