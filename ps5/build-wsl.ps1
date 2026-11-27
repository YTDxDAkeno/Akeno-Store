# Akeno Gaming Hub - builds the PS5 title from Windows through WSL.
# SPDX-License-Identifier: GPL-3.0-or-later
#
#   .\ps5\build-wsl.ps1 -Distribution Ubuntu-24.04 -Checkout /mnt/c/repos/Akeno-Store -Setup
#   .\ps5\build-wsl.ps1 -Distribution Ubuntu-24.04 -Checkout /mnt/c/repos/Akeno-Store
#
# -Setup installs the build packages inside the WSL distribution once (asks for sudo there).
# The result is <Checkout>/ps5/dist/PPSA90410.zip, reachable from Windows under the same path.
param(
    [Parameter(Mandatory)][string]$Distribution,
    [Parameter(Mandatory)][ValidatePattern('^/')][string]$Checkout,
    [switch]$Setup
)
$ErrorActionPreference = 'Stop'
if (!(Get-Command wsl.exe -ErrorAction SilentlyContinue)) {
    throw 'WSL was not found. Install WSL and an Ubuntu 24.04 distribution first (wsl --install -d Ubuntu-24.04).'
}
if ($Setup) {
    & wsl.exe --distribution $Distribution --exec sudo apt-get update
    & wsl.exe --distribution $Distribution --exec sudo apt-get install -y clang-18 lld-18 llvm-18 `
        libclang-rt-18-dev cmake ninja-build make git curl wget unzip python3 python3-pil
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}
& wsl.exe --distribution $Distribution --cd $Checkout --exec bash ps5/build.sh
exit $LASTEXITCODE
