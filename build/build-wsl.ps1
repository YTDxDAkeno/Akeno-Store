param(
    [Parameter(Mandatory)][string]$Distribution,
    [Parameter(Mandatory)][ValidatePattern('^/')][string]$Checkout,
    [ValidateSet('native-test', 'ps5-objects', 'ps5')][string]$Target = 'native-test',
    [string]$SdkConfig
)
$ErrorActionPreference = 'Stop'

if ($Target -ne 'native-test' -and !$SdkConfig) {
    throw 'Specify -SdkConfig with the WSL path to your authorized SDK makefile.'
}

$WslArgs = @('--distribution', $Distribution, '--cd', $Checkout, '--exec', 'make', '-f', 'Makefile')
if ($SdkConfig) {
    $WslArgs += @('-f', $SdkConfig)
}
$WslArgs += $Target
& wsl.exe @WslArgs
exit $LASTEXITCODE
