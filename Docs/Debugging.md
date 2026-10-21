# Debugging

## Logs

The game log is `%LOCALAPPDATA%\FactoryGame\Saved\Logs\FactoryGame.log`. Filter for `LogFreightCarControl`.

At the default level the mod logs:

- startup: `Docking hook installed`, `Time table hooks installed`, `Rule subsystem started (server|client)`
- every rule edit: `<train> stop <n>: <cars> cars ruled, skips=<k>`
- every dock of a car on a train that has a timetable, once per car and stop:
  `<train> at <station> (stop <n>): car <c> SKIPPED|transfers by <platform> (<class>, load|unload)`
- stop list changes: `stop inserted`, `stop removed`, `stops replaced (a -> b), k stop rules kept`
- save and load counts

More detail (docks at a station that isn't the current stop, replication) is at `Verbose`; per-update detail is at
`VeryVerbose`. To raise the level, add to `%LOCALAPPDATA%\FactoryGame\Saved\Config\Windows\Engine.ini`:

```
[Core.Log]
LogFreightCarControl=Verbose
```

## Testing without the UI: `/fcc`

Drive the train (sit in its locomotive), then in chat:

- `/fcc`: lists the timetable's stops with each car's mode (`X` transfer, `-` skip)
- `/fcc <stop> <car> skip|transfer`: sets one car at one stop (both count from 1)
- `/fcc <stop> all skip|transfer`: sets every car at one stop
- `/fcc clear`: removes all rules from the train

## Test checklist (M1)

- Vanilla station, 4 freight platforms, all Load, "Load/unload once": skip cars 2-4 at stop 1, check only car 1 fills
  and the train leaves.
- Same with "Fully load/unload": the train must not wait for skipped cars.
- Unload station: skipped cars keep their cargo.
- Modular Stations freight, fluid and "ports below" bases: same results, and the log names a `Build_BasePlatform...`
  class.
- Fluid freight car on a fluid platform.
- Stop reordering in the timetable editor keeps the rules on the same stations.
- Save, reload, rules still applied.
