#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "FCCWagonRulePanel.generated.h"

class AFGFreightWagon;
class UCanvasPanelSlot;
class UFCCCarModeButton;
class UScrollBox;
class USizeBox;
class UVerticalBox;

// "Timetable rules" section added to the vanilla freight car menu (Widget_FreightWagon), once in the solid and once in
// the fluid layout; only the one matching the car's cargo type shows. A toggle button opens a short scrolling list of
// the train's stops with this car's mode at each; a click cycles the mode and saves it. Grows the window to fit.
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

	// Returns true if this instance sits in the layout of the car's cargo type.
	bool IsForCargoType(const AFGFreightWagon* Wagon) const;

	// Rebuilds the stop list when the train, its stops or the rules change.
	void Poll();

	void RebuildRows();

	// Updates the toggle text, the list visibility and the window size.
	void RefreshExpanded();

	void OnStopClicked(UFCCCarModeButton* Button);

	UFUNCTION()
	void HandleToggle();

	// Window resizing: remembers the vanilla sizes, grows them to fit the panel, and puts them back on close.
	struct FWindowLayout
	{
		TWeakObjectPtr<USizeBox> Container;
		TWeakObjectPtr<UCanvasPanelSlot> ColumnSlot;
		FVector2D Size = FVector2D::ZeroVector;
		float MaxWidth = 0.f;
		FMargin ColumnOffsets;
		bool bCaptured = false;

		void Capture(UUserWidget* Menu, bool bFluid);
		void Apply(const FVector2D& Extra) const;
		void Restore();
	};

	UPROPERTY()
	TObjectPtr<UVerticalBox> mRoot;

	UPROPERTY()
	TObjectPtr<UUserWidget> mToggle;

	UPROPERTY()
	TObjectPtr<USizeBox> mListBox;

	UPROPERTY()
	TObjectPtr<UScrollBox> mRows;

	// State the rows were built from; any change triggers a rebuild.
	TArray<TWeakObjectPtr<UObject>> mShownStations;
	int32 mShownCarIndex = INDEX_NONE;
	int32 mShownCarCount = 0;
	uint32 mShownRevision = 0;
	bool bDirty = true;

	// True between construction and destruction while this instance matches the car's cargo type.
	bool bActive = false;
	FWindowLayout mLayout;
	FTimerHandle mPollTimer;
};
