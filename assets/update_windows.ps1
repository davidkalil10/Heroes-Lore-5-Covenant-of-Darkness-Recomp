param([int]$GamePid, [string]$Package, [string]$Executable, [string]$DataRoot, [switch]$NoRestart)
$ErrorActionPreference = 'Stop'
$log = Join-Path (Split-Path -LiteralPath $Package) 'update-install.log'
$changed = [System.Collections.Generic.List[object]]::new()
try {
    $game = Get-Process -Id $GamePid -ErrorAction SilentlyContinue
    if ($game) { $game.WaitForExit(120000) | Out-Null; if (!$game.HasExited) { throw 'The game is still running.' } }
    $appRoot = [IO.Path]::GetFullPath((Split-Path -LiteralPath $Executable))
    $appPrefix = $appRoot.TrimEnd('\') + '\'
    $stage = Join-Path (Split-Path -LiteralPath $Package) ('payload-' + [guid]::NewGuid().ToString('N'))
    $backup = Join-Path (Split-Path -LiteralPath $Package) ('previous-' + [guid]::NewGuid().ToString('N'))
    [IO.Directory]::CreateDirectory($stage) | Out-Null
    [IO.Directory]::CreateDirectory($backup) | Out-Null
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $zip = [IO.Compression.ZipFile]::OpenRead($Package)
    $files = [System.Collections.Generic.List[string]]::new()
    $names = [System.Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    $total = 0L
    try {
        foreach ($entry in $zip.Entries) {
            $name = $entry.FullName.Replace('\','/')
            if ($name.EndsWith('/')) { continue }
            if ($name.StartsWith('/') -or $name.Contains(':') -or ($name.Split('/') | Where-Object { $_ -eq '..' -or $_ -eq '.' -or $_ -eq '' })) { throw 'Unsafe archive path.' }
            $allowed = $name -in @('hl5.exe','heroes_lore_5.exe','SDL2.dll','libgcc_s_seh-1.dll','libstdc++-6.dll','libwinpthread-1.dll','libHeroesLore5.so','data.vfs.mp3','dict.bin','data/dict.bin') -or $name.StartsWith('assets_override/') -or $name.StartsWith('assets/')
            if (!$allowed) { continue } # Never import saves, OAuth tokens or machine preferences from a release.
            $target = [IO.Path]::GetFullPath((Join-Path $appRoot $name))
            if (!$target.StartsWith($appPrefix,[StringComparison]::OrdinalIgnoreCase) -or !$names.Add($target)) { throw 'Invalid or duplicate archive target.' }
            if ((($entry.ExternalAttributes -shr 16) -band 0xF000) -eq 0xA000) { throw 'Archive symlinks are unsupported.' }
            $total += $entry.Length
            if ($total -gt 536870912 -or $files.Count -ge 20000) { throw 'Update archive exceeds limits.' }
            $output = Join-Path $stage $name
            [IO.Directory]::CreateDirectory((Split-Path -LiteralPath $output)) | Out-Null
            [IO.Compression.ZipFileExtensions]::ExtractToFile($entry,$output,$false)
            $files.Add($name)
        }
    } finally { $zip.Dispose() }
    $exeName = [IO.Path]::GetFileName($Executable)
    if (!$files.Contains($exeName)) { throw 'The running executable is missing from this release.' }
    foreach ($name in $files) {
        $target = Join-Path $appRoot $name
        $old = Join-Path $backup $name
        $new = Join-Path $stage $name
        [IO.Directory]::CreateDirectory((Split-Path -LiteralPath $target)) | Out-Null
        # Do not follow junctions or symlinks in an existing installation.
        $parent = Split-Path -LiteralPath $target
        while ($parent.StartsWith($appRoot,[StringComparison]::OrdinalIgnoreCase)) {
            if ((Get-Item -LiteralPath $parent).Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Installation contains a directory link.' }
            if ($parent -eq $appRoot) { break }
            $parent = Split-Path -LiteralPath $parent
        }
        if (Test-Path -LiteralPath $target) {
            if ((Get-Item -LiteralPath $target).Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Installation contains a file link.' }
            [IO.Directory]::CreateDirectory((Split-Path -LiteralPath $old)) | Out-Null
            Move-Item -LiteralPath $target -Destination $old
        }
        $record = [pscustomobject]@{Target=$target; Old=$old; Installed=$false}
        $changed.Add($record)
        Move-Item -LiteralPath $new -Destination $target
        $record.Installed = $true
    }
    'Update installed. Previous application files: ' + $backup | Set-Content -LiteralPath $log
    if (!$NoRestart) { Start-Process -FilePath $Executable -WorkingDirectory $appRoot -ArgumentList @('--data',('"'+$DataRoot+'"')) }
} catch {
    $failure = $_.Exception.Message
    for ($i=$changed.Count-1; $i -ge 0; $i--) {
        $record = $changed[$i]
        if ($record.Installed -and (Test-Path -LiteralPath $record.Target)) { Remove-Item -LiteralPath $record.Target }
        if (Test-Path -LiteralPath $record.Old) { Move-Item -LiteralPath $record.Old -Destination $record.Target }
    }
    ('Update failed; previous application restored: ' + $failure) | Set-Content -LiteralPath $log
    exit 1
}
