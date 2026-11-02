[CmdletBinding(DefaultParameterSetName='Host')]
param(
 [Parameter(ParameterSetName='Host')][switch]$Headless,
 [Parameter(ParameterSetName='Join',Mandatory=$true)][string]$Invitation,
 [Parameter(ParameterSetName='JoinPublic',Mandatory=$true)][string]$JoinKey,
 [Parameter(ParameterSetName='Host')][switch]$Public,
 [string]$Game,
 [string]$Address='127.0.0.1',
 [ValidateSet('dm','tdm','arena','headball')][string]$Mode='dm',
 [string]$Maps,
 [ValidateRange(2,16)][int]$Players=4,
 [int]$Seconds=0,
 [string]$Python,
 [string]$Binary
)
$ErrorActionPreference='Stop'
$repo=Split-Path $PSScriptRoot -Parent
$env:Path=[Environment]::GetEnvironmentVariable('Path','Machine')+';'+[Environment]::GetEnvironmentVariable('Path','User')
$env:PYTHONUTF8='1'
if(!$Game){$Game=Join-Path (Split-Path $repo -Parent) 'rune-classic'}
if(!$Binary){$Binary=Join-Path $repo 'build-windows/SurrealEngine.exe'}
if(!$Python){
 $candidate=Join-Path (Split-Path (Split-Path $repo -Parent) -Parent) 'tools/python/Scripts/python.exe'
 if(Test-Path $candidate){$Python=$candidate}else{$Python=(Get-Command python -ErrorAction Stop).Source}
}
$sessionRoot=Join-Path $env:LOCALAPPDATA 'OpenRune/sessions'
New-Item -ItemType Directory -Force $sessionRoot | Out-Null
$session=Join-Path $sessionRoot ((Get-Date -Format 'yyyyMMdd-HHmmss')+'-'+[Guid]::NewGuid().ToString('N').Substring(0,8))
$launchArgs=@("$repo/Tools/RuneMultiplayer/private_session.py",'--binary',$Binary,'--game',$Game,'--session',$session,'--seconds',"$Seconds")
if($Invitation){$launchArgs+=@('--join',$Invitation)}
elseif($JoinKey){$launchArgs+=@('--join-key',$JoinKey)}
else{
 if($Headless){$launchArgs+='--headless'}else{$launchArgs+='--host'}
 $launchArgs+=@('--address',$Address,'--mode',$Mode,'--players',"$Players")
 if($Maps){$launchArgs+=@('--maps',$Maps)}
 if($Public){$launchArgs+='--public'}
}
& $Python @launchArgs
exit $LASTEXITCODE
