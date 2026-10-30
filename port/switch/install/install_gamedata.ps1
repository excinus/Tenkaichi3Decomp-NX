# Tenkaichi3Decomp for the Nintendo Switch: copies the game data of the PC version onto the SD card
# (docs/port/switch_plan.md, "The packs"). Started by "Install game data (Windows).bat" at the card's root (it is in
# bt3-install\ there), after the release's SD zip was unpacked onto the card; Windows PowerShell 5.1 or later.
#
#   1. asks for the PC installation of Tenkaichi3Decomp (the folder its setup made from your disc, with gamedata in it);
#   2. copies its gamedata into atmosphere\contents\0100B73DEC000000\gamedata on this card, with robocopy (files
#      already there and the same are skipped, so it can be run again);
#   3. writes each folder of 10,000 files or more (pzs3us2, the voices: about 65,000) as ONE file beside it, <folder>.afs,
#      instead of copying it: the Switch takes about 0.4 s to open a file in such a folder. The same pack as
#      pack_gamedata.py's (an AFS archive: "AFS\0", the count, offset and size of each file, the files at 2048-byte
#      steps), checked against the folder (every size, every 250th file's bytes) before it takes its name.
# The PC installation is only read. On the card, an unpacked copy of a packed folder (an older install) is removed.
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Windows.Forms
[System.Windows.Forms.Application]::EnableVisualStyles()

$Title = 'Tenkaichi3Decomp - game data'
function Say($text, $icon) {
    [void][System.Windows.Forms.MessageBox]::Show($text, $Title, 'OK', $icon)
}
function Fail($text) {
    Say $text 'Error'
    exit 1
}
trap { Fail ("Something went wrong, nothing on your PC was changed:`n`n$_") }

# ---- where: the card (this script's), and the PC installation ----
$card = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$game = Join-Path $card 'atmosphere\contents\0100B73DEC000000'
if (-not (Test-Path (Join-Path $game 'Tenkaichi3Decomp'))) {
    Fail ("The game's folder is not next to this file:`n$game`n`n" +
          "Unpack the whole Tenkaichi3Decomp-switch-SD.zip to the root of the SD card, then start " +
          "'Install game data (Windows).bat' from the card.")
}

$pick = New-Object System.Windows.Forms.FolderBrowserDialog
$pick.Description = 'Choose your PC installation of Tenkaichi3Decomp (the folder with "gamedata" in it).'
$pick.ShowNewFolderButton = $false
if ($pick.ShowDialog() -ne 'OK') { exit 0 }
$src = $pick.SelectedPath
if ((Split-Path -Leaf $src) -ne 'gamedata' -and (Test-Path (Join-Path $src 'gamedata'))) { $src = Join-Path $src 'gamedata' }
if (-not (Test-Path (Join-Path $src 'disc\SLUS_216.78'))) {
    Fail ("No game data in:`n$($pick.SelectedPath)`n`n" +
          "Choose the folder of your PC installation of Tenkaichi3Decomp: it has a 'gamedata' folder with " +
          "'disc\SLUS_216.78' in it (the PC setup makes it from your disc).")
}
$dst = Join-Path $game 'gamedata'

# ---- what goes how: the large numbered folders as packs, the rest copied ----
$packs = @()
foreach ($d in @(Get-ChildItem -LiteralPath $src -Directory | Where-Object { $_.Name -like 'pzs3*' })) {
    $files = @{}
    foreach ($f in $d.EnumerateFiles('*.bin')) {
        if ($f.BaseName -match '^\d{1,6}$') { $files[[int]$f.BaseName] = $f }
    }
    if ($files.Count -ge 10000) {
        $packs += [pscustomobject]@{ Name = $d.Name; Dir = $d.FullName; Files = $files; Order = @($files.Keys | Sort-Object) }
    }
}
$need = (Get-ChildItem -LiteralPath $src -Recurse -File | Measure-Object -Property Length -Sum).Sum
$drive = Get-PSDrive -Name ($card.Substring(0, 1)) -ErrorAction SilentlyContinue
if ($drive -and -not (Test-Path (Join-Path $game 'gamedata\disc')) -and $drive.Free -lt $need * 0.9) {
    Fail ("The SD card has {0:N1} GB free; the game data takes about {1:N1} GB." -f ($drive.Free / 1GB), ($need / 1GB))
}

Write-Host "Tenkaichi3Decomp: copying the game data"
Write-Host "  from $src"
Write-Host "  to   $dst"
Write-Host ''

# ---- 1. everything but the packed folders ----
$exclude = @($packs | ForEach-Object { $_.Dir })
$copy = @($src, $dst, '/E', '/R:1', '/W:1', '/NP', '/NDL', '/NJH', '/NJS')
if ($exclude.Count -gt 0) { $copy += '/XD'; $copy += $exclude }
Write-Host 'Copying the files (several minutes; files already on the card are skipped)...'
& robocopy @copy
if ($LASTEXITCODE -ge 8) { Fail "Copying failed (robocopy error $LASTEXITCODE). Is the card full or write-protected?" }

# ---- 2. the packs ----
$Align = 2048
foreach ($p in $packs) {
    $dest = Join-Path $dst ($p.Name + '.afs')
    $count = ($p.Files.Keys | Measure-Object -Maximum).Maximum + 1
    $head = [long][math]::Ceiling((8 + $count * 8) / $Align) * $Align
    # (the same pack already there: skipped)
    $expect = $head
    foreach ($i in $p.Order) { $expect += [long][math]::Ceiling($p.Files[$i].Length / $Align) * $Align }
    if ((Test-Path $dest) -and (Get-Item $dest).Length -eq $expect) {
        Write-Host "$($p.Name): already packed on the card"
    } else {
        Write-Host ("$($p.Name): {0:N0} files into one, $($p.Name).afs (a few minutes)" -f $p.Files.Count)
        $table = New-Object 'long[]' ($count * 2)
        $part = "$dest.part"
        $out = [System.IO.File]::Open($part, 'Create', 'Write')
        try {
            $at = $head
            $done = 0
            foreach ($i in $p.Order) {
                $data = [System.IO.File]::ReadAllBytes($p.Files[$i].FullName)
                if ($at + $data.Length -ge 4GB) { throw "$($p.Name) is over 4 GB, more than one pack holds" }
                [void]$out.Seek($at, 'Begin')
                $out.Write($data, 0, $data.Length)
                $table[$i * 2] = $at
                $table[$i * 2 + 1] = $data.Length
                $at = [long][math]::Ceiling(($at + $data.Length) / $Align) * $Align
                $done++
                if ($done % 500 -eq 0) {
                    Write-Progress -Activity "Packing $($p.Name)" -Status ("{0:N0} of {1:N0} files" -f $done, $p.Files.Count) `
                                   -PercentComplete ($done * 100 / $p.Files.Count)
                }
            }
            $out.SetLength($at)
            [void]$out.Seek(0, 'Begin')
            $w = New-Object System.IO.BinaryWriter($out)
            $w.Write([byte[]](0x41, 0x46, 0x53, 0))
            $w.Write([uint32]$count)
            for ($k = 0; $k -lt $count * 2; $k++) { $w.Write([uint32]$table[$k]) }
            $w.Flush()
        } finally {
            $out.Close()
        }
        Write-Progress -Activity "Packing $($p.Name)" -Completed
        # the check: every file's size, every 250th file's bytes
        $in = [System.IO.File]::OpenRead($part)
        try {
            $n = 0
            foreach ($i in $p.Order) {
                $len = $p.Files[$i].Length
                if ($table[$i * 2] -eq 0 -or $table[$i * 2 + 1] -ne $len) { throw "$($p.Files[$i].FullName) is not in the pack as it should be" }
                if ($n % 250 -eq 0) {
                    $want = [System.IO.File]::ReadAllBytes($p.Files[$i].FullName)
                    $got = New-Object byte[] $len
                    [void]$in.Seek($table[$i * 2], 'Begin')
                    [void]$in.Read($got, 0, $len)
                    if ([Convert]::ToBase64String($got) -ne [Convert]::ToBase64String($want)) { throw "$($p.Files[$i].FullName) differs in the pack" }
                }
                $n++
            }
        } finally {
            $in.Close()
        }
        Move-Item -LiteralPath $part -Destination $dest -Force
        Write-Host '  done, checked'
    }
    # an unpacked copy on the card (an older install) is not needed beside its pack
    $old = Join-Path $dst $p.Name
    if (Test-Path -LiteralPath $old) {
        Write-Host "  removing the card's unpacked $($p.Name) folder (the pack has its files)"
        Remove-Item -LiteralPath $old -Recurse -Force
    }
}

Say ("The game data is on the SD card.`n`nPut the card back in the Switch, open the homebrew menu through a game " +
     "(hold R while starting any game) and start Tenkaichi3Decomp.") 'Information'
