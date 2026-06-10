# install.ps1 for Hexagen Framework (hf) on Windows
# Downloads the precompiled hf.exe & hf_core.exe and adds them to the User's PATH.

$ErrorActionPreference = 'Stop'

$Repo = "rbaezc/hexagenframework"
$Version = "latest"

$CliName = "hf.exe"
$CoreName = "hf_core.exe"

$CliUrl = "https://github.com/$Repo/releases/$Version/download/hf_windows_x86_64.exe"
$CoreUrl = "https://github.com/$Repo/releases/$Version/download/hf_core_windows_x86_64.exe"

if ($Version -eq "latest") {
    $CliUrl = "https://github.com/$Repo/releases/latest/download/hf_windows_x86_64.exe"
    $CoreUrl = "https://github.com/$Repo/releases/latest/download/hf_core_windows_x86_64.exe"
}

$InstallDir = Join-Path $HOME ".hexagen\bin"
if (!(Test-Path $InstallDir)) {
    New-Item -ItemType Directory -Force -Path $InstallDir | Out-Null
}

$CliPath = Join-Path $InstallDir $CliName
$CorePath = Join-Path $InstallDir $CoreName

[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12

Write-Host "🏎️  Downloading Hexagen CLI (hf.exe)..." -ForegroundColor Cyan
try {
    Invoke-WebRequest -Uri $CliUrl -OutFile $CliPath -UseBasicParsing
} catch {
    Write-Error "❌ Download of hf.exe failed. Make sure the release exists on GitHub: $CliUrl"
    exit 1
}

Write-Host "🏎️  Downloading Hexagen Compiler Core (hf_core.exe)..." -ForegroundColor Cyan
try {
    Invoke-WebRequest -Uri $CoreUrl -OutFile $CorePath -UseBasicParsing
} catch {
    Write-Error "❌ Download of hf_core.exe failed. Make sure the release exists on GitHub: $CoreUrl"
    exit 1
}

Write-Host "⚙️  Adding $InstallDir to User PATH environment variable..." -ForegroundColor Cyan
$UserPath = [Environment]::GetEnvironmentVariable("Path", "User")
if ($UserPath -notlike "*$InstallDir*") {
    [Environment]::SetEnvironmentVariable("Path", $UserPath + ";" + $InstallDir, "User")
    $env:Path = "$env:Path;$InstallDir"
}

Write-Host ""
Write-Host "✨ Hexagen Framework installed successfully!" -ForegroundColor Green
Write-Host "👉 Restart your terminal/PowerShell and run 'hf --help' to verify the installation." -ForegroundColor Yellow
