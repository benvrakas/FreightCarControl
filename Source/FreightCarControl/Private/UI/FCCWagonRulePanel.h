#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "FCCWagonRulePanel.generated.h"

class AFGFreightWagon;
class UFCCCarModeButton;
class UScrollBox;
class USizeBox;
class UVerticalBox;

// "Timetable rules" section added to the vanilla freight car menu (Widget_FreightWagon). A single button that expands a
// short scrolling list of the train's stops with this car's mode at each; a click cycles the mode and saves it.
UCLASS()
class UFCCWagonRulePanel : public UUserWidget
{
	GENERATED_BODY()

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

private:
	void BuildTree();

	// Returns the freight car whose menu this is.
	AFGFreightWagon* GetWagon() const;

	// Rebuilds the stop list when the train, its stops or the rules change.
	void Poll();

	void RebuildRows();

	// Updates the toggle button text and the list visibility from bExpanded.
	void RefreshExpanded();

	void OnStopClicked(UFCCCarModeButton* Button);

	UFUNCTION()
	void HandleToggle();

	UFUNCTION()
	void HandleRulesChanged();

	UPROPERTY()
	TObjectPtr<UVerticalBox> mRoot;

	UPROPERTY()
	TObjectPtr<UUserWidget> mToggle;

	UPROPERTY()
	TObjectPtr<USizeBox> mListBox;

	UPROPERTY()
	TObjectPtr<UScrollBox> mRows;

	UPROPERTY()
	TArray<TObjectPtr<UFCCCarModeButton>> mButtons;

	// Stations of the train's stops and car index when the rows were built; a change triggers a rebuild.
	TArray<TWeakObjectPtr<UObject>> mShownStations;
	int32 mShownCarIndex = INDEX_NONE;
	int32 mShownCarCount = 0;
	bool bDirty = true;
	FTimerHandle mPollTimer;
};
