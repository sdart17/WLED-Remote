param(
  [string]$Base = 'http://192.168.4.42',
  [int]$IntervalSec = 5
)

# Add effect names here that you know ignore palettes (exact names as WLED reports)
$NoPaletteEffects = @('Solid')

function Get-Json {
  param([Parameter(Mandatory)][string]$Url)
  try {
    Invoke-RestMethod -Method Get -Uri $Url -TimeoutSec 8 -ErrorAction Stop
  } catch {
    throw "Request failed: $Url`n$($_.Exception.Message)"
  }
}

function Get-Prop {
  param($Object, [string]$Name, $Default = $null)
  if ($null -ne $Object -and $Object.PSObject.Properties.Name -contains $Name -and $null -ne $Object.$Name) {
    return $Object.$Name
  }
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

function Clamp01($x) {
  if ($x -lt 0) { return 0 }
  elseif ($x -gt 255) { return 255 }
  else { return [int]$x }
}

function HexRGB($r,$g,$b) {
  return ('#{0:X2}{1:X2}{2:X2}' -f (Clamp01 $r),(Clamp01 $g),(Clamp01 $b))
}

# Very simple color-namer for console readability
function NameRGB($r,$g,$b) {
  $r=[int]$r; $g=[int]$g; $b=[int]$b
  if ($r -eq 0 -and $g -eq 0 -and $b -eq 0) { return 'black' }
  if ($r -eq 255 -and $g -eq 255 -and $b -eq 255) { return 'white' }
  if ($r -eq $g -and $g -eq $b) {
    if ($r -lt 64)      { return 'very dark gray' }
    elseif ($r -lt 128) { return 'dark gray' }
    elseif ($r -lt 192) { return 'gray' }
    else                { return 'light gray' }
  }
  $max = [Math]::Max($r,[Math]::Max($g,$b))
  if ($max -eq $r) {
    if ($g -ge 0.6*$r -and $b -le 0.4*$r) { return 'yellow/orange' }
    if ($b -ge 0.6*$r -and $g -le 0.4*$r) { return 'magenta/pink' }
    return 'red-ish'
  } elseif ($max -eq $g) {
    if ($r -ge 0.6*$g -and $b -le 0.4*$g) { return 'yellow/lime' }
    if ($b -ge 0.6*$g -and $r -le 0.4*$g) { return 'cyan/teal' }
    return 'green-ish'
  } else {
    if ($r -ge 0.6*$b -and $g -le 0.4*$b) { return 'violet/purple' }
    if ($g -ge 0.6*$b -and $r -le 0.4*$b) { return 'cyan/azure' }
    return 'blue-ish'
  }
}

# Normalize a segment color entry [r,g,b] or [r,g,b,w] to RGB
function ToRGB($c) {
  if ($null -eq $c -or $c.Count -lt 3) { return @(0,0,0) }
  return @([int]$c[0],[int]$c[1],[int]$c[2])
}

# Build ordered list and a set of unique non-black colors from seg.col
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

while ($true) {
  try {
    # Fetch JSON
    $state    = Get-Json "$Base/json/state"
    $effects  = To-Array (Get-Json "$Base/json/effects")
    $palettes = To-Array (Get-Json "$Base/json/palettes")
    try { $presets = Get-Json "$Base/presets.json" } catch { $presets = $null }

    # Select main segment
    $ms = [int](Get-Prop $state 'mainseg' 0)
    $seg = $null
    if ($state -and $state.seg) {
      if ($state.seg.Count -gt $ms) { $seg = $state.seg[$ms] }
      elseif ($state.seg.Count -gt 0) { $seg = $state.seg[0] }
    }

    # Indices
    $fxIdx  = [int](Get-Prop $seg 'fx' 0)
    $palIdx = [int](Get-Prop $seg 'pal' 0)
    $psId   = [int](Get-Prop $state 'ps' 0)
    $plId   = [int](Get-Prop $state 'pl' 0)  # playlist id if present

    # Names
    $fxName = '(unknown)'; if ($effects.Count -gt $fxIdx -and $fxIdx -ge 0) { $fxName = $effects[$fxIdx] }
    $palName = '(unknown)'; if ($palettes.Count -gt $palIdx -and $palIdx -ge 0) { $palName = $palettes[$palIdx] }

    # Resolve current preset (child) name
    $presetName = '(unsaved / none)'
    if ($presets -and $psId -gt 0) {
      $psKey = "$psId"
      if ($presets.PSObject.Properties.Name -contains $psKey -and $presets.$psKey.n) {
        $presetName = $presets.$psKey.n
      } else {
        $presetName = "(preset $psId)"
      }
    }

    # Resolve playlist name (if running)
    $playlistName = $null
    if ($presets) {
      if ($plId -gt 0) {
        $plKey = "$plId"
        if ($presets.PSObject.Properties.Name -contains $plKey -and $presets.$plKey.n) {
          $playlistName = $presets.$plKey.n
        }
      } else {
        # Infer parent playlist if state.pl not present
        foreach ($p in $presets.PSObject.Properties) {
          $val = $p.Value
          if ($val -and $val.PSObject.Properties.Name -contains 'playlist') {
            $pls = $val.playlist
            if ($pls -and $pls.ps -and ($pls.ps -contains $psId)) {
              $playlistName = $val.n
              break
            }
          }
        }
      }
    }

    # Colors and palette usage
    $colors = ExtractSegmentColors $seg
    $uniqCount = $colors.unique.Count

    $paletteUsed = $true
    if ($NoPaletteEffects -contains $fxName) { $paletteUsed = $false }
    if ($uniqCount -eq 0) { $paletteUsed = $false } # no colors -> palette irrelevant

    $paletteNote = 'likely used'
    if (-not $paletteUsed) { $paletteNote = 'ignored or irrelevant' }

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
    }

    Write-Host ("Unique non-black colors: {0}" -f $uniqCount)
    if ($uniqCount -gt 0) {
      $rgbTriplets = @()
      foreach ($u in $colors.unique) {
        $rgbTriplets += ("[{0},{1},{2}]" -f $u.r,$u.g,$u.b)
      }
      Write-Host ("Unique RGB array:")
      Write-Host ("  [" + ($rgbTriplets -join ", ") + "]")
    }

    Write-Host ("Segment: {0}" -f $ms)

  } catch {
    Write-Host ("Error: {0}" -f $_.Exception.Message) -ForegroundColor Red
  }

  Start-Sleep -Seconds $IntervalSec
}
