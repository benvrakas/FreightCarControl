#pragma once

#include "CoreMinimal.h"
#include "Module/GameInstanceModule.h"
#include "Module/GameWorldModule.h"
#include "FCCModules.generated.h"

// Freight Car Control's root game instance module; installs hooks and registers the remote call object.
UCLASS()
class FREIGHTCARCONTROL_API UFCCGameInstanceModule : public UGameInstanceModule
{
	GENERATED_BODY()

public:
	UFCCGameInstanceModule();

	virtual void DispatchLifecycleEvent(ELifecyclePhase Phase) override;
};

// Freight Car Control's root game world module; registers the rule subsystem and the chat command.
UCLASS()
class FREIGHTCARCONTROL_API UFCCGameWorldModule : public UGameWorldModule
{
	GENERATED_BODY()

public:
	UFCCGameWorldModule();
};
