param([string]$Path, [switch]$SelfTest)
$ErrorActionPreference = 'Stop'

function Measure-Recording([System.IO.TextReader]$Reader) {
  $invariant = [Globalization.CultureInfo]::InvariantCulture
  $rows = 0L
  $rails = 0L
  $gaps = 0L
  $missingSequence = 0L
  $previousSeq = 0L
  $previousUs = 0L
  $firstUs = 0L
  $minInterval = [long]::MaxValue
  $maxInterval = 0L
  $haveColumns = $false
  $ended = $false
  $stopReason = ''
  $issues = New-Object 'System.Collections.Generic.List[string]'
  while ($null -ne ($line = $Reader.ReadLine())) {
    if ($line.StartsWith('# END,')) {
      if ($ended) { $issues.Add('Duplicate END footer.'); break }
      $ended = $true
      if ($line -match '(?:^|,)rows=(\d+)(?:,|$)') {
        if ([long]$Matches[1] -ne $rows) { $issues.Add('Footer row count mismatch.') }
      } else { $issues.Add('Footer has no row count.') }
      if ($line -match '(?:^|,)reason=([^,]+)') { $stopReason = $Matches[1] }
      continue
    }
    if ($line.StartsWith('#')) { continue }
    if ($line -eq 'utc,read_us,seq,raw,force_ok,force_N,adc_rail,gap_flag,interval_us') {
      if ($haveColumns -or $rows) { $issues.Add('Duplicate CSV header.'); break }
      $haveColumns = $true
      continue
    }
    if (-not $haveColumns -or $ended) { $issues.Add('Unexpected data before header or after footer.'); break }
    $c = $line.Split(',')
    if ($c.Count -ne 9) { $issues.Add('Incomplete or malformed row (possible unclean shutdown).'); break }
    try {
      $utc = [DateTimeOffset]::ParseExact($c[0], "yyyy-MM-dd'T'HH:mm:ss.ffffff'Z'", $invariant,
              [Globalization.DateTimeStyles]::AssumeUniversal)
      $us = [long]::Parse($c[1], $invariant)
      $seq = [long]::Parse($c[2], $invariant)
      $raw = [long]::Parse($c[3], $invariant)
      if ($c[4] -notin @('0','1') -or $c[6] -notin @('0','1') -or $c[7] -notin @('0','1')) { throw 'Invalid flags' }
      $rail = [int]$c[6]
      $gap = [int]$c[7]
      $interval = [long]::Parse($c[8], $invariant)
      if ($us -lt 0 -or $seq -lt 0 -or $seq -gt [uint32]::MaxValue -or $raw -lt -8388608 -or $raw -gt 8388607) { throw 'Invalid range' }
      if ($rail -ne [int]($raw -eq -8388608 -or $raw -eq 8388607)) { throw 'Rail flag mismatch' }
      if ($c[4] -eq '0' -and $c[5] -ne '') { throw 'Unavailable force must be empty' }
      if ($c[4] -eq '1') {
        $force = [double]::Parse($c[5], $invariant)
        if ($rail -or [double]::IsNaN($force) -or [double]::IsInfinity($force)) { throw 'Invalid valid-force field' }
      }
      if ($gap -ne [int]($interval -gt 6250)) { throw 'Gap flag mismatch' }
      if ($rows) {
        if ($us -le $previousUs) { throw 'Non-increasing read_us' }
        $seqDelta = ($seq - $previousSeq + 4294967296L) % 4294967296L
        if ($seqDelta -eq 0) { throw 'Repeated sequence' }
        $missingSequence += $seqDelta - 1
        if ($seqDelta -eq 1 -and ($us - $previousUs) -ne $interval) { throw 'Interval does not match timestamps' }
        if ($interval -lt $minInterval) { $minInterval = $interval }
        if ($interval -gt $maxInterval) { $maxInterval = $interval }
      } else { $firstUs = $us }
      $previousUs = $us
      $previousSeq = $seq
      $rails += $rail
      $gaps += $gap
      $rows++
    } catch { $issues.Add("Bad row after $rows valid rows: $($_.Exception.Message)"); break }
  }
  if (-not $haveColumns) { $issues.Add('CSV header missing.') }
  if (-not $ended) { $issues.Add('END footer missing: file may be incomplete; valid prefix only.') }
  if (-not $rows) { $issues.Add('No measurement rows.') }
  $observed = $null
  if ($rows -gt 1 -and $previousUs -gt $firstUs) { $observed = ($rows - 1) * 1000000.0 / ($previousUs - $firstUs) }
  [pscustomobject]@{
    ValidRows = $rows
    HasEndFooter = $ended
    StopReason = $stopReason
    ObservedReadHz = $observed
    MinReadIntervalUs = $(if ($rows -gt 1) { $minInterval } else { $null })
    MaxReadIntervalUs = $(if ($rows -gt 1) { $maxInterval } else { $null })
    RailRows = $rails
    SuspectedGapRows = $gaps
    MissingSequenceRows = $missingSequence
    IntegrityIssues = @($issues.ToArray())
    ExactAdcLossCountKnown = $false
    HardwareAccuracyVerified = $false
  }
}

if ($SelfTest) {
  $header = "utc,read_us,seq,raw,force_ok,force_N,adc_rail,gap_flag,interval_us`n"
  $row1 = "2026-09-27T00:00:00.100000Z,100000,1,100,0,,0,0,3125`n"
  $row2 = "2026-09-27T00:00:00.103125Z,103125,2,110,1,9.80665,0,0,3125`n"
  $end = "# END,reason=USER_STOP,rows=2`n"
  $r = Measure-Recording ([IO.StringReader]::new($header + $row1 + $row2 + $end))
  if ($r.IntegrityIssues.Count -or $r.ValidRows -ne 2 -or $r.ObservedReadHz -ne 320) { throw 'Valid fixture failed' }
  $badForce = $row1.Replace('100,0,,0,0', '100,0,0,0,0')
  $r = Measure-Recording ([IO.StringReader]::new($header + $badForce))
  if (-not $r.IntegrityIssues.Count -or $r.ValidRows) { throw 'Fake zero was accepted' }
  $r = Measure-Recording ([IO.StringReader]::new($header + $row1))
  if ($r.HasEndFooter -or -not $r.IntegrityIssues.Count -or $r.ValidRows -ne 1) { throw 'Unclean close not detected' }
  $r = Measure-Recording ([IO.StringReader]::new($header + $row1 + $row1 + $end))
  if (-not $r.IntegrityIssues.Count) { throw 'Duplicate timestamp not detected' }
  $r = Measure-Recording ([IO.StringReader]::new($header + $row1 + $row2 + $end.Replace('rows=2','rows=3')))
  if (-not $r.IntegrityIssues.Count) { throw 'Row count mismatch not detected' }
  $gapRow = "2026-09-27T00:00:00.110000Z,110000,3,110,0,,0,1,10000`n"
  $r = Measure-Recording ([IO.StringReader]::new($header + $row1 + $gapRow + $end))
  if ($r.SuspectedGapRows -ne 1 -or $r.MissingSequenceRows -ne 1) { throw 'Gap diagnostics failed' }
  $r = Measure-Recording ([IO.StringReader]::new($header + $row1 + $row2.Replace(',110,1,9.80665,0,0',',8388607,0,,1,0') + $end))
  if ($r.IntegrityIssues.Count -or $r.RailRows -ne 1) { throw 'Rail fixture failed' }
  $r = Measure-Recording ([IO.StringReader]::new($header + $row1 + $row2.Replace(',110,1,9.80665',',110,1,NaN') + $end))
  if (-not $r.IntegrityIssues.Count) { throw 'NaN accepted' }
  Write-Output 'PASS: 8 recording checker fixtures (synthetic data only).'
} else {
  if (-not $Path) { throw 'Use -Path RF0000.CSV or -SelfTest.' }
  $resolved = (Resolve-Path -LiteralPath $Path).Path
  $reader = [IO.StreamReader]::new($resolved, [Text.Encoding]::UTF8)
  try { Measure-Recording $reader | ConvertTo-Json -Depth 4 } finally { $reader.Dispose() }
}
