$ErrorActionPreference = "Stop"

$packageInf = "E:\driver_build\xdma_driver_win_src\xdma_driver_win-2020.5\build\x64\XDMA_Driver\Win10_Release\XDMA_Driver\XDMA.inf"
$backupDir = "E:\driver_build\driver_backup_20260418"
$logPath = "E:\driver_build\reinstall_xdma_8mb.log"

Start-Transcript -Path $logPath -Force

try {
    Write-Host "=== Export current Xilinx driver package ==="
    New-Item -ItemType Directory -Path $backupDir -Force | Out-Null
    pnputil /export-driver oem80.inf $backupDir

    Write-Host "=== Remove current Xilinx driver package ==="
    pnputil /delete-driver oem80.inf /uninstall /force

    Write-Host "=== Install rebuilt 8MB package ==="
    pnputil /add-driver $packageInf /install

    Write-Host "=== Query current XDMA device ==="
    Get-PnpDevice -PresentOnly |
        Where-Object { $_.FriendlyName -like "*Xilinx DMA*" -or $_.Class -eq "Xilinx Drivers" } |
        Format-List Status, Class, FriendlyName, InstanceId, Problem, ConfigManagerErrorCode
}
finally {
    Stop-Transcript
}
