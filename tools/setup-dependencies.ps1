$ErrorActionPreference = 'Stop'
$base = Join-Path $PSScriptRoot '../plugin/dependencies/union'
$dependencies = @(
    @('union-api', 'https://gitlab.com/union-framework/union-api.git', '167c5573653a8d89c1fee01865b1ac4502935e3f'),
    @('gothic-api', 'https://gitlab.com/union-framework/gothic-api.git', '6e554b0acb199c9544037fb7136e07fad44a2fc4')
)
foreach ($dep in $dependencies) {
    $dest = Join-Path $base $dep[0]
    if (!(Test-Path -LiteralPath $dest)) {
        git clone --no-checkout $dep[1] $dest
        if ($LASTEXITCODE -ne 0) { throw "Clone failed: $($dep[0])" }
    }
    $revision = git -C $dest rev-parse HEAD 2>$null
    if ($revision -ne $dep[2] -or !(Test-Path -LiteralPath (Join-Path $dest "CMakeLists.txt"))) {
        git -C $dest checkout --detach $dep[2]
        if ($LASTEXITCODE -ne 0) { throw "Checkout failed: $($dep[0])" }
    }
    git -C $dest submodule update --init --recursive
    if ($LASTEXITCODE -ne 0) { throw "Submodule setup failed: $($dep[0])" }
}
