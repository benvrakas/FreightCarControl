#include "FCCChatCommand.h"
#include "Command/CommandSender.h"
#include "FCCRuleSubsystem.h"
#include "FreightCarControl.h"
#include "FGCharacterPlayer.h"
#include "FGPlayerController.h"
#include "FGRailroadTimeTable.h"
#include "FGRailroadVehicle.h"
#include "FGTrain.h"
#include "FGTrainStationIdentifier.h"
#include "GameFramework/PlayerState.h"

namespace
{
	// Returns the train the sender is driving, or nullptr.
	AFGTrain* GetDrivenTrain(UCommandSender* Sender)
	{
		AFGPlayerController* Controller = Sender ? Sender->GetPlayer() : nullptr;
		const AFGCharacterPlayer* Character = Controller ? Cast<AFGCharacterPlayer>(Controller->GetCharacter()) : nullptr;
		const AFGRailroadVehicle* Vehicle = Character ? Cast<AFGRailroadVehicle>(Character->GetDrivenVehicle()) : nullptr;
		if (!Vehicle && Controller)
		{
			Vehicle = Cast<AFGRailroadVehicle>(Controller->GetPawn());
		}
		return Vehicle ? Vehicle->GetTrain() : nullptr;
	}
}

AFCCChatCommand::AFCCChatCommand()
{
	CommandName = TEXT("fcc");
	Aliases.Add(TEXT("freightcars"));
	MinNumberOfArguments = 0;
	bOnlyUsableByPlayer = true;
	Usage = NSLOCTEXT("FreightCarControl", "ChatUsage", "/fcc  |  /fcc <stop> <car|all> platform|skip|load|unload  |  /fcc clear   (drive the train first; stops and cars count from 1)");
}

EExecutionStatus AFCCChatCommand::ExecuteCommand_Implementation(UCommandSender* Sender, const TArray<FString>& Arguments, const FString& Label)
{
	AFGTrain* Train = GetDrivenTrain(Sender);
	AFGRailroadTimeTable* TimeTable = Train ? Train->GetTimeTable() : nullptr;
	AFCCRuleSubsystem* Rules = AFCCRuleSubsystem::Get(this);
	if (!TimeTable || !Rules)
	{
		Sender->SendChatMessage(TEXT("Freight Car Control: drive a train that has a timetable first."), FLinearColor::Red);
		UE_LOG(LogFreightCarControl, Log, TEXT("/fcc: train=%s timetable=%s subsystem=%s"), *GetNameSafe(Train), *GetNameSafe(TimeTable), *GetNameSafe(Rules));
		return EExecutionStatus::UNCOMPLETED;
	}
	const int32 CarCount = AFCCRuleSubsystem::GetFreightCars(Train).Num();
	const AFGPlayerController* Player = Sender->GetPlayer();
	const APlayerState* PlayerState = Player ? Player->GetPlayerState<APlayerState>() : nullptr;
	const FString Cause = FString::Printf(TEXT("/fcc (%s)"), PlayerState ? *PlayerState->GetPlayerName() : TEXT("?"));

	if (Arguments.Num() == 1 && Arguments[0].Equals(TEXT("clear"), ESearchCase::IgnoreCase))
	{
		Rules->SetAllStopRules(TimeTable, {}, Cause);
	}
	else if (Arguments.Num() == 3)
	{
		const int32 Stop = FCString::Atoi(*Arguments[0]) - 1;
		const bool bAllCars = Arguments[1].Equals(TEXT("all"), ESearchCase::IgnoreCase);
		const int32 Car = FCString::Atoi(*Arguments[1]) - 1;
		const TOptional<EFCCCarMode> Mode = FCCCarMode::FromName(Arguments[2]);
		if (Stop < 0 || Stop >= TimeTable->GetNumStops() || (!bAllCars && Car < 0) || !Mode.IsSet())
		{
			PrintCommandUsage(Sender);
			return EExecutionStatus::BAD_ARGUMENTS;
		}
		if (bAllCars)
		{
			FFCCStopRule Rule;
			Rule.Cars.Init(Mode.GetValue(), CarCount);
			Rules->SetStopRule(TimeTable, Stop, Rule, Cause);
		}
		else
		{
			Rules->SetCarMode(TimeTable, Stop, Car, Mode.GetValue(), Cause);
		}
	}
	else if (Arguments.Num() != 0)
	{
		PrintCommandUsage(Sender);
		return EExecutionStatus::BAD_ARGUMENTS;
	}

	Sender->SendChatMessage(FString::Printf(TEXT("%s: %d freight cars, %d stops (P = platform setting, - = skip, L = load, U = unload)"),
		*Train->GetTrainName().ToString(), CarCount, TimeTable->GetNumStops()));
	for (int32 Stop = 0; Stop < TimeTable->GetNumStops(); ++Stop)
	{
		const AFGTrainStationIdentifier* Station = TimeTable->GetStop(Stop).Station;
		Sender->SendChatMessage(FString::Printf(TEXT("  %d. %s: %s"), Stop + 1,
			Station ? *Station->GetStationName().ToString() : TEXT("?"), *Rules->GetStopRule(TimeTable, Stop).ToCodes(CarCount)));
	}
	return EExecutionStatus::COMPLETED;
}
