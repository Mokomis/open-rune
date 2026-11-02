[CmdletBinding()]
param([string]$BuildDirectory=(Join-Path (Split-Path $PSScriptRoot -Parent) 'build-windows'))
$ErrorActionPreference='Stop'
$repo=Split-Path $PSScriptRoot -Parent
$source=Join-Path $repo 'Tools/RuneDiscovery'
$node=Join-Path $env:ProgramFiles 'nodejs/node.exe'
if(!(Test-Path $node)){throw 'Install the supported Node.js runtime before preparing Internet discovery.'}
if(!(Test-Path "$source/node_modules/hyperdht")){throw 'Run npm ci in Tools/RuneDiscovery to install the pinned discovery dependencies.'}
$dest=Join-Path $BuildDirectory 'discovery'
New-Item -ItemType Directory -Force $dest | Out-Null
# Only the app runtime and its pinned dependencies; no session identities or invitations.
Get-ChildItem $source -File | Where-Object { $_.Extension -eq '.cjs' -or $_.Name -in @('package.json','package-lock.json') } | Copy-Item -Destination $dest -Force
& robocopy "$source/node_modules" "$dest/node_modules" /E /NFL /NDL /NJH /NJS /NP | Out-Null
if($LASTEXITCODE -ge 8){throw 'Failed to stage the discovery dependencies.'}
$licenses=Join-Path $BuildDirectory 'licenses'
New-Item -ItemType Directory -Force $licenses | Out-Null
foreach($file in @('LICENSE.md','THIRD_PARTY_NOTICES.md','NOTICE.md','NOTICE','COPYING')){if(Test-Path "$repo/$file"){Copy-Item "$repo/$file" $licenses -Force}}
if(Test-Path "$repo/licenses"){Copy-Item "$repo/licenses/*" $licenses -Recurse -Force}
if(Test-Path "$repo/Resources/licenses"){Copy-Item "$repo/Resources/licenses/*" $licenses -Recurse -Force}
Write-Host "Discovery ready: $dest (system Node.js runtime)."
exit 0
