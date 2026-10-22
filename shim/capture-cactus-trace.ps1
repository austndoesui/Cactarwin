# Stage 14: continuously save whatever Windows receives from CDC ACM.
# Start before fastboot reboot. DTR must be raised for gadget bulk IN.
param(
    [int]$Seconds = 45,
    [string]$Output = "F:\ROM\patching\redmi 6a xnu\cactus-stage14-raw.bin",
    [int]$CyclePortAfterSeconds = 0
)
$ErrorActionPreference = 'Stop'
$target = 'VID_1209&PID_0001'
$start = Get-Date
$deadline = $start.AddSeconds($Seconds)
$port = $null
$reopenDone = $false
$buf = New-Object byte[] 4096
$file = [System.IO.File]::Open($Output, [System.IO.FileMode]::Create,
    [System.IO.FileAccess]::Write, [System.IO.FileShare]::Read)
$count = [long]0
try {
    Write-Host "Cactus binary USB receiver listening for $target"
    while ((Get-Date) -lt $deadline) {
        if ($port -and $CyclePortAfterSeconds -gt 0 -and
            -not $reopenDone -and
            (Get-Date) -ge $start.AddSeconds($CyclePortAfterSeconds)) {
            Write-Host "Deliberately closing COM once to test reconnect"
            try { $port.Close() } catch { Write-Host "COM already disconnected while reopening: $($_.Exception.Message)" }
            $port.Dispose()
            $port = $null
            $reopenDone = $true
        }
        if (-not $port) {
            $ports = @(Get-PnpDevice -PresentOnly -ErrorAction SilentlyContinue |
                Where-Object { $_.InstanceId -match $target -and
                    ($_.Class -eq 'Ports' -or $_.FriendlyName -match 'COM\d+') })
            foreach ($pnp in $ports) {
                $match = [regex]::Match([string]$pnp.FriendlyName, 'COM\d+')
                if (-not $match.Success) { continue }
                $candidate = $null
                try {
                    $candidate = [System.IO.Ports.SerialPort]::new($match.Value,115200)
                    $candidate.ReadTimeout = 100
                    $candidate.DtrEnable = $true
                    $candidate.RtsEnable = $true
                    $candidate.Open()
                    $port = $candidate
                    Write-Host "Opened $($match.Value) with DTR/RTS"
                    break
                } catch {
                    if ($candidate) { $candidate.Dispose() }
                }
            }
        }
        if ($port) {
            try {
                $available = $port.BytesToRead
                if ($available -gt 0) {
                    $n = $port.Read($buf,0,[Math]::Min($available,$buf.Length))
                    if ($n -gt 0) {
                        $file.Write($buf,0,$n)
                        $file.Flush()
                        $count += $n
                        Write-Host "captured $n bytes; total=$count"
                    }
                }
            } catch {
                Write-Host "Disconnected: $($_.Exception.Message)"
                try { $port.Close() } catch {}
                $port.Dispose()
                $port = $null
            }
        }
        Start-Sleep -Milliseconds 100
    }
} finally {
    if ($port) {
        try { $port.Close() } catch {
            Write-Host "COM detached during shutdown: $($_.Exception.Message)"
        }
        $port.Dispose()
    }
    $file.Flush()
    $file.Close()
    Write-Host "Saved $count received bytes to $Output"
}
# Decode separately: python .\decode-cactus-usb.py $Output --require-stage14
