#pragma once

#include "CoreMinimal.h"
#include "Command/ChatCommandInstance.h"
#include "FCCChatCommand.generated.h"

// /fcc: shows or edits the freight car rules of the train the player is driving. For testing without the UI.
UCLASS()
class FREIGHTCARCONTROL_API AFCCChatCommand : public AChatCommandInstance
{
	GENERATED_BODY()

public:
	AFCCChatCommand();

	virtual EExecutionStatus ExecuteCommand_Implementation(UCommandSender* Sender, const TArray<FString>& Arguments, const FString& Label) override;
};
