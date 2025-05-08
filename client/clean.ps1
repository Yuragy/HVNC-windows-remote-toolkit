try {
    Remove-ItemProperty -Path "HKCU:\Software\Microsoft\Windows\CurrentVersion\Run" -Name "MicrosoftUpdate" -ErrorAction Stop
    Write-Output "Registry entry 'MicrosoftUpdate' removed successfully."
} catch {
    Write-Output "Registry entry 'MicrosoftUpdate' not found or could not be removed: $_"
}
try {
    $filters = Get-WmiObject -Namespace root\subscription -Class __EventFilter -Filter "Name='Microsoft_Win32Filter'" -ErrorAction Stop
    foreach ($filter in $filters) {
        $filter.Delete() | Out-Null
        Write-Output "WMI __EventFilter 'Microsoft_Win32Filter' removed successfully."
    }
} catch {
    Write-Output "Failed to remove WMI __EventFilter 'Microsoft_Win32Filter': $_"
}
try {
    $consumers = Get-WmiObject -Namespace root\subscription -Class CommandLineEventConsumer -Filter "Name='Microsoft_Win32Consumer'" -ErrorAction Stop
    foreach ($consumer in $consumers) {
        $consumer.Delete() | Out-Null
        Write-Output "WMI CommandLineEventConsumer 'Microsoft_Win32Consumer' removed successfully."
    }
} catch {
    Write-Output "Failed to remove WMI CommandLineEventConsumer 'Microsoft_Win32Consumer': $_"
}
try {
    $bindings = Get-WmiObject -Namespace root\subscription -Class __FilterToConsumerBinding -ErrorAction Stop
    $removedBinding = $false
    foreach ($binding in $bindings) {
        if ($binding.Consumer -match 'Microsoft_Win32Consumer') {
            $binding.Delete() | Out-Null
            Write-Output "WMI __FilterToConsumerBinding for 'Microsoft_Win32Consumer' removed successfully."
            $removedBinding = $true
        }
    }
    if (-not $removedBinding) {
        Write-Output "No WMI __FilterToConsumerBinding related to 'Microsoft_Win32Consumer' was found."
    }
} catch {
    Write-Output "Failed to remove WMI __FilterToConsumerBinding objects: $_"
}
$hiddenDir = "$env:LOCALAPPDATA\Microsoft\Win32Components"
if (Test-Path $hiddenDir) {
    try {
        Remove-Item -Path $hiddenDir -Recurse -Force -ErrorAction Stop
        Write-Output "Hidden directory '$hiddenDir' removed successfully."
    } catch {
        Write-Output "Failed to remove hidden directory '$hiddenDir': $_"
    }
} else {
    Write-Output "Hidden directory '$hiddenDir' not found."
}

Write-Output "Cleanup completed. All client objects have been removed."
