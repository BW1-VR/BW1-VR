# Builds and runs the controller math test with the same MSVC toolchain as the
# mod. No game and no headset are needed. Exit code 0 means every check passed.
$ErrorActionPreference = 'Stop'
$root = Resolve-Path (Join-Path $PSScriptRoot '..\..')
cmake --build $root\build --config Release --target bwvr_controller_math_test
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& "$root\build\test\controller_math\Release\bwvr_controller_math_test.exe"
exit $LASTEXITCODE
