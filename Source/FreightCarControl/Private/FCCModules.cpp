#include "FCCModules.h"
#include "FCCChatCommand.h"
#include "FCCDockingHook.h"
#include "FCCRemoteCallObject.h"
#include "FCCRuleSubsystem.h"
#include "FCCTimeTableHooks.h"
#include "FreightCarControl.h"
#include "Patching/WidgetBlueprintHookManager.h"
#include "UI/FCCUIHooks.h"

UFCCGameInstanceModule::UFCCGameInstanceModule()
{
	bRootModule = true;
	RemoteCallObjects.Add(UFCCRemoteCallObject::StaticClass());
}

void UFCCGameInstanceModule::DispatchLifecycleEvent(ELifecyclePhase Phase)
{
	if (Phase == ELifecyclePhase::INITIALIZATION)
	{
		FFCCDockingHook::InstallHooks();
		FFCCTimeTableHooks::InstallHooks();
	}
	else if (Phase == ELifecyclePhase::POST_INITIALIZATION && mWidgetHooks.Num() == 0)
	{
		TArray<UWidgetBlueprintHookData*> Hooks;
		FCCUIHooks::Register(this, Hooks);
		mWidgetHooks.Append(Hooks);
	}
	Super::DispatchLifecycleEvent(Phase);
}

UFCCGameWorldModule::UFCCGameWorldModule()
{
	bRootModule = true;
	ModSubsystems.Add(AFCCRuleSubsystem::StaticClass());
	mChatCommands.Add(AFCCChatCommand::StaticClass());
}
