#pragma once

#include "CoreMinimal.h"
#include "FCCTypes.h"
#include "FGRemoteCallObject.h"
#include "FCCRemoteCallObject.generated.h"

class AFGRailroadTimeTable;
class AFGTrain;

// Carries a player's freight car rule edits to the server. Stops are identified by position, as everywhere in the mod.
UCLASS(NotBlueprintable)
class FREIGHTCARCONTROL_API UFCCRemoteCallObject : public UFGRemoteCallObject
{
	GENERATED_BODY()

public:
	// Sets the rule of one stop.
	UFUNCTION(Server, Reliable, WithValidation)
	void Server_SetStopRule(AFGRailroadTimeTable* TimeTable, int32 StopIndex, const FFCCStopRule& Rule);

	// Sets the rules of the stops at StopIndices (positions in a list of StopCount stops) that were edited in the
	// timetable editor, sent right after the editor saved its stops.
	UFUNCTION(Server, Reliable, WithValidation)
	void Server_SetEditorRules(AFGTrain* Train, int32 StopCount, const TArray<int32>& StopIndices, const TArray<FFCCStopRule>& Rules);

	// Returns the local player's instance, or nullptr (logged) if the server doesn't run the mod.
	static UFCCRemoteCallObject* GetLocal(const UObject* WorldContext);

	// Returns the name of the player who owns this object, for the log.
	FString GetPlayerName() const;

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

private:
	UPROPERTY(Replicated, Meta = (NoAutoJson))
	bool mForceNetField_UFCCRemoteCallObject = false;
};
