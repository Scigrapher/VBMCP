param(
    [string]$Version = "dev",
    [string]$OutputDirectory = (Join-Path $PSScriptRoot "dist")
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $MyInvocation.MyCommand.Path
$SafeVersion = $Version -replace "[^0-9A-Za-z._-]", "-"
if (-not $SafeVersion) {
    throw "Version must contain at least one safe filename character."
}

$OutputDirectory = [System.IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null

$Stage = Join-Path $OutputDirectory (".stage-" + [guid]::NewGuid().ToString("N"))
$Archive = Join-Path $OutputDirectory ("MCPAddin-" + $SafeVersion + ".zip")
$Checksum = $Archive + ".sha256"

$Files = @(
    "bin\MCPAddin.dll",
    "bin\mcpaddin_selftest.exe",
    "install.cmd",
    "uninstall.cmd",
    "elevate.ps1",
    "make-cert.ps1",
    "mcpaddin.example.ini",
    "README.md"
)
$Directories = @(
    "docs",
    "examples"
)

function Remove-Stage {
    $resolvedStage = [System.IO.Path]::GetFullPath($Stage)
    $resolvedOutput = [System.IO.Path]::GetFullPath($OutputDirectory).TrimEnd('\') + '\'
    if (-not $resolvedStage.StartsWith($resolvedOutput, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to remove a package staging path outside the output directory."
    }
    if (Test-Path -LiteralPath $resolvedStage) {
        Remove-Item -LiteralPath $resolvedStage -Recurse -Force
    }
}

function Remove-StageChild {
    param([string]$Path)
    $resolvedStage = [System.IO.Path]::GetFullPath($Stage).TrimEnd('\') + '\'
    $resolvedPath = [System.IO.Path]::GetFullPath($Path)
    if (-not $resolvedPath.StartsWith($resolvedStage, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to remove a package path outside the staging directory."
    }
    Remove-Item -LiteralPath $resolvedPath -Recurse -Force
}

try {
    Remove-Stage
    New-Item -ItemType Directory -Force -Path $Stage | Out-Null

    foreach ($relativePath in $Files) {
        $source = Join-Path $Root $relativePath
        if (-not (Test-Path -LiteralPath $source -PathType Leaf)) {
            throw "Required release file was not found: $source"
        }
        $destination = Join-Path $Stage $relativePath
        New-Item -ItemType Directory -Force -Path (Split-Path -Parent $destination) | Out-Null
        Copy-Item -LiteralPath $source -Destination $destination
    }

    foreach ($relativePath in $Directories) {
        $source = Join-Path $Root $relativePath
        if (-not (Test-Path -LiteralPath $source -PathType Container)) {
            throw "Required release directory was not found: $source"
        }
        Copy-Item -LiteralPath $source -Destination $Stage -Recurse
    }

    Get-ChildItem -LiteralPath $Stage -Directory -Filter "__pycache__" -Recurse |
        ForEach-Object { Remove-StageChild -Path $_.FullName }

    if (Test-Path -LiteralPath $Archive) {
        Remove-Item -LiteralPath $Archive -Force
    }
    if (Test-Path -LiteralPath $Checksum) {
        Remove-Item -LiteralPath $Checksum -Force
    }

    Compress-Archive -Path (Join-Path $Stage "*") -DestinationPath $Archive -CompressionLevel Optimal
    $hash = (Get-FileHash -LiteralPath $Archive -Algorithm SHA256).Hash.ToLowerInvariant()
    $fileName = Split-Path -Leaf $Archive
    Set-Content -LiteralPath $Checksum -Value ("$hash  $fileName") -Encoding ASCII

    Write-Host "Packaged: $Archive"
    Write-Host "Checksum: $Checksum"
} finally {
    Remove-Stage
}
