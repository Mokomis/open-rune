param([Parameter(Mandatory=$true)][string]$GameDirectory)
$ErrorActionPreference='Stop'
Add-Type -AssemblyName System.IO.Compression.FileSystem

if(!(Test-Path (Join-Path $GameDirectory 'System/RuneI.u') -PathType Leaf)){exit 0}
$parent=Join-Path $GameDirectory 'TexturePacks'
$destination=Join-Path $parent 'OpenRune-release'
if(Test-Path (Join-Path $destination 'index.tsv') -PathType Leaf){exit 0}
[IO.Directory]::CreateDirectory($parent) | Out-Null
$cache=Join-Path $parent '.release-download'
[IO.Directory]::CreateDirectory($cache) | Out-Null

function Test-UnmeteredConnection {
    $network=[Windows.Networking.Connectivity.NetworkInformation,Windows,ContentType=WindowsRuntime]
    $profile=$network::GetInternetConnectionProfile()
    if($null -eq $profile -or $profile.GetNetworkConnectivityLevel() -ne 'InternetAccess'){return $false}
    $cost=$profile.GetConnectionCost()
    return $cost.NetworkCostType -eq 'Unrestricted' -and !$cost.Roaming -and
        !$cost.OverDataLimit -and !$cost.ApproachingDataLimit -and !$cost.BackgroundDataUsageRestricted
}

function Test-Part($path,$part) {
    if(!(Test-Path -LiteralPath $path -PathType Leaf)){return $false}
    if((Get-Item -LiteralPath $path).Length -ne [long]$part.bytes){return $false}
    return (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant() -ceq $part.sha256
}

try {
    $lock=[IO.File]::Open((Join-Path $cache 'prepare.lock'),[IO.FileMode]::OpenOrCreate,[IO.FileAccess]::Write,[IO.FileShare]::None)
} catch [IO.IOException] { exit 0 }
$completed=$false
try {
    if(Test-Path (Join-Path $destination 'index.tsv') -PathType Leaf){exit 0}
    $receipt=Get-Content -LiteralPath (Join-Path $PSScriptRoot 'texture-release.json') -Raw | ConvertFrom-Json
    $required=[long]$receipt.unpacked_bytes+128MB
    foreach($part in $receipt.parts) {
        $file=Get-Item -LiteralPath (Join-Path $cache $part.file) -ErrorAction SilentlyContinue
        $existing=if($file){$file.Length}else{0L}
        $required+=[Math]::Max(0L,[long]$part.bytes-$existing)
    }
    $drive=[IO.DriveInfo]::new([IO.Path]::GetPathRoot($parent))
    if($drive.AvailableFreeSpace -lt $required){throw ('Preparing remastered textures needs approximately {0} GB free.' -f [Math]::Ceiling($required/1e9))}
    $packFiles=@(foreach($part in $receipt.parts) {
        $path=Join-Path $cache $part.file
        if(!(Test-Part $path $part)) {
            if(!(Test-UnmeteredConnection)){throw 'Waiting for an unmetered internet connection.'}
            $file=Get-Item -LiteralPath $path -ErrorAction SilentlyContinue
            if($file -and $file.Length -ge [long]$part.bytes){Remove-Item -LiteralPath $path}
            $url=$receipt.url+$part.file
            & "$env:SystemRoot\System32\curl.exe" --silent --show-error --fail --location --max-redirs 5 --proto '=https' --proto-redir '=https' --connect-timeout 15 --speed-limit 4096 --speed-time 60 --continue-at - --max-filesize $part.bytes --output $path $url
            if($LASTEXITCODE -eq 33){Remove-Item -LiteralPath $path -ErrorAction SilentlyContinue;throw 'Texture download will retry from the start.'}
            if($LASTEXITCODE -ne 0){throw "Texture download ended with code $LASTEXITCODE. It will retry on the next launch."}
            if(!(Test-Part $path $part)){
                Remove-Item -LiteralPath $path -ErrorAction SilentlyContinue
                throw 'Texture part failed integrity checking.'
            }
        }
        Get-Item -LiteralPath $path
    })
    if($packFiles.Count -lt 1 -or $packFiles.Count -gt 64){throw 'Invalid texture volume count.'}
    $stage=Join-Path $parent ('.release-import-'+[Guid]::NewGuid().ToString('N'))
    [IO.Directory]::CreateDirectory($stage) | Out-Null
    $numbers=[Collections.Generic.HashSet[int]]::new()
    $packId=$receipt.pack
    try {
        foreach($file in $packFiles) {
            $zip=[IO.Compression.ZipFile]::OpenRead($file.FullName)
            try {
                $header=$zip.GetEntry('pack-part.json')
                if($null -eq $header -or $header.Length -gt 2MB){throw 'Invalid texture pack manifest.'}
                $reader=[IO.StreamReader]::new($header.Open())
                try{$part=ConvertFrom-Json ($reader.ReadToEnd())}finally{$reader.Dispose()}
                if($part.version -ne 1 -or $part.platform -ne 'windows' -or $part.parts -ne $packFiles.Count -or
                    $part.pack -cnotmatch '^[0-9a-f]{64}$' -or ($packId -and $packId -cne $part.pack) -or
                    $part.part -lt 1 -or $part.part -gt $part.parts -or !$numbers.Add([int]$part.part)) {
                    throw 'Select all ZIP parts from one Windows texture pack.'
                }
                $packId=$part.pack
                $files=$part.files.PSObject.Properties
                if(@($files).Count -ne ($zip.Entries.Count-1)){throw 'Invalid texture file list.'}
                Write-Host "Installing texture part $($part.part) of $($part.parts)"
                $volumeBytes=0L
                foreach($entry in $zip.Entries) {
                    $name=$entry.FullName
                    if($name -ceq 'pack-part.json'){continue}
                    if($name -cne 'index.tsv' -and
                        $name -cnotmatch '^[a-z0-9_][a-z0-9_.-]{1,239}/[124]x/(color|masked|normal|response|relief)\.ktx$') {
                        throw 'Unexpected texture file name.'
                    }
                    $record=$files[$name]
                    if($null -eq $record -or $entry.Length -ne [long]$record.Value[0] -or
                        $entry.Length -le 0 -or $entry.Length -gt 24MB -or $record.Value[1] -cnotmatch '^[0-9a-f]{64}$') {
                        throw 'Invalid texture payload size or checksum.'
                    }
                    $volumeBytes+=$entry.Length
                    if($volumeBytes -gt 1750MB){throw 'The texture ZIP is too large.'}
                    $target=Join-Path $stage $name
                    [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($target)) | Out-Null
                    $stream=$entry.Open()
                    try {
                        $out=[IO.File]::Open($target,[IO.FileMode]::CreateNew)
                        try{$stream.CopyTo($out)}finally{$out.Dispose()}
                    } finally{$stream.Dispose()}
                    if((Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash.ToLowerInvariant() -cne $record.Value[1]) {
                        throw 'A texture ZIP is incomplete or damaged.'
                    }
                }
            } finally{$zip.Dispose()}
        }
        if((Get-Content -LiteralPath (Join-Path $stage 'index.tsv') -TotalCount 1) -cne 'OPENRUNE_TEXTURES_1'){throw 'Invalid texture table.'}
        [IO.Directory]::Move($stage,$destination)
        Write-Host 'Remastered textures are ready. Original is the default.'
    } finally {
        if(Test-Path -LiteralPath $stage){[IO.Directory]::Delete($stage,$true)}
    }

    $completed=$true
} catch {
    $_.Exception.Message | Set-Content -LiteralPath (Join-Path $cache 'status.txt') -Encoding UTF8
    Write-Error -Message $_.Exception.Message -ErrorAction Continue
    exit 1
} finally {
    $lock.Dispose()
    if($completed){
        foreach($part in $receipt.parts){Remove-Item -LiteralPath (Join-Path $cache $part.file) -ErrorAction SilentlyContinue}
        Remove-Item -LiteralPath (Join-Path $cache 'status.txt') -ErrorAction SilentlyContinue
    }
}
