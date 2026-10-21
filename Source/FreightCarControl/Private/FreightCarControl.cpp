#include "FreightCarControl.h"

DEFINE_LOG_CATEGORY(LogFreightCarControl);

void FFreightCarControlModule::StartupModule()
{
	UE_LOG(LogFreightCarControl, Log, TEXT("Freight Car Control module starting"));
}

void FFreightCarControlModule::ShutdownModule()
{
}

IMPLEMENT_MODULE(FFreightCarControlModule, FreightCarControl)
