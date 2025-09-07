param(
  [string]$Base    = "http://192.168.4.42",
  [string]$Version = "v0.14.4",
  [string]$Out     = ".\wled-palettes-$($Version).json"
)

# Where the palettes live for a given WLED tag
$rawUrl = "https://raw.githubusercontent.com/wled/WLED/$Version/wled00/palettes.h"
Write-Host "[WLED] Fetching palettes from $rawUrl"

try {
  $resp = Invoke-WebRequest -UseBasicParsing -Uri $rawUrl -ErrorAction Stop
  $text = $resp.Content
} catch {
  Write-Error "Failed to download palettes.h: $($_.Exception.Message)"
  exit 1
}

# Parse 'const byte <name>_gp[] PROGMEM = { ... };'
$rx = [regex]'const\s+byte\s+([A-Za-z0-9_]+)_gp\[\]\s+PROGMEM\s*=\s*\{([^}]*)\};'
$matches = $rx.Matches($text)

if ($matches.Count -eq 0) {
  Write-Error "No gradient palettes found in palettes.h. WLED structure may have changed."
  exit 1
}

function Convert-ToStops([string]$numbers) {
  $nums = @()
  foreach($n in ($numbers -split '[,\s]+' | Where-Object { $_ -ne '' })) {
    $v = 0; if ([int]::TryParse($n, [ref]$v)) { $nums += $v }
  }
  $stops = @()
  for ($i=0; $i -le $nums.Count-4; $i+=4) {
    $pos = [int]$nums[$i]
    $r   = [int]$nums[$i+1]
    $g   = [int]$nums[$i+2]
    $b   = [int]$nums[$i+3]
    $hex = ('#{0:X2}{1:X2}{2:X2}' -f $r,$g,$b)
    $stops += [pscustomobject]@{ pos=$pos; r=$r; g=$g; b=$b; hex=$hex }
  }
  $stops | Sort-Object pos
}

function Get-ColorAt([object[]]$stops, [int]$pos) {
  if ($stops.Count -eq 0) { return @(0,0,0) }
  $stops = $stops | Sort-Object pos
  if ($pos -le $stops[0].pos) { return @([byte]$stops[0].r,[byte]$stops[0].g,[byte]$stops[0].b) }
  for ($i=0; $i -lt $stops.Count-1; $i++) {
    $a=$stops[$i]; $b=$stops[$i+1]
    if ($pos -le $b.pos) {
      $range = [double]($b.pos - $a.pos)
      if ($range -le 0) { return @([byte]$b.r,[byte]$b.g,[byte]$b.b) }
      $t = ([double]($pos - $a.pos)) / $range
      $ri = [byte][math]::Round($a.r + ($b.r - $a.r) * $t)
      $gi = [byte][math]::Round($a.g + ($b.g - $a.g) * $t)
      $bi = [byte][math]::Round($a.b + ($b.b - $a.b) * $t)
      return @($ri,$gi,$bi)
    }
  }
  $last = $stops[-1]
  return @([byte]$last.r,[byte]$last.g,[byte]$last.b)
}

$palettes = @()
foreach ($m in $matches) {
  $cid    = $m.Groups[1].Value
  $values = $m.Groups[2].Value
  $stops  = Convert-ToStops $values

  # Sample 16 evenly spaced colors across 0..255
  $sample = @()
  $step = [math]::Round(255 / 15)
  for ($p=0; $p -le 255; $p += $step) {
    $c = Get-ColorAt $stops $p
    $sample += ,@($c[0],$c[1],$c[2])
  }

  $palettes += [pscustomobject]@{
    cIdent        = $cid + "_gp"
    friendlyGuess = ($cid -replace '_gp$','' -replace '_',' ')
    stops         = $stops
    sample16      = $sample
  }
}

# Optionally collect the device's palette names/order
$devicePalettes = $null
if ($Base) {
  try {
    $devicePalettes = Invoke-RestMethod -UseBasicParsing -Uri ($Base.TrimEnd('/') + '/json/pal') -ErrorAction Stop
  } catch {
    Write-Host "[WLED] Warning: couldn't fetch /json/pal from $Base : $($_.Exception.Message)" -ForegroundColor Yellow
  }
}

$outObj = [pscustomobject]@{
  repoVersion        = $Version
  sourceUrl          = $rawUrl
  fetchedAtUtc       = (Get-Date).ToUniversalTime().ToString("s") + "Z"
  device             = $Base
  palettes           = $palettes
  devicePaletteNames = $devicePalettes
}

# Resolve path and write JSON
try {
  $outPath = $Out
  $outObj | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $outPath -Encoding UTF8
  Write-Host "[WLED] Wrote palette catalog to $outPath"
} catch {
  Write-Error "Failed writing $Out : $($_.Exception.Message)"
  exit 1
}
