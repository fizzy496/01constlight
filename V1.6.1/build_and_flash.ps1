# ============================================================
#  STM32F103RCT6 一键编译+烧录脚本
#  用法: .\build_and_flash.ps1
# ============================================================

$ErrorActionPreference = "Stop"

$UV4       = "C:\Keil_v5\UV4\UV4.exe"
$Project   = "C:\Users\FIZZZY\Desktop\111111111\MDK-ARM\STM32F103RCT6.uvprojx"
$BuildLog  = "C:\Users\FIZZZY\Desktop\111111111\MDK-ARM\STM32F103RCT6\STM32F103RCT6.build_log.htm"
$HexFile   = "C:\Users\FIZZZY\Desktop\111111111\MDK-ARM\STM32F103RCT6\STM32F103RCT6.hex"

Write-Host "============================================" -ForegroundColor Cyan
Write-Host "  STM32F103RCT6 一键编译+烧录" -ForegroundColor Cyan
Write-Host "============================================" -ForegroundColor Cyan
Write-Host ""

# ---- Step 1: Compile ----
Write-Host "[1/2] 编译中..." -ForegroundColor Yellow
$compileStart = Get-Date

& $UV4 -r $Project -j0 -o "$env:TEMP\keil_build.log" 2>&1 | Out-Null

if ($LASTEXITCODE -ne 0) {
    Write-Host "  [FAIL] 编译失败 (exit code: $LASTEXITCODE)" -ForegroundColor Red
    exit 1
}

$compileTime = [math]::Round(((Get-Date) - $compileStart).TotalSeconds, 1)
Write-Host "  [OK]  编译完成 ($compileTime s)" -ForegroundColor Green

# ---- Step 2: Parse build log ----
if (Test-Path $BuildLog) {
    $html = Get-Content $BuildLog -Raw -Encoding UTF8
    if ($html -match '(\d+)\s+Error\(s\).*?(\d+)\s+Warning\(s\)') {
        $errCount = $Matches[1]
        $warnCount = $Matches[2]
    }
    if ($html -match 'Program Size:\s*(Code=\d+\s+RO-data=\d+\s+RW-data=\d+\s+ZI-data=\d+)') {
        $progSize = $Matches[1]
    }
    if ($html -match 'Build Time Elapsed:\s*(\d+:\d+:\d+)') {
        $buildTime = $Matches[1]
    }
}

if ($errCount -ne "0") {
    Write-Host "  [FAIL] $errCount Error(s), $warnCount Warning(s)" -ForegroundColor Red
    exit 1
}

Write-Host "  [INFO] $progSize" -ForegroundColor Gray
Write-Host "  [INFO] $warnCount Warning(s)" -ForegroundColor Gray
Write-Host ""

# ---- Step 3: Flash ----
Write-Host "[2/2] 烧录中..." -ForegroundColor Yellow

& $UV4 -f $Project -j0 2>&1 | Out-Null

if ($LASTEXITCODE -ne 0) {
    Write-Host "  [FAIL] 烧录失败 (exit code: $LASTEXITCODE)" -ForegroundColor Red
    exit 1
}

Write-Host "  [OK]  烧录完成" -ForegroundColor Green
Write-Host ""

# ---- Summary ----
Write-Host "============================================" -ForegroundColor Cyan
Write-Host "  编译烧录全部完成!" -ForegroundColor Green
Write-Host "  $progSize" -ForegroundColor Gray
Write-Host "  $errCount Error(s), $warnCount Warning(s)" -ForegroundColor Gray
Write-Host "============================================" -ForegroundColor Cyan