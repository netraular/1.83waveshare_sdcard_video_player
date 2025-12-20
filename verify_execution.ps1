# Configuration
$intervalSeconds = 20

# We set the last message time to the past so it triggers immediately on the first loop
$lastMessage = (Get-Date).AddSeconds(-$intervalSeconds)

# Infinite loop
while ($true) {
    # 1. Check time for the waiting message
    $currentTime = Get-Date
    if (($currentTime - $lastMessage).TotalSeconds -ge $intervalSeconds) {
        Write-Host "[$(Get-Date -Format 'HH:mm:ss')] Testing, please keep waiting..." -ForegroundColor DarkGray
        $lastMessage = $currentTime
    }

    # 2. Check if user pressed a key to interrupt
    if ([Console]::KeyAvailable) {
        # Clear the key from buffer
        $null = [Console]::ReadKey($true)

        # Interaction prompts in English
        Write-Host "`n>>> PAUSED. User is writing feedback:" -ForegroundColor Yellow

        $inputLines = @()
        while ($true) {
            $line = Read-Host
            if ($line -eq '[END]') { break }
            $inputLines += $line
        }
        $userInput = $inputLines -join "`n"

        if (-not [string]::IsNullOrWhiteSpace($userInput)) {
            # Feedback received
        } else {
            Write-Host "Continuing..." -ForegroundColor Gray
        }

        # Reset timer so the testing message doesn't appear immediately after feedback
        $lastMessage = Get-Date
    }

    # Prevent high CPU usage
    Start-Sleep -Milliseconds 100
}
