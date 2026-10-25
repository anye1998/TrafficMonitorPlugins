<#
.SYNOPSIS
    为 TrafficMonitor 创建"静默以管理员身份启动"的计划任务与快捷方式。

.DESCRIPTION
    背景：HardwareMonitor 插件读 CPU/主板温度必须经 PawnIO 内核驱动，
    而 PawnIO 的设备对象只允许 SYSTEM / Administrators 打开（默认 DACL），
    所以 TrafficMonitor 以普通权限运行时拿不到温度，只能"右键-以管理员身份运行"，
    每次都要点 UAC。

    本脚本的做法：注册一个 RunLevel=Highest 的计划任务，指向 TrafficMonitor.exe。
    之后通过 schtasks /run 启动该任务，Windows 任务计划服务会用完整（已提升）令牌
    拉起进程，**不会弹 UAC**。效果上等于"以管理员身份运行"，但少一次确认。

    注意：进程仍然是管理员权限，不是"降权运行"。这么做不修改任何安全设置，
    也不放宽驱动访问控制，风险最小。

.PARAMETER AppPath
    TrafficMonitor.exe 的完整路径。默认 D:\TrafficMonitor\TrafficMonitor.exe。

.PARAMETER TaskName
    计划任务名称，默认 TrafficMonitor_Elevated。

.PARAMETER ShortcutPath
    生成的快捷方式路径，默认桌面下的 "TrafficMonitor（静默提权）.lnk"。

.EXAMPLE
    # 右键"以管理员身份运行 PowerShell"，然后：
    .\Install-SilentElevatedLaunch.ps1 -AppPath "D:\TrafficMonitor\TrafficMonitor.exe"
#>
[CmdletBinding()]
param(
    [string]$AppPath = 'D:\TrafficMonitor\TrafficMonitor.exe',
    [string]$TaskName = 'TrafficMonitor_Elevated',
    [string]$ShortcutPath
)

$ErrorActionPreference = 'Stop'

function Test-IsAdmin {
    $id = [Security.Principal.WindowsIdentity]::GetCurrent()
    (New-Object Security.Principal.WindowsPrincipal($id)).IsInRole(
        [Security.Principal.WindowsBuiltInRole]::Administrator)
}

if (-not (Test-IsAdmin)) {
    Write-Host '需要管理员权限才能注册 RunLevel=Highest 的计划任务。' -ForegroundColor Yellow
    Write-Host '请右键点击 PowerShell / Windows Terminal，选择"以管理员身份运行"后重试。' -ForegroundColor Yellow
    exit 1
}

if (-not (Test-Path -LiteralPath $AppPath)) {
    throw "找不到程序: $AppPath"
}
$AppPath = (Resolve-Path -LiteralPath $AppPath).Path
$appDir  = Split-Path -Parent $AppPath
$appName = Split-Path -Leaf  $AppPath

Write-Host "目标程序 : $AppPath"
Write-Host "任务名称 : $TaskName"

# 已存在则先删掉，保证幂等
$existing = Get-ScheduledTask -TaskName $TaskName -ErrorAction SilentlyContinue
if ($existing) {
    Write-Host '已存在同名任务，先删除。'
    Unregister-ScheduledTask -TaskName $TaskName -Confirm:$false
}

$action = New-ScheduledTaskAction -Execute $AppPath -WorkingDirectory $appDir
$principal = New-ScheduledTaskPrincipal -UserId "$env:USERDOMAIN\$env:USERNAME" `
                                        -LogonType Interactive `
                                        -RunLevel Highest
# 不设触发器：只能在需要时手动 /run，不会开机自启（保留 TrafficMonitor 自身的开机自启设置）
$settings = New-ScheduledTaskSettingsSet -AllowStartIfOnBatteries `
                                         -DontStopIfGoingOnBatteries `
                                         -MultipleInstances IgnoreNew `
                                         -ExecutionTimeLimit ([TimeSpan]::Zero) `
                                         -DisallowHardTerminate

Register-ScheduledTask -TaskName $TaskName `
                       -Action $action `
                       -Principal $principal `
                       -Settings $settings `
                       -Description 'TrafficMonitor: 以最高权限启动，用于 HardwareMonitor 插件读取 CPU/主板温度（不弹 UAC）' `
                       -Force | Out-Null

Write-Host '计划任务已创建。' -ForegroundColor Green

# 校验
$t = Get-ScheduledTask -TaskName $TaskName
Write-Host ("  运行级别: {0}" -f $t.Principal.RunLevel)
Write-Host ("  登录类型: {0}" -f $t.Principal.LogonType)

# 快捷方式：调 schtasks /run，不直接指向 exe
if (-not $ShortcutPath) {
    $ShortcutPath = Join-Path ([Environment]::GetFolderPath('Desktop')) 'TrafficMonitor（静默提权）.lnk'
}
$schtasks = Join-Path $env:SystemRoot 'System32\schtasks.exe'
$shell = New-Object -ComObject WScript.Shell
$lnk = $shell.CreateShortcut($ShortcutPath)
$lnk.TargetPath       = $schtasks
$lnk.Arguments        = "/run /tn `"$TaskName`""
$lnk.WorkingDirectory = $appDir
$lnk.IconLocation     = "$AppPath,0"
$lnk.Description      = '以管理员权限启动 TrafficMonitor（不弹 UAC）'
$lnk.WindowStyle      = 7      # 最小化，避免闪一下黑色控制台
$lnk.Save()

Write-Host "快捷方式已创建: $ShortcutPath" -ForegroundColor Green
Write-Host ''
Write-Host '完成。以后从这个快捷方式启动即可，不会弹 UAC，温度可以正常显示。' -ForegroundColor Green
Write-Host '若想恢复原状，运行 Uninstall-SilentElevatedLaunch.ps1。'
