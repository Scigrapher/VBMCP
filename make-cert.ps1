param(
    [string]$Subject = "CN=localhost",
    [string[]]$DnsName = @("localhost", "127.0.0.1"),
    [string]$OutFile = "$PSScriptRoot\server.pfx",
    [string]$Password = "",
    [int]$ValidDays = 730
)

$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.Security

$rsa = [System.Security.Cryptography.RSA]::Create(2048)
try {
    $request = [System.Security.Cryptography.X509Certificates.CertificateRequest]::new(
        [System.Security.Cryptography.X509Certificates.X500DistinguishedName]::new($Subject),
        $rsa,
        [System.Security.Cryptography.HashAlgorithmName]::SHA256,
        [System.Security.Cryptography.RSASignaturePadding]::Pkcs1)

    $san = [System.Security.Cryptography.X509Certificates.SubjectAlternativeNameBuilder]::new()
    foreach ($name in $DnsName) {
        $ip = $null
        if ([System.Net.IPAddress]::TryParse($name, [ref]$ip)) {
            $san.AddIpAddress($ip)
        } else {
            $san.AddDnsName($name)
        }
    }
    $request.CertificateExtensions.Add($san.Build())

    $keyUsage = [System.Security.Cryptography.X509Certificates.X509KeyUsageFlags]::DigitalSignature -bor
        [System.Security.Cryptography.X509Certificates.X509KeyUsageFlags]::KeyEncipherment
    $request.CertificateExtensions.Add(
        [System.Security.Cryptography.X509Certificates.X509KeyUsageExtension]::new($keyUsage, $false))

    $eku = [System.Security.Cryptography.OidCollection]::new()
    [void]$eku.Add([System.Security.Cryptography.Oid]::new("1.3.6.1.5.5.7.3.1"))
    $request.CertificateExtensions.Add(
        [System.Security.Cryptography.X509Certificates.X509EnhancedKeyUsageExtension]::new($eku, $false))

    $cert = $request.CreateSelfSigned(
        [System.DateTimeOffset]::UtcNow.AddDays(-1),
        [System.DateTimeOffset]::UtcNow.AddDays($ValidDays))
    try {
        $outPath = [System.IO.Path]::GetFullPath($OutFile)
        $outParent = Split-Path -Parent $outPath
        if ($outParent) {
            New-Item -ItemType Directory -Force -Path $outParent | Out-Null
        }
        $bytes = $cert.Export(
            [System.Security.Cryptography.X509Certificates.X509ContentType]::Pfx,
            $Password)
        [System.IO.File]::WriteAllBytes($outPath, $bytes)
        Write-Host "Exported certificate: $outPath"
        Write-Host "Thumbprint: $($cert.Thumbprint)"
    } finally {
        $cert.Dispose()
    }
} finally {
    $rsa.Dispose()
}
