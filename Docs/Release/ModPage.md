<!--
Text for the Freight Car Control page on ficsit.app (Satisfactory Mod Repository).
- "Short description" field: the line under SHORT DESCRIPTION (at most 128 characters).
- "Full description" field: everything under FULL DESCRIPTION.
- The two transparency fields: the text under NETWORK ACTIVITY TRANSPARENCY and AI USAGE TRANSPARENCY.
- Logo: Docs/Images/icon-1080.png (full resolution; icon-512.png is the same image smaller).
Replace the [bracketed] placeholder before publishing.
-->

# SHORT DESCRIPTION

Choose what each freight car does at each timetable stop: follow the platform, skip, load or unload.

# FULL DESCRIPTION

## Freight Car Control

In vanilla Satisfactory a freight platform loads or unloads whatever car stops beside it, for every train, and a
timetable's item filters apply to the whole train. So a train can't fill car 1 at one station, car 2 at another and
car 3 at a third when those stations have the same platforms.

Freight Car Control adds a rule per car, per timetable stop:

| Mode | What the car does at that stop |
|---|---|
| **Platform** (default) | Whatever the platform beside it is set to, as in vanilla. |
| **Skip** | Nothing. The platform leaves it alone and doesn't hold the train for it, even with "Fully load/unload". |
| **Load** | Gets loaded, even if the platform is set to unload. The platform switches back when the train leaves. |
| **Unload** | Gets unloaded, even if the platform is set to load. The platform switches back when the train leaves. |

Rules belong to the train, so other trains using the same stations aren't affected.

![Freight car rules in a timetable stop's settings](https://raw.githubusercontent.com/benvrakas/FreightCarControl/main/Docs/Images/stop-settings.png)

### Where to set rules

- **Timetable editor.** Open a locomotive, edit its timetable and open a stop's settings. Under the load and unload
  filters there's a **Freight cars at this stop** section with a button per car; each click cycles Platform → Skip →
  Load → Unload, and a right-click goes back. Save the stop, then save the timetable.
- **Freight car menu.** Open a freight car and click **Timetable rules** to see this car's mode at every stop of its
  train. Clicks save straight away.

Cars are numbered from the train's first vehicle, counting freight cars only; fluid cars are marked.

![The Timetable rules list in a freight car's menu](https://raw.githubusercontent.com/benvrakas/FreightCarControl/main/Docs/Images/freight-car-menu.png)

### Works with

- Vanilla train stations and freight and fluid platforms.
- **[Modular Stations](https://ficsit.app/mod/ModularStations)**: freight, fluid and "ports below" platforms. Modular
  Stations isn't required.
- **[DynamicTimeTable](https://ficsit.app/mod/DynamicTimeTable)**: rules stay with their stop when it sends the train to
  another station of a group.
- Reordering, adding and removing stops: rules follow their stops.
- Multiplayer and dedicated servers (Windows and Linux).

### Good to know

- **Load on an unload platform** loads whatever is in the platform's storage, which is the cargo earlier trains left
  there. Useful for passing cargo between trains.
- If you change a platform's setting yourself while a car is docked with Load or Unload, your setting is kept.
- Uninstalling the mod returns every train to vanilla behaviour. Rules are stored by the mod, not in the game's own
  save data. A platform switched by Load or Unload at the moment you uninstall stays switched; switch it back by hand.

### Bugs and ideas

Report them on [GitHub](https://github.com/benvrakas/FreightCarControl/issues). The source code is there too.

# NETWORK ACTIVITY TRANSPARENCY

Freight Car Control doesn't connect to the internet. In multiplayer, players send their rule changes to the host and
the host shares the rules through the game's own connection. There's no telemetry or analytics.

# AI USAGE TRANSPARENCY

[Edit to match how you made the mod.]

This mod was built with the help of an AI coding assistant (Anthropic's Claude):

- **Source code:** the mod's C++ source code was written by the AI, directed, reviewed and tested in game by the
  author.
- **Mod page text:** this description was drafted by the AI and edited by the author.
- **Developer documentation** in the source folder was written by the AI.
- **Icon:** drawn by a Python script written by the AI (Tools/make_icon.py). The in-game UI uses the game's own widgets.
- **In game:** the mod doesn't use or give access to generative AI.
