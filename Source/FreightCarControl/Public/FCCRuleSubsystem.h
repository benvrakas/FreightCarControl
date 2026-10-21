#pragma once

#include "CoreMinimal.h"
#include "FCCTypes.h"
#include "FGSaveInterface.h"
#include "Subsystem/ModSubsystem.h"
#include "FCCRuleSubsystem.generated.h"

class AFGRailroadTimeTable;
class AFGRailroadVehicle;
class AFGTrain;
struct FTimeTableStop;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FFCCOnRulesChanged);

// Holds every train's per-stop freight car rules, saves them with the world and replicates them to clients.
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
	UFUNCTION(BlueprintPure, Category = "FreightCarControl")
	FFCCStopRule GetStopRule(const AFGRailroadTimeTable* TimeTable, int32 StopIndex) const;

	// Returns all stop rules of TimeTable, padded to its stop count.
	UFUNCTION(BlueprintPure, Category = "FreightCarControl")
	TArray<FFCCStopRule> GetAllStopRules(const AFGRailroadTimeTable* TimeTable) const;

	// Server only. Sets one car's mode at one stop.
	void SetCarMode(AFGRailroadTimeTable* TimeTable, int32 StopIndex, int32 CarIndex, EFCCCarMode Mode);

	// Server only. Replaces the rule of one stop.
	void SetStopRule(AFGRailroadTimeTable* TimeTable, int32 StopIndex, const FFCCStopRule& Rule);

	// Server only. Replaces all stop rules of TimeTable.
	void SetAllStopRules(AFGRailroadTimeTable* TimeTable, const TArray<FFCCStopRule>& Rules);

	// Server only. Inserts an empty rule at StopIndex after a stop was added there.
	void NotifyStopInserted(AFGRailroadTimeTable* TimeTable, int32 StopIndex);

	// Server only. Drops the rule of a removed stop.
	void NotifyStopRemoved(AFGRailroadTimeTable* TimeTable, int32 StopIndex);

	// Server only. Re-aligns rules after TimeTable's stop list was replaced, matching stops by station and occurrence.
	void NotifyStopsReplaced(AFGRailroadTimeTable* TimeTable, const TArray<FTimeTableStop>& OldStops, const TArray<FTimeTableStop>& NewStops);

	// Returns the position of Vehicle among the freight cars of its train, or INDEX_NONE if it isn't a freight car.
	static int32 GetFreightCarIndex(const AFGRailroadVehicle* Vehicle);

	// Returns the number of freight cars in Train.
	static int32 GetFreightCarCount(const AFGTrain* Train);

	// Broadcast on server and clients whenever any rule changes.
	UPROPERTY(BlueprintAssignable, Category = "FreightCarControl")
	FFCCOnRulesChanged OnRulesChanged;

	//~ Begin AActor
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void BeginPlay() override;
	//~ End AActor

	//~ Begin IFGSaveInterface
	virtual bool ShouldSave_Implementation() const override { return true; }
	virtual void PreSaveGame_Implementation(int32 SaveVersion, int32 GameVersion) override;
	virtual void PostLoadGame_Implementation(int32 SaveVersion, int32 GameVersion) override;
	//~ End IFGSaveInterface

private:
	// Returns the entry for TimeTable, or nullptr.
	const FFCCTimeTableRules* FindEntry(const AFGRailroadTimeTable* TimeTable) const;

	// Returns the entry for TimeTable, adding one if needed.
	FFCCTimeTableRules& FindOrAddEntry(AFGRailroadTimeTable* TimeTable);

	// Removes entries whose time table is gone or that hold no skip; trims trailing empty stops.
	void Compact();

	// Marks the rules dirty for replication and notifies listeners.
	void RulesChanged(const TCHAR* Reason);

	UFUNCTION()
	void OnRep_Rules();

	UPROPERTY(SaveGame, ReplicatedUsing = OnRep_Rules)
	TArray<FFCCTimeTableRules> mRules;
};
