# Architecture

## What the mod does

For each stop in a train's timetable, each freight car is set to one of:

- **Platform** (default): the platform beside it loads or unloads as the platform is set (vanilla).
- **Skip**: the platform leaves the car alone and doesn't hold the train for it.
- **Load** / **Unload**: the platform is switched to that mode for this car's dock only, then switched back.

Cars are identified by their position among the train's freight cars (locomotives aren't counted), so the rules don't
depend on how the station is built.

## Car numbering

The game stores a train as a linked list from `AFGTrain::FirstVehicle`, and which end that is depends on how the train
was assembled. Numbering is anchored to a locomotive instead (`AFCCRuleSubsystem::GetFreightCars`): each train's rules
entry stores `FrontLocomotive`, and car 1 is the freight car nearest it.

- A train without rules counts from its multiple-unit master if that is in the train, else from its first locomotive in
  stored order. When it gets its first rule, that locomotive becomes its `FrontLocomotive`, so the numbering the UI
  showed is the numbering saved. Later changes of the master (a player taking over the other cab of a push-pull train)
  don't move it.
- Rules from before 1.0.3 (`bFrontChosen` false) counted from the stored first vehicle. The 2-second sweep gives them
  the first locomotive in stored order, with `bCountFromFarEnd` set if that locomotive sits in the later half, so the
  numbering stays exactly as it was and is then tied to the locomotive. Until the sweep runs they keep stored order.
- If the front locomotive leaves the train (dismantled, decoupled), a new one is chosen as for a new train and a warning
  is logged, since the numbering may flip.
- Freight cars can be added, removed or replaced without affecting numbering, which stays by position from the front.

`FrontLocomotive` and the flags replicate with the rules, so clients number cars the same way.


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
platform → docked vehicle → train → timetable → docked stop, finds the car's freight car index and its mode. Runs on
the server only, and again on every docking sequence update.

`AFGTrain::Dock` calls `IncrementCurrentStop` before `StartDocking`, so while docked the timetable's current stop is
already the next one. The docked stop is the one before it (wrapping to the last), exactly as
`AFGTrain::GetDockingRuleSetForCurrentStop` computes it.

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
`PurgeInvalidStops`. When the whole list is replaced (`MapReplacedStops`):

- the same stations in another order is a reorder, and each rule moves with its (station, occurrence);
- the same number of stops with some stations changed keeps every rule at its position. Other mods do this: they
  replace a stop's station (DynamicTimeTable sends a train to another station of a group; it hooks `SetStops` too).
  Matching by station there would hand a stop's rule to another stop that visits the new station;
- any other change matches stops by (station, occurrence), and stops left unmatched by position.

Every replacement is logged with the stations before and after and which old stop each rule came from.

Stops are identified by position everywhere else. Clients edit through `UFCCRemoteCallObject`:

- `Server_SetStopRule(timetable, stop, rule)`: one stop. Used by the freight car menu.
- `Server_SetEditorRules(train, stop count, edited positions, rules)`: the rules of the stops edited in the timetable
  editor, by position in the list it just saved, sent a tick after the editor's own save so it arrives after it on the
  same connection. Unedited stops aren't sent: they keep what the server realigned when the editor's `SetStops`
  arrived, so a stop whose station DynamicTimeTable swapped while the editor was open keeps its rule. `QueueEditorRules`
  applies the edits once the train's timetable has that many stops (a new train has no timetable until the vanilla save
  creates it, and may be built stop by stop); every stop change and the 2-second sweep re-check it; it is dropped after
  30 s.

When a train docks somewhere other than the stop's station (manual docking, or DynamicTimeTable sending it to another
station of the group), the docked stop's car rules still apply, as the game does with its own stop rules.

Clients' UI refreshes by comparing `AFCCRuleSubsystem::GetRevision()`, bumped on every change and every replication.

## UI

All UI is C++ `UUserWidget`s that build their trees from the game's own Blueprint widgets (`Widget_StandardButton`,
`BPW_TrainMenuLabel`, text styles copied from neighbouring vanilla text), injected with SML widget blueprint hooks
registered from C++ at `POST_INITIALIZATION` (`FCCUIHooks`; not on dedicated servers). Vanilla widget internals are
reached by reflection (`FCCWidgetUtils`): `SetText` is called through `ProcessEvent`, clicks are bound to a standard
button's inner `mButton`, and properties are read by name. The panel names come from dumping the starter project's
stub widget assets with an editor Python commandlet; property and function names from the cooked assets' name tables.

Hooks (vanilla widget → panel → mod widget):

- `BPW_Train_TimeTable_Setup` → `CanvasPanel_0` → `UFCCTimeTableEditSession`, invisible. The editor builds more than
  one of these, so the edits live in a draft per train shared by all sessions, keyed by (station, occurrence of that
  station in the list) because the editor recreates its stop widgets (`BPW_Train_TimeTable_SetupStation`, found inside
  `mCurrentTimeTable`). On the editor's `OnSave` dispatcher (fallback: `mSaveButton`), if there is a draft, sends the
  rule of every listed stop in order: drafted, or the server's for that key. `OnDiscard`, or the last session of the
  train closing, drops the draft.
- `BPW_Train_TimeTable_StationSettings` → `VerticalBox_58` (below the wait condition and the load/unload filters) →
  `UFCCStopRulePanel`. Follows the settings' `mStationWidget` and shows one 200 px `UFCCCarModeButton` per freight car
  of the train (fluid cars labelled) in a wrap box inside a scroll box of at most about three rows, so long trains fit;
  each click cycles Platform → Skip → Load → Unload. The settings' Discard button reverts the panel's edits.
- `Widget_FreightWagon` → `VerticalBox_0` (solid car layout) and `CanvasPanel_0` (fluid car layout, pinned to the
  bottom) → `UFCCWagonRulePanel`; only the instance in the layout of the car's cargo type runs, the other collapses. A
  toggle button opens a short scrolling list of the train's stops with this car's mode; a click saves straight away.
  One tick after construction it grows the window to fit: the solid layout is a fixed 703×568
  `mSolidInventoryContainer` (max width 680) with the inventory column `VerticalBox_0` at a fixed canvas size, the
  fluid layout a 611×486 canvas without an override. Closed: 60 px taller. Open: also 280 px wider and tall enough for
  the list. The Take All / Store All buttons are anchored right and move with the edge. The game reuses menu widgets,
  so the panel puts the vanilla sizes back when it closes.

`FindNestedWidgetsByClassName` removes duplicates: content of a named slot is reached both from the owner's widget
tree and from the slot widget's own tree.

An injected widget must not change its own slot in `NativeConstruct`: that runs while the parent panel is still
building its Slate slot, and Slate asserts. SML's generic slot setup also dereferences a horizontal-box slot when the
parent is a vertical box, so the mod passes no slot setup for vertical boxes and keeps its spacing inside its own tree.

## Multiplayer

The server holds the rules, applies them at docks and replicates them through `AFCCRuleSubsystem`; clients only show
them and send edits through `UFCCRemoteCallObject`.

Clients can't walk a train's vehicles the server's way: `AFGTrain::FirstVehicle` is saved but not replicated, so
`GetFirstVehicle()` is null on clients. The train's `AFGTrainReplicationActor` replicates `mVehicles`, which
`AFGTrain::RebuildConsist_Server` fills by walking from `FirstVehicle`, so it has the same order. That class isn't
exported from the game DLL, so `AFCCRuleSubsystem::GetFreightCars` reads `AFGTrain::mTrainReplicationActor` and its
`mVehicles` through reflection when `GetFirstVehicle()` is null.
