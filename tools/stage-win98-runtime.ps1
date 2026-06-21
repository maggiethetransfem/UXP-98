param(
    [string] $SourceBin = "$PSScriptRoot\..\obj-win32-kernelex\dist\bin",
    [string] $Destination = "$PSScriptRoot\..\..\UXP-kernelex-test-runtime",
    [string] $Win10Destination = "$PSScriptRoot\..\..\UXP-kernelex-test-runtime-win10",
    [string] $Firefox52For98 = "$PSScriptRoot\..\..\Firefox52for98\App\Firefox"
)

$ErrorActionPreference = 'Stop'

$SourceBin = [IO.Path]::GetFullPath($SourceBin)
$Destination = [IO.Path]::GetFullPath($Destination)
$Win10Destination = [IO.Path]::GetFullPath($Win10Destination)
$Firefox52For98 = [IO.Path]::GetFullPath($Firefox52For98)

New-Item -ItemType Directory -Force -Path $Destination | Out-Null

robocopy $SourceBin $Destination /E /R:2 /W:1 /NFL /NDL /NP | Out-Host
if ($LASTEXITCODE -gt 7) {
    throw "robocopy failed with code $LASTEXITCODE"
}

$goannaPrefs = Join-Path $Destination 'goanna.js'
if (Test-Path -LiteralPath $goannaPrefs) {
    $text = [IO.File]::ReadAllText($goannaPrefs)
    $text = $text.Replace('pref("security.enterprise_roots.enabled", false);', 'pref("security.enterprise_roots.enabled", true);')
    [IO.File]::WriteAllText($goannaPrefs, $text, [Text.Encoding]::ASCII)
}

$helperDlls = @(
    'ACTIVEDS.DLL',
    'ADSLDPC.DLL',
    'APPHELP.DLL',
    'ATL.DLL',
    'AUTHZ.DLL',
    'CERTCLI.DLL',
    'DBGHELP.DLL',
    'DNSAPI.DLL',
    'GdiPlus.dll',
    'MPRAPI.DLL',
    'MSDMO.DLL',
    'NETRAP.DLL',
    'NETUI0.DLL',
    'NETUI1.DLL',
    'NTDSAPI.DLL',
    'NTLANMAN.DLL',
    'PDH.DLL',
    'PSAPI.DLL',
    'QIPCAP.DLL',
    'RASDLG.DLL',
    'RASMAN.DLL',
    'REGAPI.DLL',
    'RTUTILS.DLL',
    'SAMLIB.DLL',
    'USERENV.DLL',
    'UTILDLL.DLL',
    'UXTHEME.DLL',
    'W32TOPL.DLL',
    'WINSCARD.DLL',
    'WINSTA.DLL',
    'WTSAPI32.DLL'
)

foreach ($dll in $helperDlls) {
    $source = Join-Path $Firefox52For98 $dll
    if (Test-Path -LiteralPath $source) {
        Copy-Item -LiteralPath $source -Destination (Join-Path $Destination $dll) -Force
    }
}

@(
    'VCRUNTIME140.dll',
    'MSVCP140.dll',
    'mozglue.dll',
    'nss3.dll',
    'lgpllibs.dll',
    'xul.dll'
) | Set-Content -LiteralPath (Join-Path $Destination 'dependentlibs.list') -Encoding ASCII

$win98FontPrefs = Join-Path $PSScriptRoot 'win98-font-safe-prefs.js'
if (Test-Path -LiteralPath $win98FontPrefs) {
    $prefDir = Join-Path $Destination 'defaults\pref'
    New-Item -ItemType Directory -Force -Path $prefDir | Out-Null
    Copy-Item -LiteralPath $win98FontPrefs -Destination (Join-Path $prefDir 'win98-font-safe-prefs.js') -Force
}

$discordCertPrefs = Join-Path $PSScriptRoot 'discord-cert-prefs.js'
if (Test-Path -LiteralPath $discordCertPrefs) {
    $prefDir = Join-Path $Destination 'defaults\pref'
    New-Item -ItemType Directory -Force -Path $prefDir | Out-Null
    Copy-Item -LiteralPath $discordCertPrefs -Destination (Join-Path $prefDir 'discord-cert-prefs.js') -Force
}

$discordCompat = Join-Path $PSScriptRoot 'discord-compat@uxp-kernelex'
if (Test-Path -LiteralPath $discordCompat) {
    $extensionDir = Join-Path $Destination 'browser\extensions\discord-compat@uxp-kernelex'
    New-Item -ItemType Directory -Force -Path $extensionDir | Out-Null
    robocopy $discordCompat $extensionDir /E /R:2 /W:1 /NFL /NDL /NP | Out-Host
    if ($LASTEXITCODE -gt 7) {
        throw "robocopy failed with code $LASTEXITCODE"
    }
}

Write-Host "Staged Win98/KernelEx runtime at $Destination"

New-Item -ItemType Directory -Force -Path $Win10Destination | Out-Null
robocopy $Destination $Win10Destination /E /R:2 /W:1 /NFL /NDL /NP | Out-Host
if ($LASTEXITCODE -gt 7) {
    throw "robocopy failed with code $LASTEXITCODE"
}

$optionalHelperDir = Join-Path $Win10Destination '_win98-helper-disabled'
New-Item -ItemType Directory -Force -Path $optionalHelperDir | Out-Null

$dnsapiInWin10Root = Join-Path $Win10Destination 'DNSAPI.DLL'
if (Test-Path -LiteralPath $dnsapiInWin10Root) {
    Move-Item -LiteralPath $dnsapiInWin10Root -Destination (Join-Path $optionalHelperDir 'DNSAPI.DLL') -Force
}

$win10FontPrefs = Join-Path $Win10Destination 'defaults\pref\win98-font-safe-prefs.js'
if (Test-Path -LiteralPath $win10FontPrefs) {
    Remove-Item -LiteralPath $win10FontPrefs -Force
}

Write-Host "Staged Windows 10 smoke-test runtime at $Win10Destination"
