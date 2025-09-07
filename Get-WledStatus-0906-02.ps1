param(
  [string]$Base = "http://192.168.4.42",
  [string]$PaletteCatalog = ".\wled-palettes-v0.14.4.json",
  [int]$IntervalSec = 5,
  [switch]$ForceAnsi,
  [switch]$NoAnsi,
  [switch]$DebugNames  # prints name-mapping table once at start
)

# =============== Console color helpers ===============
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8
$ESC = [char]27

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

# =============== Utils & JSON ===============
function Get-Json($url) {
  try { return Invoke-RestMethod -UseBasicParsing -Uri $url -TimeoutSec 8 -ErrorAction Stop }
  catch { throw "Request failed: $url`n$($_.Exception.Message)" }
}
function Normalize-Name([string]$s) {
  if ($null -eq $s) { return "" }
  # strip star prefix and ~ custom markers
  $t = $s.Trim()
  if ($t.StartsWith('*')) { $t = $t.TrimStart('*').Trim() }
  $t = $t -replace '^\~\s*Custom\s+\d+\s*\~$',''
  $n = $t.ToLowerInvariant()
  $n = ($n -replace '[^a-z0-9]+',' ')
  $n = ($n -replace '\s+',' ').Trim()
  return $n
}

# =============== Palette plumbing ===============
# Parse custom palette stops: [pos,r,g,b, ...]
function Parse-PaletteStops([array]$arr) {
  $stops=@()
  if (-not $arr -or ($arr.Count % 4) -ne 0) { return $stops }
  for ($i=0; $i -lt $arr.Count; $i+=4) {
    $pos=[int]$arr[$i]; $r=[int]$arr[$i+1]; $g=[int]$arr[$i+2]; $b=[int]$arr[$i+3]
    $stops += ,[pscustomobject]@{pos=$pos; r=$r; g=$g; b=$b}
  }
  $stops = $stops | Sort-Object pos
  if ($stops.Count -eq 0) { return $stops }
  if ($stops[0].pos -gt 0)   { $stops = ,([pscustomobject]@{pos=0;   r=$stops[0].r;   g=$stops[0].g;   b=$stops[0].b}) + $stops }
  if ($stops[-1].pos -lt 255){ $stops += ,[pscustomobject]@{pos=255; r=$stops[-1].r; g=$stops[-1].g; b=$stops[-1].b} }
  return $stops
}
function Sample-Stops([array]$stops, [int]$n = 16) {
  $out=@()
  if (-not $stops -or $stops.Count -lt 2) { return $out }
  for ($i=0; $i -lt $n; $i++) {
    $x = [double](($i) * 255.0 / [Math]::Max(1,($n-1)))
    $lo = $stops[0]; $hi = $stops[-1]
    for ($k=0; $k -lt ($stops.Count-1); $k++) {
      if ($x -ge $stops[$k].pos -and $x -le $stops[$k+1].pos) { $lo=$stops[$k]; $hi=$stops[$k+1]; break }
    }
    $span = [double]($hi.pos - $lo.pos); if ($span -le 0) { $span = 1.0 }
    $t = ($x - $lo.pos) / $span
    $r = [int][Math]::Round($lo.r + ($hi.r - $lo.r)*$t)
    $g = [int][Math]::Round($lo.g + ($hi.g - $lo.g)*$t)
    $b = [int][Math]::Round($lo.b + ($hi.b - $lo.b)*$t)
    $out += ,@($r,$g,$b)
  }
  return $out
}

# Dynamic palette flags (UI entries not tied to palettes.h)
function IsDynamicPaletteName([string]$name) {
  if ([string]::IsNullOrWhiteSpace($name)) { return $false }
  if ($name.TrimStart().StartsWith('*')) { return $true }
  $n = Normalize-Name $name
  return $n -in @('default','color 1','colors 1 2','colors 1&2','colors only','color gradient','random cycle')
}
function IsCustomPaletteName([string]$name) {
  return ($name -match '^\s*~\s*Custom\s+\d+\s*~\s*$')
}

function DynPalette-Color1($segColors) {
  if (-not $segColors -or $segColors.Count -lt 1) { return $null }
  $c = $segColors[0]; $out=@(); for($i=0;$i -lt 16;$i++){ $out += ,$c }; return $out
}
function DynPalette-Colors12($segColors) {
  if (-not $segColors -or $segColors.Count -lt 2) { return $null }
  $c0=$segColors[0]; $c1=$segColors[1]; $out=@()
  for($i=0;$i -lt 16;$i++){ if (($i % 2) -eq 0) { $out += ,$c0 } else { $out += ,$c1 } }
  return $out
}
function DynPalette-ColorsOnly($segColors) {
  if (-not $segColors) { return $null }
  $use = @(); foreach($c in $segColors){ if ($c -and $c.Count -ge 3){ $use += ,$c } }
  if ($use.Count -eq 0){ return $null }
  $out=@(); for($i=0;$i -lt 16;$i++){ $out += ,$use[$i % $use.Count] }
  return $out
}
function DynPalette-ColorGradient($segColors) {
  if (-not $segColors -or $segColors.Count -lt 2) { return $null }
  $stops=@(); $m=$segColors.Count
  for ($i=0; $i -lt $m; $i++) {
    $pos = [int][Math]::Round(255 * ($i / [Math]::Max(1,($m-1))))
    $stops += ,[pscustomobject]@{pos=$pos; r=[int]$segColors[$i][0]; g=[int]$segColors[$i][1]; b=[int]$segColors[$i][2]}
  }
  return (Sample-Stops $stops 16)
}
function Try-DynamicPalette([string]$palName, $segColors, [ref]$note) {
  $n = Normalize-Name $palName
  if ($n -eq 'default')       { $note.Value = "effect-defined"; return $null }
  if ($n -eq 'random cycle')  { $note.Value = "randomized";     return $null }
  if ($n -eq 'color 1')       { return DynPalette-Color1 $segColors }
  if ($n -in @('colors 1 2','colors 1&2')) { return DynPalette-Colors12 $segColors }
  if ($n -eq 'colors only')   { return DynPalette-ColorsOnly $segColors }
  if ($n -eq 'color gradient'){ return DynPalette-ColorGradient $segColors }
  return $null
}

# =============== Effect metadata ===============
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

# =============== Load catalog (palettes.h JSON) ===============
if (-not (Test-Path -LiteralPath $PaletteCatalog)) {
  Write-Error "Palette catalog not found: $PaletteCatalog"
  exit 1
}
$catalog = Get-Content -LiteralPath $PaletteCatalog -Raw | ConvertFrom-Json
$catalogGradients = $catalog.palettes  # each has .sample16, .friendlyGuess, .cIdent

# Build a lookup of normalized names -> catalog index (both friendly and cIdent forms)
$CatalogIndexByNorm = @{}
for ($j=0; $j -lt $catalogGradients.Count; $j++) {
  $p = $catalogGradients[$j]
  if ($p.friendlyGuess) {
    $nn = Normalize-Name([string]$p.friendlyGuess)
    if ($nn -ne '') { $CatalogIndexByNorm[$nn] = $j }
  }
  if ($p.cIdent) {
    $label = ($p.cIdent -replace '_gp$','' -replace '_',' ')
    $nn2 = Normalize-Name($label)
    if ($nn2 -ne '') { if (-not $CatalogIndexByNorm.ContainsKey($nn2)) { $CatalogIndexByNorm[$nn2] = $j } }
  }
}

# A few aliases where device UI shortens/varies names
$AliasToCanonical = @{
  'analogous'      = 'analogous'
  'analagous'      = 'analogous'   # typo guard
  'yelblu'         = 'yelblu'
  'yelblu hot'     = 'yelblu hot'
  'yelmag'         = 'yelmag'
  'magred'         = 'magred'
  'orangery'       = 'orangery'

  'red blue'       = 'red blue'
  'red & blue'     = 'red blue'
  'semi blue'      = 'semi blue'
  'ocean breeze'   = 'breeze'
  'breeze'         = 'breeze'

  'toxy reaf'      = 'toxy reaf'
  'toxy reef'      = 'toxy reaf'
  'fairy reaf'     = 'fairy reaf'
  'fairy reef'     = 'fairy reaf'

  'c9 new'         = 'c9 new'
  'c9 2'           = 'c9 2'
  'light pink'     = 'light pink'
  'april night'    = 'april night'
  'aurora 2'       = 'aurora 2'

  'party'          = 'party'
  'cloud'          = 'cloud'
  'lava'           = 'lava'
  'ocean'          = 'ocean'
  'forest'         = 'forest'
  'rainbow'        = 'rainbow'
  'rainbow bands'  = 'rainbow bands'
  'sunset'         = 'sunset'
  'rivendell'      = 'rivendell'
  'yellowout'      = 'yellowout'
  'splash'         = 'splash'
  'pastel'         = 'pastel'
  'sunset 2'       = 'sunset 2'
  'beach'          = 'beach'
  'vintage'        = 'vintage'
  'departure'      = 'departure'
  'landscape'      = 'landscape'
  'beech'          = 'beech'
  'sherbet'        = 'sherbet'
  'hult'           = 'hult'
  'hult 64'        = 'hult 64'
  'drywet'         = 'drywet'
  'jul'            = 'jul'
  'grintage'       = 'grintage'
  'rewhi'          = 'rewhi'
  'tertiary'       = 'tertiary'
  'fire'           = 'fire'
  'icefire'        = 'icefire'
  'cyane'          = 'cyane'
  'autumn'         = 'autumn'
  'magenta'        = 'magenta'
  'orange teal'    = 'orange teal'
  'orange & teal'  = 'orange teal'
  'tiamat'         = 'tiamat'
  'c9'             = 'c9'
  'sakura'         = 'sakura'
  'aurora'         = 'aurora'
  'atlantica'      = 'atlantica'
  'temperature'    = 'temperature'
  'retro clown'    = 'retro clown'
  'candy'          = 'candy'
  'pink candy'     = 'pink candy'
  'red shift'      = 'red shift'
  'red tide'       = 'red tide'
  'red flash'      = 'red flash'
  'blink red'      = 'blink red'
  'lite light'     = 'lite light'
  'aqua flash'     = 'aqua flash'
  'red reaf'       = 'red reaf'
  'red reef'       = 'red reaf'
  'candy2'         = 'candy2'
}
# Build alias → index map by resolving alias to canonical and looking up in CatalogIndexByNorm
$AliasIndex = @{}
foreach ($k in $AliasToCanonical.Keys) {
  $canon = $AliasToCanonical[$k]
  $canonNorm = Normalize-Name $canon
  if ($CatalogIndexByNorm.ContainsKey($canonNorm)) {
    $AliasIndex[$k] = $CatalogIndexByNorm[$canonNorm]
  }
}

# =============== Fetch device names and palette data ===============
$devicePalNames = $null
$devicePaletteData = @{}

# Fetch palette names
try { $devicePalNames = Invoke-RestMethod -UseBasicParsing -Uri ($Base.TrimEnd('/') + "/json/pal") } catch { $devicePalNames = $null }
if (-not $devicePalNames) {
  try { $tmp = Get-Json ($Base.TrimEnd('/') + "/json"); if ($tmp -and $tmp.palettes) { $devicePalNames = $tmp.palettes } } catch { }
}
if (-not $devicePalNames) { Write-Error "Could not fetch palette names from device."; exit 1 }

# Fetch palette color data from all pages (like WLED web UI)
Write-Host "Loading palette data from device..." -ForegroundColor Yellow
$maxPage = 20  # reasonable upper limit
$totalPalettes = 0
for ($page = 0; $page -lt $maxPage; $page++) {
  try {
    $palData = Invoke-RestMethod -UseBasicParsing -Uri ($Base.TrimEnd('/') + "/json/palx?page=$page") -TimeoutSec 5
    if (-not $palData -or -not $palData.p) { break }  # No more pages
    
    # Merge palette data from this page
    foreach ($palId in $palData.p.PSObject.Properties.Name) {
      $devicePaletteData[[int]$palId] = $palData.p.$palId
      $totalPalettes++
    }
  } catch {
    break
  }
}
Write-Host "Loaded palette data for $totalPalettes palettes" -ForegroundColor Green

# Debug palette data loading
if ($DebugNames) {
  Write-Host "\nDevice palette data summary:"
  Write-Host "  Total palettes with color data: $($devicePaletteData.Count)"
  Write-Host "  Palette data coverage:"
  for ($i = 0; $i -lt [Math]::Min($devicePalNames.Count, 20); $i++) {
    $hasData = $devicePaletteData.ContainsKey($i)
    $dataType = if ($hasData) { 
      $data = $devicePaletteData[$i]
      if ($data -is [array] -and $data[0] -is [array] -and $data[0].Count -eq 4) { "gradient stops" }
      elseif ($data -is [array] -and $data[0] -eq 'r') { "random" }
      elseif ($data -is [array] -and $data[0] -like 'c*') { "color reference" }
      else { "unknown format" }
    } else { "no data" }
    Write-Host "    #$i $($devicePalNames[$i]): $dataType"
  }
}


# =============== WLED Palette Data Converter ===============
# Convert WLED device palette data to RGB triples (like WLED web UI genPalPrevCss)
function Convert-WledPaletteData($palData, $segColors) {
  if (-not $palData) { return $null }
  
  $colors = @()
  
  # Handle different palette data formats from /json/palx
  foreach ($entry in $palData) {
    if ($entry -is [array] -and $entry.Count -eq 4) {
      # Gradient stop format: [position, r, g, b]
      $r = [int]$entry[1]
      $g = [int]$entry[2] 
      $b = [int]$entry[3]
      $pos = [int]$entry[0]  # 0-255 position
      $colors += ,@($pos, $r, $g, $b)
    }
    elseif ($entry -eq 'r') {
      # Random color - generate a random RGB
      $r = Get-Random -Maximum 256
      $g = Get-Random -Maximum 256
      $b = Get-Random -Maximum 256
      $colors += ,@(0, $r, $g, $b)  # Use position 0 for now
    }
    elseif ($entry -like 'c*') {
      # Color reference (c1, c2, c3) - use segment colors
      $colorNum = [int]($entry.Substring(1)) - 1
      if ($segColors -and $colorNum -ge 0 -and $colorNum -lt $segColors.Count) {
        $segColor = $segColors[$colorNum]
        $colors += ,@(0, [int]$segColor[0], [int]$segColor[1], [int]$segColor[2])
      } else {
        # Fallback to white if segment color not available
        $colors += ,@(0, 255, 255, 255)
      }
    }
  }
  
  if ($colors.Count -eq 0) { return $null }
  
  # If we have gradient stops, interpolate to 16 colors
  if ($colors[0].Count -eq 4) {
    $stops = @()
    foreach ($c in $colors) {
      $stops += ,[pscustomobject]@{pos=$c[0]; r=$c[1]; g=$c[2]; b=$c[3]}
    }
    $stops = $stops | Sort-Object pos
    return (Sample-Stops $stops 16)
  } else {
    # Just return the colors as-is (for single color or color reference palettes)
    $result = @()
    for ($i = 0; $i -lt 16; $i++) {
      $colorIdx = $i % $colors.Count
      $result += ,@($colors[$colorIdx][1], $colors[$colorIdx][2], $colors[$colorIdx][3])
    }
    return $result
  }
}

# =============== Custom palette loader ===============
function Load-CustomPalette([string]$baseUrl, [int]$n) {
  $url = ($baseUrl.TrimEnd('/') + "/palette{0}.json" -f $n)
  try {
    $j = Get-Json $url
    if ($j -and $j.palette) {
      $stops = Parse-PaletteStops $j.palette
      if ($stops.Count -ge 2) { return (Sample-Stops $stops 16) }
    }
  } catch { }
  return $null
}

# =============== Main loop ===============
while ($true) {
  try {
    $baseTrim = $Base.TrimEnd('/')
    $j = Get-Json "$baseTrim/json"
    $state    = $j.state
    $effects  = if ($j.PSObject.Properties.Name -contains 'effects')  { $j.effects }  else { @() }
    $palNames = if ($j.PSObject.Properties.Name -contains 'palettes') { $j.palettes } else { $devicePalNames }

    # selected segment
    $seg = $null
    if ($state -and $state.seg) {
      foreach ($s in $state.seg) { if ($s.sel) { $seg = $s; break } }
      if (-not $seg -and $state.seg.Count -gt 0) { $seg = $state.seg[0] }
    }

    # indices
    $fxIdx  = 0; if ($seg -and $seg.PSObject.Properties.Name -contains 'fx')  { $fxIdx  = [int]$seg.fx }  elseif ($state.PSObject.Properties.Name -contains 'fx')  { $fxIdx  = [int]$state.fx }
    $palIdx = 0; if ($seg -and $seg.PSObject.Properties.Name -contains 'pal') { $palIdx = [int]$seg.pal } elseif ($state.PSObject.Properties.Name -contains 'pal') { $palIdx = [int]$state.pal }

    # names
    $fxName  = "(unknown effect)";  if ($effects -and $fxIdx -ge 0 -and $fxIdx -lt $effects.Count) { $fxName  = [string]$effects[$fxIdx] }
    $palName = "(unknown palette)"; if ($palNames -and $palIdx -ge 0 -and $palIdx -lt $palNames.Count) { $palName = [string]$palNames[$palIdx] }

    # segment colors
    $segColors = @()
    if ($seg -and $seg.col) { foreach ($c in $seg.col) { if ($c -and $c.Count -ge 3) { $segColors += ,@([int]$c[0],[int]$c[1],[int]$c[2]) } } }

    # effect metadata
    $paletteUsed = Effect-UsesPaletteFx $fxIdx
    $colorLabels = Effect-ColorLabels   $fxIdx

    # resolve palette swatch from device data
    $palSwatch = $null
    $dynNote   = ""

    if (IsCustomPaletteName $palName) {
      if ($palName -match 'Custom\s+(\d+)') {
        $n = [int]$Matches[1]
        $palSwatch = Load-CustomPalette $baseTrim $n
        if (-not $palSwatch) { $dynNote = "(custom file missing or invalid)" }
      }
    } elseif (IsDynamicPaletteName $palName) {
      $refNote = New-Object System.Management.Automation.PSReference ('')
      $palSwatch = Try-DynamicPalette $palName $segColors ([ref]$refNote)
      if ($refNote.Value -ne '') { $dynNote = "(" + $refNote.Value + ")" }
    } else {
      # Get palette data directly from device like WLED web UI
      if ($devicePaletteData.ContainsKey($palIdx)) {
        $palData = $devicePaletteData[$palIdx]
        $palSwatch = Convert-WledPaletteData $palData $segColors
      } else {
        $dynNote = "(no device data for palette #$palIdx)"
      }
    }

    # -------- Output --------
    Clear-Host
    Write-Host ("WLED @ {0}" -f $Base)
    Write-Host ("Effect : {0} (#{1})" -f $fxName, $fxIdx)

    $paletteNote = "likely used"
    if (-not $paletteUsed) { $paletteNote = "ignored or irrelevant" }
    if ($dynNote -ne '') { $paletteNote = "$paletteNote $dynNote" }
    Write-Host ("Palette: {0} (#{1}) - {2}" -f $palName, $palIdx, $paletteNote)

    if ($colorLabels.Count -gt 0 -and $segColors.Count -gt 0) {
      $showCount  = [Math]::Min($colorLabels.Count, $segColors.Count)
      $showColors = $segColors[0..($showCount-1)]
      Write-Host ("Segment colors ({0}):" -f ($colorLabels -join ', '))
      Show-ColorBar -RgbTriples $showColors -Width 6
    } else {
      Write-Host "Segment colors: (none for this effect)"
    }

    if ($palSwatch -and $paletteUsed) {
      Show-ColorBar -RgbTriples $palSwatch -Label ("Palette swatch: {0}" -f $palName) -Width 4
    } elseif (-not $paletteUsed) {
      Write-Host "Palette swatch: (effect does not use a palette)"
    } else {
      Write-Host "Palette swatch: (no data)"
    }

  } catch {
    Write-Host ("Error: {0}" -f $_.Exception.Message) -ForegroundColor Red
  }

  Start-Sleep -Seconds $IntervalSec
}
