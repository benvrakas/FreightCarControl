# Architecture

## What the mod does

For each stop in a train's timetable, each freight car is set to one of:

- **Platform** (default): the platform beside it loads or unloads as the platform is set (vanilla).
- **Skip**: the platform leaves the car alone and doesn't hold the train for it.
- **Load** / **Unload**: the platform is switched to that mode for this car's dock only, then switched back.

Cars are identified by their position among the train's freight cars (locomotives aren't counted), so the rules don't
depend on how the station is built.

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
5. `SetIsInLoadMode` only flips the mode bit. `Undock` clears `mDockedRailroadVehicle`.

## Enforcement

`FFCCDockingHook` wraps `EvaluateRuleSet` (private; reached through an access-transformer `Friend`). It looks up
platform → docked vehicle → train → timetable → current stop, checks that stop's station is the station the platform
belongs to (manual docking elsewhere is left vanilla), finds the car's freight car index and its mode. Runs on the
server only, and again on every docking sequence update.

### Skip

After `EvaluateRuleSet`, both filters are set to `[UFGNoneDescriptor]`, `mTransferBlockedByNoneFilter` is set and
`mHasFullyLoadUnloadRule` cleared. The rest of the vanilla sequence runs unchanged, so animations, power loss and
multiplayer behave as vanilla.

If this doesn't stop a transfer in game, the fallback is a pre-hook on `NotifyTrainDocked` that completes that
platform's docking at once, like an empty platform.

### Load / Unload override

Before `EvaluateRuleSet`, a Load or Unload car makes `AFCCRuleSubsystem::BeginPlatformOverride` record the platform's
own setting and call `SetIsInLoadMode`. The setting is put back when the platform undocks (hooks after the virtual
`Undock` and `CancelDockingSequence`) and, as a safety net, by a 2-second sweep that restores any platform whose docked
vehicle is no longer the one recorded. Overrides are saved, so a save made mid-dock restores correctly after loading.
If the player changes the platform's setting during the dock, the mod leaves their choice alone.

A platform switched to Load loads whatever is in its own inventory, which on an unload platform is cargo earlier trains
left there.

## Modular Stations

Modular Stations 2.0.6 has no C++ platform classes (its C++ is holograms and a save subsystem). Its freight and fluid
bases (`Build_BasePlatformFreight`, `Build_BasePlatformFluid`, and the `_Underfed` "ports below" variants) are
Blueprint children of `AFGBuildableTrainPlatformCargo`, and its stations are children of
`AFGBuildableRailroadStation`. Hooks on the native functions therefore cover them without any special case, and the
mod doesn't depend on Modular Stations.

## Rule storage

`AFCCRuleSubsystem` (an SML mod subsystem, spawned on the server and replicated) keeps an array of
`FFCCTimeTableRules`: a timetable reference plus one `FFCCStopRule` per stop, each holding car modes. Saved with the
world through `IFGSaveInterface`; nothing is written to vanilla objects, so removing the mod leaves a vanilla save
(except a platform switched by a Load/Unload override at the moment of removal, which stays switched).
Entries with no rules, or whose timetable is gone, are dropped before saving.

Stop alignment is kept by hooks on `AFGRailroadTimeTable::AddStop`, `RemoveStop`, `SetStops` and
`PurgeInvalidStops`. When the whole list is replaced, stops are matched by (station, occurrence of that station), so
reordering stops keeps their rules.

Clients edit through `UFCCRemoteCallObject`:

- `Server_SetStopRule(timetable, stop, expected station, rule)`: one stop, ignored if that stop no longer visits the
  expected station. Used by the freight car menu.
- `Server_SetAllStopRules(train, stations, rules)`: sent one tick after the timetable editor saves, so it arrives after
  the vanilla save RPC on the same channel. Applied by position once the train's timetable has as many stops as there
  are rules (a new train has no timetable until the vanilla save creates it); kept pending until then and dropped
  after 30 s. Stations aren't required to match: DynamicTimeTable hooks `SetStops` too and can swap a stop's station
  within its group. A mismatch is only logged.

The timetable editor recreates its stop widgets (for example after a stop's settings are saved), so the edit session
keys edits by (station, occurrence of that station in the list) rather than by widget.

When a train docks somewhere other than its current stop's station (manual docking, or DynamicTimeTable sending it to
another station of the group), the current stop's car rules still apply, as the game does with its own stop rules.

## UI

All UI is C++ `UUserWidget`s that build their trees from the game's own Blueprint widgets (`Widget_StandardButton`,
`BPW_TrainMenuLabel`, text styles copied from neighbouring vanilla text), injected with SML widget blueprint hooks
registered from C++ at `POST_INITIALIZATION` (`FCCUIHooks`; not on dedicated servers). Vanilla widget internals are
reached by reflection (`FCCWidgetUtils`): `SetText` is called through `ProcessEvent`, clicks are bound to a standard
button's inner `mButton`, and properties are read by name. The panel names come from dumping the starter project's
stub widget assets with an editor Python commandlet; property and function names from the cooked assets' name tables.

Hooks (vanilla widget → panel → mod widget):

- `BPW_Train_TimeTable_Setup` → `CanvasPanel_0` → `UFCCTimeTableEditSession`, invisible. Gives each listed stop widget
  (`BPW_Train_TimeTable_SetupStation`, found inside `mCurrentTimeTable`) the rule its stop has on the server, matched by
  (station, occurrence); keeps edits; on the editor's `OnSave` dispatcher (fallback: `mSaveButton`) sends all rules in
  the list's order. `OnDiscard` drops edits.
- `BPW_Train_TimeTable_StationSettings` → `VerticalBox_58` (below the wait condition and the load/unload filters) →
  `UFCCStopRulePanel`. Follows the settings' `mStationWidget` and shows one `UFCCCarModeButton` per freight car of the
  train (fluid cars labelled); each click cycles Platform → Skip → Load → Unload. The settings' Discard button
  reverts the panel's edits.
- `Widget_FreightWagon` → `VerticalBox_0` (solid car layout) and `CanvasPanel_0` (fluid car layout, pinned to the
  bottom) → `UFCCWagonRulePanel`. Lists the train's stops with this car's mode; a click saves straight away.

SML's generic slot setup dereferences a horizontal-box slot when the parent is a vertical box, so the mod passes no
slot setup for vertical boxes and sets its own padding in `NativeConstruct`.
