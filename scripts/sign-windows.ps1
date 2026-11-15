param(
    [Parameter(Mandatory = $true)]
    [string]$File,
    [string]$ThumbprintFile,
    [string]$SignTool,
    [string]$TimestampUrl
)

$ErrorActionPreference = 'Stop'
if (-not $ThumbprintFile) {
    $ThumbprintFile = Join-Path (Split-Path -Parent $PSScriptRoot) 'secrets/codesigning/thumbprint.txt'
}
if (-not (Test-Path -LiteralPath $File -PathType Leaf)) { throw 'Signing input file does not exist.' }
$thumbprint = (Get-Content -Raw -LiteralPath $ThumbprintFile).Trim()
if ($thumbprint -notmatch '^[0-9a-fA-F]{40}$') { throw 'Invalid certificate thumbprint.' }
$certificate = Get-Item -LiteralPath "Cert:\CurrentUser\My\$thumbprint"
if (-not $certificate.HasPrivateKey -or $certificate.NotAfter -le (Get-Date) -or $certificate.NotBefore -gt (Get-Date)) {
    throw 'Signing certificate is not usable.'
}
if (-not ($certificate.EnhancedKeyUsageList | Where-Object { $_.ObjectId -eq '1.3.6.1.5.5.7.3.3' })) {
    throw 'Certificate does not permit code signing.'
}
if (-not $SignTool) {
    $sdkBin = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits/10/bin'
    $SignTool = Get-ChildItem -LiteralPath $sdkBin -Directory | Sort-Object Name -Descending |
        ForEach-Object { Join-Path $_.FullName 'x64/signtool.exe' } |
        Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
}
if (-not $SignTool -or -not (Test-Path -LiteralPath $SignTool)) { throw 'Windows SDK signtool.exe was not found.' }
$signArguments = @('sign', '/fd', 'SHA256', '/s', 'My', '/sha1', $thumbprint, '/d', 'OldMoneyTatarin')
if ($TimestampUrl) {
    $timestamp = [Uri]$TimestampUrl
    if (-not $timestamp.IsAbsoluteUri -or $timestamp.Scheme -ne 'https' -or $timestamp.UserInfo) {
        throw 'Timestamp server must use HTTPS without credentials.'
    }
    $signArguments += @('/tr', $TimestampUrl, '/td', 'SHA256')
}
$signArguments += (Resolve-Path -LiteralPath $File).Path
& $SignTool @signArguments
if ($LASTEXITCODE -ne 0) { throw 'SignTool failed.' }
$signature = Get-AuthenticodeSignature -LiteralPath $File
if ($signature.SignerCertificate.Thumbprint -ne $thumbprint -or $signature.Status -eq 'HashMismatch' -or
    $signature.Status -eq 'NotSigned') {
    throw 'Signing result does not match the requested certificate.'
}
Write-Output "Authenticode signature written. Windows trust status: $($signature.Status)"
