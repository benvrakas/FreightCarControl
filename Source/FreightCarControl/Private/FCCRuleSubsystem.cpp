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

	// Seconds between checks for platform overrides whose dock has ended.
	constexpr float SweepInterval = 2.0f;

	// Seconds a client's timetable save waits for the matching stop list before it is dropped.
	constexpr double PendingLifetime = 30.0;

	// Returns "station#occurrence" keys for Stops, so the same station visited twice gets two keys.
	TArray<TPair<const AFGTrainStationIdentifier*, int32>> MakeStopKeys(const TArray<FTimeTableStop>& Stops)
	{
		TArray<TPair<const AFGTrainStationIdentifier*, int32>> Keys;
		TMap<const AFGTrainStationIdentifier*, int32> Seen;
		for (const FTimeTableStop& Stop : Stops)
		{
			int32& Count = Seen.FindOrAdd(Stop.Station);
			Keys.Emplace(Stop.Station, Count++);
		}
		return Keys;
	}

	// Returns the stations Stops visit, in order.
	TArray<const AFGTrainStationIdentifier*> GetStations(const TArray<FTimeTableStop>& Stops)
	{
		TArray<const AFGTrainStationIdentifier*> Stations;
		for (const FTimeTableStop& Stop : Stops)
		{
			Stations.Add(Stop.Station);
		}
		return Stations;
	}

	FString DescribeTimeTable(const AFGRailroadTimeTable* TimeTable)
	{
		const AFGTrain* Train = TimeTable ? Cast<AFGTrain>(TimeTable->GetOwner()) : nullptr;
		return Train ? FString::Printf(TEXT("%s (%s)"), *Train->GetTrainName().ToString(), *GetNameSafe(TimeTable)) : GetNameSafe(TimeTable);
	}

	FString DescribeRule(const FFCCStopRule& Rule)
	{
		FString Codes;
		for (const EFCCCarMode Mode : Rule.Cars)
		{
			Codes += FCCCarMode::GetCode(Mode);
		}
		return Codes;
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
		GetWorldTimerManager().SetTimer(mSweepTimer, this, &AFCCRuleSubsystem::SweepOverrides, SweepInterval, true);
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
	const FFCCTimeTableRules* Entry = FindEntry(TimeTable);
	if (!Entry || !Entry->Stops.IsValidIndex(StopIndex))
	{
		return EFCCCarMode::Platform;
	}
	return Entry->Stops[StopIndex].GetCarMode(CarIndex);
}

FFCCStopRule AFCCRuleSubsystem::GetStopRule(const AFGRailroadTimeTable* TimeTable, int32 StopIndex) const
{
	const FFCCTimeTableRules* Entry = FindEntry(TimeTable);
	return Entry && Entry->Stops.IsValidIndex(StopIndex) ? Entry->Stops[StopIndex] : FFCCStopRule();
}

TArray<FFCCStopRule> AFCCRuleSubsystem::GetAllStopRules(const AFGRailroadTimeTable* TimeTable) const
{
	TArray<FFCCStopRule> Result;
	if (const FFCCTimeTableRules* Entry = FindEntry(TimeTable))
	{
		Result = Entry->Stops;
	}
	if (TimeTable && Result.Num() < TimeTable->GetNumStops())
	{
		Result.SetNum(TimeTable->GetNumStops());
	}
	return Result;
}

void AFCCRuleSubsystem::SetCarMode(AFGRailroadTimeTable* TimeTable, int32 StopIndex, int32 CarIndex, EFCCCarMode Mode)
{
	if (!HasAuthority() || !TimeTable || StopIndex < 0 || StopIndex >= TimeTable->GetNumStops() || CarIndex < 0 || CarIndex > MaxCarIndex)
	{
		UE_LOG(LogFreightCarControl, Warning, TEXT("SetCarMode rejected: authority=%d timetable=%s stop=%d car=%d"),
			HasAuthority(), *GetNameSafe(TimeTable), StopIndex, CarIndex);
		return;
	}
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
	UE_LOG(LogFreightCarControl, Log, TEXT("%s stop %d: cars [%s]"), *DescribeTimeTable(TimeTable), StopIndex, *DescribeRule(Entry.Stops[StopIndex]));
	RulesChanged(TEXT("set stop rule"));
}

void AFCCRuleSubsystem::SetAllStopRules(AFGRailroadTimeTable* TimeTable, const TArray<FFCCStopRule>& Rules)
{
	if (!HasAuthority() || !TimeTable || Rules.Num() > TimeTable->GetMaxNumStops())
	{
		UE_LOG(LogFreightCarControl, Warning, TEXT("SetAllStopRules rejected: authority=%d timetable=%s rules=%d"),
			HasAuthority(), *GetNameSafe(TimeTable), Rules.Num());
		return;
	}
	FFCCTimeTableRules& Entry = FindOrAddEntry(TimeTable);
	Entry.Stops = Rules;
	Entry.Stops.SetNum(FMath::Min(Entry.Stops.Num(), TimeTable->GetNumStops()));
	for (int32 Index = 0; Index < Entry.Stops.Num(); ++Index)
	{
		FFCCStopRule& Stop = Entry.Stops[Index];
		Stop.Cars.SetNum(FMath::Min(Stop.Cars.Num(), MaxCarIndex + 1));
		Stop.Trim();
		UE_LOG(LogFreightCarControl, Verbose, TEXT("  stop %d: cars [%s]"), Index, *DescribeRule(Stop));
	}
	UE_LOG(LogFreightCarControl, Log, TEXT("%s: replaced all rules (%d stops)"), *DescribeTimeTable(TimeTable), Entry.Stops.Num());
	RulesChanged(TEXT("set all rules"));
}

void AFCCRuleSubsystem::SetAllStopRulesWhenStopsMatch(AFGTrain* Train, const TArray<FFCCStopRule>& Rules, const TArray<AFGTrainStationIdentifier*>& ExpectedStations)
{
	if (!HasAuthority() || !Train)
	{
		return;
	}
	mPending.RemoveAll([Train](const FFCCPendingRules& Pending) { return Pending.Train == Train; });
	FFCCPendingRules& Pending = mPending.AddDefaulted_GetRef();
	Pending.Train = Train;
	Pending.Rules = Rules;
	Pending.ExpectedStations.Append(ExpectedStations);
	Pending.ReceivedTime = FPlatformTime::Seconds();
	if (AFGRailroadTimeTable* TimeTable = Train->GetTimeTable())
	{
		TryApplyPending(TimeTable);
	}
	if (mPending.ContainsByPredicate([Train](const FFCCPendingRules& P) { return P.Train == Train; }))
	{
		UE_LOG(LogFreightCarControl, Log, TEXT("%s: rules for %d stops wait for the timetable's new stops"), *Train->GetTrainName().ToString(), Rules.Num());
	}
}

void AFCCRuleSubsystem::TryApplyPending(AFGRailroadTimeTable* TimeTable)
{
	const int32 Index = mPending.IndexOfByPredicate([TimeTable](const FFCCPendingRules& Pending)
	{
		return IsValid(Pending.Train) && TimeTable && Pending.Train->GetTimeTable() == TimeTable;
	});
	if (Index == INDEX_NONE || !TimeTable)
	{
		return;
	}
	TArray<FTimeTableStop> Stops;
	TimeTable->GetStops(Stops);
	const TArray<const AFGTrainStationIdentifier*> Current = GetStations(Stops);
	TArray<const AFGTrainStationIdentifier*> Expected;
	for (const TObjectPtr<AFGTrainStationIdentifier>& Station : mPending[Index].ExpectedStations)
	{
		Expected.Add(Station);
	}
	if (Current.Num() != mPending[Index].Rules.Num())
	{
		UE_LOG(LogFreightCarControl, Log, TEXT("%s: rules from the timetable editor are for %d stops, timetable has %d; waiting"),
			*DescribeTimeTable(TimeTable), mPending[Index].Rules.Num(), Current.Num());
		return;
	}
	const FFCCPendingRules Pending = mPending[Index];
	mPending.RemoveAt(Index);
	UE_LOG(LogFreightCarControl, Log, TEXT("%s: applying rules saved from the timetable editor%s"), *DescribeTimeTable(TimeTable),
		Current == Expected ? TEXT("") : TEXT(" (some stops' stations differ from the editor's, e.g. changed by another mod; matched by position)"));
	SetAllStopRules(TimeTable, Pending.Rules);
}

void AFCCRuleSubsystem::NotifyStopInserted(AFGRailroadTimeTable* TimeTable, int32 StopIndex)
{
	FFCCTimeTableRules* Entry = FindEntry(TimeTable);
	if (!Entry || StopIndex < 0 || StopIndex >= Entry->Stops.Num())
	{
		return;
	}
	Entry->Stops.Insert(FFCCStopRule(), StopIndex);
	UE_LOG(LogFreightCarControl, Log, TEXT("%s: stop inserted at %d, rules shifted"), *DescribeTimeTable(TimeTable), StopIndex);
	RulesChanged(TEXT("stop inserted"));
}

void AFCCRuleSubsystem::NotifyStopRemoved(AFGRailroadTimeTable* TimeTable, int32 StopIndex)
{
	FFCCTimeTableRules* Entry = FindEntry(TimeTable);
	if (!Entry || !Entry->Stops.IsValidIndex(StopIndex))
	{
		return;
	}
	Entry->Stops.RemoveAt(StopIndex);
	UE_LOG(LogFreightCarControl, Log, TEXT("%s: stop %d removed, rules shifted"), *DescribeTimeTable(TimeTable), StopIndex);
	RulesChanged(TEXT("stop removed"));
}

void AFCCRuleSubsystem::NotifyStopsReplaced(AFGRailroadTimeTable* TimeTable, const TArray<FTimeTableStop>& OldStops, const TArray<FTimeTableStop>& NewStops)
{
	FFCCTimeTableRules* Entry = FindEntry(TimeTable);
	if (Entry)
	{
		const TArray<TPair<const AFGTrainStationIdentifier*, int32>> OldKeys = MakeStopKeys(OldStops);
		const TArray<TPair<const AFGTrainStationIdentifier*, int32>> NewKeys = MakeStopKeys(NewStops);
		TArray<FFCCStopRule> Remapped;
		Remapped.SetNum(NewStops.Num());
		int32 Kept = 0;
		for (int32 NewIndex = 0; NewIndex < NewKeys.Num(); ++NewIndex)
		{
			const int32 OldIndex = OldKeys.IndexOfByKey(NewKeys[NewIndex]);
			if (Entry->Stops.IsValidIndex(OldIndex))
			{
				Remapped[NewIndex] = Entry->Stops[OldIndex];
				Kept += Remapped[NewIndex].IsDefault() ? 0 : 1;
			}
		}
		Entry->Stops = MoveTemp(Remapped);
		UE_LOG(LogFreightCarControl, Log, TEXT("%s: stops replaced (%d -> %d), %d stop rules kept"),
			*DescribeTimeTable(TimeTable), OldStops.Num(), NewStops.Num(), Kept);
		RulesChanged(TEXT("stops replaced"));
	}
	TryApplyPending(TimeTable);
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

void AFCCRuleSubsystem::SweepOverrides()
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
	mOverrides.RemoveAll([](const FFCCPlatformOverride& O) { return !IsValid(O.Platform); });

	const double Now = FPlatformTime::Seconds();
	mPending.RemoveAll([Now](const FFCCPendingRules& Pending)
	{
		const bool bStale = !IsValid(Pending.Train) || Now - Pending.ReceivedTime > PendingLifetime;
		if (bStale)
		{
			UE_LOG(LogFreightCarControl, Warning, TEXT("Dropped rules from the timetable editor for %s: the saved stops never arrived"), *GetNameSafe(Pending.Train));
		}
		return bStale;
	});
}

int32 AFCCRuleSubsystem::GetFreightCarIndex(const AFGRailroadVehicle* Vehicle)
{
	const AFGTrain* Train = Vehicle ? Vehicle->GetTrain() : nullptr;
	if (!Train || !Vehicle->IsA<AFGFreightWagon>())
	{
		return INDEX_NONE;
	}
	int32 Index = 0;
	for (AFGRailroadSubsystem::TConstTrainIterator It(Train->GetFirstVehicle()); It; ++It)
	{
		if (*It == Vehicle)
		{
			return Index;
		}
		if (It->IsA<AFGFreightWagon>())
		{
			++Index;
		}
	}
	return INDEX_NONE;
}

int32 AFCCRuleSubsystem::GetFreightCarCount(const AFGTrain* Train)
{
	int32 Count = 0;
	if (Train)
	{
		for (AFGRailroadSubsystem::TConstTrainIterator It(Train->GetFirstVehicle()); It; ++It)
		{
			Count += It->IsA<AFGFreightWagon>() ? 1 : 0;
		}
	}
	return Count;
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
	UE_LOG(LogFreightCarControl, Verbose, TEXT("Rules changed: %s"), Reason);
	if (HasAuthority())
	{
		ForceNetUpdate();
	}
	OnRulesChanged.Broadcast();
}

void AFCCRuleSubsystem::OnRep_Rules()
{
	UE_LOG(LogFreightCarControl, Verbose, TEXT("Rules replicated: %d time tables"), mRules.Num());
	OnRulesChanged.Broadcast();
}
