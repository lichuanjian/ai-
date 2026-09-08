$ErrorActionPreference = "Stop"

$isoPath = "E:\driver_build\EWDK_ge_release_svc_prod1_26100_250904-1728.iso"
$sourceRoot = "E:\DAS_9.0(1)\DAS_9.0\driver_work\xdma_driver_win-2020.5"
$configuration = "Win10_Release"
$platform = "x64"

if (!(Test-Path -LiteralPath $isoPath)) {
    throw "ISO not found: $isoPath"
}

if (!(Test-Path -LiteralPath $sourceRoot)) {
    throw "Source root not found: $sourceRoot"
}

$diskImage = Get-DiskImage -ImagePath $isoPath -ErrorAction SilentlyContinue
if (!$diskImage -or $diskImage.Attached -ne $true) {
    $diskImage = Mount-DiskImage -ImagePath $isoPath -PassThru
}

$volume = $diskImage | Get-Volume
if (!$volume.DriveLetter) {
    throw "Mounted ISO does not have a drive letter."
}

$driveRoot = "$($volume.DriveLetter):"
$setupBuildEnv = Join-Path $driveRoot "BuildEnv\SetupBuildEnv.cmd"
$setupVsEnv = Join-Path $driveRoot "BuildEnv\SetupVSEnv.cmd"
$projectPath = Join-Path $sourceRoot "sys\XDMA_Driver.vcxproj"
$buildBat = Join-Path $env:TEMP "build_xdma_driver_local.bat"
$buildLog = Join-Path $sourceRoot "build_xdma_driver.log"

$batContent = @"
@echo off
call "$setupBuildEnv" amd64
call "$setupVsEnv"
msbuild "$projectPath" /t:Build /p:Configuration=$configuration /p:Platform=$platform /m /nologo
"@

Set-Content -LiteralPath $buildBat -Value $batContent -Encoding ASCII

try {
    & cmd.exe /c $buildBat 2>&1 | Tee-Object -FilePath $buildLog
    if ($LASTEXITCODE -ne 0) {
        throw "Driver build failed with exit code $LASTEXITCODE. See $buildLog"
    }
}
finally {
    Remove-Item -LiteralPath $buildBat -ErrorAction SilentlyContinue
}

$outputDir = Join-Path $sourceRoot "build\$platform\XDMA_Driver\$configuration"
Write-Host "Build completed. Output directory: $outputDir"
Get-ChildItem -LiteralPath $outputDir | Select-Object Name, Length, LastWriteTime
