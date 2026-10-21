#pragma once

#include "CoreMinimal.h"
#include "FCCTypes.generated.h"

class AFGBuildableTrainPlatformCargo;
class AFGRailroadTimeTable;
class AFGRailroadVehicle;
class AFGTrain;

// What a freight car does when its train docks at a timetable stop.
UENUM(BlueprintType)
enum class EFCCCarMode : uint8
{
	// The platform beside the car loads or unloads it according to the platform's own setting.
	Platform,
	// The platform beside the car leaves it untouched and doesn't hold the train for it.
	Skip,
	// The platform beside the car loads it, whatever the platform is set to, for this dock only.
	Load,
	// The platform beside the car unloads it, whatever the platform is set to, for this dock only.
	Unload,

	Count UMETA(Hidden)
};

namespace FCCCarMode
{
	// Returns the mode after Mode in the UI cycle.
	inline EFCCCarMode Next(EFCCCarMode Mode)
	{
		return static_cast<EFCCCarMode>((static_cast<uint8>(Mode) + 1) % static_cast<uint8>(EFCCCarMode::Count));
	}

	// Returns the mode's display name.
	FREIGHTCARCONTROL_API FText GetDisplayName(EFCCCarMode Mode);

	// Returns the one-letter code used in logs and the chat command.
	FREIGHTCARCONTROL_API const TCHAR* GetCode(EFCCCarMode Mode);

	// Parses a mode from its enum name ("platform", "skip", "load", "unload"; any case), independent of the game language.
	FREIGHTCARCONTROL_API TOptional<EFCCCarMode> FromName(const FString& Name);
}

// Car modes for one timetable stop, indexed by freight car position (0 = first freight car of the train).
// Cars beyond the end of the array use the platform setting.
USTRUCT(BlueprintType)
struct FREIGHTCARCONTROL_API FFCCStopRule
{
	GENERATED_BODY()

	UPROPERTY(SaveGame, BlueprintReadWrite)
	TArray<EFCCCarMode> Cars;

	// Returns the mode of the car at CarIndex.
	EFCCCarMode GetCarMode(int32 CarIndex) const
	{
		return Cars.IsValidIndex(CarIndex) ? Cars[CarIndex] : EFCCCarMode::Platform;
	}

	// Sets the mode of the car at CarIndex, growing the array as needed.
	void SetCarMode(int32 CarIndex, EFCCCarMode Mode)
	{
		if (CarIndex < 0)
		{
			return;
		}
		if (Cars.Num() <= CarIndex)
		{
			Cars.SetNum(CarIndex + 1);
		}
		Cars[CarIndex] = Mode;
	}

	// Returns true if every car uses the platform setting.
	bool IsDefault() const
	{
		return !Cars.ContainsByPredicate([](EFCCCarMode Mode) { return Mode != EFCCCarMode::Platform; });
	}

	// Drops trailing Platform entries and replaces out-of-range values (e.g. from a client) with Platform.
	void Trim()
	{
		for (EFCCCarMode& Mode : Cars)
		{
			if (Mode >= EFCCCarMode::Count)
			{
				Mode = EFCCCarMode::Platform;
			}
		}
		while (Cars.Num() > 0 && Cars.Last() == EFCCCarMode::Platform)
		{
			Cars.Pop(EAllowShrinking::No);
		}
	}

	// Returns the cars' codes, e.g. "P-LU", padded with "P" to at least MinCars.
	FString ToCodes(int32 MinCars = 0) const
	{
		FString Codes;
		for (int32 Car = 0; Car < FMath::Max(MinCars, Cars.Num()); ++Car)
		{
			Codes += FCCCarMode::GetCode(GetCarMode(Car));
		}
		return Codes;
	}

	bool operator==(const FFCCStopRule& Other) const = default;
};

// Per-stop rules of one train's timetable, aligned with the timetable's stops.
USTRUCT(BlueprintType)
struct FREIGHTCARCONTROL_API FFCCTimeTableRules
{
	GENERATED_BODY()

	UPROPERTY(SaveGame, BlueprintReadOnly)
	TObjectPtr<AFGRailroadTimeTable> TimeTable = nullptr;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	TArray<FFCCStopRule> Stops;

	// Returns true if no stop has a car rule.
	bool IsDefault() const
	{
		return !Stops.ContainsByPredicate([](const FFCCStopRule& Stop) { return !Stop.IsDefault(); });
	}
};

// A cargo platform whose load/unload setting was switched for one docked car, and the setting to put back.
USTRUCT()
struct FREIGHTCARCONTROL_API FFCCPlatformOverride
{
	GENERATED_BODY()

	UPROPERTY(SaveGame)
	TObjectPtr<AFGBuildableTrainPlatformCargo> Platform = nullptr;

	UPROPERTY(SaveGame)
	TObjectPtr<AFGRailroadVehicle> Vehicle = nullptr;

	// The platform's own setting before the switch.
	UPROPERTY(SaveGame)
	bool bOriginalLoadMode = false;

	// The setting the mod switched it to.
	UPROPERTY(SaveGame)
	bool bForcedLoadMode = false;
};

// Rules a client saved from the timetable editor, waiting until the train's timetable has as many stops.
USTRUCT()
struct FREIGHTCARCONTROL_API FFCCPendingRules
{
	GENERATED_BODY()

	UPROPERTY()
	TObjectPtr<AFGTrain> Train = nullptr;

	UPROPERTY()
	TArray<FFCCStopRule> Rules;

	double ReceivedTime = 0.0;
};
