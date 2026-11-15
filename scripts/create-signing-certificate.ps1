param()

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$certificateDirectory = Join-Path $repoRoot 'secrets/codesigning'
$thumbprintFile = Join-Path $certificateDirectory 'thumbprint.txt'
if (Test-Path -LiteralPath $thumbprintFile) {
    $thumbprint = (Get-Content -Raw -LiteralPath $thumbprintFile).Trim()
    $existing = Get-Item -LiteralPath "Cert:\CurrentUser\My\$thumbprint" -ErrorAction Stop
    if (-not $existing.HasPrivateKey -or $existing.NotAfter -le (Get-Date)) {
        throw 'Existing signing certificate is expired or has no private key.'
    }
    Write-Output 'Existing local TATARIN signing certificate retained.'
    return
}

New-Item -ItemType Directory -Force -Path $certificateDirectory | Out-Null
$certificate = New-SelfSignedCertificate -Type CodeSigningCert `
    -Subject 'CN=TATARIN, O=TATARIN Co.' -FriendlyName 'TATARIN local code signing' `
    -CertStoreLocation 'Cert:\CurrentUser\My' -KeyAlgorithm RSA -KeyLength 3072 `
    -HashAlgorithm SHA256 -KeyExportPolicy NonExportable -NotAfter (Get-Date).AddYears(3)
# Only a public certificate and its identifier are written to disk. The private key
# remains in the current user's Windows key store; no trust stores are modified.
Export-Certificate -Cert $certificate -FilePath (Join-Path $certificateDirectory 'TATARIN.cer') | Out-Null
[IO.File]::WriteAllText($thumbprintFile, $certificate.Thumbprint, [Text.Encoding]::ASCII)
Write-Output 'Created local TATARIN signing certificate in CurrentUser/My (not trusted).'
