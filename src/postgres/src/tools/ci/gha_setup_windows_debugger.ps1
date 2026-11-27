# Setup Windows debugger to log all crashes to
# <workspace>\crashlogs\crashlog-<pid-in-hex>.txt

$ErrorActionPreference = 'Stop'

$crashdir = "$env:GITHUB_WORKSPACE/crashlogs"
New-Item -ItemType Directory -Force -Path $crashdir

# Ensure restricted child processes can write the log file
icacls $crashdir /grant "${env:USERNAME}:(OI)(CI)F" /Q

# Prevent windows error handling dialog from causing hangs
New-ItemProperty -Force -Path 'HKLM:\SOFTWARE\Microsoft\Windows\Windows Error Reporting' `
    -Name 'DontShowUI' -Value 1 -PropertyType DWord
New-ItemProperty -Force -Path 'HKLM:\SOFTWARE\Microsoft\Windows\Windows Error Reporting' `
    -Name 'Disabled' -Value 1 -PropertyType DWord

### Fallback minidumps if the JIT debugger below doesn't run
New-Item -Force -Path 'HKLM:\SOFTWARE\Microsoft\Windows\Windows Error Reporting' `
    -Name 'LocalDumps'
New-ItemProperty -Force -Path 'HKLM:\SOFTWARE\Microsoft\Windows\Windows Error Reporting\LocalDumps' `
    -Name 'DumpFolder' -Value $crashdir -PropertyType ExpandString
New-ItemProperty -Force -Path 'HKLM:\SOFTWARE\Microsoft\Windows\Windows Error Reporting\LocalDumps' `
    -Name 'DumpCount' -Value 5 -PropertyType DWord
New-ItemProperty -Force -Path 'HKLM:\SOFTWARE\Microsoft\Windows\Windows Error Reporting\LocalDumps' `
    -Name 'DumpType'  -Value 1 -PropertyType DWord
###

$cdb64 = @(
    'C:\Program Files (x86)\Windows Kits\10\Debuggers\x64\cdb.exe',
    'C:\Program Files\Windows Kits\10\Debuggers\x64\cdb.exe'
    ) | Where-Object { Test-Path $_ } | Select-Object -First 1
$cdb86 = $cdb64.Replace('\x64\', '\x86\')

###
# -p PID:
#   Specifies the decimal process ID to be debugged. This is used to debug a
#   process that is already running.
# -e Event:
#   Signals the debugger that the specified event has occurred. This option is
#   only used when starting the debugger programmatically.
# -g:
#   Ignores the initial breakpoint in target application. This option will
#   cause the target application to continue running after it is started or
#   CDB attaches to it, unless another breakpoint has been set.
# -kqm:
#   Starts CDB/NTSD in quiet mode.
# -c "command":
#   Specifies the initial debugger command to run at start-up. This command
#   must be surrounded with quotation marks. Multiple commands can be
#   separated with semicolons.
###
$debuggerArgs = ' -p %ld -e %ld -g -kqm -c ".lines -e; .symfix+ ; aS /x proc $tpid ; .block {.logappend ' + "$crashdir/crashlog-" + '${proc}.txt} ; lsa $ip ; ~*kP ; !peb ; .logclose ; q "'

Write-Host "Using cdb (x64): $cdb64"
Set-ItemProperty `
    -Path 'HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion\AeDebug' `
    -Name 'Debugger' -Value ('"' + $cdb64 + '"' + $debuggerArgs)
New-ItemProperty -Force -PropertyType DWord -Value 1 `
    -Path 'HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion\AeDebug' `
    -Name 'Auto'

Write-Host "Using cdb (x86): $cdb86"
Set-ItemProperty `
    -Path 'HKLM:\SOFTWARE\WOW6432Node\Microsoft\Windows NT\CurrentVersion\AeDebug' `
    -Name 'Debugger' -Value ('"' + $cdb86 + '"' + $debuggerArgs)
New-ItemProperty -Force -PropertyType DWord -Value 1 `
    -Path 'HKLM:\SOFTWARE\WOW6432Node\Microsoft\Windows NT\CurrentVersion\AeDebug' `
    -Name 'Auto'

# Show registered AeDebug values for diagnostics
Get-ItemProperty 'HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion\AeDebug' |
    Format-List Debugger,Auto
Get-ItemProperty 'HKLM:\SOFTWARE\WOW6432Node\Microsoft\Windows NT\CurrentVersion\AeDebug' |
    Format-List Debugger,Auto
