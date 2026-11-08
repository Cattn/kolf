# SPDX-License-Identifier: GPL-2.0-or-later
# Writes a one-hole course with 256 gameplay objects for profiling:
#   .\tests\replay\make-stress-course.ps1 -Output $env:TEMP\stress\Stress
#   $env:KOLF_REPLAY_PROFILE = '1'; .\tests\replay\run-replay.ps1 ... (see tests/replay/README.md)
param([Parameter(Mandatory = $true)][string]$Output)
$ErrorActionPreference = 'Stop'
$mix = @(
    @{ Type = 'bumper'; Count = 64; Keys = @() },
    @{ Type = 'floater'; Count = 32; Keys = @('speed=4', 'width=24', 'height=16') },
    @{ Type = 'windmill'; Count = 16; Keys = @('speed=5', 'width=24', 'height=16') },
    @{ Type = 'slope'; Count = 32; Keys = @('width=22', 'height=22', 'gradient=Vertical', 'grade=2') },
    @{ Type = 'sand'; Count = 16; Keys = @('width=20', 'height=20') },
    @{ Type = 'puddle'; Count = 16; Keys = @('width=20', 'height=20') },
    @{ Type = 'ice'; Count = 32; Keys = @('width=22', 'height=22', 'rotation=0', 'slipperiness=0.25') },
    @{ Type = 'plate'; Count = 24; Keys = @('channel=a', 'trigger=enter') },
    @{ Type = 'gate'; Count = 24; Keys = @('width=20', 'height=6', 'rotation=0', 'channel=a', 'action=toggle', 'mode=latch') }
)
$lines = [System.Collections.Generic.List[string]]::new()
$lines.AddRange([string[]]@('[0-course@-50,-50]', 'Name=Stress', 'author=Kolf tests', '', '[1-hole@-50,-50|0]', 'par=10', 'maxstrokes=10', 'borderWalls=true', '',
    '[1-ball@20,200|0]', 'dummy=1', '', '[1-cup@385,200|1]', 'dummy=1', ''))
$index = 0
$id = 2
foreach ($entry in $mix) {
    for ($i = 0; $i -lt $entry.Count; ++$i) {
        $x = 40 + ($index % 16) * 22
        $y = 30 + [math]::Floor($index / 16) * 22
        $lines.Add("[1-$($entry.Type)@$x,$y|$id]")
        if ($entry.Keys.Count) { $lines.AddRange([string[]]$entry.Keys) } else { $lines.Add('dummy=1') }
        $lines.Add('')
        ++$index; ++$id
    }
}
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $Output) | Out-Null
[System.IO.File]::WriteAllText($Output, ($lines -join "`n"))
"Wrote $index gameplay objects to $Output"
