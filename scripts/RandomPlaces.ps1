<#
.SYNOPSIS
  Random US road points by place type, for scripts\launch-review.ps1 -RandomUS (dot-source this file).

.DESCRIPTION
  The game builds any place in the US as you drive, so a review must not only look at the hero
  roads. Each place type has three areas where roads are common; a seeded random number picks an
  area and a point in it, and the game puts the car on the nearest road (-SimCruiseStart without a
  road name). The same seed gives the same points, so a failure repeats.
#>

<#
  The seeded draw from data\test-places.json that scripts\preflight.ps1 -Roads random makes (a
  System.Random partial shuffle of the keys, the same in Windows PowerShell 5.1 and PowerShell 7),
  so a seed picks the same places in every script. Each place gets its own local solar noon on the
  -Utc date (12 h - lon/15) as .Utc. Empty when the file is not there.
#>
function Get-TestPlaceDraw([string]$Root, [int]$Seed, [int]$Count, [string]$Utc = '2026-11-12T17:00:00Z') {
    $File = Join-Path $Root 'data\test-places.json'
    if (-not (Test-Path $File)) { return @() }
    $Places = @((Get-Content $File -Raw | ConvertFrom-Json))
    $Rng = [System.Random]::new($Seed)
    $Keys = @($Places | ForEach-Object { $_.key })
    $Take = [Math]::Max(1, [Math]::Min($Count, $Keys.Count))
    for ($i = 0; $i -lt $Take; $i++) { $j = $Rng.Next($i, $Keys.Count); $t = $Keys[$i]; $Keys[$i] = $Keys[$j]; $Keys[$j] = $t }
    $Day = ([datetime]::Parse($Utc, [Globalization.CultureInfo]::InvariantCulture, [Globalization.DateTimeStyles]::AdjustToUniversal)).Date
    return @($Places | Where-Object { $Keys[0..($Take - 1)] -contains $_.key } | ForEach-Object {
        $_ | Add-Member -NotePropertyName Utc -NotePropertyValue ($Day.AddHours(12.0 - [double]$_.lon / 15.0).ToString('yyyy-MM-ddTHH:mm:ssZ')) -PassThru -Force })
}

# Areas: south, north, west, east (degrees).
$script:RandomUSAreas = [ordered]@{
    'desert'     = @(@(32.3, 33.8, -114.5, -111.0), @(35.5, 36.6, -116.0, -114.6), @(34.5, 35.3, -108.5, -106.5))
    'mountains'  = @(@(38.6, 40.0, -107.0, -105.5), @(35.2, 36.0, -84.0, -82.5), @(45.5, 46.8, -114.0, -112.0))
    'plains'     = @(@(41.5, 42.8, -95.5, -92.0), @(37.8, 39.0, -100.0, -97.0), @(40.3, 41.5, -99.5, -96.8))
    'forest'     = @(@(44.0, 45.3, -71.5, -69.5), @(44.5, 46.5, -123.5, -122.3), @(46.5, 47.5, -94.5, -92.5))
    'coast'      = @(@(35.0, 37.5, -122.4, -120.6), @(27.0, 28.6, -82.8, -82.3), @(39.2, 40.2, -74.3, -74.0))
    'suburb'     = @(@(33.85, 34.1, -84.6, -84.15), @(32.95, 33.2, -96.95, -96.6), @(41.75, 42.0, -88.2, -87.95))
    'small-town' = @(@(40.0, 40.6, -83.5, -82.5), @(44.0, 44.6, -73.0, -72.4), @(29.9, 30.4, -99.2, -98.5))
    'city'       = @(@(40.70, 40.80, -74.02, -73.93), @(41.86, 41.92, -87.65, -87.61), @(37.74, 37.80, -122.47, -122.39))
}

<#
  N places. When data\test-places.json (the team's list of ordinary US places) is there, a seeded
  draw from it, one place type at a time; else the types in a seeded order (each once before any
  repeats), an area of that type and a point in it.
#>
function Get-RandomUSPlaces([int]$Count, [int]$Seed, [string]$Root = (Split-Path -Parent $PSScriptRoot)) {
    $Rng = New-Object System.Random($Seed)
    # The team's place list first, with the same seeded draw as preflight.ps1 -Roads random.
    $Draw = @(Get-TestPlaceDraw -Root $Root -Seed $Seed -Count $Count)
    if ($Draw.Count) {
        $I = 0
        return @($Draw | ForEach-Object { [pscustomobject]@{ Index = $I++; Type = $_.kind; Lat = [double]$_.lat; Lon = [double]$_.lon; Key = $_.key; Street = ''; Utc = $_.Utc } })
    }
    $Types = @($script:RandomUSAreas.Keys)
    $Order = @()
    while ($Order.Count -lt $Count) { $Order += @($Types | Sort-Object { $Rng.Next() }) }
    $Places = @()
    for ($I = 0; $I -lt $Count; $I++) {
        $Type = $Order[$I]
        $Areas = $script:RandomUSAreas[$Type]
        $A = $Areas[$Rng.Next($Areas.Count)]
        $Lat = $A[0] + ($A[1] - $A[0]) * $Rng.NextDouble()
        $Lon = $A[2] + ($A[3] - $A[2]) * $Rng.NextDouble()
        $Places += [pscustomobject]@{ Index = $I; Type = $Type; Lat = [Math]::Round($Lat, 5); Lon = [Math]::Round($Lon, 5) }
    }
    return $Places
}

<#
  A geotagged photo from Wikimedia Commons near a point (licence and author kept), saved as
  photo.jpg with photo.txt. Commons API etiquette: one request at a time, a User-Agent that names
  the client, thumbnails rather than originals. Returns the attribution text or $null.
#>
function Save-CommonsPhoto([string]$RunDir, [double]$Lat, [double]$Lon, [int]$RadiusM = 10000) {
    $Ua = @{ 'User-Agent' = 'SimCruiseReview/1.0 (game test review; contact via the SimCruise project)' }
    try {
        $Geo = Invoke-RestMethod -Headers $Ua -TimeoutSec 20 -Uri ("https://commons.wikimedia.org/w/api.php?action=query&list=geosearch&gsnamespace=6&gslimit=10&format=json&gsradius={0}&gscoord={1}|{2}" -f $RadiusM, $Lat, $Lon)
        foreach ($Hit in @($Geo.query.geosearch)) {
            $Title = [uri]::EscapeDataString($Hit.title)
            $Info = Invoke-RestMethod -Headers $Ua -TimeoutSec 20 -Uri "https://commons.wikimedia.org/w/api.php?action=query&prop=imageinfo&iiprop=url|extmetadata|mime&iiurlwidth=960&format=json&titles=$Title"
            $Page = @($Info.query.pages.PSObject.Properties)[0].Value
            $Ii = @($Page.imageinfo)[0]
            if (-not $Ii -or $Ii.mime -notmatch 'jpeg|png') { continue }
            $Licence = $Ii.extmetadata.LicenseShortName.value
            if ($Licence -notmatch 'CC|Public domain|PD') { continue }
            $Artist = ($Ii.extmetadata.Artist.value -replace '<[^>]+>', '').Trim()
            Invoke-WebRequest -Headers $Ua -TimeoutSec 30 -Uri $Ii.thumburl -OutFile (Join-Path $RunDir 'photo.jpg') -UseBasicParsing | Out-Null
            $Text = "$($Hit.title) by $Artist, $Licence, Wikimedia Commons, $($Ii.descriptionurl) ($([int]$Hit.dist) m from the point)"
            $Text | Set-Content -Encoding utf8 (Join-Path $RunDir 'photo.txt')
            return $Text
        }
    } catch { return $null }
    return $null
}
