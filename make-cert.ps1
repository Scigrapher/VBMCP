param(
    [string]$Subject = "CN=localhost",
    [string[]]$DnsName = @("localhost", "127.0.0.1"),
    [string]$OutFile = "$PSScriptRoot\server.pfx",
    [string]$Password = "",
    [int]$ValidDays = 730
)

$ErrorActionPreference = "Stop"
$cert = New-SelfSignedCertificate -Type Custom -Subject $Subject `
    -KeyAlgorithm RSA -KeyLength 2048 -HashAlgorithm SHA256 `
    -CertStoreLocation "Cert:\CurrentUser\My" `
    -DnsName $DnsName -NotAfter (Get-Date).AddDays($ValidDays)
try {
    if ($Password) {
        $secure = ConvertTo-SecureString -String $Password -Force -AsPlainText
        Export-PfxCertificate -Cert $cert -FilePath $OutFile -Password $secure | Out-Null
    } else {
        Export-PfxCertificate -Cert $cert -FilePath $OutFile | Out-Null
    }
    Write-Host "Exported certificate: $OutFile"
    Write-Host "Thumbprint: $($cert.Thumbprint)"
} finally {
    Remove-Item -LiteralPath ("Cert:\CurrentUser\My\" + $cert.Thumbprint) -Force
}
