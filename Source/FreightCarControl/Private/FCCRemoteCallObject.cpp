#include "FCCRemoteCallObject.h"
#include "FCCRuleSubsystem.h"
#include "FreightCarControl.h"
#include "FGPlayerController.h"
#include "FGRailroadTimeTable.h"
#include "FGTrain.h"
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

bool UFCCRemoteCallObject::Server_SetStopRule_Validate(AFGRailroadTimeTable* TimeTable, int32 StopIndex, const FFCCStopRule& Rule)
{
	return IsRuleValid(Rule);
}

void UFCCRemoteCallObject::Server_SetStopRule_Implementation(AFGRailroadTimeTable* TimeTable, int32 StopIndex, const FFCCStopRule& Rule)
{
	UE_LOG(LogFreightCarControl, Log, TEXT("Server_SetStopRule from %s: %s stop %d [%s]"),
		*GetNameSafe(GetOwnerPlayerController()), *GetNameSafe(TimeTable), StopIndex + 1, *Rule.ToCodes());
	if (AFCCRuleSubsystem* Rules = AFCCRuleSubsystem::Get(GetOwnerPlayerController()))
	{
		Rules->SetStopRule(TimeTable, StopIndex, Rule);
	}
}

bool UFCCRemoteCallObject::Server_SetEditorRules_Validate(AFGTrain* Train, const TArray<FFCCStopRule>& Rules)
{
	return Rules.Num() <= MaxStops && !Rules.ContainsByPredicate([](const FFCCStopRule& Rule) { return !IsRuleValid(Rule); });
}

void UFCCRemoteCallObject::Server_SetEditorRules_Implementation(AFGTrain* Train, const TArray<FFCCStopRule>& Rules)
{
	UE_LOG(LogFreightCarControl, Log, TEXT("Server_SetEditorRules from %s: %s, %d stops"),
		*GetNameSafe(GetOwnerPlayerController()), Train ? *Train->GetTrainName().ToString() : TEXT("no train"), Rules.Num());
	if (AFCCRuleSubsystem* Subsystem = AFCCRuleSubsystem::Get(GetOwnerPlayerController()))
	{
		Subsystem->QueueEditorRules(Train, Rules);
	}
}
