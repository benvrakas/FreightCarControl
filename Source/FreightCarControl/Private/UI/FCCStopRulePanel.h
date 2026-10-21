#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "FCCTypes.h"
#include "FCCStopRulePanel.generated.h"

class UFCCCarModeButton;
class UTextBlock;
class UVerticalBox;
class UWrapBox;

// "Freight cars" section added to the vanilla timetable stop settings (BPW_Train_TimeTable_StationSettings).
// Shows one button per freight car of the train; each click cycles that car's mode at this stop.
UCLASS()
class UFCCStopRulePanel : public UUserWidget
{
	GENERATED_BODY()

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

private:
	void BuildTree();

	// Follows the stop being edited and the train's car count, rebuilding the buttons when either changes.
	void Poll();

	// Recreates one button per freight car from mRule.
	void RebuildButtons(int32 CarCount);

	void OnCarClicked(UFCCCarModeButton* Button);

	UFUNCTION()
	void HandleDiscard();

	UPROPERTY()
	TObjectPtr<UVerticalBox> mRoot;

	UPROPERTY()
	TObjectPtr<UTextBlock> mHint;

	UPROPERTY()
	TObjectPtr<UWrapBox> mCars;

	UPROPERTY()
	TArray<TObjectPtr<UFCCCarModeButton>> mButtons;

	TWeakObjectPtr<UUserWidget> mStationWidget;
	FFCCStopRule mRule;
	FFCCStopRule mRuleWhenOpened;
	int32 mCarCount = INDEX_NONE;
	bool bHasSession = false;
	FTimerHandle mPollTimer;
};
