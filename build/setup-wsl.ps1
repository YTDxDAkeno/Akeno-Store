param(
    [Parameter(Mandatory)][string]$Distribution,
    [Parameter(Mandatory)][ValidatePattern('^/')][string]$Checkout
)
$ErrorActionPreference = 'Stop'
if (!(Get-Command wsl.exe -ErrorAction SilentlyContinue)) {
    throw 'WSL was not found. Install WSL and start a Linux distribution first.'
}
& wsl.exe --distribution $Distribution --cd $Checkout --exec bash build/setup-toolchain.sh
if ($LASTEXITCODE -ne 0) {
    Write-Error 'Setup failed. Install make, clang-19, lld-19, curl, tar, coreutils and Python 3.11+ in the selected WSL distribution. Review the Linux command output above.'
    exit 1
}
