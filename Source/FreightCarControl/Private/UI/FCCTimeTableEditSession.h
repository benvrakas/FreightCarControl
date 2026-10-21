#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "FCCTypes.h"
#include "FCCTimeTableEditSession.generated.h"

class AFGRailroadTimeTable;
class AFGTrain;

// Invisible widget added to the vanilla timetable editor (BPW_Train_TimeTable_Setup). Keeps each listed stop's car rule
// while the player edits, and sends them all to the server when the timetable is saved.
UCLASS()
class UFCCTimeTableEditSession : public UUserWidget
{
	GENERATED_BODY()

public:
	// Returns the session of the timetable editor currently open, or nullptr.
	static UFCCTimeTableEditSession* GetActive();

	// Returns the train being edited.
	AFGTrain* GetTrain() const;

	// Returns the rule of the stop shown by StationWidget (a BPW_Train_TimeTable_SetupStation).
	FFCCStopRule GetRule(UUserWidget* StationWidget);

	// Sets the rule of the stop shown by StationWidget; sent to the server when the timetable is saved.
	void SetRule(UUserWidget* StationWidget, const FFCCStopRule& Rule);

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

private:
	// Returns the timetable editor this session belongs to.
	UUserWidget* GetSetup() const;

	// Returns the stop widgets of the editor's timetable list, in order.
	TArray<UUserWidget*> GetStationWidgets() const;

	// Gives every newly listed stop widget the rule its stop has on the server.
	void Poll();

	UFUNCTION()
	void HandleSave();

	UFUNCTION()
	void HandleDiscard();

	TMap<TWeakObjectPtr<UUserWidget>, FFCCStopRule> mRules;
	FTimerHandle mPollTimer;
	bool bBoundSave = false;
	bool bBoundDiscard = false;
};
