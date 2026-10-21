#include "FCCModules.h"
#include "FCCChatCommand.h"
#include "FCCDockingHook.h"
#include "FCCRemoteCallObject.h"
#include "FCCRuleSubsystem.h"
#include "FCCTimeTableHooks.h"
#include "FreightCarControl.h"

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
	Super::DispatchLifecycleEvent(Phase);
}

UFCCGameWorldModule::UFCCGameWorldModule()
{
	bRootModule = true;
	ModSubsystems.Add(AFCCRuleSubsystem::StaticClass());
	mChatCommands.Add(AFCCChatCommand::StaticClass());
}
