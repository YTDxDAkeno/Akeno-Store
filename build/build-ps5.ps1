param(
    [Parameter(Mandatory)][string]$Distribution,
    [Parameter(Mandatory)][ValidatePattern('^/')][string]$Checkout,
    [ValidateSet('module', 'sdk')][string]$Mode = 'module',
    [string]$SdkConfig
)
$ErrorActionPreference = 'Stop'
if ($Mode -eq 'sdk' -and !$SdkConfig) {
    throw 'Specify the WSL path to the authorized SDK makefile via -SdkConfig.'
}
$arguments = @('--distribution', $Distribution, '--cd', $Checkout, '--exec', 'bash', 'build/build-ps5.sh', $Mode)
if ($Mode -eq 'sdk') { $arguments += $SdkConfig }
& wsl.exe @arguments
exit $LASTEXITCODE
