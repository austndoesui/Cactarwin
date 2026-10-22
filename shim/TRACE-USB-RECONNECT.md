# Continuous USB capture for Stage 16

Run one command in Windows PowerShell:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File 'F:\ROM\patching\redmi 6a xnu\ios-cactus\shim\watch-cactus-usb.ps1' -Seconds 120
```

Run it before rebooting into Stage16, and leave it active through
several USB disconnect/reconnect cycles. With `-Seconds 0` it runs
until you stop it with Ctrl+C. The script does **not** flash the phone.

The output directory is `F:\ROM\patching\redmi 6a xnu\usb-captures\`.
Each invocation creates `stage-cactus-YYYYMMDD-HHMMSS-mmm` containing:

- `events.log` with timestamps for USB appearance/disappearance,
  serial-port opens and closes, byte counts, and errors.
- `session-001-HHMMSS-mmm.bin`, `session-002-...`, etc. Each is
  a separate COM25 attachment, so resetting the device cannot mix
  together different boot sessions.
- On normal exit, the Python CLOG decoder prints recovered records
  for every nonempty session. To decode any session manually:

```powershell
python 'F:\ROM\patching\redmi 6a xnu\ios-cactus\shim\decode-cactus-usb.py' 'PATH_TO_SESSION.bin' --show 80
```

Default COM selection is `COM25`; change with `-PortName COM26`.
Windows PnP discovery is preferred for VID_1209/PID_0001. If Windows
denies PnP/CIM access, the script falls back to the selected COM port
through SerialPort.GetPortNames(). That fallback depends on the COM
number staying assigned to the cactus device.

**2026-10-22 fix:** The first hardware captures exposed a
`Close-Session` scope error: the old file stream was disposed but its
script-level variable stayed non-null, causing repeated CLOSE events
and a fatal `Cannot access a closed file` on USB disconnect. The
recorder now clears the script-level objects before disposal and
repeated cleanup is safe. The two original captures remained intact:
both held 21 valid Stage16 records through the live FDT parse. Run
the fixed version to verify recording across the next COM25 return.
