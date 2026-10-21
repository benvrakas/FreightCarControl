#include "UI/FCCCarModeButton.h"
#include "FreightCarControl.h"
#include "UI/FCCWidgetUtils.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"

#define LOCTEXT_NAMESPACE "FreightCarControl"

void UFCCCarModeButton::SetLabel(const FText& InLabel)
{
	Label = InLabel;
	Refresh();
}

void UFCCCarModeButton::SetMode(EFCCCarMode InMode)
{
	Mode = InMode;
	Refresh();
}

TSharedRef<SWidget> UFCCCarModeButton::RebuildWidget()
{
	BuildTree();
	return Super::RebuildWidget();
}

void UFCCCarModeButton::BuildTree()
{
	if (!WidgetTree)
	{
		WidgetTree = NewObject<UWidgetTree>(this, TEXT("WidgetTree"), RF_Transient);
	}
	if (WidgetTree->RootWidget)
	{
		return;
	}
	mVanillaButton = FCCWidgetUtils::CreateStandardButton(WidgetTree, FText::GetEmpty());
	if (mVanillaButton)
	{
		WidgetTree->RootWidget = mVanillaButton;
		mVanillaButton->Initialize();
		UButton* Inner = FCCWidgetUtils::GetInnerButton(mVanillaButton);
		if (Inner)
		{
			Inner->OnClicked.AddUniqueDynamic(this, &UFCCCarModeButton::HandleClicked);
		}
		else if (!FCCWidgetUtils::BindEventDispatcher(mVanillaButton, TEXT("OnClicked"), this, GET_FUNCTION_NAME_CHECKED(UFCCCarModeButton, HandleClicked)))
		{
			UE_LOG(LogFreightCarControl, Warning, TEXT("%s: vanilla button has no mButton or OnClicked; clicks won't work"), *GetName());
		}
	}
	else
	{
		mFallbackButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass());
		mFallbackText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
		mFallbackButton->AddChild(mFallbackText);
		mFallbackButton->OnClicked.AddUniqueDynamic(this, &UFCCCarModeButton::HandleClicked);
		WidgetTree->RootWidget = mFallbackButton;
	}
	Refresh();
}

void UFCCCarModeButton::Refresh()
{
	const FText Text = Label.IsEmpty()
		? FCCCarMode::GetDisplayName(Mode)
		: FText::Format(LOCTEXT("CarButton", "{0}: {1}"), Label, FCCCarMode::GetDisplayName(Mode));
	if (mVanillaButton)
	{
		FCCWidgetUtils::SetWidgetText(mVanillaButton, Text);
	}
	else if (mFallbackText)
	{
		mFallbackText->SetText(Text);
	}
}

void UFCCCarModeButton::HandleClicked()
{
	UE_LOG(LogFreightCarControl, Verbose, TEXT("%s clicked (index %d, mode %s)"), *GetName(), Index, *FCCCarMode::GetDisplayName(Mode).ToString());
	if (OnClickedHandler)
	{
		OnClickedHandler(this);
	}
}

#undef LOCTEXT_NAMESPACE
