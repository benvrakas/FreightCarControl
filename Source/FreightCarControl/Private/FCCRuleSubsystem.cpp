#include "FCCRuleSubsystem.h"
#include "FreightCarControl.h"
#include "FGFreightWagon.h"
#include "FGRailroadSubsystem.h"
#include "FGRailroadTimeTable.h"
#include "FGRailroadVehicle.h"
#include "FGTrain.h"
#include "FGTrainStationIdentifier.h"
#include "Net/UnrealNetwork.h"
#include "Subsystem/SubsystemActorManager.h"

namespace
{
	// Largest freight car position a rule may name; longer trains than this aren't buildable in practice.
	constexpr int32 MaxCarIndex = 255;

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
	UE_LOG(LogFreightCarControl, Log, TEXT("Rule subsystem started (%s), %d time tables with rules"),
		HasAuthority() ? TEXT("server") : TEXT("client"), mRules.Num());
}

void AFCCRuleSubsystem::PreSaveGame_Implementation(int32 SaveVersion, int32 GameVersion)
{
	Compact();
	UE_LOG(LogFreightCarControl, Log, TEXT("Saving rules for %d time tables"), mRules.Num());
}

void AFCCRuleSubsystem::PostLoadGame_Implementation(int32 SaveVersion, int32 GameVersion)
{
	const int32 Loaded = mRules.Num();
	Compact();
	UE_LOG(LogFreightCarControl, Log, TEXT("Loaded rules for %d time tables (%d dropped as stale)"), mRules.Num(), Loaded - mRules.Num());
	for (const FFCCTimeTableRules& Entry : mRules)
	{
		UE_LOG(LogFreightCarControl, Verbose, TEXT("  %s: %d stop rules"), *DescribeTimeTable(Entry.TimeTable), Entry.Stops.Num());
	}
	RulesChanged(TEXT("load"));
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
	if (FFCCTimeTableRules* Found = mRules.FindByPredicate([TimeTable](const FFCCTimeTableRules& Entry) { return Entry.TimeTable == TimeTable; }))
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
		return EFCCCarMode::Transfer;
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
	if (Rule.Cars.Num() <= CarIndex)
	{
		Rule.Cars.SetNum(CarIndex + 1);
	}
	Rule.Cars[CarIndex] = Mode;
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
	UE_LOG(LogFreightCarControl, Log, TEXT("%s stop %d: %d cars ruled, skips=%d"), *DescribeTimeTable(TimeTable), StopIndex,
		Entry.Stops[StopIndex].Cars.Num(), Entry.Stops[StopIndex].Cars.FilterByPredicate([](EFCCCarMode M) { return M == EFCCCarMode::Skip; }).Num());
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
	for (FFCCStopRule& Stop : Entry.Stops)
	{
		Stop.Cars.SetNum(FMath::Min(Stop.Cars.Num(), MaxCarIndex + 1));
		Stop.Trim();
	}
	UE_LOG(LogFreightCarControl, Log, TEXT("%s: replaced all rules (%d stops)"), *DescribeTimeTable(TimeTable), Entry.Stops.Num());
	RulesChanged(TEXT("set all rules"));
}

void AFCCRuleSubsystem::NotifyStopInserted(AFGRailroadTimeTable* TimeTable, int32 StopIndex)
{
	FFCCTimeTableRules* Entry = const_cast<FFCCTimeTableRules*>(FindEntry(TimeTable));
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
	FFCCTimeTableRules* Entry = const_cast<FFCCTimeTableRules*>(FindEntry(TimeTable));
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
	FFCCTimeTableRules* Entry = const_cast<FFCCTimeTableRules*>(FindEntry(TimeTable));
	if (!Entry)
	{
		return;
	}
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
