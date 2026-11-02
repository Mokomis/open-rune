[CmdletBinding()]
param([string]$GameDirectory='C:\OpenRune\work\rune-classic',[string]$SaveDirectory='')
$ErrorActionPreference='Stop'
$repo=Split-Path $PSScriptRoot -Parent
$binary=Join-Path $repo 'build-windows/SurrealEngine.exe'
if(!(Test-Path $binary)){throw 'Build Open Rune using Windows/Build.ps1 first.'}
if(!(Test-Path "$GameDirectory/System/Rune.ini")){throw 'Choose the original Rune Classic game directory.'}
$argsList=@('--direct','--desktop-menu','--menu-title')
if($SaveDirectory){$argsList+="--save-directory=$SaveDirectory"}
$argsList+=$GameDirectory
& $binary @argsList
exit $LASTEXITCODE
