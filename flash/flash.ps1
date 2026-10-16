# Windows 烧录（需要 Python + esptool：py -m pip install esptool）
param([string]$Port = "COM5")
$ErrorActionPreference = "Stop"
Set-Location $PSScriptRoot
py -m esptool --chip esp32s3 --port $Port --baud 921600 `
  write_flash 0x0 bootloader.bin 0x8000 partition-table.bin 0x10000 esp32_buddy.bin
