$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$www = Join-Path $root "homeassistant\www"
$target = Join-Path $www "mushroom.js"
$url = "https://github.com/piitaya/lovelace-mushroom/releases/download/v5.2.3/mushroom.js"

New-Item -ItemType Directory -Force -Path $www | Out-Null
Invoke-WebRequest -Uri $url -OutFile $target

Write-Host "Mushroom downloaded to:"
Write-Host $target
