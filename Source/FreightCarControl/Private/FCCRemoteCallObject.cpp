#include "FCCRemoteCallObject.h"
#include "FCCRuleSubsystem.h"
#include "FreightCarControl.h"
#include "FGPlayerController.h"
#include "FGRailroadTimeTable.h"
#include "FGTrain.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/PlayerState.h"
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

FString UFCCRemoteCallObject::GetPlayerName() const
{
	const AFGPlayerController* Controller = GetOwnerPlayerController();
	const APlayerState* State = Controller ? Controller->GetPlayerState<APlayerState>() : nullptr;
	return State ? State->GetPlayerName() : GetNameSafe(Controller);
}

bool UFCCRemoteCallObject::Server_SetStopRule_Validate(AFGRailroadTimeTable* TimeTable, int32 StopIndex, const FFCCStopRule& Rule)
{
	return IsRuleValid(Rule);
}

void UFCCRemoteCallObject::Server_SetStopRule_Implementation(AFGRailroadTimeTable* TimeTable, int32 StopIndex, const FFCCStopRule& Rule)
{
	if (AFCCRuleSubsystem* Rules = AFCCRuleSubsystem::Get(GetOwnerPlayerController()))
	{
		Rules->SetStopRule(TimeTable, StopIndex, Rule, FString::Printf(TEXT("freight car menu (%s)"), *GetPlayerName()));
	}
}

bool UFCCRemoteCallObject::Server_SetEditorRules_Validate(AFGTrain* Train, int32 StopCount, const TArray<int32>& StopIndices, const TArray<FFCCStopRule>& Rules)
{
	return StopCount >= 0 && StopCount <= MaxStops && StopIndices.Num() == Rules.Num() && Rules.Num() <= MaxStops
		&& !Rules.ContainsByPredicate([](const FFCCStopRule& Rule) { return !IsRuleValid(Rule); });
}

void UFCCRemoteCallObject::Server_SetEditorRules_Implementation(AFGTrain* Train, int32 StopCount, const TArray<int32>& StopIndices, const TArray<FFCCStopRule>& Rules)
{
	UE_LOG(LogFreightCarControl, Log, TEXT("Server_SetEditorRules from %s: %s, %d of %d stops edited"),
		*GetPlayerName(), Train ? *Train->GetTrainName().ToString() : TEXT("no train"), Rules.Num(), StopCount);
	if (AFCCRuleSubsystem* Subsystem = AFCCRuleSubsystem::Get(GetOwnerPlayerController()))
	{
		Subsystem->QueueEditorRules(Train, StopCount, StopIndices, Rules, FString::Printf(TEXT("timetable editor (%s)"), *GetPlayerName()));
	}
}
