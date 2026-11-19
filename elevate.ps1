param(
    [Parameter(Mandatory = $true)]
    [string]$FilePath,
    [string]$Arguments = ""
)

$ErrorActionPreference = "Stop"

try {
    $command = '"' + $FilePath + '"'
    if ($Arguments.Length -gt 0) {
        $command += " " + $Arguments
    }
    $process = Start-Process -FilePath $env:ComSpec `
        -ArgumentList @("/d", "/c", $command) `
        -WorkingDirectory (Split-Path -Parent $FilePath) `
        -Verb RunAs -Wait -PassThru
    exit $process.ExitCode
} catch {
    Write-Host ""
    Write-Host "Elevation failed: $($_.Exception.Message)"
    exit 5
}
