# Making objects

Most Kolf objects are two files: a definition (`<id>.kobj.json`) and a sprite
(`<id>.svg`). Kolf loads them at startup and the object then works in the
editor, in saved courses, offline, online, in undo and in both course
validators. Only behaviour that the components below cannot express needs C++.

## Quick start

```powershell
.\scripts\new-object.ps1 -Id mud -Name "Mud" -Category terrain
```

This writes `objects/mud.kobj.json` and `objects/mud.svg` from a template and
adds both to `objects/CMakeLists.txt`. Edit the two files, then try them
without rebuilding by pointing Kolf at the folder:

```powershell
$env:KOLF_OBJECT_DIRS = "$PWD\objects"
C:\CraftRoot\bin\kolf.exe
```

Open the course editor: the object is in the palette under its category.
If Kolf rejects the definition, the reason is written to the log as
`Object definition rejected: mud.kobj.json: components[0].friction: must be a
number from 0.05 to 20 or a $property` (on Windows set
`QT_FORCE_STDERR_LOGGING=1` to see it in a terminal).

When it works:

1. Build and install (`craft --compile --install --qmerge kolf`).
2. Regenerate the catalog: `kolf --dump-object-catalog protocol/object-catalog.json`.
   `objectregistrytest` fails until the committed catalog matches.
3. Commit the definition, the sprite, `objects/CMakeLists.txt` and the catalog.
4. The online server validates courses against the catalog, so courses with
   the new object only work online after the server is redeployed.

## Definition format

```json
{
  "kolfObject": 1,
  "id": "mud",
  "name": "Mud",
  "category": "terrain",
  "help": "Sticky ground that slows the ball.",
  "shape": { "type": "ellipse", "size": [60, 40], "resizable": true, "rotatable": false },
  "visual": { "sprite": "mud.svg", "layer": "ground" },
  "properties": {
    "stickiness": { "type": "number", "label": "Friction", "min": 1, "max": 10, "step": 0.5, "default": 4 }
  },
  "components": [
    { "type": "surface", "friction": "$stickiness", "priority": 1 }
  ]
}
```

| Key | Meaning |
| --- | --- |
| `kolfObject` | Always `1` |
| `id` | Lower case letters, digits and `_`, starting with a letter; also the group name in course files. `ball`, `course` and `hole` are reserved |
| `name`, `help` | Shown in the editor palette, tooltip and property panel |
| `category` | `terrain`, `obstacle`, `hazard`, `mechanism`, `decoration` or `course` (palette group) |
| `affectsGameplay` | `false` only for pure decoration (every component is `decor` or `sound`) |
| `shape` | `rect` or `ellipse`, default `size`, and whether the editor may resize or rotate it. Resizable shapes get `width`/`height` properties, rotatable ones `rotation` |
| `visual.sprite` | An `.svg` next to the definition. Stretched to the object's size |
| `visual.layer` | `ground` (under balls, like sand), `raised` (on top, carried by bridges) or `canopy` (above everything) |
| `properties` | Settings a course author can change in the editor; each is saved in the course |
| `components` | What the object does (below) |

`objects/kobj.schema.json` describes the same format for editors with JSON
Schema support.

### Properties

| Type | Options | Editor |
| --- | --- | --- |
| `number` | `min`, `max`, `step`, `default` | Spin box |
| `int` | `min`, `max`, `default` (whole numbers) | Spin box |
| `angle` | `default` (0–359) | Spin box in degrees |
| `bool` | `default` | Check box |
| `enum` | `values`, `default` | Combo box |
| `text` | `maxLength`, `default` | Line edit |

Every property needs a `label`. Values in course files are checked against
these ranges by the game and by the server.

### Components

| Component | Parameters | Effect |
| --- | --- | --- |
| `surface` | `friction` (0.05–20), `priority` (0–10, default 1) | Changes rolling friction while the ball's centre is on the object. The highest priority wins; different surfaces at the same priority multiply |
| `force` | `strength` (0–20), `direction` (degrees, default `$rotation`) | Pushes the ball every step, like a slope of that grade |
| `boost` | `impulse` (0–10), `direction`, `cooldown` (steps, default 30) | One kick each time the ball arrives |
| `hazard` | `outcome`: `water` | Like a puddle: a penalty stroke, then drop or rehit |
| `solid` | — | The ball bounces off the shape; switched off while a receiver has opened the object |
| `trigger` | `on`: `occupied`, `enter`, `exit` or `stop`; `channel` | Signals a channel when a ball is on it (level) or enters, leaves or stops on it (pulse) |
| `receiver` | `channel`, `action`: `open`, `close`, `toggle`, `start` or `stop`; `mode`: `latch` or `momentary` | Reacts to its channel. Open objects fade and lose their `solid` body |
| `motion` | `dx`, `dy`, `speed` (pixels per step), `autostart` | Moves back and forth along an offset |
| `sound` | `on`: `boost` or `hazard`; `key` | Plays a built-in sound |
| `decor` | — | Nothing; for decorations |

Any parameter can be bound to a property with `"$name"`. Bindings must match
the parameter's type: numbers to `number`/`int`/`angle`, choices to an `enum`
whose values are all allowed, channels to `text` or `enum`, flags to `bool`.
There are no expressions or scripts on purpose: everything stays checkable,
safe to share and identical for every player online.

### Sprites

SVG only, at most 256 KiB, with an `<svg>` root. Kolf refuses sprites with
scripts, `foreignObject`, audio or video, event handlers (`onload` and the
like), a DOCTYPE or entities, and any reference other than `#fragment` links
or embedded PNG/JPEG/GIF images of at most 1 MiB. Use
`preserveAspectRatio="none"` if the shape is resizable.

## Worked examples

### A surface: Ice

`objects/ice.kobj.json` is a resizable, rotatable rectangle with one surface
component whose friction is a property:

```json
"properties": {
  "slipperiness": { "type": "number", "label": "Friction", "min": 0.05, "max": 1, "step": 0.05, "default": 0.25 }
},
"components": [
  { "type": "surface", "friction": "$slipperiness", "priority": 1 }
]
```

A friction below 1 makes the ball roll further: on a test hole the same shot
rolls about eleven times longer over ice.

### A force field: Wind

```json
{
  "kolfObject": 1,
  "id": "wind",
  "name": "Wind",
  "category": "terrain",
  "help": "A steady wind that pushes the ball in the arrow's direction.",
  "shape": { "type": "rect", "size": [80, 80], "resizable": true, "rotatable": true },
  "visual": { "sprite": "wind.svg", "layer": "ground" },
  "properties": {
    "strength": { "type": "number", "label": "Strength", "min": 0.5, "max": 8, "step": 0.5, "default": 3 }
  },
  "components": [
    { "type": "force", "strength": "$strength" }
  ]
}
```

`direction` is left out, so it follows the object's rotation: rotate the wind
zone in the editor to change where it blows. Forces of different kinds add
up; two overlapping wind zones count once.

### A trigger and a receiver: Pressure Plate and Gate

The plate sends on a channel while a ball is on it (or on enter, exit or
stop), and the gate reacts:

```json
"components": [ { "type": "trigger", "on": "$trigger", "channel": "$channel" } ]
```

```json
"components": [
  { "type": "solid" },
  { "type": "receiver", "channel": "$channel", "action": "$action", "mode": "$mode" }
]
```

Give both the same channel name in the editor. The editor's problem list
warns about a channel with a trigger but no receiver, or the other way round.
Plate and gate state is part of undo and of the online match state.

## Defining an object in C++

For behaviour no component covers. Subclass a `CanvasItem` (usually through
`QGraphicsItem` or one of Kolf's item bases), describe its properties, and
register it next to its implementation:

```cpp
QVariantMap Kolf::Turnstile::properties() const
{
	return {{QStringLiteral("stiffness"), m_stiffness}};
}

void Kolf::Turnstile::applyProperties(const QVariantMap &values)
{
	setStiffness(values.value(QStringLiteral("stiffness"), m_stiffness).toDouble());
}

QVariantMap Kolf::Turnstile::runtimeState() const
{
	return {{QStringLiteral("angle"), m_angle}, {QStringLiteral("spin"), m_spin}};
}

void Kolf::Turnstile::setRuntimeState(const QVariantMap &state)
{
	m_angle = state.value(QStringLiteral("angle"), m_angle).toDouble();
	m_spin = state.value(QStringLiteral("spin"), m_spin).toDouble();
}

void Kolf::Turnstile::onBall(Kolf::BallStepContext &context)
{
	if (context.claimContact())
		pushArms(context.ball());
}

KOLF_REGISTER_OBJECT(Kolf::Turnstile, Kolf::ObjectType(QStringLiteral("turnstile"), kli18n("Turnstile"))
	.category(Kolf::ObjectCategory::Mechanism)
	.sprite(QStringLiteral("turnstile"))
	.help(kli18n("Arms that turn when the ball pushes them."))
	.property(Kolf::PropertySpec::number(QStringLiteral("stiffness"), kli18n("Stiffness"), 0.8, 0.1, 3, 0.1))
	.runtime({QStringLiteral("angle"), QStringLiteral("spin")}))
```

- `properties()`/`applyProperties()` are loaded and saved through the schema;
  the values the constructor sets must equal the schema defaults (the resave
  check in `tests/replay/README.md` verifies this).
- `runtimeState()` holds everything that changes during play, so undo and
  online guests stay exact. Its keys must match `.runtime(...)`.
- `onBall()` contributes to the ball's step: `context.surface()`,
  `context.force()`, `context.impulse()`, or `claimContact()` for exclusive
  effects such as holing or teleporting.

## Course files and missing objects

Saved courses carry `formatVersion=2` and list the decoration types they use
(`decorationTypes=`). When a course uses an object type this Kolf does not
have:

- a missing **gameplay** type stops the course from being played, with a
  message naming the missing objects (older courses without the list treat
  every unknown type as gameplay);
- a missing **decoration** shows a `?` placeholder and the course plays;
- in both cases the editor keeps the unknown object and its settings when the
  course is saved.

Online, both validators reject unknown types before a match starts.

## Checklist

- [ ] Definition and sprite load without errors, and the object is in the palette
- [ ] Defaults look right when placed; handles resize/rotate as intended
- [ ] A course using it saves, reloads and plays offline
- [ ] `--dump-object-catalog` run and the catalog committed
- [ ] Shown to someone else, who guessed what it does from the sprite
