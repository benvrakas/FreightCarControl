#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "FCCTypes.h"
#include "FCCTimeTableEditSession.generated.h"

class AFGTrain;
class AFGTrainStationIdentifier;

// Invisible widget added to the vanilla timetable editor (BPW_Train_TimeTable_Setup). Collects the car rules the player
// edits as a draft per train, keyed by (station, occurrence of that station in the list) because the editor recreates
// its stop widgets; when the timetable is saved with a draft, sends the edited stops' rules and positions to the server.
UCLASS()
class UFCCTimeTableEditSession : public UUserWidget
{
	GENERATED_BODY()

public:
	// Returns the most recently opened timetable editor session that has a train, or nullptr.
	static UFCCTimeTableEditSession* GetActive();

	// Returns the train being edited.
	AFGTrain* GetTrain() const;

	// Returns the rule of the stop shown by StationWidget (a BPW_Train_TimeTable_SetupStation): the draft, or the server's.
	FFCCStopRule GetRule(UUserWidget* StationWidget) const;

	// Sets the draft rule of the stop shown by StationWidget.
	void SetRule(UUserWidget* StationWidget, const FFCCStopRule& Rule);

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

private:
	using FStopKey = TPair<const AFGTrainStationIdentifier*, int32>;
	using FDraft = TMap<FStopKey, FFCCStopRule>;

	// Returns the timetable editor this session belongs to.
	UUserWidget* GetSetup() const;

	// Returns the stop widgets of the editor's timetable list, in order.
	TArray<UUserWidget*> GetStationWidgets() const;

	// Returns StationWidget's key in the current list, and its position there (INDEX_NONE if it isn't listed).
	FStopKey GetKey(UUserWidget* StationWidget, int32* OutListIndex = nullptr) const;

	// Returns the server's rule for the listed stop with Key at ListIndex: the stop with that key, or the stop at the same
	// position if another mod changed that stop's station since the editor opened; empty otherwise.
	FFCCStopRule GetServerRule(const FStopKey& Key, int32 ListIndex) const;

	// Returns the draft of the train being edited, or nullptr if there is none.
	const FDraft* FindDraft() const;

	UFUNCTION()
	void HandleSave();

	UFUNCTION()
	void HandleDiscard();
};
