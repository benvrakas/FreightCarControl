#include "FCCRemoteCallObject.h"
#include "FCCRuleSubsystem.h"
#include "FreightCarControl.h"
#include "FGPlayerController.h"
#include "FGRailroadTimeTable.h"
#include "FGTrain.h"
#include "FGTrainStationIdentifier.h"
#include "Kismet/GameplayStatics.h"
#include "Net/UnrealNetwork.h"

namespace
{
	// Largest rule a client may send; matches the subsystem's car limit.
	constexpr int32 MaxCars = 256;

	// Most stops a timetable can have.
	constexpr int32 MaxStops = 100;

	bool IsRuleValid(const FFCCStopRule& Rule)
	{
		return Rule.Cars.Num() <= MaxCars;
	}
}

void UFCCRemoteCallObject::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UFCCRemoteCallObject, mForceNetField_UFCCRemoteCallObject);
}

UFCCRemoteCallObject* UFCCRemoteCallObject::GetLocal(const UObject* WorldContext)
{
	AFGPlayerController* Controller = WorldContext ? Cast<AFGPlayerController>(UGameplayStatics::GetPlayerController(WorldContext, 0)) : nullptr;
	if (!Controller)
	{
		UE_LOG(LogFreightCarControl, Warning, TEXT("No local player controller to send rule edits through"));
		return nullptr;
	}
	UFCCRemoteCallObject* RCO = Controller->GetRemoteCallObjectOfClass<UFCCRemoteCallObject>();
	if (!RCO)
	{
		UE_LOG(LogFreightCarControl, Warning, TEXT("Remote call object not registered on the local player (is the mod installed on the server?)"));
	}
	return RCO;
}

bool UFCCRemoteCallObject::Server_SetStopRule_Validate(AFGRailroadTimeTable* TimeTable, int32 StopIndex, AFGTrainStationIdentifier* ExpectedStation, const FFCCStopRule& Rule)
{
	return IsRuleValid(Rule);
}

void UFCCRemoteCallObject::Server_SetStopRule_Implementation(AFGRailroadTimeTable* TimeTable, int32 StopIndex, AFGTrainStationIdentifier* ExpectedStation, const FFCCStopRule& Rule)
{
	AFCCRuleSubsystem* Rules = AFCCRuleSubsystem::Get(GetOwnerPlayerController());
	if (!Rules || !TimeTable)
	{
		UE_LOG(LogFreightCarControl, Warning, TEXT("Server_SetStopRule: no rule subsystem or timetable (%s)"), *GetNameSafe(TimeTable));
		return;
	}
	if (TimeTable->GetStop(StopIndex).Station != ExpectedStation)
	{
		UE_LOG(LogFreightCarControl, Warning, TEXT("Server_SetStopRule from %s: stop %d of %s no longer visits %s, ignored"),
			*GetNameSafe(GetOwnerPlayerController()), StopIndex, *GetNameSafe(TimeTable), *GetNameSafe(ExpectedStation));
		return;
	}
	UE_LOG(LogFreightCarControl, Log, TEXT("Server_SetStopRule from %s: %s stop %d"), *GetNameSafe(GetOwnerPlayerController()), *GetNameSafe(TimeTable), StopIndex);
	Rules->SetStopRule(TimeTable, StopIndex, Rule);
}

bool UFCCRemoteCallObject::Server_SetAllStopRules_Validate(AFGTrain* Train, const TArray<AFGTrainStationIdentifier*>& ExpectedStations, const TArray<FFCCStopRule>& Rules)
{
	if (Rules.Num() > MaxStops || ExpectedStations.Num() > MaxStops)
	{
		return false;
	}
	for (const FFCCStopRule& Rule : Rules)
	{
		if (!IsRuleValid(Rule))
		{
			return false;
		}
	}
	return true;
}

void UFCCRemoteCallObject::Server_SetAllStopRules_Implementation(AFGTrain* Train, const TArray<AFGTrainStationIdentifier*>& ExpectedStations, const TArray<FFCCStopRule>& Rules)
{
	AFCCRuleSubsystem* Subsystem = AFCCRuleSubsystem::Get(GetOwnerPlayerController());
	if (!Subsystem || !Train)
	{
		UE_LOG(LogFreightCarControl, Warning, TEXT("Server_SetAllStopRules: no rule subsystem or train (%s)"), *GetNameSafe(Train));
		return;
	}
	UE_LOG(LogFreightCarControl, Log, TEXT("Server_SetAllStopRules from %s: %s, %d stops"), *GetNameSafe(GetOwnerPlayerController()), *Train->GetTrainName().ToString(), Rules.Num());
	Subsystem->SetAllStopRulesWhenStopsMatch(Train, Rules, ExpectedStations);
}
