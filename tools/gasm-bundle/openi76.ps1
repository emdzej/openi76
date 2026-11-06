# OpenI76 @VERSION@ (openi76.wasm) on the bundled gasm-run @GASM_VERSION@: Windows launcher, started by OpenI76.cmd.
#   OpenI76.cmd [options] [DATA] [gasm-run options...]          (OpenI76.cmd --help)
# The game data location comes from the argument (then saved), else %APPDATA%\OpenI76\data-location, else a
# folder picker. gasm-run's output goes to the console and to %LOCALAPPDATA%\OpenI76\gasm.log. Test hooks (no
# dialogs): OPENI76_GAME=<folder|image> uses that data without saving it (and turns missing data into exit 2),
# OPENI76_DRY_RUN=1 or --dry-run prints the gasm-run command instead of running it.
# Strings that hold the game's name are double-quoted (it has an apostrophe).
$ErrorActionPreference = 'Stop'
$Here = Split-Path -Parent $MyInvocation.MyCommand.Path
$ConfDir = Join-Path $env:APPDATA 'OpenI76'
$LocFile = Join-Path $ConfDir 'data-location'
$LogDir = Join-Path $(if ($env:LOCALAPPDATA) { $env:LOCALAPPDATA } else { $env:APPDATA }) 'OpenI76'
$Log = Join-Path $LogDir 'gasm.log'
$Game = "Interstate '76"
$Title = "$Game (gasm)"
$ExeSize = 1019392   # I76.EXE 1.0 (CRC-32 4129421d; the game checks the CRC)
$Dry = $env:OPENI76_DRY_RUN -eq '1'
$Change = $false

function Show-Usage {
  @"
OpenI76 @VERSION@ on gasm-run @GASM_VERSION@

  OpenI76.cmd [options] [DATA] [gasm-run options...]

DATA is disc 1 of your $Game CD: an image of it (I76_CD1.ISO, or a raw .bin), or a folder
with its files (I76.EXE 1.0, I76.ZFS), e.g. the CD drive, or an installed game's folder. It is saved in
  $LocFile
so later runs need no argument. Without one, a dialog asks for it.

Options:
  --change-data  ask for the game data even if a location is saved
  --forget-data  delete the saved location and exit
  --dry-run      print the gasm-run command instead of running it (also OPENI76_DRY_RUN=1)
  --help         this text
Anything after DATA goes to gasm-run, e.g. --window 1280x960, --fullscreen, --mute.
OPENI76_GAME=<DATA> uses that data for one run without saving it.

Log: $Log
More: https://openi76.emdzej.pl/guide/game-data
"@
}

# $null if usable, else the reason (Windows paths are case-insensitive)
function Test-Data([string]$p) {
  if (Test-Path -LiteralPath $p -PathType Leaf) {
    switch ([IO.Path]::GetExtension($p).ToLowerInvariant()) {
      { $_ -in '.iso', '.bin' } { return $null }   # the game itself checks what is inside (exit 1 and its message)
      '.cue' { return "A .cue sheet only names the image: choose the .bin file next to it.`n$p" }
      { $_ -in '.mdf', '.nrg' } { return "OpenI76 reads .iso and raw .bin images only: convert it, or choose a folder with the files of disc 1.`n$p" }
    }
    return "This is a file: choose the image of disc 1 (.iso) or a folder with its files.`n$p"
  }
  if (-not (Test-Path -LiteralPath $p -PathType Container)) {
    return "The game data was not found at:`n$p`nChoose it again."
  }
  $exe = Join-Path $p 'I76.EXE'
  if (-not (Test-Path -LiteralPath $exe -PathType Leaf) -or
      -not (Test-Path -LiteralPath (Join-Path $p 'I76.ZFS') -PathType Leaf)) {
    return "This folder is not disc 1 of $Game or an installed game (it needs I76.EXE and I76.ZFS):`n$p"
  }
  $size = (Get-Item -LiteralPath $exe).Length
  if ($size -ne $ExeSize) {
    return "I76.EXE is $size bytes, not 1,019,392: OpenI76 needs version 1.0 (the one on the CD).`n$p"
  }
  return $null
}

function Get-Absolute([string]$p) {
  $full = [IO.Path]::GetFullPath([IO.Path]::Combine((Get-Location).ProviderPath, $p))
  if ($full.Length -gt 3) { $full = $full.TrimEnd('\') }   # keep D:\ as it is
  return $full
}

# A small dialog: game folder / disc image / Quit; then the picker. Returns the path or $null.
function Request-Data([string]$message) {
  Add-Type -AssemblyName System.Windows.Forms
  [System.Windows.Forms.Application]::EnableVisualStyles()
  $form = New-Object System.Windows.Forms.Form
  $form.Text = $Title; $form.FormBorderStyle = 'FixedDialog'; $form.StartPosition = 'CenterScreen'
  $form.MaximizeBox = $false; $form.MinimizeBox = $false; $form.AutoSize = $true; $form.AutoSizeMode = 'GrowAndShrink'
  $form.Padding = New-Object System.Windows.Forms.Padding(12)
  $label = New-Object System.Windows.Forms.Label
  $label.Text = $message; $label.AutoSize = $true; $label.MaximumSize = New-Object System.Drawing.Size(460, 0)
  $label.Location = New-Object System.Drawing.Point(12, 12)
  $form.Controls.Add($label)
  $y = $label.PreferredHeight + 28
  $buttons = @(@('Choose the disc image...', 'image'), @('Choose a folder...', 'folder'), @('Quit', 'quit'))
  $x = 12
  foreach ($b in $buttons) {
    $btn = New-Object System.Windows.Forms.Button
    $btn.Text = $b[0]; $btn.Tag = $b[1]; $btn.AutoSize = $true; $btn.Location = New-Object System.Drawing.Point($x, $y)
    $btn.Add_Click({ $form.Tag = $this.Tag; $form.Close() })
    $form.Controls.Add($btn); $x += 150
  }
  $form.AcceptButton = $form.Controls[1]; $form.CancelButton = $form.Controls[3]
  [void]$form.ShowDialog()
  switch ($form.Tag) {
    'folder' {
      $d = New-Object System.Windows.Forms.FolderBrowserDialog
      $d.Description = 'Choose a folder with the files of disc 1 (I76.EXE, I76.ZFS), e.g. the CD drive, or an installed game'
      $d.ShowNewFolderButton = $false
      if ($d.ShowDialog() -eq 'OK') { return $d.SelectedPath }
    }
    'image' {
      $d = New-Object System.Windows.Forms.OpenFileDialog
      $d.Title = "Choose the image of $Game disc 1 (I76_CD1.ISO, or a raw .bin)"
      $d.Filter = 'Disc images (*.iso;*.bin)|*.iso;*.bin|All files (*.*)|*.*'
      if ($d.ShowDialog() -eq 'OK') { return $d.FileName }
    }
  }
  return $null
}

$Intro = @"
OpenI76 needs your copy of $Game (it is not included): the PC CD.

Choose an image of disc 1 (I76_CD1.ISO), or a folder with the files of disc 1 (I76.EXE, I76.ZFS): the CD drive itself works, and so does an installed game.

Your choice is remembered; run OpenI76.cmd --change-data to pick another.
"@

$rest = @($args)
while ($rest.Count -gt 0) {
  $a = [string]$rest[0]
  if ($a -eq '--help' -or $a -eq '-h' -or $a -eq '/?') { Show-Usage; exit 0 }
  elseif ($a -eq '--dry-run') { $Dry = $true }
  elseif ($a -eq '--change-data') { $Change = $true }
  elseif ($a -eq '--forget-data') {
    Remove-Item -LiteralPath $LocFile -ErrorAction SilentlyContinue; "forgot the game data location ($LocFile)"; exit 0
  }
  else { break }
  $rest = @($rest | Select-Object -Skip 1)
}
$data = $null; $save = $false
if ($rest.Count -gt 0 -and -not ([string]$rest[0]).StartsWith('-')) {
  $data = Get-Absolute ([string]$rest[0]); $save = $true; $rest = @($rest | Select-Object -Skip 1)
}
if (-not $data -and $env:OPENI76_GAME) { $data = Get-Absolute $env:OPENI76_GAME }
if (-not $data -and -not $Change -and (Test-Path -LiteralPath $LocFile)) {
  $data = (Get-Content -LiteralPath $LocFile -TotalCount 1).Trim()
}

$msg = $Intro
if ($data) { $msg = Test-Data $data }
while (-not $data -or (Test-Data $data)) {
  if ($Dry -or $env:OPENI76_GAME) {   # never open dialogs in test runs
    if ($data) { [Console]::Error.WriteLine("no usable game data: $msg") } else { [Console]::Error.WriteLine('no usable game data') }
    exit 2
  }
  $data = Request-Data $msg
  if (-not $data) { exit 0 }
  $data = Get-Absolute $data; $save = $true
  $msg = Test-Data $data
}
if ($save -and -not $Dry) {
  New-Item -ItemType Directory -Force -Path $ConfDir | Out-Null
  Set-Content -LiteralPath $LocFile -Value $data -Encoding UTF8
}

# No ANGLE: the game presents frames through video_present, not gasm:gl.
$run = Join-Path $Here 'gasm-run.exe'
if (Test-Path -LiteralPath $data -PathType Container) { $src = @('--asset-dir', $data) } else { $src = @('--asset', "cd=$data") }
$cmd = @((Join-Path $Here 'openi76.wasm')) + $src + $rest
if ($Dry) { (@($run) + $cmd | ForEach-Object { '"' + $_ + '"' }) -join ' '; exit 0 }

New-Item -ItemType Directory -Force -Path $LogDir | Out-Null
Add-Content -LiteralPath $Log -Value ("--- {0} OpenI76 @VERSION@, gasm-run @GASM_VERSION@" -f (Get-Date -Format 'yyyy-MM-dd HH:mm:ss'))
Add-Content -LiteralPath $Log -Value ((@($run) + $cmd | ForEach-Object { '"' + $_ + '"' }) -join ' ')
# gasm-run's stderr lines arrive as error records: keep going, and write them out as plain text.
$ErrorActionPreference = 'Continue'
& $run @cmd 2>&1 | ForEach-Object { "$_" } | Tee-Object -FilePath $Log -Append
$rc = $LASTEXITCODE
if ($rc -ne 0 -and (Test-Path -LiteralPath $data -PathType Leaf)) {
  "A disc image must be disc 1 of $Game. OpenI76.cmd --change-data picks another."
}
exit $rc
