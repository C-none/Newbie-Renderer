[CmdletBinding()]
param(
    [switch]$Check,
    [string]$ClangFormat = $env:NR_CLANG_FORMAT
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Resolve-ClangFormat
{
    param([string]$RequestedFormatter)

    $candidates = [System.Collections.Generic.List[string]]::new()
    if (-not [string]::IsNullOrWhiteSpace($RequestedFormatter))
    {
        $candidates.Add($RequestedFormatter)
    }

    $pathFormatter = Get-Command clang-format -ErrorAction SilentlyContinue
    if ($null -ne $pathFormatter)
    {
        $candidates.Add($pathFormatter.Source)
    }

    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path -LiteralPath $vswhere)
    {
        $installations = & $vswhere -all -products * -property installationPath
        foreach ($installation in $installations)
        {
            $candidates.Add("$installation\VC\Tools\Llvm\x64\bin\clang-format.exe")
        }
    }

    $candidates.Add("$env:ProgramFiles\LLVM\bin\clang-format.exe")

    foreach ($candidate in $candidates | Select-Object -Unique)
    {
        $resolvedCommand = Get-Command $candidate -ErrorAction SilentlyContinue
        if ($null -eq $resolvedCommand)
        {
            continue
        }

        $versionOutput = & $resolvedCommand.Source --version 2>&1
        if ($LASTEXITCODE -eq 0 -and "$versionOutput" -match '^clang-format version 22\.')
        {
            return $resolvedCommand.Source
        }
    }

    throw "clang-format 22 was not found. Install LLVM 22 or pass -ClangFormat <path>."
}

function Invoke-SlangFormat
{
    param(
        [string]$Formatter,
        [string]$RepositoryRoot,
        [string]$RelativePath,
        [bool]$VerifyOnly
    )

    $absolutePath = Join-Path $RepositoryRoot $RelativePath
    $source = [System.IO.File]::ReadAllText($absolutePath)
    $utf8WithoutBom = [System.Text.UTF8Encoding]::new($false)

    $startInfo = [System.Diagnostics.ProcessStartInfo]::new()
    $startInfo.FileName = $Formatter
    $startInfo.UseShellExecute = $false
    $startInfo.RedirectStandardInput = $true
    $startInfo.RedirectStandardOutput = $true
    $startInfo.RedirectStandardError = $true
    $startInfo.StandardOutputEncoding = $utf8WithoutBom
    $startInfo.StandardErrorEncoding = $utf8WithoutBom
    # Slang's declaration modifiers are parsed correctly by clang-format's C#
    # frontend. C++ mode treats `public` as an access label and splits valid
    # shader declarations across lines.
    # ProcessStartInfo.ArgumentList and StandardInputEncoding are unavailable in
    # Windows PowerShell 5.1. Arguments plus raw UTF-8 stdin work in both 5.1
    # and PowerShell 7.
    $startInfo.Arguments = "--style=file --fallback-style=none `"--assume-filename=$absolutePath.cs`""

    $process = [System.Diagnostics.Process]::new()
    $process.StartInfo = $startInfo
    if (-not $process.Start())
    {
        throw "Could not start clang-format for '$RelativePath'."
    }

    $outputTask = $process.StandardOutput.ReadToEndAsync()
    $errorTask = $process.StandardError.ReadToEndAsync()
    $inputBytes = $utf8WithoutBom.GetBytes($source)
    $process.StandardInput.BaseStream.Write($inputBytes, 0, $inputBytes.Length)
    $process.StandardInput.BaseStream.Close()
    $process.WaitForExit()
    $formatted = $outputTask.GetAwaiter().GetResult()
    $diagnostics = $errorTask.GetAwaiter().GetResult()
    if ($process.ExitCode -ne 0)
    {
        throw "clang-format failed for '$RelativePath': $diagnostics"
    }

    if ($VerifyOnly)
    {
        return $source -ceq $formatted
    }

    $changed = $source -cne $formatted
    if ($changed)
    {
        [System.IO.File]::WriteAllText($absolutePath, $formatted, $utf8WithoutBom)
    }
    return $changed
}

$repositoryRoot = (& git -C $PSScriptRoot rev-parse --show-toplevel).Trim()
if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($repositoryRoot))
{
    throw "Could not resolve the repository root from '$PSScriptRoot'."
}

$formatter = Resolve-ClangFormat -RequestedFormatter $ClangFormat
$repositoryFiles = & git -C $repositoryRoot ls-files --cached --others --exclude-standard
if ($LASTEXITCODE -ne 0)
{
    throw "Could not enumerate repository files."
}

$sourceExtension = '\.(c|cc|cpp|cxx|h|hh|hpp|hxx|ixx|slang)$'
$thirdPartySource = '^(src/extern/(Aftermath|NsightGraphics|DLSS|slang)/|src/extern/shlobj_core\.h$|tools/ninjatracing/)'
$sourceFiles = @(
    $repositoryFiles |
        Where-Object { [System.IO.File]::Exists((Join-Path $repositoryRoot $_)) } |
        Where-Object { $_ -match $sourceExtension -and $_ -notmatch $thirdPartySource } |
        Sort-Object -Unique
)
if ($sourceFiles.Count -eq 0)
{
    throw "No project C++ or Slang source files were found."
}

$cppFiles = @($sourceFiles | Where-Object { $_ -notmatch '\.slang$' })
$slangFiles = @($sourceFiles | Where-Object { $_ -match '\.slang$' })

$checkArguments = @('--style=file', '--fallback-style=none', '--dry-run', '--Werror')
$writeArguments = @('--style=file', '--fallback-style=none', '-i')
$formattedCppCount = 0
$formattedSlangCount = 0

Push-Location $repositoryRoot
try
{
    $batchSize = 40
    $unformattedCppFiles = [System.Collections.Generic.HashSet[string]]::new(
        [System.StringComparer]::OrdinalIgnoreCase)
    for ($offset = 0; $offset -lt $cppFiles.Count; $offset += $batchSize)
    {
        $lastIndex = [Math]::Min($offset + $batchSize - 1, $cppFiles.Count - 1)
        $batch = $cppFiles[$offset..$lastIndex]
        $diagnostics = @(& $formatter @checkArguments -- @batch 2>&1)
        $checkExitCode = $LASTEXITCODE
        if ($checkExitCode -eq 0)
        {
            continue
        }

        $batchFoundUnformattedFile = $false
        foreach ($diagnostic in $diagnostics)
        {
            if ("$diagnostic" -match '^(?<path>.+?):\d+:\d+:\s+(?:error|warning):\s+code should be clang-formatted')
            {
                $unformattedCppFiles.Add($Matches.path) > $null
                $batchFoundUnformattedFile = $true
            }
        }
        if (-not $batchFoundUnformattedFile)
        {
            throw "clang-format check failed for '$($batch[0])':`n$($diagnostics -join "`n")"
        }
    }

    if ($Check -and $unformattedCppFiles.Count -ne 0)
    {
        $orderedFiles = $unformattedCppFiles | Sort-Object
        throw "C++ files require formatting:`n$($orderedFiles -join "`n")"
    }

    if (-not $Check -and $unformattedCppFiles.Count -ne 0)
    {
        $formattedCppCount = $unformattedCppFiles.Count
        $filesToFormat = @($unformattedCppFiles | Sort-Object)
        for ($offset = 0; $offset -lt $filesToFormat.Count; $offset += $batchSize)
        {
            $lastIndex = [Math]::Min($offset + $batchSize - 1, $filesToFormat.Count - 1)
            $batch = $filesToFormat[$offset..$lastIndex]
            & $formatter @writeArguments -- @batch
            if ($LASTEXITCODE -ne 0)
            {
                throw "clang-format failed for source batch beginning with '$($batch[0])'."
            }
        }
    }

    $unformattedSlangFiles = [System.Collections.Generic.List[string]]::new()
    foreach ($slangFile in $slangFiles)
    {
        $slangFormatArguments = @{
            Formatter      = $formatter
            RepositoryRoot = $repositoryRoot
            RelativePath   = $slangFile
            VerifyOnly     = $Check
        }
        $formatResult = Invoke-SlangFormat @slangFormatArguments
        if ($Check -and -not $formatResult)
        {
            $unformattedSlangFiles.Add($slangFile)
        }
        elseif (-not $Check -and $formatResult)
        {
            ++$formattedSlangCount
        }
    }
    if ($unformattedSlangFiles.Count -ne 0)
    {
        throw "Slang files require formatting:`n$($unformattedSlangFiles -join "`n")"
    }
}
finally
{
    Pop-Location
}

if ($Check)
{
    Write-Host "clang-format 22 verified $($sourceFiles.Count) project C++ and Slang files."
}
else
{
    $formattedCount = $formattedCppCount + $formattedSlangCount
    Write-Host "clang-format 22 formatted $formattedCount changed files and verified $($sourceFiles.Count) total files."
}
