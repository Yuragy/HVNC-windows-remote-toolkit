## Structure
	•	server – the server-side component
	•	client – the client-side component

Each new connection is opened in a separate console.

## Supported commands
	•	CMD_START_EXPLORER – launches Windows Explorer (explorer.exe)
	•	CMD_START_RUN – opens the Windows Command Prompt (cmd.exe)
	•	CMD_START_CHROME – launches Google Chrome (chrome.exe)
	•	CMD_START_EDGE – launches Microsoft Edge (msedge.exe)
	•	CMD_START_BRAVE – launches Brave (brave.exe)
	•	CMD_START_FIREFOX – launches Mozilla Firefox (firefox.exe)
	•	CMD_START_IEXPL – launches Internet Explorer (iexplore.exe)
	•	CMD_START_POWERSHELL – opens Windows PowerShell (powershell.exe)
	•	CMD_SHELL_OPEN – initiates a remote interactive shell session
	•	CMD_SHELL_COMMAND – sends a command to an already-open remote shell session
	•	CMD_FILE_LIST – requests a list of files and folders in a specified directory
	•	CMD_FILE_DOWNLOAD – requests download of a specified file from the remote machine
	•	CMD_FILE_UPLOAD – requests upload of a file to the remote machine
	•	CMD_KEYLOGGER_START – starts capturing and logging keystrokes
	•	CMD_KEYLOGGER_STOP – stops capturing and logging keystrokes

## Start
	•	When launched, the server will ask you to specify a listening port—use the same port configured in the client.
	•	In the client’s main.cpp, edit the host and port values for the connection.
	•	The client executable is copied to %LOCALAPPDATA%\Microsoft\Win32Components and set to auto-start according to privileges (using WMI for Administrator, registry for standard user).

## Test
	•	Use client/clean.ps1 to remove all artifacts created by the client (the copied build, its folder, and any auto-start triggers).

WMI check and cleanup:
# Cleanup:

-  Get-WmiObject -Namespace root\subscription -Class __EventFilter -Filter "Name='Microsoft_Win32Filter'" |
  ForEach-Object { $_.Delete() }
-  Get-WmiObject -Namespace root\subscription -Class CommandLineEventConsumer -Filter "Name='Microsoft_Win32Consumer'" |
  ForEach-Object { $_.Delete() }
-  Get-WmiObject -Namespace root\subscription -Class __FilterToConsumerBinding |
  Where-Object { $_.Consumer -match 'Microsoft_Win32Consumer' } |
#**This is a training material demonstrating capabilities, the responsibility to use it lies with you.** 
  ForEach-Object { $_.Delete() }

# Verification:
- Get-WmiObject -Namespace root\subscription -Class __FilterToConsumerBinding |
  Where-Object { $_.Consumer -match 'Microsoft_Win32Consumer' } |
  Format-List Filter, Consumer
