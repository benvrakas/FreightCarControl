#pragma once

#include "CoreMinimal.h"
#include "FCCTypes.h"
#include "FGSaveInterface.h"
#include "Subsystem/ModSubsystem.h"
#include "FCCRuleSubsystem.generated.h"

class AFGBuildableTrainPlatformCargo;
class AFGFreightWagon;
class AFGLocomotive;
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

	// Server only. Sets one car's mode at one stop. Cause says who or what made the change, for the log.
	void SetCarMode(AFGRailroadTimeTable* TimeTable, int32 StopIndex, int32 CarIndex, EFCCCarMode Mode, const FString& Cause);

	// Server only. Replaces the rule of one stop.
	void SetStopRule(AFGRailroadTimeTable* TimeTable, int32 StopIndex, const FFCCStopRule& Rule, const FString& Cause);

	// Server only. Replaces all stop rules of TimeTable.
	void SetAllStopRules(AFGRailroadTimeTable* TimeTable, const TArray<FFCCStopRule>& Rules, const FString& Cause);

	// Server only. Applies rules edited in the timetable editor to the stops at StopIndices, once Train's timetable has
	// StopCount stops; the editor's own save may still be on its way, or the train may not have a timetable yet.
	void QueueEditorRules(AFGTrain* Train, int32 StopCount, const TArray<int32>& StopIndices, const TArray<FFCCStopRule>& Rules, const FString& Cause);

	// Server only. Inserts an empty rule at StopIndex after a stop was added there.
	void NotifyStopInserted(AFGRailroadTimeTable* TimeTable, int32 StopIndex);

	// Server only. Drops the rule of a removed stop.
	void NotifyStopRemoved(AFGRailroadTimeTable* TimeTable, int32 StopIndex);

	// Server only. Re-aligns rules after TimeTable's stop list was replaced (see Docs/Architecture.md, Rule storage).
	void NotifyStopsReplaced(AFGRailroadTimeTable* TimeTable, const TArray<FTimeTableStop>& OldStops, const TArray<FTimeTableStop>& NewStops);

	// Server only. Switches Platform to bLoadMode for Vehicle's dock, remembering its own setting.
	void BeginPlatformOverride(AFGBuildableTrainPlatformCargo* Platform, AFGRailroadVehicle* Vehicle, bool bLoadMode);

	// Server only. Puts Platform's own setting back if the mod switched it for a vehicle other than KeepForVehicle.
	void EndPlatformOverride(AFGBuildableTrainPlatformCargo* Platform, const AFGRailroadVehicle* KeepForVehicle = nullptr);

	// Returns Train's freight cars numbered as rules number them: from the end of its front locomotive.
	static TArray<AFGFreightWagon*> GetFreightCars(const AFGTrain* Train);

	// Returns the position of Vehicle among the freight cars of its train, or INDEX_NONE if it isn't a freight car.
	static int32 GetFreightCarIndex(const AFGRailroadVehicle* Vehicle);

	// Returns a sentence for the UI saying where car 1 is on Train, or empty text if the train has no locomotive.
	static FText DescribeCarNumbering(const AFGTrain* Train);

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

	// Returns the entry for TimeTable, adding one (with its front locomotive chosen) if needed.
	FFCCTimeTableRules& FindOrAddEntry(AFGRailroadTimeTable* TimeTable);

	// Returns the train that owns TimeTable, remembering it in Entry.
	AFGTrain* FindTrain(FFCCTimeTableRules& Entry) const;

	// Returns "Train name" for logs, or the timetable's object name if the train isn't known.
	FString Describe(const AFGRailroadTimeTable* TimeTable) const;

	// Server only. Gives Entry a front locomotive if it has none or its locomotive left the train. Rules from before
	// locomotive anchoring keep the numbering they had.
	void EnsureFrontLocomotive(FFCCTimeTableRules& Entry);

	// Server only. Logs changes to the order of each ruled train's vehicles and freight cars since the last check.
	void WatchCarOrder();

	// Removes entries whose time table is gone or that hold no rule; trims trailing empty stops.
	void Compact();

	// Bumps the revision and marks the rules for replication.
	void RulesChanged(const TCHAR* Reason);

	// Applies queued editor rules whose train's timetable now has the right number of stops, and drops stale ones.
	void ProcessPendingRules();

	// Restores platforms whose overridden dock has ended, keeps front locomotives valid, watches car order and processes
	// pending editor rules.
	void Sweep();

	UFUNCTION()
	void OnRep_Rules();

	UPROPERTY(SaveGame, ReplicatedUsing = OnRep_Rules)
	TArray<FFCCTimeTableRules> mRules;

	UPROPERTY(SaveGame)
	TArray<FFCCPlatformOverride> mOverrides;

	UPROPERTY()
	TArray<FFCCPendingRules> mPending;

	// Vehicle order (as the game stores it) and freight car numbering last seen per ruled train, for WatchCarOrder.
	struct FSeenOrder
	{
		TArray<TWeakObjectPtr<AFGRailroadVehicle>> Vehicles;
		TArray<TWeakObjectPtr<AFGFreightWagon>> Cars;
	};
	TMap<TWeakObjectPtr<AFGTrain>, FSeenOrder> mSeenOrders;

	uint32 mRevision = 0;
	FTimerHandle mSweepTimer;
};
