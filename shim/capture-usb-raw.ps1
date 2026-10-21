# Start this script BEFORE rebooting an authorized Stage 12 image.
# Records all USB VID/PID changes and captures binary bytes from its COM port.
# No pip/driver dependency. Requires Windows PowerShell 5.1+.
param(
    [int]$Seconds = 30,
    [string]$Output = "F:\ROM\patching\redmi 6a xnu\cactus-usb-raw.bin"
)

$ErrorActionPreference = 'Stop'
$target = 'VID_1209&PID_0001'
$deadline = (Get-Date).AddSeconds($Seconds)
$start = Get-Date
$lastDevices = ''
$lastUnknown = ''
$port = $null
$bytes = [System.Collections.Generic.List[byte]]::new()
$buf = New-Object byte[] 2048

try {
    Write-Host "Listening for temporary USB test device $target for $Seconds seconds..."
    while ((Get-Date) -lt $deadline) {
        $all = @(Get-PnpDevice -PresentOnly -ErrorAction SilentlyContinue |
            Where-Object { $_.InstanceId -match '^(USB|USBSTOR)\\' -or
                $_.InstanceId -match $target })
        $matched = @($all | Where-Object { $_.InstanceId -match $target })
        $ids = ($matched | ForEach-Object { "$($_.Status):$($_.Class):$($_.InstanceId)" }) -join ';'
        if ($ids -ne $lastDevices) {
            $when = (Get-Date).ToString('HH:mm:ss.fff')
            if ($ids) {
                Write-Host "[$when] Test USB detected: $ids"
            } else {
                Write-Host "[$when] Test VID:PID not enumerated"
            }
            $lastDevices = $ids
        }
        # Windows can report a failed descriptor request as an Unknown USB
        # Device, without ever receiving our test VID/PID.
        $unknown = @($all | Where-Object {
            $_.FriendlyName -like '*Unknown USB*' -or
            ($_.Status -ne 'OK' -and $_.InstanceId -notmatch $target)
        })
        $unknownSummary = ($unknown | ForEach-Object {
            "$($_.Status):$($_.FriendlyName):$($_.InstanceId)"
        }) -join ';'
        if ($unknownSummary -ne $lastUnknown) {
            if ($unknownSummary) {
                Write-Host "[$((Get-Date).ToString('HH:mm:ss.fff'))] USB errors: $unknownSummary"
            }
            $lastUnknown = $unknownSummary
        }
        if (-not $port) {
            $ports = @($matched | Where-Object { $_.Class -eq 'Ports' -or
                $_.FriendlyName -match '\(COM\d+\)' })
            foreach ($pnp in $ports) {
                $hit = [regex]::Match([string]$pnp.FriendlyName,'COM\d+')
                if (-not $hit.Success) { continue }
                $name = $hit.Value
                try {
                    $port = [System.IO.Ports.SerialPort]::new($name,115200)
                    $port.ReadTimeout = 100
                    $port.DtrEnable = $true
                    $port.RtsEnable = $true
                    $port.Open()
                    Write-Host "[$((Get-Date).ToString('HH:mm:ss.fff'))] Opened $name (binary capture)"
                    break
                } catch {
                    Write-Host "Could not open $name yet: $($_.Exception.Message)"
                    if ($port) { $port.Dispose(); $port = $null }
                }
            }
        }
        if ($port) {
            try {
                $available = $port.BytesToRead
                if ($available -gt 0) {
                    $read = $port.Read($buf,0,[Math]::Min($buf.Length,$available))
                    for ($i = 0; $i -lt $read; $i++) { $bytes.Add($buf[$i]) }
                    Write-Host "[$((Get-Date).ToString('HH:mm:ss.fff'))] Received $read raw bytes; total $($bytes.Count)"
                }
            } catch {
                Write-Host "Serial device disconnected: $($_.Exception.Message)"
                $port.Dispose()
                $port = $null
            }
        }
        Start-Sleep -Milliseconds 200
    }
} finally {
    if ($port) { $port.Close(); $port.Dispose() }
    [System.IO.File]::WriteAllBytes($Output,$bytes.ToArray())
    Write-Host "Saved $($bytes.Count) bytes in $Output"
}

$expected = [byte[]]@(
    0x43,0x4c,0x4f,0x47,0x01,0x00,0x00,0x00,
    0x01,0x00,0x00,0x00,0x00,0xff,0x7f,0x80,
    0x55,0xaa,0x0d,0x0a,0x10,0x20,0x30,0x40,
    0x50,0x60,0x70,0x90,0xa0,0xb0,0xc0,0xd0
)
$matchedFrame = $false
for ($i = 0; $i -le $bytes.Count - $expected.Length; $i++) {
    $equal = $true
    for ($j = 0; $j -lt $expected.Length; $j++) {
        if ($bytes[$i+$j] -ne $expected[$j]) { $equal = $false; break }
    }
    if ($equal) { $matchedFrame = $true; break }
}
if ($matchedFrame) {
    Write-Host 'PASS: exact 32-byte CLOG binary marker transferred over USB'
} else {
    Write-Host 'NO VERIFIED RAW FRAME: inspect USB detection and Stage 12 CTOK trace'
}
