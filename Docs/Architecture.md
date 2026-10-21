# Architecture

## What the mod does

For each stop in a train's timetable, each freight car is set to **Transfer** (vanilla) or **Skip**. A skipped car
is left alone by the platform beside it, and that platform doesn't hold the train. Cars are identified by their
position among the train's freight cars (locomotives aren't counted), so the rules don't depend on how the station
is built.

## How the game docks a train (game CL 513885)

Found by disassembling the shipped `FactoryGame` DLL with its PDB.

1. `AFGBuildableRailroadStation::DockVehiclesToPlatforms` walks the platforms from the station's output connection
   and calls the virtual `NotifyTrainDocked(vehicle, station)` on each platform with the vehicle beside it.
2. Each cargo platform runs `UpdateDockingSequence`, which first calls `EvaluateRuleSet`. That copies the station's
   `mCurrentDockedWithRuleSet` (the train's current timetable stop rules) into the platform, rebuilds
   `mLoadItemFilter` / `mUnloadItemFilter` from it (an empty list becomes `[UFGItemDescriptor]`, meaning
   everything), and sets `mTransferBlockedByNoneFilter` from `IsLoadUnloadBlockedByNoneFilter()`.
3. `UpdateLoadSettings` / `UpdateUnloadSettings` find the stacks matching the filter
   (`UFGInventoryComponent::GetRelevantStackIndexes`); with none, nothing is transferred.
4. `CanCompleteDocking` holds the train for a "fully load/unload" rule (`mHasFullyLoadUnloadRule`) unless the
   transfer is blocked by filters, then waits for the stop's duration.

## Enforcement

`FFCCDockingHook` hooks after `EvaluateRuleSet` (private; reached through an access-transformer `Friend`). It looks
up platform → docked vehicle → train → timetable → current stop, checks that stop's station is the station the
platform belongs to (manual docking elsewhere is left vanilla), finds the car's freight car index, and for a
skipped car sets both filters to `[UFGNoneDescriptor]`, sets `mTransferBlockedByNoneFilter` and clears
`mHasFullyLoadUnloadRule`. The rest of the vanilla sequence runs unchanged, so animations, power loss and
multiplayer behave as vanilla. Runs on the server only.

`EvaluateRuleSet` runs on every docking sequence update, so the override is reapplied each time.

If this turns out not to stop a transfer in game, the fallback is a pre-hook on `NotifyTrainDocked` that completes
that platform's docking at once, like an empty platform.

## Modular Stations

Modular Stations 2.0.6 has no C++ platform classes (its C++ is holograms and a save subsystem). Its freight and fluid
bases (`Build_BasePlatformFreight`, `Build_BasePlatformFluid`, and the `_Underfed` "ports below" variants) are
Blueprint children of `AFGBuildableTrainPlatformCargo`, and its stations are children of
`AFGBuildableRailroadStation`. A hook on the native function therefore covers them without any special case, and the
mod doesn't depend on Modular Stations.

## Rule storage

`AFCCRuleSubsystem` (an SML mod subsystem, spawned on the server and replicated) keeps an array of
`FFCCTimeTableRules`: a timetable reference plus one `FFCCStopRule` per stop, each holding car modes. Saved with the
world through `IFGSaveInterface`; nothing is written to vanilla objects, so removing the mod leaves a vanilla save.
Entries with no skips, or whose timetable is gone, are dropped before saving.

Stop alignment is kept by hooks on `AFGRailroadTimeTable::AddStop`, `RemoveStop`, `SetStops` and
`PurgeInvalidStops`. When the whole list is replaced, stops are matched by (station, occurrence of that station), so
reordering stops keeps their rules. The timetable UI will send exact rules with its own save, which overrides this
matching.

Clients edit through `UFCCRemoteCallObject` (`Server_SetStopRule`, `Server_SetAllStopRules`).

## UI (planned, M2)

- A "Freight Cars" tab in the vanilla timetable stop settings `BPW_Train_TimeTable_StationSettings` (it has a tab
  container `mTabs` next to the load and unload filter tabs).
- A per-stop list in the freight car menu `Widget_FreightWagon`.

Both are built from the game's own widget classes and injected with SML widget blueprint hooks registered from C++.
