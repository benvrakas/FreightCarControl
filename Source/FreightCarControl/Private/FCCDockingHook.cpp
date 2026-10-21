#include "FCCDockingHook.h"
#include "FCCRuleSubsystem.h"
#include "FreightCarControl.h"
#include "Buildables/FGBuildableRailroadStation.h"
#include "Buildables/FGBuildableTrainPlatformCargo.h"
#include "FGRailroadTimeTable.h"
#include "FGRailroadVehicle.h"
#include "FGTrain.h"
#include "FGTrainStationIdentifier.h"
#include "Patching/NativeHookManager.h"
#include "Resources/FGNoneDescriptor.h"

namespace
{
	// The last decision logged per platform, so a dock is logged once rather than on every docking sequence update.
	struct FLoggedDecision
	{
		TWeakObjectPtr<AFGRailroadVehicle> Vehicle;
		int32 StopIndex = INDEX_NONE;
	};
	TMap<TWeakObjectPtr<AFGBuildableTrainPlatformCargo>, FLoggedDecision> GLoggedDecisions;

	// Returns true the first time a decision for Platform, Vehicle and StopIndex is seen.
	bool ShouldLogDecision(AFGBuildableTrainPlatformCargo* Platform, AFGRailroadVehicle* Vehicle, int32 StopIndex)
	{
		FLoggedDecision& Last = GLoggedDecisions.FindOrAdd(Platform);
		if (Last.Vehicle == Vehicle && Last.StopIndex == StopIndex)
		{
			return false;
		}
		Last.Vehicle = Vehicle;
		Last.StopIndex = StopIndex;
		if (GLoggedDecisions.Num() > 512)
		{
			for (auto It = GLoggedDecisions.CreateIterator(); It; ++It)
			{
				if (!It.Key().IsValid())
				{
					It.RemoveCurrent();
				}
			}
		}
		return true;
	}
}

void FFCCDockingHook::InstallHooks()
{
#if WITH_EDITOR
	UE_LOG(LogFreightCarControl, Log, TEXT("Editor build: docking hooks not installed"));
#else
	static bool bInstalled = false;
	if (bInstalled)
	{
		return;
	}
	bInstalled = true;

	SUBSCRIBE_METHOD(AFGBuildableTrainPlatformCargo::EvaluateRuleSet, [](auto& Scope, AFGBuildableTrainPlatformCargo* Self)
	{
		const EFCCCarMode Mode = ResolveMode(Self);
		BeforeEvaluateRuleSet(Self, Mode);
		Scope(Self);
		AfterEvaluateRuleSet(Self, Mode);
	});

	AFGBuildableTrainPlatformCargo* Sample = GetMutableDefault<AFGBuildableTrainPlatformCargo>();
	SUBSCRIBE_METHOD_VIRTUAL_AFTER(AFGBuildableTrainPlatformCargo::Undock, Sample, [](AFGBuildableTrainPlatformCargo* Self)
	{
		if (Self && Self->HasAuthority())
		{
			if (AFCCRuleSubsystem* Rules = AFCCRuleSubsystem::Get(Self))
			{
				Rules->EndPlatformOverride(Self);
			}
		}
	});
	SUBSCRIBE_METHOD_VIRTUAL_AFTER(AFGBuildableTrainPlatformCargo::CancelDockingSequence, Sample, [](AFGBuildableTrainPlatformCargo* Self)
	{
		if (Self && Self->HasAuthority())
		{
			if (AFCCRuleSubsystem* Rules = AFCCRuleSubsystem::Get(Self))
			{
				Rules->EndPlatformOverride(Self);
			}
		}
	});
	UE_LOG(LogFreightCarControl, Log, TEXT("Docking hooks installed (EvaluateRuleSet, Undock, CancelDockingSequence)"));
#endif
}

EFCCCarMode FFCCDockingHook::ResolveMode(AFGBuildableTrainPlatformCargo* Platform)
{
	if (!Platform || !Platform->HasAuthority())
	{
		return EFCCCarMode::Platform;
	}
	AFGRailroadVehicle* Vehicle = Platform->mDockedRailroadVehicle;
	AFGBuildableRailroadStation* Station = Platform->mStationDockingMaster;
	AFGTrain* Train = Vehicle ? Vehicle->GetTrain() : nullptr;
	AFGRailroadTimeTable* TimeTable = Train ? Train->GetTimeTable() : nullptr;
	if (!Vehicle || !Station || !TimeTable)
	{
		UE_LOG(LogFreightCarControl, VeryVerbose, TEXT("%s: no docked vehicle, station or time table"), *GetNameSafe(Platform));
		return EFCCCarMode::Platform;
	}
	const AFCCRuleSubsystem* Rules = AFCCRuleSubsystem::Get(Platform);
	if (!Rules)
	{
		UE_LOG(LogFreightCarControl, Warning, TEXT("%s: rule subsystem missing, car rules not applied"), *GetNameSafe(Platform));
		return EFCCCarMode::Platform;
	}

	// The train advances its current stop before docking; the docked stop is the one before it, as in
	// AFGTrain::GetDockingRuleSetForCurrentStop.
	const int32 CurrentStop = TimeTable->GetCurrentStop();
	const int32 StopIndex = (CurrentStop == 0 ? TimeTable->GetNumStops() : CurrentStop) - 1;
	const int32 CarIndex = AFCCRuleSubsystem::GetFreightCarIndex(Vehicle);
	if (StopIndex < 0 || CarIndex == INDEX_NONE)
	{
		UE_LOG(LogFreightCarControl, VeryVerbose, TEXT("%s: no stop (%d) or %s isn't a freight car"), *GetNameSafe(Platform), StopIndex, *GetNameSafe(Vehicle));
		return EFCCCarMode::Platform;
	}

	const EFCCCarMode Mode = Rules->GetCarMode(TimeTable, StopIndex, CarIndex);
	if (ShouldLogDecision(Platform, Vehicle, StopIndex))
	{
		const AFGTrainStationIdentifier* DockedAt = Station->GetStationIdentifier();
		const AFGTrainStationIdentifier* Planned = TimeTable->GetStop(StopIndex).Station;
		const FString PlannedNote = DockedAt == Planned ? FString()
			: FString::Printf(TEXT(", planned %s"), Planned ? *Planned->GetStationName().ToString() : TEXT("?"));
		UE_LOG(LogFreightCarControl, Log, TEXT("%s at %s (stop %d%s): car %d -> %s, by %s (%s, set to %s)"),
			*Train->GetTrainName().ToString(), DockedAt ? *DockedAt->GetStationName().ToString() : TEXT("?"), StopIndex + 1, *PlannedNote,
			CarIndex + 1, *FCCCarMode::GetDisplayName(Mode).ToString(), *GetNameSafe(Platform), *GetNameSafe(Platform->GetClass()),
			Platform->GetIsInLoadMode() ? TEXT("load") : TEXT("unload"));
	}
	return Mode;
}

void FFCCDockingHook::BeforeEvaluateRuleSet(AFGBuildableTrainPlatformCargo* Platform, EFCCCarMode Mode)
{
	if (!Platform || !Platform->HasAuthority())
	{
		return;
	}
	AFCCRuleSubsystem* Rules = AFCCRuleSubsystem::Get(Platform);
	if (!Rules)
	{
		return;
	}
	AFGRailroadVehicle* Vehicle = Platform->mDockedRailroadVehicle;
	if (Mode == EFCCCarMode::Load || Mode == EFCCCarMode::Unload)
	{
		Rules->BeginPlatformOverride(Platform, Vehicle, Mode == EFCCCarMode::Load);
	}
	else
	{
		Rules->EndPlatformOverride(Platform, Vehicle);
	}
}

void FFCCDockingHook::AfterEvaluateRuleSet(AFGBuildableTrainPlatformCargo* Platform, EFCCCarMode Mode)
{
	if (Mode == EFCCCarMode::Skip && Platform && Platform->HasAuthority())
	{
		ApplySkip(Platform);
	}
}

void FFCCDockingHook::ApplySkip(AFGBuildableTrainPlatformCargo* Platform)
{
	const TSubclassOf<UFGItemDescriptor> Nothing = UFGNoneDescriptor::StaticClass();
	Platform->mLoadItemFilter.Reset();
	Platform->mLoadItemFilter.Add(Nothing);
	Platform->mUnloadItemFilter.Reset();
	Platform->mUnloadItemFilter.Add(Nothing);
	Platform->mTransferBlockedByNoneFilter = true;
	Platform->mHasFullyLoadUnloadRule = false;
	UE_LOG(LogFreightCarControl, VeryVerbose, TEXT("%s: filters set to none, departure hold cleared"), *GetNameSafe(Platform));
}
