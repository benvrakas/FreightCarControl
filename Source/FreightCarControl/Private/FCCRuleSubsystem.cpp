#include "FCCRuleSubsystem.h"
#include "FreightCarControl.h"
#include "Buildables/FGBuildableTrainPlatformCargo.h"
#include "FGFreightWagon.h"
#include "FGLocomotive.h"
#include "FGRailroadSubsystem.h"
#include "FGRailroadTimeTable.h"
#include "FGRailroadVehicle.h"
#include "FGTrain.h"
#include "FGTrainStationIdentifier.h"
#include "Net/UnrealNetwork.h"
#include "Subsystem/SubsystemActorManager.h"
#include "TimerManager.h"
#include "Algo/Reverse.h"

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

	// Returns the stops' station names, numbered, e.g. "1. Iron SRC, 2. Iron SNK".
	FString DescribeStops(const TArray<FTimeTableStop>& Stops)
	{
		TArray<FString> Names;
		for (int32 Index = 0; Index < Stops.Num(); ++Index)
		{
			Names.Add(FString::Printf(TEXT("%d. %s"), Index + 1, Stops[Index].Station ? *Stops[Index].Station->GetStationName().ToString() : TEXT("?")));
		}
		return FString::Join(Names, TEXT(", "));
	}

	// Returns, for each new stop, the old stop whose rule it keeps (INDEX_NONE: none). A reorder moves rules with their
	// stations; the same stop count with changed stations keeps them by position; otherwise stops match by
	// (station, occurrence), then by position.
	TArray<int32> MapReplacedStops(const TArray<FTimeTableStop>& OldStops, const TArray<FTimeTableStop>& NewStops)
	{
		TArray<int32> Source;
		Source.Init(INDEX_NONE, NewStops.Num());
		if (OldStops.Num() == NewStops.Num())
		{
			auto SortedStations = [](const TArray<FTimeTableStop>& Stops)
			{
				TArray<UPTRINT> Stations;
				for (const FTimeTableStop& Stop : Stops)
				{
					Stations.Add(reinterpret_cast<UPTRINT>(Stop.Station.Get()));
				}
				Stations.Sort();
				return Stations;
			};
			if (SortedStations(OldStops) != SortedStations(NewStops))
			{
				for (int32 Index = 0; Index < NewStops.Num(); ++Index)
				{
					Source[Index] = Index;
				}
				return Source;
			}
		}
		const TArray<TPair<const AFGTrainStationIdentifier*, int32>> OldKeys = MakeStopKeys(OldStops);
		const TArray<TPair<const AFGTrainStationIdentifier*, int32>> NewKeys = MakeStopKeys(NewStops);
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
		return Source;
	}

	// Returns every vehicle of Train in the order the game stores it (from its first vehicle).
	TArray<AFGRailroadVehicle*> GetStoredVehicles(const AFGTrain* Train)
	{
		TArray<AFGRailroadVehicle*> Vehicles;
		if (!Train)
		{
			return Vehicles;
		}
		if (Train->GetFirstVehicle())
		{
			for (AFGRailroadSubsystem::TConstTrainIterator It(Train->GetFirstVehicle()); It; ++It)
			{
				Vehicles.Add(const_cast<AFGRailroadVehicle*>(*It));
			}
			return Vehicles;
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
			return Vehicles;
		}
		FScriptArrayHelper Array(VehiclesProperty, VehiclesProperty->ContainerPtrToValuePtr<void>(ReplicationActor));
		for (int32 Index = 0; Index < Array.Num(); ++Index)
		{
			if (AFGRailroadVehicle* Vehicle = Cast<AFGRailroadVehicle>(VehicleProperty->GetObjectPropertyValue(Array.GetRawPtr(Index))))
			{
				Vehicles.Add(Vehicle);
			}
		}
		return Vehicles;
	}

	// Returns the first locomotive in Vehicles, or nullptr.
	AFGLocomotive* FindFirstLocomotive(const TArray<AFGRailroadVehicle*>& Vehicles)
	{
		for (AFGRailroadVehicle* Vehicle : Vehicles)
		{
			if (AFGLocomotive* Locomotive = Cast<AFGLocomotive>(Vehicle))
			{
				return Locomotive;
			}
		}
		return nullptr;
	}

	// Returns the locomotive a train without a chosen front counts from: the one in control, or else the first one.
	AFGLocomotive* FindDefaultFront(const AFGTrain* Train, const TArray<AFGRailroadVehicle*>& Vehicles)
	{
		AFGLocomotive* Master = Train ? Train->GetMultipleUnitMaster() : nullptr;
		return Master && Vehicles.Contains(Master) ? Master : FindFirstLocomotive(Vehicles);
	}

	// Returns true if Vehicle sits in the later half of Vehicles.
	bool IsNearStoredEnd(const TArray<AFGRailroadVehicle*>& Vehicles, const AFGRailroadVehicle* Vehicle)
	{
		const int32 Index = Vehicles.IndexOfByKey(Vehicle);
		return Index != INDEX_NONE && Index > Vehicles.Num() - 1 - Index;
	}

	// Returns true if Entry's cars are counted from the end of Vehicles rather than its start.
	bool CountsFromStoredEnd(const AFGTrain* Train, const TArray<AFGRailroadVehicle*>& Vehicles, const FFCCTimeTableRules* Entry)
	{
		if (Entry && !Entry->bFrontChosen)
		{
			return false;
		}
		const bool bUseEntry = Entry && IsValid(Entry->FrontLocomotive) && Vehicles.Contains(Entry->FrontLocomotive);
		const AFGLocomotive* Front = bUseEntry ? Entry->FrontLocomotive.Get() : FindDefaultFront(Train, Vehicles);
		const bool bFromFarEnd = bUseEntry && Entry->bCountFromFarEnd;
		return Front && IsNearStoredEnd(Vehicles, Front) != bFromFarEnd;
	}

	// Returns the vehicles' object names, e.g. "[BP_Locomotive_C_1, BP_FreightWagon_C_2]".
	template <typename T>
	FString DescribeVehicles(const TArray<T>& Vehicles)
	{
		return FString::Printf(TEXT("[%s]"), *FString::JoinBy(Vehicles, TEXT(", "), [](const T& Vehicle) { return GetNameSafe(Vehicle.Get()); }));
	}

	// Returns the codes of each stop's rule, e.g. "[P-] [L] []".
	FString DescribeRules(const TArray<FFCCStopRule>& Rules)
	{
		return FString::JoinBy(Rules, TEXT(" "), [](const FFCCStopRule& Rule) { return FString::Printf(TEXT("[%s]"), *Rule.ToCodes()); });
	}

	// Returns Array reversed.
	template <typename T>
	TArray<T> Reversed(TArray<T> Array)
	{
		Algo::Reverse(Array);
		return Array;
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
	for (const FFCCTimeTableRules& Entry : mRules)
	{
		UE_LOG(LogFreightCarControl, Log, TEXT("  %s: front %s%s, stops %s"), *Describe(Entry.TimeTable),
			*GetNameSafe(Entry.FrontLocomotive), Entry.bCountFromFarEnd ? TEXT(" (counted from the far end)") : TEXT(""), *DescribeRules(Entry.Stops));
	}
}

void AFCCRuleSubsystem::PostLoadGame_Implementation(int32 SaveVersion, int32 GameVersion)
{
	const int32 Loaded = mRules.Num();
	Compact();
	UE_LOG(LogFreightCarControl, Log, TEXT("Loaded rules for %d time tables (%d dropped as stale), %d platform overrides"),
		mRules.Num(), Loaded - mRules.Num(), mOverrides.Num());
	for (const FFCCTimeTableRules& Entry : mRules)
	{
		UE_LOG(LogFreightCarControl, Log, TEXT("  %s: front %s%s, stops %s"), *GetNameSafe(Entry.Train),
			*GetNameSafe(Entry.FrontLocomotive), Entry.bCountFromFarEnd ? TEXT(" (counted from the far end)") : TEXT(""), *DescribeRules(Entry.Stops));
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
	EnsureFrontLocomotive(Added);
	return Added;
}

AFGTrain* AFCCRuleSubsystem::FindTrain(FFCCTimeTableRules& Entry) const
{
	if (IsValid(Entry.Train) && Entry.Train->GetTimeTable() == Entry.TimeTable)
	{
		return Entry.Train;
	}
	Entry.Train = nullptr;
	if (AFGRailroadSubsystem* Railroad = AFGRailroadSubsystem::Get(const_cast<AFCCRuleSubsystem*>(this)))
	{
		TArray<AFGTrain*> Trains;
		Railroad->GetAllTrains(Trains);
		for (AFGTrain* Train : Trains)
		{
			if (Train && Entry.TimeTable && Train->GetTimeTable() == Entry.TimeTable)
			{
				Entry.Train = Train;
				break;
			}
		}
	}
	return Entry.Train;
}

FString AFCCRuleSubsystem::Describe(const AFGRailroadTimeTable* TimeTable) const
{
	const FFCCTimeTableRules* Entry = FindEntry(TimeTable);
	const AFGTrain* Train = Entry ? FindTrain(const_cast<FFCCTimeTableRules&>(*Entry)) : nullptr;
	return Train ? Train->GetTrainName().ToString() : GetNameSafe(TimeTable);
}

void AFCCRuleSubsystem::EnsureFrontLocomotive(FFCCTimeTableRules& Entry)
{
	AFGTrain* Train = HasAuthority() ? FindTrain(Entry) : nullptr;
	const TArray<AFGRailroadVehicle*> Vehicles = GetStoredVehicles(Train);
	if (!Train || (Entry.bFrontChosen && IsValid(Entry.FrontLocomotive) && Vehicles.Contains(Entry.FrontLocomotive)))
	{
		return;
	}
	const bool bLegacy = !Entry.bFrontChosen && !Entry.IsDefault();
	const bool bLost = Entry.bFrontChosen;
	AFGLocomotive* Front = bLegacy ? FindFirstLocomotive(Vehicles) : FindDefaultFront(Train, Vehicles);
	if (!Front)
	{
		return;
	}
	Entry.FrontLocomotive = Front;
	Entry.bCountFromFarEnd = bLegacy && IsNearStoredEnd(Vehicles, Front);
	Entry.bFrontChosen = true;
	if (bLost)
	{
		UE_LOG(LogFreightCarControl, Warning, TEXT("%s: its front locomotive left the train; cars are now counted from %s. Check its car rules."),
			*Train->GetTrainName().ToString(), *GetNameSafe(Front));
	}
	else
	{
		UE_LOG(LogFreightCarControl, Log, TEXT("%s: cars counted from locomotive %s%s"), *Train->GetTrainName().ToString(), *GetNameSafe(Front),
			bLegacy ? (Entry.bCountFromFarEnd ? TEXT(" (from its far end, as before)") : TEXT(" (as before)")) : TEXT(""));
	}
	RulesChanged(TEXT("front locomotive"));
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

void AFCCRuleSubsystem::SetCarMode(AFGRailroadTimeTable* TimeTable, int32 StopIndex, int32 CarIndex, EFCCCarMode Mode, const FString& Cause)
{
	FFCCStopRule Rule = GetStopRule(TimeTable, StopIndex);
	Rule.SetCarMode(CarIndex, Mode);
	SetStopRule(TimeTable, StopIndex, Rule, Cause);
}

void AFCCRuleSubsystem::SetStopRule(AFGRailroadTimeTable* TimeTable, int32 StopIndex, const FFCCStopRule& Rule, const FString& Cause)
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
	const FString Before = Entry.Stops[StopIndex].ToCodes();
	Entry.Stops[StopIndex] = Rule;
	Entry.Stops[StopIndex].Trim();
	const AFGTrainStationIdentifier* Station = TimeTable->GetStop(StopIndex).Station;
	UE_LOG(LogFreightCarControl, Log, TEXT("%s stop %d (%s): [%s] -> [%s] by %s"), *Describe(TimeTable), StopIndex + 1,
		Station ? *Station->GetStationName().ToString() : TEXT("?"), *Before, *Entry.Stops[StopIndex].ToCodes(), *Cause);
	RulesChanged(TEXT("set stop rule"));
}

void AFCCRuleSubsystem::SetAllStopRules(AFGRailroadTimeTable* TimeTable, const TArray<FFCCStopRule>& Rules, const FString& Cause)
{
	if (!HasAuthority() || !TimeTable)
	{
		UE_LOG(LogFreightCarControl, Warning, TEXT("SetAllStopRules rejected: authority=%d timetable=%s"), HasAuthority(), *GetNameSafe(TimeTable));
		return;
	}
	FFCCTimeTableRules& Entry = FindOrAddEntry(TimeTable);
	const FString Before = DescribeRules(Entry.Stops);
	Entry.Stops = Rules;
	Entry.Stops.SetNum(FMath::Min(Entry.Stops.Num(), TimeTable->GetNumStops()));
	for (FFCCStopRule& Stop : Entry.Stops)
	{
		Stop.Cars.SetNum(FMath::Min(Stop.Cars.Num(), MaxCarIndex + 1));
		Stop.Trim();
	}
	UE_LOG(LogFreightCarControl, Log, TEXT("%s: all stop rules %s -> %s by %s"), *Describe(TimeTable), *Before, *DescribeRules(Entry.Stops), *Cause);
	RulesChanged(TEXT("set all rules"));
}

void AFCCRuleSubsystem::QueueEditorRules(AFGTrain* Train, int32 StopCount, const TArray<int32>& StopIndices, const TArray<FFCCStopRule>& Rules, const FString& Cause)
{
	if (!HasAuthority() || !Train || StopIndices.Num() != Rules.Num())
	{
		return;
	}
	mPending.RemoveAll([Train](const FFCCPendingRules& Pending) { return Pending.Train == Train; });
	FFCCPendingRules& Pending = mPending.AddDefaulted_GetRef();
	Pending.Train = Train;
	Pending.StopCount = StopCount;
	Pending.StopIndices = StopIndices;
	Pending.Rules = Rules;
	Pending.Cause = Cause;
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
		if (TimeTable && TimeTable->GetNumStops() == Pending.StopCount)
		{
			const FFCCPendingRules Applied = Pending;
			mPending.RemoveAt(Index);
			UE_LOG(LogFreightCarControl, Log, TEXT("%s: applying %d stop rules edited in the timetable editor"), *Describe(TimeTable), Applied.Rules.Num());
			for (int32 Edit = 0; Edit < Applied.StopIndices.Num(); ++Edit)
			{
				SetStopRule(TimeTable, Applied.StopIndices[Edit], Applied.Rules[Edit], Applied.Cause);
			}
		}
		else if (!IsValid(Pending.Train) || Now - Pending.ReceivedTime > PendingLifetime)
		{
			UE_LOG(LogFreightCarControl, Warning, TEXT("Dropped rules from the timetable editor for %s: its timetable never got %d stops (has %d)"),
				*GetNameSafe(Pending.Train), Pending.StopCount, TimeTable ? TimeTable->GetNumStops() : 0);
			mPending.RemoveAt(Index);
		}
		else
		{
			UE_LOG(LogFreightCarControl, Verbose, TEXT("%s: editor rules for %d stops wait (timetable has %d)"),
				*GetNameSafe(Pending.Train), Pending.StopCount, TimeTable ? TimeTable->GetNumStops() : 0);
		}
	}
}

void AFCCRuleSubsystem::NotifyStopInserted(AFGRailroadTimeTable* TimeTable, int32 StopIndex)
{
	FFCCTimeTableRules* Entry = FindEntry(TimeTable);
	if (Entry && StopIndex >= 0 && StopIndex < Entry->Stops.Num())
	{
		const FString Before = DescribeRules(Entry->Stops);
		Entry->Stops.Insert(FFCCStopRule(), StopIndex);
		UE_LOG(LogFreightCarControl, Log, TEXT("%s: stop inserted at %d, rules %s -> %s"), *Describe(TimeTable), StopIndex + 1, *Before, *DescribeRules(Entry->Stops));
		RulesChanged(TEXT("stop inserted"));
	}
	ProcessPendingRules();
}

void AFCCRuleSubsystem::NotifyStopRemoved(AFGRailroadTimeTable* TimeTable, int32 StopIndex)
{
	FFCCTimeTableRules* Entry = FindEntry(TimeTable);
	if (Entry && Entry->Stops.IsValidIndex(StopIndex))
	{
		const FString Before = DescribeRules(Entry->Stops);
		Entry->Stops.RemoveAt(StopIndex);
		UE_LOG(LogFreightCarControl, Log, TEXT("%s: stop %d removed, rules %s -> %s"), *Describe(TimeTable), StopIndex + 1, *Before, *DescribeRules(Entry->Stops));
		RulesChanged(TEXT("stop removed"));
	}
	ProcessPendingRules();
}

void AFCCRuleSubsystem::NotifyStopsReplaced(AFGRailroadTimeTable* TimeTable, const TArray<FTimeTableStop>& OldStops, const TArray<FTimeTableStop>& NewStops)
{
	FFCCTimeTableRules* Entry = FindEntry(TimeTable);
	if (Entry)
	{
		const TArray<int32> Source = MapReplacedStops(OldStops, NewStops);
		TArray<FFCCStopRule> Remapped;
		Remapped.SetNum(NewStops.Num());
		for (int32 NewIndex = 0; NewIndex < NewStops.Num(); ++NewIndex)
		{
			if (Entry->Stops.IsValidIndex(Source[NewIndex]))
			{
				Remapped[NewIndex] = Entry->Stops[Source[NewIndex]];
			}
		}
		const FString RulesBefore = DescribeRules(Entry->Stops);
		if (Remapped != Entry->Stops)
		{
			Entry->Stops = MoveTemp(Remapped);
			RulesChanged(TEXT("stops replaced"));
		}
		UE_LOG(LogFreightCarControl, Log, TEXT("%s: stops replaced. Before: %s. After: %s. Rules taken from old stops: %s. Rules %s -> %s"),
			*Describe(TimeTable), *DescribeStops(OldStops), *DescribeStops(NewStops),
			*FString::JoinBy(Source, TEXT(" "), [](int32 Index) { return Index == INDEX_NONE ? FString(TEXT("-")) : FString::FromInt(Index + 1); }),
			*RulesBefore, *DescribeRules(Entry->Stops));
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
	for (FFCCTimeTableRules& Entry : mRules)
	{
		EnsureFrontLocomotive(Entry);
	}
	WatchCarOrder();
	ProcessPendingRules();
}

void AFCCRuleSubsystem::WatchCarOrder()
{
	TSet<TWeakObjectPtr<AFGTrain>> Watched;
	for (FFCCTimeTableRules& Entry : mRules)
	{
		AFGTrain* Train = FindTrain(Entry);
		if (!Train || Entry.IsDefault())
		{
			continue;
		}
		Watched.Add(Train);
		FSeenOrder Now;
		for (AFGRailroadVehicle* Vehicle : GetStoredVehicles(Train))
		{
			Now.Vehicles.Add(Vehicle);
		}
		for (AFGFreightWagon* Car : GetFreightCars(Train))
		{
			Now.Cars.Add(Car);
		}
		FSeenOrder* Seen = mSeenOrders.Find(Train);
		if (!Seen)
		{
			UE_LOG(LogFreightCarControl, Verbose, TEXT("%s: cars %s"), *Train->GetTrainName().ToString(), *DescribeVehicles(Now.Cars));
			mSeenOrders.Add(Train, MoveTemp(Now));
			continue;
		}
		if (Now.Vehicles != Seen->Vehicles)
		{
			UE_LOG(LogFreightCarControl, Log, TEXT("%s: the game's vehicle order changed%s: %s -> %s"), *Train->GetTrainName().ToString(),
				Now.Vehicles == Reversed(Seen->Vehicles) ? TEXT(" (reversed)") : TEXT(""), *DescribeVehicles(Seen->Vehicles), *DescribeVehicles(Now.Vehicles));
		}
		if (Now.Cars != Seen->Cars)
		{
			if (Now.Cars.Num() > 1 && Now.Cars == Reversed(Seen->Cars))
			{
				UE_LOG(LogFreightCarControl, Warning, TEXT("%s: freight car numbering reversed: %s -> %s"), *Train->GetTrainName().ToString(),
					*DescribeVehicles(Seen->Cars), *DescribeVehicles(Now.Cars));
			}
			else
			{
				UE_LOG(LogFreightCarControl, Log, TEXT("%s: freight cars changed: %s -> %s"), *Train->GetTrainName().ToString(),
					*DescribeVehicles(Seen->Cars), *DescribeVehicles(Now.Cars));
			}
		}
		*Seen = MoveTemp(Now);
	}
	for (auto It = mSeenOrders.CreateIterator(); It; ++It)
	{
		if (!Watched.Contains(It.Key()))
		{
			It.RemoveCurrent();
		}
	}
}

TArray<AFGFreightWagon*> AFCCRuleSubsystem::GetFreightCars(const AFGTrain* Train)
{
	TArray<AFGRailroadVehicle*> Vehicles = GetStoredVehicles(Train);
	const AFCCRuleSubsystem* Subsystem = Train ? Get(Train) : nullptr;
	if (CountsFromStoredEnd(Train, Vehicles, Subsystem ? Subsystem->FindEntry(Train->GetTimeTable()) : nullptr))
	{
		Algo::Reverse(Vehicles);
	}
	TArray<AFGFreightWagon*> Cars;
	for (AFGRailroadVehicle* Vehicle : Vehicles)
	{
		if (AFGFreightWagon* Wagon = Cast<AFGFreightWagon>(Vehicle))
		{
			Cars.Add(Wagon);
		}
	}
	return Cars;
}

FText AFCCRuleSubsystem::DescribeCarNumbering(const AFGTrain* Train)
{
	TArray<AFGRailroadVehicle*> Vehicles = GetStoredVehicles(Train);
	const AFCCRuleSubsystem* Subsystem = Train ? Get(Train) : nullptr;
	if (CountsFromStoredEnd(Train, Vehicles, Subsystem ? Subsystem->FindEntry(Train->GetTimeTable()) : nullptr))
	{
		Algo::Reverse(Vehicles);
	}
	auto IsCar = [](const AFGRailroadVehicle* Vehicle) { return Vehicle && Vehicle->IsA<AFGFreightWagon>(); };
	auto IsLocomotive = [](const AFGRailroadVehicle* Vehicle) { return Vehicle && Vehicle->IsA<AFGLocomotive>(); };
	const int32 FirstCar = Vehicles.IndexOfByPredicate(IsCar);
	const int32 LastCar = Vehicles.FindLastByPredicate(IsCar);
	bool bLocomotiveBefore = false;
	bool bLocomotiveAfter = false;
	for (int32 Index = 0; Index < Vehicles.Num() && FirstCar != INDEX_NONE; ++Index)
	{
		bLocomotiveBefore |= Index < FirstCar && IsLocomotive(Vehicles[Index]);
		bLocomotiveAfter |= Index > LastCar && IsLocomotive(Vehicles[Index]);
	}
	if (!bLocomotiveBefore && !bLocomotiveAfter)
	{
		return FText::GetEmpty();
	}
	if (!bLocomotiveBefore)
	{
		return NSLOCTEXT("FreightCarControl", "NumberingFarEnd", "Car 1 is the freight car furthest from the locomotive.");
	}
	return bLocomotiveAfter
		? NSLOCTEXT("FreightCarControl", "NumberingPushPull", "Car 1 is next to the front locomotive: the one in control when this train got its first car rule.")
		: NSLOCTEXT("FreightCarControl", "NumberingLocomotive", "Car 1 is the freight car next to the locomotive.");
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
