#pragma once

#include "CoreMinimal.h"
#include "FCCTypes.h"

class AFGBuildableTrainPlatformCargo;

// Applies freight car rules to cargo platforms while a train is docked. Friend of AFGBuildableTrainPlatformCargo.
class FFCCDockingHook
{
public:
	static void InstallHooks();

private:
	// Returns the docked car's mode at its train's current stop, or Platform if no rule applies.
	static EFCCCarMode ResolveMode(AFGBuildableTrainPlatformCargo* Platform);

	// Runs before the platform rebuilds its filters; switches its load/unload setting for a Load or Unload car.
	static void BeforeEvaluateRuleSet(AFGBuildableTrainPlatformCargo* Platform, EFCCCarMode Mode);

	// Runs after the platform rebuilt its filters from the current stop's rules; blocks the transfer if the car is skipped.
	static void AfterEvaluateRuleSet(AFGBuildableTrainPlatformCargo* Platform, EFCCCarMode Mode);

	// Makes Platform transfer nothing and not hold the train.
	static void ApplySkip(AFGBuildableTrainPlatformCargo* Platform);
};
