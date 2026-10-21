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

namespace
{
	// Returns the train the sender is driving, or nullptr.
	AFGTrain* GetDrivenTrain(UCommandSender* Sender)
	{
		AFGPlayerController* Controller = Sender ? Sender->GetPlayer() : nullptr;
		AFGCharacterPlayer* Character = Controller ? Cast<AFGCharacterPlayer>(Controller->GetPawn()) : nullptr;
		if (!Character && Controller)
		{
			Character = Cast<AFGCharacterPlayer>(Controller->GetCharacter());
		}
		AFGRailroadVehicle* Vehicle = Character ? Cast<AFGRailroadVehicle>(Character->GetDrivenVehicle()) : nullptr;
		if (!Vehicle && Controller)
		{
			Vehicle = Cast<AFGRailroadVehicle>(Controller->GetPawn());
		}
		return Vehicle ? Vehicle->GetTrain() : nullptr;
	}

	FString DescribeRule(const FFCCStopRule& Rule, int32 CarCount)
	{
		FString Text;
		for (int32 Car = 0; Car < FMath::Max(CarCount, Rule.Cars.Num()); ++Car)
		{
			Text += Rule.GetCarMode(Car) == EFCCCarMode::Skip ? TEXT("-") : TEXT("X");
		}
		return Text.IsEmpty() ? TEXT("(no freight cars)") : Text;
	}
}

AFCCChatCommand::AFCCChatCommand()
{
	CommandName = TEXT("fcc");
	Aliases.Add(TEXT("freightcars"));
	MinNumberOfArguments = 0;
	bOnlyUsableByPlayer = true;
	Usage = NSLOCTEXT("FreightCarControl", "ChatUsage", "/fcc  |  /fcc <stop> <car> skip|transfer  |  /fcc <stop> all skip|transfer  |  /fcc clear   (drive the train first; stops and cars count from 1)");
}

EExecutionStatus AFCCChatCommand::ExecuteCommand_Implementation(UCommandSender* Sender, const TArray<FString>& Arguments, const FString& Label)
{
	AFGTrain* Train = GetDrivenTrain(Sender);
	AFGRailroadTimeTable* TimeTable = Train ? Train->GetTimeTable() : nullptr;
	AFCCRuleSubsystem* Rules = AFCCRuleSubsystem::Get(this);
	if (!Train || !TimeTable || !Rules)
	{
		Sender->SendChatMessage(TEXT("Freight Car Control: drive a train that has a timetable first."), FLinearColor::Red);
		UE_LOG(LogFreightCarControl, Log, TEXT("/fcc: train=%s timetable=%s subsystem=%s"), *GetNameSafe(Train), *GetNameSafe(TimeTable), *GetNameSafe(Rules));
		return EExecutionStatus::UNCOMPLETED;
	}
	const int32 CarCount = AFCCRuleSubsystem::GetFreightCarCount(Train);

	if (Arguments.Num() == 1 && Arguments[0].Equals(TEXT("clear"), ESearchCase::IgnoreCase))
	{
		Rules->SetAllStopRules(TimeTable, {});
		Sender->SendChatMessage(FString::Printf(TEXT("Freight Car Control: cleared all rules of %s."), *Train->GetTrainName().ToString()));
		return EExecutionStatus::COMPLETED;
	}

	if (Arguments.Num() == 3)
	{
		const int32 Stop = FCString::Atoi(*Arguments[0]) - 1;
		const bool bAllCars = Arguments[1].Equals(TEXT("all"), ESearchCase::IgnoreCase);
		const int32 Car = bAllCars ? 0 : FCString::Atoi(*Arguments[1]) - 1;
		const bool bSkip = Arguments[2].Equals(TEXT("skip"), ESearchCase::IgnoreCase);
		const bool bTransfer = Arguments[2].Equals(TEXT("transfer"), ESearchCase::IgnoreCase);
		if (Stop < 0 || Stop >= TimeTable->GetNumStops() || (!bAllCars && Car < 0) || (!bSkip && !bTransfer))
		{
			PrintCommandUsage(Sender);
			return EExecutionStatus::BAD_ARGUMENTS;
		}
		const EFCCCarMode Mode = bSkip ? EFCCCarMode::Skip : EFCCCarMode::Transfer;
		if (bAllCars)
		{
			FFCCStopRule Rule;
			Rule.Cars.Init(Mode, CarCount);
			Rules->SetStopRule(TimeTable, Stop, Rule);
		}
		else
		{
			Rules->SetCarMode(TimeTable, Stop, Car, Mode);
		}
	}
	else if (Arguments.Num() != 0)
	{
		PrintCommandUsage(Sender);
		return EExecutionStatus::BAD_ARGUMENTS;
	}

	Sender->SendChatMessage(FString::Printf(TEXT("%s: %d freight cars, %d stops (X = transfer, - = skip)"),
		*Train->GetTrainName().ToString(), CarCount, TimeTable->GetNumStops()));
	for (int32 Stop = 0; Stop < TimeTable->GetNumStops(); ++Stop)
	{
		const FTimeTableStop StopData = TimeTable->GetStop(Stop);
		Sender->SendChatMessage(FString::Printf(TEXT("  %d. %s: %s"), Stop + 1,
			StopData.Station ? *StopData.Station->GetStationName().ToString() : TEXT("?"), *DescribeRule(Rules->GetStopRule(TimeTable, Stop), CarCount)));
	}
	return EExecutionStatus::COMPLETED;
}
