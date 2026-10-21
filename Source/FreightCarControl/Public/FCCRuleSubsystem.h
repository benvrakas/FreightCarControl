#pragma once

#include "CoreMinimal.h"
#include "FCCTypes.h"
#include "FGSaveInterface.h"
#include "Subsystem/ModSubsystem.h"
#include "FCCRuleSubsystem.generated.h"

class AFGBuildableTrainPlatformCargo;
class AFGFreightWagon;
class AFGRailroadTimeTable;
class AFGRailroadVehicle;
class AFGTrain;
struct FTimeTableStop;

// Holds every train's per-stop freight car rules, saves them with the world and replicates them to clients.
// Also tracks platforms whose load/unload setting is switched for a docked car, and puts them back.
UCLASS()
class FREIGHTCARCONTROL_API AFCCRuleSubsystem : public AModSubsystem, public IFGSaveInterface
{
	GENERATED_BODY()

public:
	AFCCRuleSubsystem();

	static AFCCRuleSubsystem* Get(const UObject* WorldContext);

	// Returns the mode of the freight car at CarIndex at stop StopIndex of TimeTable.
	EFCCCarMode GetCarMode(const AFGRailroadTimeTable* TimeTable, int32 StopIndex, int32 CarIndex) const;

	// Returns the rule of stop StopIndex of TimeTable (empty if none is stored).
	FFCCStopRule GetStopRule(const AFGRailroadTimeTable* TimeTable, int32 StopIndex) const;

	// Returns a number that changes whenever any rule changes, on the server and on clients; UI compares it to refresh.
	uint32 GetRevision() const { return mRevision; }

	// Server only. Sets one car's mode at one stop.
	void SetCarMode(AFGRailroadTimeTable* TimeTable, int32 StopIndex, int32 CarIndex, EFCCCarMode Mode);

	// Server only. Replaces the rule of one stop.
	void SetStopRule(AFGRailroadTimeTable* TimeTable, int32 StopIndex, const FFCCStopRule& Rule);

	// Server only. Replaces all stop rules of TimeTable.
	void SetAllStopRules(AFGRailroadTimeTable* TimeTable, const TArray<FFCCStopRule>& Rules);

	// Server only. Applies rules saved from the timetable editor, by position, once Train's timetable has one stop per
	// rule; the editor's own save may still be on its way, or the train may not have a timetable yet.
	void QueueEditorRules(AFGTrain* Train, const TArray<FFCCStopRule>& Rules);

	// Server only. Inserts an empty rule at StopIndex after a stop was added there.
	void NotifyStopInserted(AFGRailroadTimeTable* TimeTable, int32 StopIndex);

	// Server only. Drops the rule of a removed stop.
	void NotifyStopRemoved(AFGRailroadTimeTable* TimeTable, int32 StopIndex);

	// Server only. Re-aligns rules after TimeTable's stop list was replaced: by station and occurrence, then by position
	// for stops whose station changed (e.g. swapped by another mod).
	void NotifyStopsReplaced(AFGRailroadTimeTable* TimeTable, const TArray<FTimeTableStop>& OldStops, const TArray<FTimeTableStop>& NewStops);

	// Server only. Switches Platform to bLoadMode for Vehicle's dock, remembering its own setting.
	void BeginPlatformOverride(AFGBuildableTrainPlatformCargo* Platform, AFGRailroadVehicle* Vehicle, bool bLoadMode);

	// Server only. Puts Platform's own setting back if the mod switched it for a vehicle other than KeepForVehicle.
	void EndPlatformOverride(AFGBuildableTrainPlatformCargo* Platform, const AFGRailroadVehicle* KeepForVehicle = nullptr);

	// Returns the freight cars of Train in order from its first vehicle; rules index cars by this order.
	static TArray<AFGFreightWagon*> GetFreightCars(const AFGTrain* Train);

	// Returns the position of Vehicle among the freight cars of its train, or INDEX_NONE if it isn't a freight car.
	static int32 GetFreightCarIndex(const AFGRailroadVehicle* Vehicle);

	//~ Begin AActor
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	//~ End AActor

	//~ Begin IFGSaveInterface
	virtual bool ShouldSave_Implementation() const override { return true; }
	virtual void PreSaveGame_Implementation(int32 SaveVersion, int32 GameVersion) override;
	virtual void PostLoadGame_Implementation(int32 SaveVersion, int32 GameVersion) override;
	//~ End IFGSaveInterface

private:
	// Returns the entry for TimeTable, or nullptr.
	FFCCTimeTableRules* FindEntry(const AFGRailroadTimeTable* TimeTable);
	const FFCCTimeTableRules* FindEntry(const AFGRailroadTimeTable* TimeTable) const;

	// Returns the entry for TimeTable, adding one if needed.
	FFCCTimeTableRules& FindOrAddEntry(AFGRailroadTimeTable* TimeTable);

	// Removes entries whose time table is gone or that hold no rule; trims trailing empty stops.
	void Compact();

	// Bumps the revision and marks the rules for replication.
	void RulesChanged(const TCHAR* Reason);

	// Applies queued editor rules whose train's timetable now has the right number of stops, and drops stale ones.
	void ProcessPendingRules();

	// Restores platforms whose overridden dock has ended, and processes pending editor rules.
	void Sweep();

	UFUNCTION()
	void OnRep_Rules();

	UPROPERTY(SaveGame, ReplicatedUsing = OnRep_Rules)
	TArray<FFCCTimeTableRules> mRules;

	UPROPERTY(SaveGame)
	TArray<FFCCPlatformOverride> mOverrides;

	UPROPERTY()
	TArray<FFCCPendingRules> mPending;

	uint32 mRevision = 0;
	FTimerHandle mSweepTimer;
};
