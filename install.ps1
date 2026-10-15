[CmdletBinding()]
param(
    [string]$Version = '0.2.0',
    [string]$InstallDir = '',
    [switch]$NoPath,
    [switch]$Help
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2.0

if ($Help) {
    Write-Output 'Usage: .\install.ps1 [-Version VERSION] [-InstallDir DIRECTORY] [-NoPath]'
    Write-Output 'Default version: 0.2.0 (including its pre-release).'
    Write-Output 'Default directory: %LOCALAPPDATA%\Programs\watx'
    Write-Output 'Adds bin to your user PATH unless -NoPath is set.'
    return
}

if ($env:OS -ne 'Windows_NT') {
    throw 'Use install.sh on macOS and Linux.'
}
if ($PSVersionTable.PSVersion -lt [version]'5.1') {
    throw 'PowerShell 5.1 or newer is required.'
}
$Version = $Version -creplace '^v', ''
if ($Version -notmatch '^[0-9][0-9A-Za-z.-]*$') {
    throw 'Invalid release version.'
}
if ([string]::IsNullOrWhiteSpace($InstallDir)) {
    if ([string]::IsNullOrWhiteSpace($env:LOCALAPPDATA)) {
        throw 'Set LOCALAPPDATA or pass -InstallDir.'
    }
    $InstallDir = Join-Path $env:LOCALAPPDATA 'Programs\watx'
}
if (-not [System.IO.Path]::IsPathRooted($InstallDir)) {
    throw '-InstallDir must be an absolute path.'
}
$InstallDir = [System.IO.Path]::GetFullPath($InstallDir)

$nativeArchitecture = $env:PROCESSOR_ARCHITECTURE
if (-not [string]::IsNullOrEmpty($env:PROCESSOR_ARCHITEW6432)) {
    $nativeArchitecture = $env:PROCESSOR_ARCHITEW6432
}
try {
    $nativeArchitecture = [System.Runtime.InteropServices.RuntimeInformation]::OSArchitecture.ToString()
} catch {
}
switch ($nativeArchitecture.ToUpperInvariant()) {
    { $_ -in 'AMD64', 'X64', 'X86_64' } { $architecture = 'x86_64'; break }
    { $_ -in 'ARM64', 'AARCH64' } { $architecture = 'arm64'; break }
    default { throw "Unsupported CPU architecture: $nativeArchitecture" }
}

$packageName = "watx-$Version-windows-$architecture"
$asset = "$packageName.zip"
$releaseUrl = "https://github.com/theyhaveiceinthem/watx/releases/download/v$Version"
$temporaryDirectory = Join-Path ([System.IO.Path]::GetTempPath()) ('watx-install-' + [Guid]::NewGuid().ToString('N'))
$previousProtocol = [Net.ServicePointManager]::SecurityProtocol

try {
    [Net.ServicePointManager]::SecurityProtocol = $previousProtocol -bor [Net.SecurityProtocolType]::Tls12
    New-Item -ItemType Directory -Path $temporaryDirectory | Out-Null
    $archive = Join-Path $temporaryDirectory $asset
    $checksums = Join-Path $temporaryDirectory 'SHA256SUMS.txt'
    Write-Host "Downloading watx $Version for Windows $architecture..."
    Invoke-WebRequest -UseBasicParsing -Uri "$releaseUrl/$asset" -OutFile $archive
    Invoke-WebRequest -UseBasicParsing -Uri "$releaseUrl/SHA256SUMS.txt" -OutFile $checksums

    $checksumPattern = '^([0-9a-fA-F]{64})\s+\*?' + [Regex]::Escape($asset) + '$'
    $expected = @(
        foreach ($line in Get-Content -LiteralPath $checksums) {
            if ($line -match $checksumPattern) {
                $Matches[1]
            }
        }
    )
    if ($expected.Count -ne 1) {
        throw 'Release checksum is missing, invalid, or duplicated.'
    }
    $actual = (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash
    if ($actual -ine $expected[0]) {
        throw 'Checksum mismatch; nothing was installed.'
    }

    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $zip = [System.IO.Compression.ZipFile]::OpenRead($archive)
    try {
        if ($zip.Entries.Count -eq 0) {
            throw 'The release archive is empty.'
        }
        foreach ($entry in $zip.Entries) {
            $entryName = $entry.FullName.Replace('\', '/')
            if (-not $entryName.StartsWith($packageName + '/', [StringComparison]::Ordinal) -or
                $entryName -match '(^|/)\.\.(/|$)' -or $entryName.Contains(':')) {
                throw 'Unexpected release archive layout.'
            }
        }
    } finally {
        $zip.Dispose()
    }
    $unpackDirectory = Join-Path $temporaryDirectory 'unpack'
    Expand-Archive -LiteralPath $archive -DestinationPath $unpackDirectory
    $package = Join-Path $unpackDirectory $packageName
    foreach ($required in @('bin\watx.exe', 'bin\watx.dll', 'lib\watx.lib',
            'include\watx\host.h', 'lib\cmake\watx\watxConfig.cmake', 'LICENSE', 'README.md')) {
        if (-not (Test-Path -LiteralPath (Join-Path $package $required) -PathType Leaf)) {
            throw "Release does not contain $required."
        }
    }

    Write-Host "Installing to $InstallDir..."
    foreach ($directory in @('bin', 'lib', 'include', 'share\doc\watx')) {
        New-Item -ItemType Directory -Path (Join-Path $InstallDir $directory) -Force | Out-Null
    }
    foreach ($directory in @('bin', 'lib', 'include')) {
        $source = Join-Path $package $directory
        $destination = Join-Path $InstallDir $directory
        Get-ChildItem -LiteralPath $source | Copy-Item -Destination $destination -Recurse -Force
    }
    foreach ($document in @('LICENSE', 'README.md')) {
        Copy-Item -LiteralPath (Join-Path $package $document) -Destination (Join-Path $InstallDir 'share\doc\watx') -Force
    }

    $binDirectory = Join-Path $InstallDir 'bin'
    if (-not $NoPath) {
        $userPath = [Environment]::GetEnvironmentVariable('Path', 'User')
        $pathEntries = @($userPath -split ';' | Where-Object { -not [string]::IsNullOrWhiteSpace($_) })
        $alreadyPresent = @($pathEntries | Where-Object {
            [Environment]::ExpandEnvironmentVariables($_).TrimEnd([char[]]'\/') -ieq $binDirectory.TrimEnd([char[]]'\/')
        }).Count -gt 0
        if (-not $alreadyPresent) {
            $updatedPath = ($pathEntries + $binDirectory) -join ';'
            [Environment]::SetEnvironmentVariable('Path', $updatedPath, 'User')
        }
        $env:Path = $binDirectory + ';' + $env:Path
    }
    Write-Host "Installed watx $Version to $InstallDir"
    if ($NoPath) {
        Write-Host "Add this directory to PATH: $binDirectory"
    } else {
        Write-Host 'Open a new terminal to use watx --help.'
    }
    Write-Host "For embedding, set CMAKE_PREFIX_PATH to $InstallDir"
} finally {
    [Net.ServicePointManager]::SecurityProtocol = $previousProtocol
    if (Test-Path -LiteralPath $temporaryDirectory) {
        Remove-Item -LiteralPath $temporaryDirectory -Recurse -Force
    }
}
