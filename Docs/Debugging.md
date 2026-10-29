# Debugging

## Logs

The game log is `%LOCALAPPDATA%\FactoryGame\Saved\Logs\FactoryGame.log`. Filter for `LogFreightCarControl`.

At the default level the mod logs:

- startup: `Docking hooks installed`, `Time table hooks installed`, `UI hook ... added to ...`, `4 of 4 UI hooks
  registered`, `Rule subsystem started (server|client)`
- every rule change: `<train> stop <n> (<station>): [before] -> [after] by <cause>`, where the cause is
  `timetable editor (<player>)`, `freight car menu (<player>)` or `/fcc (<player>)`; whole-train changes, stop
  insertions/removals and stop-list replacements log every stop's rules before and after
- car numbering: `<train>: cars counted from locomotive <name>`, a warning if the front locomotive left the train,
  `the game's vehicle order changed (reversed)`, `freight cars changed: [...] -> [...]`, and a warning
  `freight car numbering reversed` (checked every 2 seconds for trains with rules)
- at each save and load, every train's front locomotive and rules
- every dock of a car on a train that has a timetable, once per car and stop:
  `<train> at <station> (stop <n>): car <c> -> Platform|Skip|Load|Unload, by <platform> (<class>, set to load|unload)`
- Load/Unload overrides: `<platform> switched to load for <car> (own setting: unload)`, then `<platform> restored to unload`
- UI: `Timetable editor session started ... (save hook: yes ...)`, `Stop settings panel shown`, `Freight car panel
  shown`, `Timetable saved for ...: sending N edited stop rules of M stops` (each stop listed as its rule or
  `unchanged`), and on the server `applying N stop rules edited in the timetable editor`
- stop list changes: `stop inserted`, `stop removed`, `stops replaced. Before: ... After: ... Rules taken from old stops: ...`
- save and load counts

More detail (docks at a station that isn't the current stop, button clicks, replication) is at `Verbose`; per-update
detail is at `VeryVerbose`. To raise the level, add to `%LOCALAPPDATA%\FactoryGame\Saved\Config\Windows\Engine.ini`:

```
[Core.Log]
LogFreightCarControl=Verbose
```

## Testing without the UI: `/fcc`

Drive the train (sit in its locomotive), then in chat:

- `/fcc`: lists the timetable's stops with each car's mode
- `/fcc <stop> <car> platform|skip|load|unload`: sets one car at one stop (both count from 1)
- `/fcc <stop> all platform|skip|load|unload`: sets every car at one stop
- `/fcc clear`: removes all rules from the train

Codes: `P` platform setting, `-` skip, `L` load, `U` unload.

## Test checklist

UI:

- Locomotive → edit timetable → edit a stop: a "Freight cars at this stop" section under the load/unload filters, one
  button per freight car. A click cycles Platform → Skip → Load → Unload. Save the stop, save the timetable, reopen:
  the modes are kept. Discard on the stop or on the timetable drops the change.
- Reorder stops in the editor and save: rules follow their stops.
- Freight car menu (solid and fluid car): "Timetable rules for this car" lists the stops; clicks save at once and show
  in the timetable editor.

Load/Unload override:

- Unload platform, car set to Load at that stop: the car loads from the platform's storage, and the platform is set to
  Unload again after the train leaves.
- Save while docked with an override, reload: the platform goes back to its own setting after the train leaves.

Skip:

- Vanilla station, 4 freight platforms, all Load, "Load/unload once": skip cars 2-4 at stop 1, check only car 1 fills
  and the train leaves.
- Same with "Fully load/unload": the train must not wait for skipped cars.
- Unload station: skipped cars keep their cargo.
- Modular Stations freight, fluid and "ports below" bases: same results, and the log names a `Build_BasePlatform...`
  class.
- Fluid freight car on a fluid platform.
- Save, reload, rules still applied.
