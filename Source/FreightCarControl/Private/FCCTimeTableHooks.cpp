#include "FCCTimeTableHooks.h"
#include "FCCRuleSubsystem.h"
#include "FreightCarControl.h"
#include "FGRailroadTimeTable.h"
#include "Patching/NativeHookManager.h"

namespace
{
	// Returns the rule subsystem if TimeTable is the server's copy.
	AFCCRuleSubsystem* GetServerRules(const AFGRailroadTimeTable* TimeTable)
	{
		return TimeTable && TimeTable->HasAuthority() ? AFCCRuleSubsystem::Get(TimeTable) : nullptr;
	}

	// Returns TimeTable's current stops.
	TArray<FTimeTableStop> CopyStops(const AFGRailroadTimeTable* TimeTable)
	{
		TArray<FTimeTableStop> Stops;
		TimeTable->GetStops(Stops);
		return Stops;
	}
}

void FFCCTimeTableHooks::InstallHooks()
{
#if WITH_EDITOR
	UE_LOG(LogFreightCarControl, Log, TEXT("Editor build: time table hooks not installed"));
#else
	static bool bInstalled = false;
	if (bInstalled)
	{
		return;
	}
	bInstalled = true;

	SUBSCRIBE_METHOD(AFGRailroadTimeTable::AddStop, [](auto& Scope, AFGRailroadTimeTable* Self, int32 Index, const FTimeTableStop& Stop)
	{
		AFCCRuleSubsystem* Rules = GetServerRules(Self);
		const int32 CountBefore = Self->GetNumStops();
		const bool bAdded = Scope(Self, Index, Stop);
		if (Rules && bAdded && Self->GetNumStops() == CountBefore + 1)
		{
			Rules->NotifyStopInserted(Self, FMath::Clamp(Index, 0, CountBefore));
		}
	});

	SUBSCRIBE_METHOD(AFGRailroadTimeTable::RemoveStop, [](auto& Scope, AFGRailroadTimeTable* Self, int32 Index)
	{
		AFCCRuleSubsystem* Rules = GetServerRules(Self);
		const int32 CountBefore = Self->GetNumStops();
		Scope(Self, Index);
		if (Rules && Self->GetNumStops() == CountBefore - 1)
		{
			Rules->NotifyStopRemoved(Self, Index);
		}
	});

	SUBSCRIBE_METHOD(AFGRailroadTimeTable::SetStops, [](auto& Scope, AFGRailroadTimeTable* Self, const TArray<FTimeTableStop>& Stops)
	{
		AFCCRuleSubsystem* Rules = GetServerRules(Self);
		const TArray<FTimeTableStop> OldStops = Rules ? CopyStops(Self) : TArray<FTimeTableStop>();
		Scope(Self, Stops);
		if (Rules)
		{
			Rules->NotifyStopsReplaced(Self, OldStops, CopyStops(Self));
		}
	});

	SUBSCRIBE_METHOD(AFGRailroadTimeTable::PurgeInvalidStops, [](auto& Scope, AFGRailroadTimeTable* Self)
	{
		AFCCRuleSubsystem* Rules = GetServerRules(Self);
		const TArray<FTimeTableStop> OldStops = Rules ? CopyStops(Self) : TArray<FTimeTableStop>();
		Scope(Self);
		if (Rules && Self->GetNumStops() != OldStops.Num())
		{
			Rules->NotifyStopsReplaced(Self, OldStops, CopyStops(Self));
		}
	});

	UE_LOG(LogFreightCarControl, Log, TEXT("Time table hooks installed (AddStop, RemoveStop, SetStops, PurgeInvalidStops)"));
#endif
}
