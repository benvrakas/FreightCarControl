#include "FCCRuleSubsystem.h"
#include "FreightCarControl.h"
#include "Buildables/FGBuildableTrainPlatformCargo.h"
#include "FGFreightWagon.h"
#include "FGRailroadSubsystem.h"
#include "FGRailroadTimeTable.h"
#include "FGRailroadVehicle.h"
#include "FGTrain.h"
#include "FGTrainStationIdentifier.h"
#include "Net/UnrealNetwork.h"
#include "Subsystem/SubsystemActorManager.h"
#include "TimerManager.h"

namespace
{
	// Largest freight car position a rule may name; longer trains than this aren't buildable in practice.
	constexpr int32 MaxCarIndex = 255;

	// Seconds between sweeps for ended platform overrides and pending editor rules.
	constexpr float SweepInterval = 2.0f;

	// Seconds rules saved from the timetable editor wait for the timetable's stops before they are dropped.
	constexpr double PendingLifetime = 30.0;

	// Returns (station, occurrence) keys for Stops, so the same station visited twice gets two keys.
	TArray<TPair<const AFGTrainStationIdentifier*, int32>> MakeStopKeys(const TArray<FTimeTableStop>& Stops)
	{
		TArray<TPair<const AFGTrainStationIdentifier*, int32>> Keys;
		TMap<const AFGTrainStationIdentifier*, int32> Seen;
		for (const FTimeTableStop& Stop : Stops)
		{
			Keys.Emplace(Stop.Station, Seen.FindOrAdd(Stop.Station)++);
		}
		return Keys;
	}

	FString DescribeTimeTable(const AFGRailroadTimeTable* TimeTable)
	{
		const AFGTrain* Train = TimeTable ? Cast<AFGTrain>(TimeTable->GetOwner()) : nullptr;
		return Train ? FString::Printf(TEXT("%s (%s)"), *Train->GetTrainName().ToString(), *GetNameSafe(TimeTable)) : GetNameSafe(TimeTable);
	}
}

AFCCRuleSubsystem::AFCCRuleSubsystem()
{
	ReplicationPolicy = ESubsystemReplicationPolicy::SpawnOnServer_Replicate;
}

AFCCRuleSubsystem* AFCCRuleSubsystem::Get(const UObject* WorldContext)
{
	UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	USubsystemActorManager* Manager = World ? World->GetSubsystem<USubsystemActorManager>() : nullptr;
	return Manager ? Manager->GetSubsystemActor<AFCCRuleSubsystem>() : nullptr;
}

void AFCCRuleSubsystem::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AFCCRuleSubsystem, mRules);
}

void AFCCRuleSubsystem::BeginPlay()
{
	Super::BeginPlay();
	UE_LOG(LogFreightCarControl, Log, TEXT("Rule subsystem started (%s), %d time tables with rules, %d platform overrides"),
		HasAuthority() ? TEXT("server") : TEXT("client"), mRules.Num(), mOverrides.Num());
	if (HasAuthority())
	{
		GetWorldTimerManager().SetTimer(mSweepTimer, this, &AFCCRuleSubsystem::Sweep, SweepInterval, true);
	}
}

void AFCCRuleSubsystem::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GetWorldTimerManager().ClearTimer(mSweepTimer);
	Super::EndPlay(EndPlayReason);
}

void AFCCRuleSubsystem::PreSaveGame_Implementation(int32 SaveVersion, int32 GameVersion)
{
	Compact();
	UE_LOG(LogFreightCarControl, Log, TEXT("Saving rules for %d time tables, %d platform overrides"), mRules.Num(), mOverrides.Num());
}

void AFCCRuleSubsystem::PostLoadGame_Implementation(int32 SaveVersion, int32 GameVersion)
{
	const int32 Loaded = mRules.Num();
	Compact();
	UE_LOG(LogFreightCarControl, Log, TEXT("Loaded rules for %d time tables (%d dropped as stale), %d platform overrides"),
		mRules.Num(), Loaded - mRules.Num(), mOverrides.Num());
	for (const FFCCTimeTableRules& Entry : mRules)
	{
		UE_LOG(LogFreightCarControl, Verbose, TEXT("  %s: %d stop rules"), *DescribeTimeTable(Entry.TimeTable), Entry.Stops.Num());
	}
	RulesChanged(TEXT("load"));
}

FFCCTimeTableRules* AFCCRuleSubsystem::FindEntry(const AFGRailroadTimeTable* TimeTable)
{
	return const_cast<FFCCTimeTableRules*>(static_cast<const AFCCRuleSubsystem*>(this)->FindEntry(TimeTable));
}

const FFCCTimeTableRules* AFCCRuleSubsystem::FindEntry(const AFGRailroadTimeTable* TimeTable) const
{
	if (!TimeTable)
	{
		return nullptr;
	}
	return mRules.FindByPredicate([TimeTable](const FFCCTimeTableRules& Entry) { return Entry.TimeTable == TimeTable; });
}

FFCCTimeTableRules& AFCCRuleSubsystem::FindOrAddEntry(AFGRailroadTimeTable* TimeTable)
{
	if (FFCCTimeTableRules* Found = FindEntry(TimeTable))
	{
		return *Found;
	}
	FFCCTimeTableRules& Added = mRules.AddDefaulted_GetRef();
	Added.TimeTable = TimeTable;
	return Added;
}

EFCCCarMode AFCCRuleSubsystem::GetCarMode(const AFGRailroadTimeTable* TimeTable, int32 StopIndex, int32 CarIndex) const
{
	return GetStopRule(TimeTable, StopIndex).GetCarMode(CarIndex);
}

FFCCStopRule AFCCRuleSubsystem::GetStopRule(const AFGRailroadTimeTable* TimeTable, int32 StopIndex) const
{
	const FFCCTimeTableRules* Entry = FindEntry(TimeTable);
	return Entry && Entry->Stops.IsValidIndex(StopIndex) ? Entry->Stops[StopIndex] : FFCCStopRule();
}

void AFCCRuleSubsystem::SetCarMode(AFGRailroadTimeTable* TimeTable, int32 StopIndex, int32 CarIndex, EFCCCarMode Mode)
{
	FFCCStopRule Rule = GetStopRule(TimeTable, StopIndex);
	Rule.SetCarMode(CarIndex, Mode);
	SetStopRule(TimeTable, StopIndex, Rule);
}

void AFCCRuleSubsystem::SetStopRule(AFGRailroadTimeTable* TimeTable, int32 StopIndex, const FFCCStopRule& Rule)
{
	if (!HasAuthority() || !TimeTable || StopIndex < 0 || StopIndex >= TimeTable->GetNumStops() || Rule.Cars.Num() > MaxCarIndex + 1)
	{
		UE_LOG(LogFreightCarControl, Warning, TEXT("SetStopRule rejected: authority=%d timetable=%s stop=%d cars=%d"),
			HasAuthority(), *GetNameSafe(TimeTable), StopIndex, Rule.Cars.Num());
		return;
	}
	FFCCTimeTableRules& Entry = FindOrAddEntry(TimeTable);
	if (Entry.Stops.Num() <= StopIndex)
	{
		Entry.Stops.SetNum(StopIndex + 1);
	}
	Entry.Stops[StopIndex] = Rule;
	Entry.Stops[StopIndex].Trim();
	UE_LOG(LogFreightCarControl, Log, TEXT("%s stop %d: cars [%s]"), *DescribeTimeTable(TimeTable), StopIndex + 1, *Entry.Stops[StopIndex].ToCodes());
	RulesChanged(TEXT("set stop rule"));
}

void AFCCRuleSubsystem::SetAllStopRules(AFGRailroadTimeTable* TimeTable, const TArray<FFCCStopRule>& Rules)
{
	if (!HasAuthority() || !TimeTable)
	{
		UE_LOG(LogFreightCarControl, Warning, TEXT("SetAllStopRules rejected: authority=%d timetable=%s"), HasAuthority(), *GetNameSafe(TimeTable));
		return;
	}
	FFCCTimeTableRules& Entry = FindOrAddEntry(TimeTable);
	Entry.Stops = Rules;
	Entry.Stops.SetNum(FMath::Min(Entry.Stops.Num(), TimeTable->GetNumStops()));
	FString Summary;
	for (FFCCStopRule& Stop : Entry.Stops)
	{
		Stop.Cars.SetNum(FMath::Min(Stop.Cars.Num(), MaxCarIndex + 1));
		Stop.Trim();
		Summary += FString::Printf(TEXT(" [%s]"), *Stop.ToCodes());
	}
	UE_LOG(LogFreightCarControl, Log, TEXT("%s: all stop rules set:%s"), *DescribeTimeTable(TimeTable), *Summary);
	RulesChanged(TEXT("set all rules"));
}

void AFCCRuleSubsystem::QueueEditorRules(AFGTrain* Train, const TArray<FFCCStopRule>& Rules)
{
	if (!HasAuthority() || !Train)
	{
		return;
	}
	mPending.RemoveAll([Train](const FFCCPendingRules& Pending) { return Pending.Train == Train; });
	FFCCPendingRules& Pending = mPending.AddDefaulted_GetRef();
	Pending.Train = Train;
	Pending.Rules = Rules;
	Pending.ReceivedTime = FPlatformTime::Seconds();
	ProcessPendingRules();
}

void AFCCRuleSubsystem::ProcessPendingRules()
{
	const double Now = FPlatformTime::Seconds();
	for (int32 Index = mPending.Num() - 1; Index >= 0; --Index)
	{
		const FFCCPendingRules& Pending = mPending[Index];
		AFGRailroadTimeTable* TimeTable = IsValid(Pending.Train) ? Pending.Train->GetTimeTable() : nullptr;
		if (TimeTable && TimeTable->GetNumStops() == Pending.Rules.Num())
		{
			UE_LOG(LogFreightCarControl, Log, TEXT("%s: applying rules saved from the timetable editor"), *DescribeTimeTable(TimeTable));
			const TArray<FFCCStopRule> Rules = Pending.Rules;
			mPending.RemoveAt(Index);
			SetAllStopRules(TimeTable, Rules);
		}
		else if (!IsValid(Pending.Train) || Now - Pending.ReceivedTime > PendingLifetime)
		{
			UE_LOG(LogFreightCarControl, Warning, TEXT("Dropped rules from the timetable editor for %s: its timetable never got %d stops (has %d)"),
				*GetNameSafe(Pending.Train), Pending.Rules.Num(), TimeTable ? TimeTable->GetNumStops() : 0);
			mPending.RemoveAt(Index);
		}
		else
		{
			UE_LOG(LogFreightCarControl, Verbose, TEXT("%s: editor rules for %d stops wait (timetable has %d)"),
				*GetNameSafe(Pending.Train), Pending.Rules.Num(), TimeTable ? TimeTable->GetNumStops() : 0);
		}
	}
}

void AFCCRuleSubsystem::NotifyStopInserted(AFGRailroadTimeTable* TimeTable, int32 StopIndex)
{
	FFCCTimeTableRules* Entry = FindEntry(TimeTable);
	if (Entry && StopIndex >= 0 && StopIndex < Entry->Stops.Num())
	{
		Entry->Stops.Insert(FFCCStopRule(), StopIndex);
		UE_LOG(LogFreightCarControl, Log, TEXT("%s: stop inserted at %d, rules shifted"), *DescribeTimeTable(TimeTable), StopIndex + 1);
		RulesChanged(TEXT("stop inserted"));
	}
	ProcessPendingRules();
}

void AFCCRuleSubsystem::NotifyStopRemoved(AFGRailroadTimeTable* TimeTable, int32 StopIndex)
{
	FFCCTimeTableRules* Entry = FindEntry(TimeTable);
	if (Entry && Entry->Stops.IsValidIndex(StopIndex))
	{
		Entry->Stops.RemoveAt(StopIndex);
		UE_LOG(LogFreightCarControl, Log, TEXT("%s: stop %d removed, rules shifted"), *DescribeTimeTable(TimeTable), StopIndex + 1);
		RulesChanged(TEXT("stop removed"));
	}
	ProcessPendingRules();
}

void AFCCRuleSubsystem::NotifyStopsReplaced(AFGRailroadTimeTable* TimeTable, const TArray<FTimeTableStop>& OldStops, const TArray<FTimeTableStop>& NewStops)
{
	FFCCTimeTableRules* Entry = FindEntry(TimeTable);
	if (Entry)
	{
		const TArray<TPair<const AFGTrainStationIdentifier*, int32>> OldKeys = MakeStopKeys(OldStops);
		const TArray<TPair<const AFGTrainStationIdentifier*, int32>> NewKeys = MakeStopKeys(NewStops);
		TArray<int32> Source;
		Source.Init(INDEX_NONE, NewKeys.Num());
		TBitArray<> Used(false, OldKeys.Num());
		for (int32 NewIndex = 0; NewIndex < NewKeys.Num(); ++NewIndex)
		{
			const int32 OldIndex = OldKeys.IndexOfByKey(NewKeys[NewIndex]);
			if (OldIndex != INDEX_NONE)
			{
				Source[NewIndex] = OldIndex;
				Used[OldIndex] = true;
			}
		}
		for (int32 NewIndex = 0; NewIndex < NewKeys.Num(); ++NewIndex)
		{
			if (Source[NewIndex] == INDEX_NONE && OldKeys.IsValidIndex(NewIndex) && !Used[NewIndex])
			{
				Source[NewIndex] = NewIndex;
				Used[NewIndex] = true;
			}
		}
		TArray<FFCCStopRule> Remapped;
		Remapped.SetNum(NewKeys.Num());
		for (int32 NewIndex = 0; NewIndex < NewKeys.Num(); ++NewIndex)
		{
			if (Entry->Stops.IsValidIndex(Source[NewIndex]))
			{
				Remapped[NewIndex] = Entry->Stops[Source[NewIndex]];
			}
		}
		if (Remapped != Entry->Stops)
		{
			Entry->Stops = MoveTemp(Remapped);
			UE_LOG(LogFreightCarControl, Log, TEXT("%s: stops replaced (%d -> %d), rules realigned"), *DescribeTimeTable(TimeTable), OldStops.Num(), NewStops.Num());
			RulesChanged(TEXT("stops replaced"));
		}
	}
	ProcessPendingRules();
}

void AFCCRuleSubsystem::BeginPlatformOverride(AFGBuildableTrainPlatformCargo* Platform, AFGRailroadVehicle* Vehicle, bool bLoadMode)
{
	if (!HasAuthority() || !Platform || !Vehicle)
	{
		return;
	}
	EndPlatformOverride(Platform, Vehicle);
	if (mOverrides.ContainsByPredicate([Platform](const FFCCPlatformOverride& O) { return O.Platform == Platform; }))
	{
		return;
	}
	const bool bOriginal = Platform->GetIsInLoadMode();
	if (bOriginal == bLoadMode)
	{
		return;
	}
	FFCCPlatformOverride& Override = mOverrides.AddDefaulted_GetRef();
	Override.Platform = Platform;
	Override.Vehicle = Vehicle;
	Override.bOriginalLoadMode = bOriginal;
	Override.bForcedLoadMode = bLoadMode;
	Platform->SetIsInLoadMode(bLoadMode);
	UE_LOG(LogFreightCarControl, Log, TEXT("%s switched to %s for %s (own setting: %s)"), *GetNameSafe(Platform),
		bLoadMode ? TEXT("load") : TEXT("unload"), *GetNameSafe(Vehicle), bOriginal ? TEXT("load") : TEXT("unload"));
}

void AFCCRuleSubsystem::EndPlatformOverride(AFGBuildableTrainPlatformCargo* Platform, const AFGRailroadVehicle* KeepForVehicle)
{
	const int32 Index = mOverrides.IndexOfByPredicate([Platform](const FFCCPlatformOverride& O) { return O.Platform == Platform; });
	if (Index == INDEX_NONE || (KeepForVehicle && mOverrides[Index].Vehicle == KeepForVehicle))
	{
		return;
	}
	const FFCCPlatformOverride Override = mOverrides[Index];
	mOverrides.RemoveAt(Index);
	if (!IsValid(Platform))
	{
		return;
	}
	if (Platform->GetIsInLoadMode() != Override.bForcedLoadMode)
	{
		UE_LOG(LogFreightCarControl, Log, TEXT("%s: setting was changed during the dock, left as %s"), *GetNameSafe(Platform),
			Platform->GetIsInLoadMode() ? TEXT("load") : TEXT("unload"));
		return;
	}
	Platform->SetIsInLoadMode(Override.bOriginalLoadMode);
	UE_LOG(LogFreightCarControl, Log, TEXT("%s restored to %s"), *GetNameSafe(Platform), Override.bOriginalLoadMode ? TEXT("load") : TEXT("unload"));
}

void AFCCRuleSubsystem::Sweep()
{
	TArray<AFGBuildableTrainPlatformCargo*> Ended;
	for (const FFCCPlatformOverride& Override : mOverrides)
	{
		if (!IsValid(Override.Platform) || !IsValid(Override.Vehicle) || Override.Platform->GetDockedActor() != Override.Vehicle)
		{
			Ended.Add(Override.Platform);
		}
	}
	for (AFGBuildableTrainPlatformCargo* Platform : Ended)
	{
		EndPlatformOverride(Platform);
	}
	ProcessPendingRules();
}

TArray<AFGFreightWagon*> AFCCRuleSubsystem::GetFreightCars(const AFGTrain* Train)
{
	TArray<AFGFreightWagon*> Cars;
	auto AddIfFreight = [&Cars](const AFGRailroadVehicle* Vehicle)
	{
		if (AFGFreightWagon* Wagon = Cast<AFGFreightWagon>(const_cast<AFGRailroadVehicle*>(Vehicle)))
		{
			Cars.Add(Wagon);
		}
	};
	if (!Train)
	{
		return Cars;
	}
	if (Train->GetFirstVehicle())
	{
		for (AFGRailroadSubsystem::TConstTrainIterator It(Train->GetFirstVehicle()); It; ++It)
		{
			AddIfFreight(*It);
		}
		return Cars;
	}

	// On clients, reads the train replication actor's vehicle list instead (see Docs/Architecture.md, Multiplayer).
	static FObjectPropertyBase* ReplicationActorProperty = FindFProperty<FObjectPropertyBase>(AFGTrain::StaticClass(), TEXT("mTrainReplicationActor"));
	const UObject* ReplicationActor = ReplicationActorProperty ? ReplicationActorProperty->GetObjectPropertyValue_InContainer(Train) : nullptr;
	const FArrayProperty* VehiclesProperty = ReplicationActor ? FindFProperty<FArrayProperty>(ReplicationActor->GetClass(), TEXT("mVehicles")) : nullptr;
	const FObjectPropertyBase* VehicleProperty = VehiclesProperty ? CastField<FObjectPropertyBase>(VehiclesProperty->Inner) : nullptr;
	if (!VehicleProperty)
	{
		UE_LOG(LogFreightCarControl, Warning, TEXT("%s: vehicle list not found on the client (replication actor %s)"),
			*Train->GetTrainName().ToString(), *GetNameSafe(ReplicationActor));
		return Cars;
	}
	FScriptArrayHelper Vehicles(VehiclesProperty, VehiclesProperty->ContainerPtrToValuePtr<void>(ReplicationActor));
	for (int32 Index = 0; Index < Vehicles.Num(); ++Index)
	{
		AddIfFreight(Cast<AFGRailroadVehicle>(VehicleProperty->GetObjectPropertyValue(Vehicles.GetRawPtr(Index))));
	}
	return Cars;
}

int32 AFCCRuleSubsystem::GetFreightCarIndex(const AFGRailroadVehicle* Vehicle)
{
	return Vehicle ? GetFreightCars(Vehicle->GetTrain()).IndexOfByKey(Vehicle) : INDEX_NONE;
}

void AFCCRuleSubsystem::Compact()
{
	mRules.RemoveAll([](FFCCTimeTableRules& Entry)
	{
		if (!IsValid(Entry.TimeTable))
		{
			return true;
		}
		Entry.Stops.SetNum(FMath::Min(Entry.Stops.Num(), Entry.TimeTable->GetNumStops()));
		for (FFCCStopRule& Stop : Entry.Stops)
		{
			Stop.Trim();
		}
		while (Entry.Stops.Num() > 0 && Entry.Stops.Last().IsDefault())
		{
			Entry.Stops.Pop(EAllowShrinking::No);
		}
		return Entry.IsDefault();
	});
	mOverrides.RemoveAll([](const FFCCPlatformOverride& O) { return !IsValid(O.Platform); });
}

void AFCCRuleSubsystem::RulesChanged(const TCHAR* Reason)
{
	++mRevision;
	UE_LOG(LogFreightCarControl, Verbose, TEXT("Rules changed (%s), revision %u"), Reason, mRevision);
	if (HasAuthority())
	{
		ForceNetUpdate();
	}
}

void AFCCRuleSubsystem::OnRep_Rules()
{
	++mRevision;
	UE_LOG(LogFreightCarControl, Verbose, TEXT("Rules replicated: %d time tables, revision %u"), mRules.Num(), mRevision);
}
