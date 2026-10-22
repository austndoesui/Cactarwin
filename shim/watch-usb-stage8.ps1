# Start before a Stage 8 hardware test. Poll Windows PnP during the
# shim's short CDC-ACM enumeration window; print device state and COM port.
param([int]$Seconds = 20)

$deadline = (Get-Date).AddSeconds($Seconds)
$previous = ''
while ((Get-Date) -lt $deadline) {
    $matchesNow = @(
        Get-PnpDevice -PresentOnly -ErrorAction SilentlyContinue |
            Where-Object { $_.InstanceId -match 'VID_1209&PID_0001' } |
            Select-Object Status, Class, FriendlyName, InstanceId
    )
    $snapshot = $matchesNow | ConvertTo-Json -Compress
    if (-not $snapshot) { $snapshot = 'Not detected' }
    if ($snapshot -ne $previous) {
        Write-Host ("[{0:HH:mm:ss.fff}] {1}" -f (Get-Date), $snapshot)
        $previous = $snapshot
    }
    Start-Sleep -Milliseconds 400
}
