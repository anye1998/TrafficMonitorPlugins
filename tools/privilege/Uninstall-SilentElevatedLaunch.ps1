<#
.SYNOPSIS
    撤销 Install-SilentElevatedLaunch.ps1 所做的改动。

.DESCRIPTION
    删除计划任务与快捷方式。不会动 TrafficMonitor 自身的任何文件或设置。
#>
[CmdletBinding()]
param(
    [string]$TaskName = 'TrafficMonitor_Elevated',
    [string]$ShortcutPath
)

$ErrorActionPreference = 'Stop'

function Test-IsAdmin {
    $id = [Security.Principal.WindowsIdentity]::GetCurrent()
    (New-Object Security.Principal.WindowsPrincipal($id)).IsInRole(
        [Security.Principal.WindowsBuiltInRole]::Administrator)
}

if (-not $ShortcutPath) {
    $ShortcutPath = Join-Path ([Environment]::GetFolderPath('Desktop')) 'TrafficMonitor（静默提权）.lnk'
}

if (Test-Path -LiteralPath $ShortcutPath) {
    Remove-Item -LiteralPath $ShortcutPath -Force
    Write-Host "已删除快捷方式: $ShortcutPath" -ForegroundColor Green
} else {
    Write-Host "快捷方式不存在，跳过: $ShortcutPath"
}

if (-not (Test-IsAdmin)) {
    Write-Host '删除计划任务需要管理员权限，请以管理员身份重试（快捷方式已删除）。' -ForegroundColor Yellow
    exit 1
}

$existing = Get-ScheduledTask -TaskName $TaskName -ErrorAction SilentlyContinue
if ($existing) {
    Unregister-ScheduledTask -TaskName $TaskName -Confirm:$false
    Write-Host "已删除计划任务: $TaskName" -ForegroundColor Green
} else {
    Write-Host "计划任务不存在，跳过: $TaskName"
}

Write-Host ''
Write-Host '已恢复原状。以后需以管理员身份运行 TrafficMonitor 才能看到 CPU/主板温度。'
