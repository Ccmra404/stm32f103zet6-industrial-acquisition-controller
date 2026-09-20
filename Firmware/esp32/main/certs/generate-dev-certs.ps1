$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$cert = Join-Path $root "servercert.pem"
$key = Join-Path $root "serverkey.pem"

if (-not (Get-Command openssl -ErrorAction SilentlyContinue)) {
    throw "OpenSSL is required. Install it or run this script from an ESP-IDF environment."
}

& openssl req -x509 -newkey rsa:2048 -nodes `
    -keyout $key `
    -out $cert `
    -days 3650 `
    -subj "/CN=industrial-controller.local" `
    -addext "subjectAltName=DNS:industrial-controller.local"

Write-Host "Created:"
Write-Host $cert
Write-Host $key
