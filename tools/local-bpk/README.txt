BPK local installation test

Extract this entire ZIP on the computer physically connected to the board.
Python 3 is required; pyserial is included, so pip installation is unnecessary.
Close the flash tool and serial monitors. Keep the board on its normal desktop.
Do not press BOOT. This tool does not flash firmware or erase data.

Open PowerShell in this extracted folder:

  powershell -ExecutionPolicy Bypass -File .\test.ps1 -Port COM5

Replace COM5 with the actual local board port. This checks HELLO, installs
resource-monitor, then lists installed packages. If already installed, it updates it.

Other commands:
  powershell -ExecutionPolicy Bypass -File .\test.ps1 -Port COM5 -Action hello
  powershell -ExecutionPolicy Bypass -File .\test.ps1 -Port COM5 -Package calculator

Included packages: resource-monitor, api-test, service-test, music, alarm,
countdown, stopwatch, calculator, counter.

Send the generated test-YYYYMMDD-HHMMSS.log if installation fails.
An END rejection with Device diagnostics is a device validation error;
a GetOverlappedResult error is a Windows serial transport error.

For this test, use a local terminal on the computer connected to the board,
not a terminal inside the remote desktop. If COM5 is still reserved by RDP,
disable Ports redirection and disconnect/reconnect the remote desktop first.
