[CmdletBinding()]
param(
    [string]$BuildPreset = "debug",
    [string]$Target = "nr_sync_clangd_database"
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $repoRoot

cmake --build --preset $BuildPreset --target $Target
