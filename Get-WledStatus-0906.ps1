param(
  [string]$Base = 'http://192.168.4.42',
  [int]$IntervalSec = 5
)

# Effects that ignore palettes (edit as needed; names must match WLED UI)
$NoPaletteEffects = @('Solid')

# Built-in palette approximations (visual only; override via wled-palettes.json next to this script)
$BuiltInPaletteApprox = @{
  "Rainbow"        = @(@(255,0,0),@(255,127,0),@(255,255,0),@(0,255,0),@(0,255,255),@(0,0,255),@(127,0,255));
  "Rainbow Bands"  = @(@(255,0,0),@(255,0,0),@(255,255,0),@(255,255,0),@(0,255,0),@(0,255,0),@(0,255,255),@(0,255,255),@(0,0,255),@(0,0,255),@(127,0,255),@(127,0,255));
  "Party"          = @(@(255,5,5),@(0,255,45),@(0,91,255),@(255,0,255),@(255,200,0));
  "Ocean"          = @(@(0,32,64),@(0,80,160),@(0,160,200),@(0,255,255),@(0,64,128));
  "Forest"         = @(@(0,64,0),@(0,128,0),@(34,139,34),@(85,107,47),@(154,205,50));
  "Lava"           = @(@(0,0,0),@(64,0,0),@(128,0,0),@(200,32,0),@(255,80,0),@(255,160,0),@(255,255,0));
  "Heat"           = @(@(0,0,0),@(64,0,0),@(128,0,0),@(255,0,0),@(255,128,0),@(255,255,0),@(255,255,255));
  "Cloud"          = @(@(0,0,32),@(0,32,64),@(64,128,192),@(160,200,255),@(255,255,255));
  "Sunset"         = @(@(128,0,64),@(200,32,0),@(255,128,0),@(255,200,0),@(255,64,32));
  "Aurora"         = @(@(0,128,64),@(0,200,128),@(64,255,200),@(128,64,255),@(64,0,128))
}

# -------- JSON helpers --------
function Get-Json {
  param([Parameter(Mandatory)][string]$Url)
  try { Invoke-RestMethod -Method Get -Uri $Url -TimeoutSec 8 -ErrorAction Stop }
  catch { throw "Request failed: $Url`n$($_.Exception.Message)" }
}
function Get-Prop {
  param($Object, [string]$Name, $Default = $null)
  if ($null -ne $Object -and $Object.PSObject.Properties.Name -contains $Name -and $null -ne $Object.$Name) { return $Object.$Name }
  return $Default
}
function To-Array {
  param($Value)
  if ($null -eq $Value) { return @() }
  if ($Value -is [System.Collections.IList]) { return @($Value) }
  $arr = @()
  foreach ($p in $Value.PSObject.Properties) { $arr += $p.Value }
  return $arr
}

# -------- Color utils --------
function Clamp01($x) { if ($x -lt 0) {0} elseif ($x -gt 255) {255} else {[int]$x} }
function HexRGB($r,$g,$b) { ('#{0:X2}{1:X2}{2:X2}' -f (Clamp01 $r),(Clamp01 $g),(Clamp01 $b)) }
function NameRGB($r,$g,$b) {
  $r=[int]$r; $g=[int]$g; $b=[int]$b
  if ($r -eq 0 -and $g -eq 0 -and $b -eq 0) { return 'black' }
  if ($r -eq 255 -and $g -eq 255 -and $b -eq 255) { return 'white' }
  if ($r -eq $g -and $g -eq $b) {
    if     ($r -lt 64)  { return 'very dark gray' }
    elseif ($r -lt 128) { return 'dark gray' }
    elseif ($r -lt 192) { return 'gray' }
    else                { return 'light gray' }
  }
  $m = [Math]::Max($r,[Math]::Max($g,$b))
  if     ($m -eq $r) { if ($g -ge 0.6*$r -and $b -le 0.4*$r) { 'yellow/orange' } elseif ($b -ge 0.6*$r -and $g -le 0.4*$r) { 'magenta/pink' } else { 'red-ish' } }
  elseif ($m -eq $g) { if ($r -ge 0.6*$g -and $b -le 0.4*$g) { 'yellow/lime' }   elseif ($b -ge 0.6*$g -and $r -le 0.4*$g) { 'cyan/teal'   } else { 'green-ish' } }
  else               { if ($r -ge 0.6*$b -and $g -le 0.4*$b) { 'violet/purple'} elseif ($g -ge 0.6*$b -and $r -le 0.4*$b) { 'cyan/azure'  } else { 'blue-ish' } }
}
function ToRGB($c) {
  if ($null -eq $c -or $c.Count -lt 3) { return @(0,0,0) }
  return @([int]$c[0],[int]$c[1],[int]$c[2])
}
function ExtractSegmentColors($seg) {
  $ordered = @()
  $uniqSet = New-Object System.Collections.Generic.HashSet[string]
  $uniqArr = @()
  if ($seg -and $seg.col) {
    foreach ($c in $seg.col) {
      $rgb = ToRGB $c
      $hex = HexRGB $rgb[0] $rgb[1] $rgb[2]
      $name = NameRGB $rgb[0] $rgb[1] $rgb[2]
      $obj = [pscustomobject]@{ r=$rgb[0]; g=$rgb[1]; b=$rgb[2]; hex=$hex; name=$name }
      $ordered += $obj
      if (-not ($rgb[0] -eq 0 -and $rgb[1] -eq 0 -and $rgb[2] -eq 0)) {
        $key = "$($rgb[0]),$($rgb[1]),$($rgb[2])"
        if ($uniqSet.Add($key)) { $uniqArr += $obj }
      }
    }
  }
  return [pscustomobject]@{ ordered=$ordered; unique=$uniqArr }
}

# -------- Swatch rendering using BLOCK characters (works in ISE & classic console) --------
# Detect whether this session likely supports ANSI truecolor
$SupportsTrueColor = $false
try {
  $isWT   = [bool]$env:WT_SESSION
  $vtReg  = (Get-ItemProperty HKCU:\Console -Name VirtualTerminalLevel -ErrorAction SilentlyContinue).VirtualTerminalLevel
  $hasSty = $PSStyle -ne $null
  if ($isWT -or $vtReg -eq 1 -or $hasSty) { $SupportsTrueColor = $true }
} catch { }

# ANSI helpers (only used if $SupportsTrueColor)
function Ansi-FG($r,$g,$b) { $esc=[char]27; return "$esc[38;2;$r;$g;${b}m" }
function Ansi-Reset()      { $esc=[char]27; return "$esc[0m" }

# Map RGB to nearest 16-color ConsoleColor for fallback
$ConsolePalette = @(
  @{c=[ConsoleColor]::Black;        r=  0; g=  0; b=  0}
  @{c=[ConsoleColor]::DarkBlue;     r=  0; g=  0; b=128}
  @{c=[ConsoleColor]::DarkGreen;    r=  0; g=128; b=  0}
  @{c=[ConsoleColor]::DarkCyan;     r=  0; g=128; b=128}
  @{c=[ConsoleColor]::DarkRed;      r=128; g=  0; b=  0}
  @{c=[ConsoleColor]::DarkMagenta;  r=128; g=  0; b=128}
  @{c=[ConsoleColor]::DarkYellow;   r=128; g=128; b=  0}
  @{c=[ConsoleColor]::Gray;         r=192; g=192; b=192}
  @{c=[ConsoleColor]::DarkGray;     r=128; g=128; b=128}
  @{c=[ConsoleColor]::Blue;         r=  0; g=  0; b=255}
  @{c=[ConsoleColor]::Green;        r=  0; g=255; b=  0}
  @{c=[ConsoleColor]::Cyan;         r=  0; g=255; b=255}
  @{c=[ConsoleColor]::Red;          r=255; g=  0; b=  0}
  @{c=[ConsoleColor]::Magenta;      r=255; g=  0; b=255}
  @{c=[ConsoleColor]::Yellow;       r=255; g=255; b=  0}
  @{c=[ConsoleColor]::White;        r=255; g=255; b=255}
)
function ClosestConsoleColor([int]$r,[int]$g,[int]$b) {
  $best=$ConsolePalette[0]; $bestD=[double]::PositiveInfinity
  foreach ($p in $ConsolePalette) {
    $dr=$r-$p.r; $dg=$g-$p.g; $db=$b-$p.b
    $d=($dr*$dr)+($dg*$dg)+($db*$db)
    if ($d -lt $bestD) { $best=$p; $bestD=$d }
  }
  return $best.c
}

# Draw a row of squares using full block char; width controls blocks per cell; height fixed to 2 rows
function Show-ColorRow {
  param([Parameter(Mandatory=$true)][array]$RgbObjects, [int]$Width=4, [string]$Label=$null)
  if ($Label) { Write-Host $Label }
  $cell = "█" * $Width
  foreach ($row in 1,2) {
    if ($SupportsTrueColor) {
      $line = ""
      foreach ($c in $RgbObjects) {
        $r=[int]$c.r; $g=[int]$c.g; $b=[int]$c.b
        $line += (Ansi-FG $r $g $b) + $cell + (Ansi-Reset) + " "
      }
      Write-Host $line
    } else {
      foreach ($c in $RgbObjects) {
        $fg = ClosestConsoleColor ([int]$c.r) ([int]$c.g) ([int]$c.b)
        Write-Host -NoNewline $cell -ForegroundColor $fg
        Write-Host -NoNewline " "
      }
      Write-Host ""
    }
  }
}
function Show-ColorRowFromArrays {
  param([Parameter(Mandatory=$true)][array]$RgbTriplets, [int]$Width=4, [string]$Label=$null)
  $objs=@()
  foreach ($t in $RgbTriplets) { $objs += [pscustomobject]@{ r=[int]$t[0]; g=[int]$t[1]; b=[int]$t[2] } }
  Show-ColorRow -RgbObjects $objs -Width $Width -Label $Label
}

# -------- Optional local palette map loader --------
function Load-LocalPaletteMap {
  try {
    $scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
    $path = Join-Path $scriptDir 'wled-palettes.json'
    if (Test-Path $path) {
      $raw = Get-Content -Raw -LiteralPath $path -ErrorAction Stop
      $json = ConvertFrom-Json -InputObject $raw
      return $json
    }
  } catch {}
  return $null
}
$LocalPaletteMap = Load-LocalPaletteMap

function Get-PaletteColors {
  param([int]$PalIdx, [string]$PalName)
  if ($LocalPaletteMap -and $LocalPaletteMap.PSObject.Properties.Name -contains "$PalIdx") {
    $val = $LocalPaletteMap."$PalIdx"; if ($val) { return $val }
  }
  if ($LocalPaletteMap -and $PalName -and $LocalPaletteMap.PSObject.Properties.Name -contains $PalName) {
    $val = $LocalPaletteMap.$PalName; if ($val) { return $val }
  }
  if ($PalName -and $BuiltInPaletteApprox.ContainsKey($PalName)) { return $BuiltInPaletteApprox[$PalName] }
  return $null
}

# -------- Main loop --------
while ($true) {
  try {
    # Fetch WLED state + name tables
    $state    = Get-Json "$Base/json/state"
    $effects  = To-Array (Get-Json "$Base/json/effects")
    $palettes = To-Array (Get-Json "$Base/json/palettes")
    try { $presets = Get-Json "$Base/presets.json" } catch { $presets = $null }

    # Segment selection
    $ms = [int](Get-Prop $state 'mainseg' 0)
    $seg = $null
    if ($state -and $state.seg) {
      if ($state.seg.Count -gt $ms) { $seg = $state.seg[$ms] }
      elseif ($state.seg.Count -gt 0) { $seg = $state.seg[0] }
    }

    # Indices and names
    $fxIdx  = [int](Get-Prop $seg 'fx' 0)
    $palIdx = [int](Get-Prop $seg 'pal' 0)
    $psId   = [int](Get-Prop $state 'ps' 0)
    $plId   = [int](Get-Prop $state 'pl' 0)

    $fxName = '(unknown)'; if ($effects.Count -gt $fxIdx -and $fxIdx -ge 0) { $fxName = $effects[$fxIdx] }
    $palName = '(unknown)'; if ($palettes.Count -gt $palIdx -and $palIdx -ge 0) { $palName = $palettes[$palIdx] }

    # Resolve preset & playlist
    $presetName = '(unsaved / none)'
    if ($presets -and $psId -gt 0) {
      $psKey = "$psId"
      if ($presets.PSObject.Properties.Name -contains $psKey -and $presets.$psKey.n) { $presetName = $presets.$psKey.n }
      else { $presetName = "(preset $psId)" }
    }
    $playlistName = $null
    if ($presets) {
      if ($plId -gt 0) {
        $plKey = "$plId"
        if ($presets.PSObject.Properties.Name -contains $plKey -and $presets.$plKey.n) { $playlistName = $presets.$plKey.n }
      } else {
        foreach ($p in $presets.PSObject.Properties) {
          $val = $p.Value
          if ($val -and $val.PSObject.Properties.Name -contains 'playlist') {
            $pls = $val.playlist
            if ($pls -and $pls.ps -and ($pls.ps -contains $psId)) { $playlistName = $val.n; break }
          }
        }
      }
    }

    # Colors and palette usage
    $colors = ExtractSegmentColors $seg
    $uniqCount = $colors.unique.Count

    $paletteUsed = $true
    if ($NoPaletteEffects -contains $fxName) { $paletteUsed = $false }
    if ($uniqCount -eq 0) { $paletteUsed = $false }
    $paletteNote = 'likely used'; if (-not $paletteUsed) { $paletteNote = 'ignored or irrelevant' }

    # Find palette RGBs (local map or approximations)
    $palRgb = Get-PaletteColors -PalIdx $palIdx -PalName $palName

    # Output
    Clear-Host
    Write-Host ("WLED @ {0} (refreshed {1})" -f $Base, (Get-Date))
    if ($playlistName) {
      Write-Host ("Playlist: {0}  (ID {1})" -f $playlistName, $plId)
      Write-Host (" -> Now playing preset: {0} (ID {1})" -f $presetName, $psId)
    } else {
      Write-Host ("Preset : {0} (ID {1})" -f $presetName, $psId)
    }
    Write-Host ("Effect : {0} (#{1})" -f $fxName, $fxIdx)
    Write-Host ("Palette: {0} (#{1}) - {2}" -f $palName, $palIdx, $paletteNote)

    # Segment colors (from seg.col)
    Write-Host "Colors (ordered from seg.col):"
    if ($colors.ordered.Count -eq 0) {
      Write-Host "  (none)"
    } else {
      $i=0
      foreach ($c in $colors.ordered) {
        Write-Host ("  [{0}] {1}  rgb({2},{3},{4})" -f $i, $c.hex, $c.r, $c.g, $c.b)
        Write-Host ("       name: {0}" -f $c.name)
        $i++
      }
      Show-ColorRow -RgbObjects $colors.ordered -Width 6 -Label "Color swatches (seg.col):"
    }

    # Palette swatches (visual representation of current palette)
    if ($palRgb) {
      Show-ColorRowFromArrays -RgbTriplets $palRgb -Width 4 -Label "Palette swatches (approximate or local map):"
      $parts=@(); foreach ($t in $palRgb) { $parts += "[{0},{1},{2}]" -f $t[0],$t[1],$t[2] }
      Write-Host ("Palette RGB: [" + ($parts -join ", ") + "]")
    } else {
      Write-Host "Palette swatches: (no RGB data available for this palette)"
      Write-Host "Tip: Create 'wled-palettes.json' next to this script to define RGB stops by name or ID."
    }

    Write-Host ("Unique non-black colors: {0}" -f $uniqCount)
    Write-Host ("Segment: {0}" -f $ms)

  } catch {
    Write-Host ("Error: {0}" -f $_.Exception.Message) -ForegroundColor Red
  }

  Start-Sleep -Seconds $IntervalSec
}
