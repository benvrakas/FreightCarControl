# Freight Car Control

A Satisfactory mod that lets a train's timetable say, for each stop, which freight cars load or unload and which are
skipped. A train can fill car 1 at one station, car 2 at another and so on, even when every station has the same
platforms.

- Works with vanilla train stations and [Modular Stations](https://ficsit.app/mod/ModularStations).
- Rules belong to the train, so other trains using the same stations are unaffected.
- Skipped cars don't hold the train, even with "Fully load/unload".
- Nothing is written into vanilla save data; removing the mod returns every train to vanilla behaviour.

Status: early development. Rules can be set with the `/fcc` chat command; the in-game UI (a tab in each timetable
stop's settings and a section in the freight car menu) is in progress.

See [Docs/Architecture.md](Docs/Architecture.md) and [Docs/Debugging.md](Docs/Debugging.md).
