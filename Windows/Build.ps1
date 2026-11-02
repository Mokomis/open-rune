[CmdletBinding()]
param([ValidateSet('Debug','RelWithDebInfo','Release')][string]$Configuration='RelWithDebInfo', [int]$Jobs=8, [switch]$NetworkTests)
$ErrorActionPreference='Stop'
$repo=Split-Path $PSScriptRoot -Parent
$env:Path=[Environment]::GetEnvironmentVariable('Path','Machine')+';'+[Environment]::GetEnvironmentVariable('Path','User')
$vswhere="${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if(!(Test-Path $vswhere)){throw 'Install Visual Studio Build Tools with Desktop development with C++.'}
$vs=& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if(!$vs){throw 'The MSVC x64 build tools are missing.'}
& "$vs\Common7\Tools\Launch-VsDevShell.ps1" -Arch amd64 -HostArch amd64 -SkipAutomaticLocation
$env:Path="$vs\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja;"+$env:Path
$build=Join-Path $repo 'build-windows'
# PowerShell 5 maps redirected native stderr warnings to errors; use exit codes.
$ErrorActionPreference='Continue'
& cmake -S $repo -B $build -G Ninja "-DCMAKE_BUILD_TYPE=$Configuration" '-DCMAKE_POLICY_VERSION_MINIMUM=3.5' -DOPENRUNE_ENABLE_NETWORK=ON -DENABLE_SDL2=OFF -DENABLE_SDL3=OFF -DALSOFT_EXAMPLES=OFF -DALSOFT_UTILS=OFF
if($LASTEXITCODE){throw 'Game configuration failed.'}
& cmake --build $build --target SurrealEngine --parallel $Jobs
if($LASTEXITCODE){throw 'Game build failed.'}
if($NetworkTests){
 $net=Join-Path $repo 'build-network-windows'
 & cmake -S "$repo/Network" -B $net -G Ninja "-DCMAKE_BUILD_TYPE=$Configuration" -DBUILD_TESTING=ON
 if($LASTEXITCODE){throw 'Network configuration failed.'}
 & cmake --build $net --parallel $Jobs
 if($LASTEXITCODE){throw 'Network build failed.'}
 & ctest --test-dir $net --output-on-failure
 if($LASTEXITCODE){throw 'Network tests failed.'}
}
if(Test-Path "$repo/Tools/RuneDiscovery/node_modules/hyperdht") {
 & "$PSScriptRoot/Prepare-Discovery.ps1" -BuildDirectory $build
 if($LASTEXITCODE){throw 'Discovery preparation failed.'}
 $python=Join-Path (Split-Path (Split-Path $repo -Parent) -Parent) 'tools/python/Scripts/python.exe'
 if(!(Test-Path $python)){$python=(Get-Command python -ErrorAction Stop).Source}
 & $python "$PSScriptRoot/package_licenses.py"
 if($LASTEXITCODE){throw 'Windows notice packaging failed.'}
} else {Write-Warning 'Run npm ci in Tools/RuneDiscovery, then Windows/Prepare-Discovery.ps1 for Internet discovery.'}
Write-Host "Ready: $build\SurrealEngine.exe"
