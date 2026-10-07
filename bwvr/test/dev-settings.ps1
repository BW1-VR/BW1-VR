# Switch Black & White between the player's normal settings and settings that
# make development practical: a small window you can alt-tab out of.
#
#   .\dev-settings.ps1 dev      windowed, 1280x720
#   .\dev-settings.ps1 restore  fullscreen, 2560x1440
#   .\dev-settings.ps1 show     print the current values
#
# A full backup of the original registry state is in backup\.

param(
    [Parameter(Position = 0)]
    [ValidateSet('dev', 'restore', 'show')]
    [string]$Mode = 'show'
)

$key = 'HKCU:\SOFTWARE\Lionhead Studios Ltd\Black & White\BWSetup'
$watched = 'ScreenW', 'ScreenH', 'ScreenD', 'FullScreen', 'WindowedMode', 'HardwareTnL'

function Show-Settings {
    Get-ItemProperty $key | Select-Object $watched | Format-List
}

switch ($Mode) {
    'dev' {
        Set-ItemProperty $key -Name ScreenW       -Value 1280 -Type DWord
        Set-ItemProperty $key -Name ScreenH       -Value 720  -Type DWord
        Set-ItemProperty $key -Name FullScreen    -Value 0    -Type DWord
        Set-ItemProperty $key -Name WindowedMode  -Value 1    -Type DWord
        Write-Host 'Development settings applied.'
        Show-Settings
    }
    'restore' {
        Set-ItemProperty $key -Name ScreenW      -Value 2560 -Type DWord
        Set-ItemProperty $key -Name ScreenH      -Value 1440 -Type DWord
        Set-ItemProperty $key -Name ScreenD      -Value 32   -Type DWord
        Set-ItemProperty $key -Name FullScreen   -Value 1    -Type DWord
        Set-ItemProperty $key -Name HardwareTnL  -Value 0    -Type DWord
        Remove-ItemProperty $key -Name WindowedMode -ErrorAction SilentlyContinue
        Write-Host 'Original settings restored.'
        Show-Settings
    }
    'show' {
        Show-Settings
    }
}
