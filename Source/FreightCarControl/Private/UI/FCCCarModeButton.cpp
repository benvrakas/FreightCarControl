#include "UI/FCCCarModeButton.h"
#include "FreightCarControl.h"
#include "UI/FCCWidgetUtils.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/SizeBox.h"
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

void UFCCCarModeButton::SetFixedWidth(float Width)
{
	FixedWidth = Width;
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
	mFrame = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
	WidgetTree->RootWidget = mFrame;

	mVanillaButton = FCCWidgetUtils::CreateStandardButton(WidgetTree, FText::GetEmpty());
	if (mVanillaButton)
	{
		mFrame->AddChild(mVanillaButton);
		if (UButton* Inner = FCCWidgetUtils::GetInnerButton(mVanillaButton))
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
		UButton* Button = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass());
		mFallbackText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
		Button->AddChild(mFallbackText);
		Button->OnClicked.AddUniqueDynamic(this, &UFCCCarModeButton::HandleClicked);
		mFrame->AddChild(Button);
	}
	Refresh();
}

void UFCCCarModeButton::Refresh()
{
	FText Text = FCCCarMode::GetDisplayName(Mode);
	if (!Label.IsEmpty())
	{
		Text = bModeFirst
			? FText::Format(LOCTEXT("CarButtonModeFirst", "{1}  |  {0}"), Label, Text)
			: FText::Format(LOCTEXT("CarButton", "{0}: {1}"), Label, Text);
	}
	if (mVanillaButton)
	{
		FCCWidgetUtils::SetWidgetText(mVanillaButton, Text);
	}
	else if (mFallbackText)
	{
		mFallbackText->SetText(Text);
	}
	if (mFrame)
	{
		if (FixedWidth > 0.f)
		{
			mFrame->SetWidthOverride(FixedWidth);
		}
		else
		{
			mFrame->ClearWidthOverride();
		}
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
