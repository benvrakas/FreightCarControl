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
	UE_LOG(LogFreightCarControl, Log, TEXT("Editor build: docking hook not installed"));
#else
	static bool bInstalled = false;
	if (bInstalled)
	{
		return;
	}
	bInstalled = true;
	SUBSCRIBE_METHOD_AFTER(AFGBuildableTrainPlatformCargo::EvaluateRuleSet, [](AFGBuildableTrainPlatformCargo* Self)
	{
		AfterEvaluateRuleSet(Self);
	});
	UE_LOG(LogFreightCarControl, Log, TEXT("Docking hook installed (AFGBuildableTrainPlatformCargo::EvaluateRuleSet)"));
#endif
}

void FFCCDockingHook::AfterEvaluateRuleSet(AFGBuildableTrainPlatformCargo* Platform)
{
	if (!Platform || !Platform->HasAuthority())
	{
		return;
	}
	AFGRailroadVehicle* Vehicle = Platform->mDockedRailroadVehicle;
	AFGBuildableRailroadStation* Station = Platform->mStationDockingMaster;
	AFGTrain* Train = Vehicle ? Vehicle->GetTrain() : nullptr;
	AFGRailroadTimeTable* TimeTable = Train ? Train->GetTimeTable() : nullptr;
	if (!Vehicle || !Station || !TimeTable)
	{
		UE_LOG(LogFreightCarControl, VeryVerbose, TEXT("%s: no docked vehicle, station or time table"), *GetNameSafe(Platform));
		return;
	}
	const AFCCRuleSubsystem* Rules = AFCCRuleSubsystem::Get(Platform);
	if (!Rules)
	{
		UE_LOG(LogFreightCarControl, Warning, TEXT("%s: rule subsystem missing, car rules not applied"), *GetNameSafe(Platform));
		return;
	}

	const int32 StopIndex = TimeTable->GetCurrentStop();
	const FTimeTableStop Stop = TimeTable->GetStop(StopIndex);
	if (Stop.Station != Station->GetStationIdentifier())
	{
		if (ShouldLogDecision(Platform, Vehicle, StopIndex))
		{
			UE_LOG(LogFreightCarControl, Verbose, TEXT("%s: %s docked at %s, which isn't its current stop %d (%s); car rules not applied"),
				*GetNameSafe(Platform), *Train->GetTrainName().ToString(), *GetNameSafe(Station), StopIndex,
				Stop.Station ? *Stop.Station->GetStationName().ToString() : TEXT("none"));
		}
		return;
	}

	const int32 CarIndex = AFCCRuleSubsystem::GetFreightCarIndex(Vehicle);
	if (CarIndex == INDEX_NONE)
	{
		UE_LOG(LogFreightCarControl, Warning, TEXT("%s: docked vehicle %s isn't a freight car of %s"),
			*GetNameSafe(Platform), *GetNameSafe(Vehicle), *Train->GetTrainName().ToString());
		return;
	}

	const EFCCCarMode Mode = Rules->GetCarMode(TimeTable, StopIndex, CarIndex);
	if (ShouldLogDecision(Platform, Vehicle, StopIndex))
	{
		UE_LOG(LogFreightCarControl, Log, TEXT("%s at %s (stop %d): car %d %s by %s (%s, %s)"),
			*Train->GetTrainName().ToString(), *Stop.Station->GetStationName().ToString(), StopIndex, CarIndex + 1,
			Mode == EFCCCarMode::Skip ? TEXT("SKIPPED") : TEXT("transfers"), *GetNameSafe(Platform),
			*GetNameSafe(Platform->GetClass()), Platform->GetIsInLoadMode() ? TEXT("load") : TEXT("unload"));
	}
	if (Mode == EFCCCarMode::Skip)
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
