# install.ps1 for Hexagen Framework (hf) on Windows
# Downloads the precompiled hf.exe and adds it to the User's PATH.

$ErrorActionPreference = 'Stop'

$Repo = "rbaezc/hexagenframework"
$Version = "latest"
$BinaryName = "hf.exe"

$DownloadUrl = "https://github.com/$Repo/releases/$Version/download/hf_windows_x86_64.exe"
if ($Version -eq "latest") {
    $DownloadUrl = "https://github.com/$Repo/releases/latest/download/hf_windows_x86_64.exe"
}

$InstallDir = Join-Path $HOME ".hexagen\bin"
if (!(Test-Path $InstallDir)) {
    New-Item -ItemType Directory -Force -Path $InstallDir | Out-Null
}

$TargetPath = Join-Path $InstallDir $BinaryName

Write-Host "🏎️  Downloading Hexagen compiler for Windows..." -ForegroundColor Cyan
try {
    [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
    Invoke-WebRequest -Uri $DownloadUrl -OutFile $TargetPath -UseBasicParsing
} catch {
    Write-Error "❌ Download failed. Make sure the release exists on GitHub: $DownloadUrl"
    exit 1
}

Write-Host "⚙️  Adding $InstallDir to User PATH environment variable..." -ForegroundColor Cyan
$UserPath = [Environment]::GetEnvironmentVariable("Path", "User")
if ($UserPath -notlike "*$InstallDir*") {
    [Environment]::SetEnvironmentVariable("Path", $UserPath + ";" + $InstallDir, "User")
    $env:Path = "$env:Path;$InstallDir"
}

Write-Host ""
Write-Host "✨ Hexagen Framework (hf) installed successfully!" -ForegroundColor Green
Write-Host "👉 Restart your terminal/PowerShell and run 'hf --help' to verify the installation." -ForegroundColor Yellow
