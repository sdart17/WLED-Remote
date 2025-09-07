param(
  [string]$Base = "http://192.168.6.221",
  [string]$PaletteCatalog = ".\wled-palettes-v0.14.4.json",
  [int]$IntervalSec = 5,
  [switch]$ForceAnsi,
  [switch]$NoAnsi
)

# ===== Console color helpers (truecolor if available; 16-color fallback with block characters) =====
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8
$ESC = [char]27

# Detect whether this session likely supports ANSI truecolor (conservative; overridable)
$SupportsTrueColor = $false
if ($PSBoundParameters.ContainsKey('NoAnsi') -and $NoAnsi) {
  $SupportsTrueColor = $false
} elseif ($PSBoundParameters.ContainsKey('ForceAnsi') -and $ForceAnsi) {
  $SupportsTrueColor = $true
} else {
  try {
    if ($host -and $host.Name -like '*ISE*') {
      $SupportsTrueColor = $false
    } else {
      $isWT   = [bool]$env:WT_SESSION
      $vtReg  = (Get-ItemProperty HKCU:\Console -Name VirtualTerminalLevel -ErrorAction SilentlyContinue).VirtualTerminalLevel
      $hasSty = $PSStyle -ne $null
      if ($isWT -or $vtReg -eq 1 -or $hasSty) { $SupportsTrueColor = $true }
    }
  } catch { $SupportsTrueColor = $false }
}

function Ansi-FG($r,$g,$b) { return "$ESC[38;2;$r;$g;${b}m" }
function Ansi-Reset()      { return "$ESC[0m" }

# 16-color fallback mapping
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

# Draw a row of colored "squares" using block glyph
function Show-ColorBar {
  param([Parameter(Mandatory=$true)][array]$RgbTriples, [string]$Label = $null, [int]$Width = 4)
  if ($Label) { Write-Host $Label }
  $BlockChar = [char]0x2588
  $cell = ($BlockChar.ToString()) * $Width
  foreach ($row in 1,2) {
    if ($SupportsTrueColor) {
      $line = ""
      foreach ($c in $RgbTriples) {
        if ($null -eq $c -or $c.Count -lt 3) { continue }
        $r=[int]$c[0]; $g=[int]$c[1]; $b=[int]$c[2]
        $line += (Ansi-FG $r $g $b) + $cell + (Ansi-Reset) + " "
      }
      Write-Host $line
    } else {
      foreach ($c in $RgbTriples) {
        if ($null -eq $c -or $c.Count -lt 3) { continue }
        $fg = ClosestConsoleColor ([int]$c[0]) ([int]$c[1]) ([int]$c[2])
        Write-Host -NoNewline $cell -ForegroundColor $fg
        Write-Host -NoNewline " "
      }
      Write-Host ""
    }
  }
}

# ===== Utility: JSON GET with friendly error =====
function Get-Json($url) {
  try { return Invoke-RestMethod -UseBasicParsing -Uri $url -TimeoutSec 8 -ErrorAction Stop }
  catch { throw "Request failed: $url`n$($_.Exception.Message)" }
}

# ===== Load local palette catalog =====
if (-not (Test-Path -LiteralPath $PaletteCatalog)) {
  Write-Error "Palette catalog not found: $PaletteCatalog"
  exit 1
}
$catalog = Get-Content -LiteralPath $PaletteCatalog -Raw | ConvertFrom-Json
$catalogGradients = $catalog.palettes  # ordered list of built-in gradients (from palettes.h)

# ===== Normalization and dynamic palette helpers =====
function Normalize-Name([string]$s) {
  if ($null -eq $s) { return "" }
  $n = $s.ToLowerInvariant()
  $n = ($n -replace '[^a-z0-9]+',' ')
  $n = ($n -replace '\s+',' ').Trim()
  return $n
}

function RGBtoHSV([int]$r,[int]$g,[int]$b) {
  $rf=$r/255.0; $gf=$g/255.0; $bf=$b/255.0
  $max=[Math]::Max($rf,[Math]::Max($gf,$bf)); $min=[Math]::Min($rf,[Math]::Min($gf,$bf))
  $d=$max-$min
  $h=0.0
  if ($d -eq 0) { $h=0 }
  elseif ($max -eq $rf) { $h = 60.0 * (((($gf-$bf)/$d) % 6)) }
  elseif ($max -eq $gf) { $h = 60.0 * (((($bf-$rf)/$d) + 2)) }
  else { $h = 60.0 * (((($rf-$gf)/$d) + 4)) }
  if ($h -lt 0) { $h += 360.0 }
  $s = 0.0; if ($max -ne 0) { $s = $d / $max }
  $v = $max
  return @($h,$s,$v)
}
function HSVtoRGB([double]$h,[double]$s,[double]$v) {
  while ($h -lt 0) { $h += 360.0 }; while ($h -ge 360.0) { $h -= 360.0 }
  $c = $v * $s
  $x = $c * (1 - [Math]::Abs((($h/60.0) % 2) - 1))
  $m = $v - $c
  $rf=0; $gf=0; $bf=0
  if     ($h -lt 60)  { $rf=$c; $gf=$x; $bf=0 }
  elseif ($h -lt 120) { $rf=$x; $gf=$c; $bf=0 }
  elseif ($h -lt 180) { $rf=0; $gf=$c; $bf=$x }
  elseif ($h -lt 240) { $rf=0; $gf=$x; $bf=$c }
  elseif ($h -lt 300) { $rf=$x; $gf=0; $bf=$c }
  else                 { $rf=$c; $gf=0; $bf=$x }
  $r=[int][Math]::Round(255*($rf+$m))
  $g=[int][Math]::Round(255*($gf+$m))
  $b=[int][Math]::Round(255*($bf+$m))
  return @($r,$g,$b)
}

function IsDynamicPaletteName([string]$name) {
  $n = Normalize-Name $name
  if ($n -in @(
    'default','color','color 1','color1','primary',
    'color 2','color2','secondary',
    'color 3','color3','tertiary',
    'color cycle','colorcycle',
    'analogous','analagous'
  )) { return $true }
  # Also treat "~ Custom N ~" as dynamic so these do not shift built-in ordering
  if ($name -match '^\s*~\s*Custom\s+\d+\s*~\s*$') { return $true }
  return $false
}

function DynPalette-ColorN($segColors, [int]$slot) {
  if (-not $segColors -or $segColors.Count -le $slot) { return $null }
  $c = $segColors[$slot]
  $out=@(); for($i=0;$i -lt 16;$i++){ $out += ,@([int]$c[0],[int]$c[1],[int]$c[2]) }
  return $out
}
function DynPalette-ColorCycle($segColors) {
  if (-not $segColors -or $segColors.Count -eq 0) { return $null }
  $uniq = @(); $seen=@{}
  foreach($c in $segColors){
    $k="$($c[0]),$($c[1]),$($c[2])"
    if (-not $seen.ContainsKey($k)) { $uniq += ,$c; $seen[$k]=$true }
  }
  if ($uniq.Count -eq 0){ return $null }
  $out=@(); for($i=0;$i -lt 16;$i++){ $out += ,$uniq[$i % $uniq.Count] }
  return $out
}
function DynPalette-Analogous($segColors) {
  if (-not $segColors -or $segColors.Count -eq 0) { return $null }
  $c = $segColors[0]
  $hsv = RGBtoHSV $c[0] $c[1] $c[2]
  $h=[double]$hsv[0]; $s=[double]$hsv[1]; $v=[double]$hsv[2]
  if ($s -lt 0.1) { $s = 0.6 }
  $range = 90.0
  $out=@()
  for($i=0;$i -lt 16;$i++){
    $t = $i / 15.0
    $hi = $h - $range/2.0 + $range * $t
    $rgb = HSVtoRGB $hi $s $v
    $out += ,$rgb
  }
  return $out
}
function Try-DynamicPalette([string]$palName, $segColors) {
  $n = Normalize-Name $palName
  if ($n -in @('color','color 1','color1','primary')) { return DynPalette-ColorN $segColors 0 }
  if ($n -in @('color 2','color2','secondary'))       { return DynPalette-ColorN $segColors 1 }
  if ($n -in @('color 3','color3','tertiary'))        { return DynPalette-ColorN $segColors 2 }
  if ($n -in @('color cycle','colorcycle'))           { return DynPalette-ColorCycle $segColors }
  if ($n -in @('analogous','analagous'))              { return DynPalette-Analogous $segColors }
  if ($n -eq 'default') { return $null }
  return $null
}

# ===== Effect metadata (0.14+): palette usage + color slot labels =====
$fxMeta = $null
try { $fxMeta = Invoke-RestMethod -UseBasicParsing -Uri ($Base.TrimEnd('/') + "/json/fxdata") } catch { $fxMeta = $null }

function Effect-UsesPaletteFx { param([int]$idx)
  if (-not $fxMeta -or $idx -lt 0 -or $idx -ge $fxMeta.Count) { return $true }
  $parts = $fxMeta[$idx].Split(';')
  if ($parts.Count -lt 3) { return $true }
  $palSec = $parts[2]
  if ([string]::IsNullOrWhiteSpace($palSec)) { return $false } else { return $true }
}
function Effect-ColorLabels { param([int]$idx)
  if (-not $fxMeta -or $idx -lt 0 -or $idx -ge $fxMeta.Count) { return @('Fx','Bg','Cs') }
  $parts = $fxMeta[$idx].Split(';')
  if ($parts.Count -lt 2) { return @('Fx','Bg','Cs') }
  $colSec = $parts[1]
  if ([string]::IsNullOrEmpty($colSec)) { return @() }
  $tokens   = $colSec.Split(',')
  $defaults = @('Fx','Bg','Cs')
  $labels = @()
  for ($i=0; $i -lt [Math]::Min($tokens.Count,3); $i++) {
    $t = $tokens[$i]
    if ($t -eq '') { continue }
    if ($t -eq '!') { $labels += $defaults[$i] } else { $labels += $t }
  }
  return $labels
}

# ===== Device palette list and ID -> catalog mapping (order-based, skip dynamics and customs) =====
$devicePalNames = $null
try { $devicePalNames = Invoke-RestMethod -UseBasicParsing -Uri ($Base.TrimEnd('/') + "/json/pal") } catch { $devicePalNames = $null }
if (-not $devicePalNames) {
  try {
    $tmp = Get-Json ($Base.TrimEnd('/') + "/json")
    if ($tmp -and $tmp.palettes) { $devicePalNames = $tmp.palettes }
  } catch { }
}
if (-not $devicePalNames) {
  Write-Error "Could not fetch palette names from device."
  exit 1
}

# Build mapping: for each device palette index, what is its built-in rank (skipping dynamics and customs)
$builtInRankOfPalIdx = @{}
$rank = 0
for ($i=0; $i -lt $devicePalNames.Count; $i++) {
  $nm = [string]$devicePalNames[$i]
  if (-not (IsDynamicPaletteName $nm)) {
    $builtInRankOfPalIdx["$i"] = $rank
    $rank++
  }
}
$builtInCountDevice  = $rank
$builtInCountCatalog = $catalogGradients.Count

# ===== Main loop =====
while ($true) {
  try {
    $baseTrim = $Base.TrimEnd('/')
    $j = Get-Json "$baseTrim/json"
    $state    = $j.state
    $effects  = if ($j.PSObject.Properties.Name -contains 'effects')  { $j.effects }  else { @() }
    $palNames = if ($j.PSObject.Properties.Name -contains 'palettes') { $j.palettes } else { $devicePalNames }

    # Selected segment and indices
    $seg = $null
    if ($state -and $state.seg) {
      foreach ($s in $state.seg) { if ($s.sel) { $seg = $s; break } }
      if (-not $seg -and $state.seg.Count -gt 0) { $seg = $state.seg[0] }
    }

    $fxIdx  = 0; if ($seg -and $seg.PSObject.Properties.Name -contains 'fx')  { $fxIdx  = [int]$seg.fx }  elseif ($state.PSObject.Properties.Name -contains 'fx')  { $fxIdx  = [int]$state.fx }
    $palIdx = 0; if ($seg -and $seg.PSObject.Properties.Name -contains 'pal') { $palIdx = [int]$seg.pal } elseif ($state.PSObject.Properties.Name -contains 'pal') { $palIdx = [int]$state.pal }

    # Names
    $fxName  = "(unknown effect)";  if ($effects -and $fxIdx -ge 0 -and $fxIdx -lt $effects.Count) { $fxName  = [string]$effects[$fxIdx] }
    $palName = "(unknown palette)"; if ($palNames -and $palIdx -ge 0 -and $palIdx -lt $palNames.Count) { $palName = [string]$palNames[$palIdx] }

    # Segment colors (array of [r,g,b])
    $segColors = @()
    if ($seg -and $seg.col) {
      foreach ($c in $seg.col) { if ($c -and $c.Count -ge 3) { $segColors += ,@([int]$c[0],[int]$c[1],[int]$c[2]) } }
    }

    # From fxdata
    $paletteUsed = Effect-UsesPaletteFx $fxIdx
    $colorLabels = Effect-ColorLabels   $fxIdx

    # Resolve palette swatch
    $palSwatch = $null
    $palLabel  = $palName

    if (IsDynamicPaletteName $palName) {
      $palSwatch = Try-DynamicPalette $palName $segColors
    } else {
      $rankKey = "$palIdx"
      if ($builtInRankOfPalIdx.ContainsKey($rankKey)) {
        $rankVal = [int]$builtInRankOfPalIdx[$rankKey]
        if ($rankVal -ge 0 -and $rankVal -lt $builtInCountCatalog) {
          $palObj = $catalogGradients[$rankVal]
          if ($palObj -and $palObj.sample16) {
            $palSwatch = $palObj.sample16
            if ($palObj.friendlyGuess) { $palLabel = "$palName -> " + [string]$palObj.friendlyGuess }
            elseif ($palObj.cIdent)   { $palLabel = "$palName -> " + [string]$palObj.cIdent }
          }
        }
      }
    }

    # Output
    Clear-Host
    Write-Host ("WLED @ {0}" -f $Base)
    Write-Host ("Effect : {0} (#{1})" -f $fxName, $fxIdx)

    $paletteNote = "likely used"
    if (-not $paletteUsed) { $paletteNote = "ignored or irrelevant" }
    Write-Host ("Palette: {0} (#{1}) - {2}" -f $palName, $palIdx, $paletteNote)

    # Segment colors: only show slots the effect actually uses
    if ($colorLabels.Count -gt 0 -and $segColors.Count -gt 0) {
      $showCount  = [Math]::Min($colorLabels.Count, $segColors.Count)
      $showColors = $segColors[0..($showCount-1)]
      Show-ColorBar -RgbTriples $showColors -Label ("Segment colors ({0}):" -f ($colorLabels -join ', ')) -Width 6
    } else {
      Write-Host "Segment colors: (none for this effect)"
    }

    # Palette swatch
    if ($palSwatch -and $paletteUsed) {
      Show-ColorBar -RgbTriples $palSwatch -Label ("Palette swatch: {0}" -f $palLabel) -Width 4
    } elseif (-not $paletteUsed) {
      Write-Host "Palette swatch: (effect does not use a palette)"
    } else {
      Write-Host "Palette swatch: (no match - dynamic with no colors, or catalog mismatch)"
      if ((Normalize-Name $palName) -eq "default") {
        Write-Host "Note: 'Default' is effect-defined in WLED; there is no single fixed gradient."
      }
    }

  } catch {
    Write-Host ("Error: {0}" -f $_.Exception.Message) -ForegroundColor Red
  }

  Start-Sleep -Seconds $IntervalSec
}
