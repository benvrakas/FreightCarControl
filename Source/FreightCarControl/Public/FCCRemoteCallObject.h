#pragma once

#include "CoreMinimal.h"
#include "FCCTypes.h"
#include "FGRemoteCallObject.h"
#include "FCCRemoteCallObject.generated.h"

class AFGRailroadTimeTable;
class AFGTrain;
class AFGTrainStationIdentifier;

// Carries a player's freight car rule edits to the server.
UCLASS(NotBlueprintable)
class FREIGHTCARCONTROL_API UFCCRemoteCallObject : public UFGRemoteCallObject
{
	GENERATED_BODY()

public:
	// Sets the rule of one stop if that stop still visits ExpectedStation.
	UFUNCTION(Server, Reliable, WithValidation)
	void Server_SetStopRule(AFGRailroadTimeTable* TimeTable, int32 StopIndex, AFGTrainStationIdentifier* ExpectedStation, const FFCCStopRule& Rule);

	// Sets all stop rules once the timetable's stops visit ExpectedStations in order (sent right after a timetable save).
	UFUNCTION(Server, Reliable, WithValidation)
	void Server_SetAllStopRules(AFGTrain* Train, const TArray<AFGTrainStationIdentifier*>& ExpectedStations, const TArray<FFCCStopRule>& Rules);

	// Returns the local player's instance, or nullptr (logged) if the server doesn't run the mod.
	static UFCCRemoteCallObject* GetLocal(const UObject* WorldContext);

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

private:
	UPROPERTY(Replicated, Meta = (NoAutoJson))
	bool mForceNetField_UFCCRemoteCallObject = false;
};
