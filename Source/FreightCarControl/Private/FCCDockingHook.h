#pragma once

#include "CoreMinimal.h"

class AFGBuildableTrainPlatformCargo;

// Applies freight car rules to cargo platforms while a train is docked. Friend of AFGBuildableTrainPlatformCargo.
class FFCCDockingHook
{
public:
	static void InstallHooks();

private:
	// Runs after the platform rebuilt its filters from the current stop's rules; blocks the transfer if the car is skipped.
	static void AfterEvaluateRuleSet(AFGBuildableTrainPlatformCargo* Platform);

	// Makes Platform transfer nothing and not hold the train.
	static void ApplySkip(AFGBuildableTrainPlatformCargo* Platform);
};
