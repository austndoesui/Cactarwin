# Persistent, reconnecting CDC-ACM CLOG receiver for cactus.
# One .bin file per USB attachment, preserving evidence across boot loops.
param(
    [int]$Seconds = 0,
    [string]$OutputRoot = 'F:\ROM\patching\redmi 6a xnu\usb-captures',
    [int]$PollMilliseconds = 150,
    [string]$PortName = 'COM25',
    [switch]$SkipDecode
)

$ErrorActionPreference = 'Stop'
if ($Seconds -lt 0 -or $PollMilliseconds -lt 50) {
    throw 'Seconds must be >= 0 and PollMilliseconds >= 50'
}
$target = 'VID_1209&PID_0001'
$started = Get-Date
$deadline = if ($Seconds -eq 0) { [datetime]::MaxValue } else { $started.AddSeconds($Seconds) }
$runFolder = Join-Path $OutputRoot ('stage-cactus-' + $started.ToString('yyyyMMdd-HHmmss-fff'))
[void][System.IO.Directory]::CreateDirectory($runFolder)
$eventsPath = Join-Path $runFolder 'events.log'
$runLog = [System.IO.StreamWriter]::new($eventsPath, $true, [System.Text.UTF8Encoding]::new($false))
$runLog.AutoFlush = $true
$port = $null
$stream = $null
$session = 0
$sessionBytes = [long]0
$totalBytes = [long]0
$presentPort = ''
$lastScan = [datetime]::MinValue
$lastPnp = '<unknown>'
$pnpBlocked = $false
$lastStatus = $started
$buffer = New-Object byte[] 8192
$records = [System.Collections.Generic.List[object]]::new()

function Write-Event([string]$message) {
    $line = '{0:HH:mm:ss.fff} {1}' -f (Get-Date), $message
    $runLog.WriteLine($line)
    Write-Host $line
}
function Close-Session([string]$reason) {
    # PowerShell functions have their own assignment scope. Always clear
    # the actual script variables first, so cleanup cannot run twice on
    # an already disposed FileStream after a COM unplug.
    $oldPort = $script:port
    $oldStream = $script:stream
    $script:port = $null
    $script:stream = $null
    if ($null -ne $oldPort) {
        Write-Event ("CLOSE session={0} port={1} bytes={2} reason={3}" -f $script:session, $oldPort.PortName, $script:sessionBytes, $reason)
        try { $oldPort.Close() } catch {}
        try { $oldPort.Dispose() } catch {}
    }
    if ($null -ne $oldStream) {
        try { $oldStream.Flush() } catch {
            Write-Event ("WARN flush failed on close: " + $_.Exception.Message)
        }
        try { $oldStream.Dispose() } catch {}
    }
    $script:sessionBytes = [long]0
}

try {
    Write-Event "START path=$runFolder seconds=$Seconds target=$target"
    while ((Get-Date) -lt $deadline) {
        $now = Get-Date
        if (($now - $lastScan).TotalMilliseconds -ge [Math]::Max(300,$PollMilliseconds)) {
            $lastScan = $now
            $devices = @()
            if (-not $pnpBlocked) {
                try {
                    $devices = @(Get-PnpDevice -PresentOnly -ErrorAction Stop |
                        Where-Object { $_.InstanceId -match $target })
                } catch {
                    $pnpBlocked = $true
                    Write-Event ("PnP unavailable; falling back to serial port discovery: " + $_.Exception.Message)
                }
            }
            $names = @($devices | Where-Object {
                $_.Class -eq 'Ports' -or $_.FriendlyName -match '\(COM\d+\)'
            } | ForEach-Object {
                $m = [regex]::Match([string]$_.FriendlyName, 'COM\d+')
                if ($m.Success) { $m.Value }
            } | Select-Object -Unique)
            if ($pnpBlocked) {
                $names = @([System.IO.Ports.SerialPort]::GetPortNames())
            }
            if ($PortName) { $names = @($names | Where-Object { $_ -eq $PortName }) }
            $presentPort = if ($names.Count -gt 0) { [string]$names[0] } else { '' }
            $pnp = if ($pnpBlocked) {
                if ($presentPort) { "SERIAL_PRESENT $presentPort (PnP blocked)" } else { 'SERIAL_ABSENT (PnP blocked)' }
            } elseif ($devices.Count) {
                (($devices | Sort-Object InstanceId | ForEach-Object {
                    "$($_.Status):$($_.FriendlyName)"
                }) -join '; ')
            } else { 'ABSENT' }
            if ($pnp -ne $lastPnp) {
                Write-Event "PNP $pnp"
                $lastPnp = $pnp
            }
            if ($null -ne $port -and ($presentPort -ne $port.PortName)) {
                Close-Session 'PnP port disappeared/changed'
            }
        }

        if ($null -eq $port -and $presentPort) {
            $candidate = $null
            try {
                $candidate = [System.IO.Ports.SerialPort]::new($presentPort,115200)
                $candidate.ReadTimeout = 100
                $candidate.DtrEnable = $true
                $candidate.RtsEnable = $true
                $candidate.Open()
                $session++
                $sessionBytes = [long]0
                $rawPath = Join-Path $runFolder ('session-{0:D3}-{1}.bin' -f $session,(Get-Date).ToString('HHmmss-fff'))
                $stream = [System.IO.File]::Open($rawPath,[System.IO.FileMode]::CreateNew,
                    [System.IO.FileAccess]::Write,[System.IO.FileShare]::Read)
                $port = $candidate
                $records.Add($rawPath)
                Write-Event "OPEN session=$session port=$presentPort file=$rawPath DTR=1 RTS=1"
            } catch {
                if ($candidate) { try { $candidate.Close() } catch {}; $candidate.Dispose() }
                if ($stream) { $stream.Dispose(); $stream=$null }
                Start-Sleep -Milliseconds 100
            }
        }
        if ($null -ne $port) {
            try {
                $available = $port.BytesToRead
                while ($available -gt 0) {
                    $n = $port.Read($buffer,0,[Math]::Min($available,$buffer.Length))
                    if ($n -le 0) { break }
                    $stream.Write($buffer,0,$n)
                    $stream.Flush()
                    $sessionBytes += $n
                    $totalBytes += $n
                    $available = $port.BytesToRead
                }
            } catch {
                Close-Session ('serial error: ' + $_.Exception.Message)
                $presentPort = ''
            }
        }
        if (($now - $lastStatus).TotalSeconds -ge 10) {
            Write-Event "STATUS sessions=$session total_bytes=$totalBytes connected=$([bool]$port)"
            $lastStatus = $now
        }
        Start-Sleep -Milliseconds $PollMilliseconds
    }
} finally {
    Close-Session 'capture ended'
    Write-Event "END sessions=$session bytes=$totalBytes path=$runFolder"
    $runLog.Dispose()
    if (-not $SkipDecode) {
        $decoder = Join-Path $PSScriptRoot 'decode-cactus-usb.py'
        foreach ($raw in $records) {
            if ((Get-Item -LiteralPath $raw).Length -eq 0) { continue }
            Write-Host "DECODE $raw"
            & python $decoder $raw --show 40
        }
    }
}
