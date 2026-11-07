# SPDX-License-Identifier: GPL-2.0-or-later
# Creates objects/<Id>.kobj.json and objects/<Id>.svg from a template and installs them.
# See docs/making-objects.md.
param(
    [Parameter(Mandatory = $true)][string]$Id,
    [Parameter(Mandatory = $true)][string]$Name,
    [ValidateSet('terrain', 'obstacle', 'hazard', 'mechanism', 'decoration', 'course')]
    [string]$Category = 'terrain',
    [string]$Help = '',
    [string]$ObjectsDirectory = (Join-Path $PSScriptRoot '..\objects')
)
$ErrorActionPreference = 'Stop'
if ($Id -notmatch '^[a-z][a-z0-9_]{0,31}$' -or $Id -in 'ball', 'course', 'hole') {
    throw "Id must be lower case letters, digits and _, start with a letter, and not be ball, course or hole"
}
if ($Name.Length -lt 1 -or $Name.Length -gt 64) { throw 'Name must be 1 to 64 characters' }
$directory = (Resolve-Path $ObjectsDirectory).Path
$definition = Join-Path $directory "$Id.kobj.json"
$sprite = Join-Path $directory "$Id.svg"
foreach ($path in $definition, $sprite) {
    if (Test-Path $path) { throw "$path already exists" }
}

$decoration = $Category -eq 'decoration'
$component = if ($decoration) { '{ "type": "decor" }' } else { '{ "type": "surface", "friction": "$friction", "priority": 1 }' }
$properties = if ($decoration) { '' } else {
    @'

  "properties": {
    "friction": { "type": "number", "label": "Friction", "min": 0.05, "max": 10, "step": 0.05, "default": 2 }
  },
'@
}
$gameplay = if ($decoration) { "`n  `"affectsGameplay`": false," } else { '' }
$helpText = if ($Help) { $Help } else { "Describe what $Name does." }
$json = @"
{
  "kolfObject": 1,
  "id": "$Id",
  "name": "$Name",
  "category": "$Category",$gameplay
  "help": "$helpText",
  "shape": { "type": "rect", "size": [60, 40], "resizable": true, "rotatable": true },
  "visual": { "sprite": "$Id.svg", "layer": "ground" },$properties
  "components": [
    $component
  ]
}
"@
$svg = @'
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 60 40" preserveAspectRatio="none">
  <rect x="0.5" y="0.5" width="59" height="39" rx="4" fill="#c9a66b" stroke="#8a6a3a"/>
</svg>
'@
[System.IO.File]::WriteAllText($definition, $json.Replace("`r`n", "`n") + "`n")
[System.IO.File]::WriteAllText($sprite, $svg.Replace("`r`n", "`n") + "`n")

$cmake = Join-Path $directory 'CMakeLists.txt'
$text = [System.IO.File]::ReadAllText($cmake)
$text = $text.Replace("    kobj.schema.json`n", "    $Id.kobj.json`n    $Id.svg`n    kobj.schema.json`n")
[System.IO.File]::WriteAllText($cmake, $text)

"Created $definition"
"Created $sprite"
"Added both to $cmake"
"Try it: `$env:KOLF_OBJECT_DIRS = '$directory'; then start Kolf and open the course editor."
