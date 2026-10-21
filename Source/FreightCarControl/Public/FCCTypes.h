#pragma once

#include "CoreMinimal.h"
#include "FCCTypes.generated.h"

class AFGRailroadTimeTable;

// What a freight car does when its train docks at a timetable stop.
UENUM(BlueprintType)
enum class EFCCCarMode : uint8
{
	// The platform beside the car loads or unloads it as usual.
	Transfer,
	// The platform beside the car leaves it untouched and doesn't hold the train for it.
	Skip
};

// Car modes for one timetable stop, indexed by freight car position (0 = first freight car of the train).
// Cars beyond the end of the array transfer.
USTRUCT(BlueprintType)
struct FREIGHTCARCONTROL_API FFCCStopRule
{
	GENERATED_BODY()

	UPROPERTY(SaveGame, BlueprintReadWrite)
	TArray<EFCCCarMode> Cars;

	// Returns the mode of the car at CarIndex.
	EFCCCarMode GetCarMode(int32 CarIndex) const
	{
		return Cars.IsValidIndex(CarIndex) ? Cars[CarIndex] : EFCCCarMode::Transfer;
	}

	// Returns true if no car is skipped.
	bool IsDefault() const
	{
		return !Cars.Contains(EFCCCarMode::Skip);
	}

	// Drops trailing Transfer entries.
	void Trim()
	{
		while (Cars.Num() > 0 && Cars.Last() == EFCCCarMode::Transfer)
		{
			Cars.Pop(EAllowShrinking::No);
		}
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

	// Returns true if no stop skips any car.
	bool IsDefault() const
	{
		for (const FFCCStopRule& Stop : Stops)
		{
			if (!Stop.IsDefault())
			{
				return false;
			}
		}
		return true;
	}
};
