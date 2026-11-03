#include "UI/FCCMapTrainButton.h"
#include "FCCRuleSubsystem.h"
#include "FreightCarControl.h"
#include "UI/FCCWidgetUtils.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/Image.h"
#include "Components/SizeBox.h"
#include "Engine/Texture2D.h"
#include "FGActorRepresentation.h"
#include "FGActorRepresentationManager.h"
#include "FGLocomotive.h"
#include "FGPlayerController.h"
#include "FGRailroadSubsystem.h"
#include "FGTrain.h"
#include "TimerManager.h"
#include "UI/FGGameUI.h"
#include "UI/FGInteractWidget.h"

#define LOCTEXT_NAMESPACE "FreightCarControl"

namespace
{
	// Icon shown on the button, and the one used if it is missing.
	const TCHAR* MapButtonIcon = TEXT("/Game/FactoryGame/Buildable/Factory/Train/Station/UI/TXUI_Train_Icon_Timetable.TXUI_Train_Icon_Timetable");
	const TCHAR* MapButtonFallbackIcon = TEXT("/Game/FactoryGame/Interface/UI/Assets/MonochromeIcons/TXUI_MIcon_Cogwheel.TXUI_MIcon_Cogwheel");

	// Size of the square button and of its icon.
	constexpr float MapButtonSize = 26.f;
	constexpr float MapButtonIconSize = 18.f;

	// Ticks a list row may take to get its representation before the button gives up and stays hidden.
	constexpr int32 MapButtonMaxRefreshAttempts = 30;

	// Returns a flat brush of Color.
	FSlateBrush MakeFlatBrush(const FLinearColor& Color)
	{
		FSlateBrush Brush;
		Brush.DrawAs = ESlateBrushDrawType::Box;
		Brush.TintColor = FSlateColor(Color);
		return Brush;
	}
}

TSharedRef<SWidget> UFCCMapTrainButton::RebuildWidget()
{
	BuildTree();
	return Super::RebuildWidget();
}

void UFCCMapTrainButton::BuildTree()
{
	if (!WidgetTree)
	{
		WidgetTree = NewObject<UWidgetTree>(this, TEXT("WidgetTree"), RF_Transient);
	}
	if (WidgetTree->RootWidget)
	{
		return;
	}
	USizeBox* Frame = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
	Frame->SetWidthOverride(MapButtonSize);
	Frame->SetHeightOverride(MapButtonSize);
	WidgetTree->RootWidget = Frame;

	mButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass());
	FButtonStyle Style;
	Style.SetNormal(MakeFlatBrush(FLinearColor(0.02f, 0.02f, 0.02f, 0.85f)));
	Style.SetHovered(MakeFlatBrush(FLinearColor(0.904661f, 0.296138f, 0.033105f, 1.f)));
	Style.SetPressed(MakeFlatBrush(FLinearColor(0.6f, 0.19f, 0.02f, 1.f)));
	Style.SetNormalPadding(FMargin(0.f));
	Style.SetPressedPadding(FMargin(0.f));
	mButton->SetStyle(Style);
	mButton->SetToolTipText(LOCTEXT("MapTrainButtonTooltip", "Open this train's locomotive menu"));
	mButton->OnClicked.AddUniqueDynamic(this, &UFCCMapTrainButton::HandleClicked);
	Frame->AddChild(mButton);

	UImage* Icon = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass());
	UTexture2D* Texture = LoadObject<UTexture2D>(nullptr, MapButtonIcon);
	if (!Texture)
	{
		UE_LOG(LogFreightCarControl, Warning, TEXT("Map train button: icon %s not found, using %s"), MapButtonIcon, MapButtonFallbackIcon);
		Texture = LoadObject<UTexture2D>(nullptr, MapButtonFallbackIcon);
	}
	if (Texture)
	{
		Icon->SetBrushFromTexture(Texture);
	}
	Icon->SetDesiredSizeOverride(FVector2D(MapButtonIconSize, MapButtonIconSize));
	Icon->SetVisibility(ESlateVisibility::HitTestInvisible);
	mButton->AddChild(Icon);
}

void UFCCMapTrainButton::NativeConstruct()
{
	Super::NativeConstruct();
	RefreshAttempts = 0;
	Refresh();
}

void UFCCMapTrainButton::NativeDestruct()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(RefreshTimer);
	}
	Super::NativeDestruct();
}

void UFCCMapTrainButton::Refresh()
{
	const UFGActorRepresentation* Representation = GetRepresentation();
	if (!Representation)
	{
		SetVisibility(ESlateVisibility::Collapsed);
		UWorld* World = GetWorld();
		if (World && ++RefreshAttempts <= MapButtonMaxRefreshAttempts)
		{
			RefreshTimer = World->GetTimerManager().SetTimerForNextTick(FTimerDelegate::CreateUObject(this, &UFCCMapTrainButton::Refresh));
		}
		else
		{
			UE_LOG(LogFreightCarControl, Verbose, TEXT("Map train button in %s: row has no representation; hidden"),
				*GetNameSafe(FCCWidgetUtils::GetOwningUserWidget(this)));
		}
		return;
	}
	const bool bIsTrain = Representation->GetRepresentationType() == ERepresentationType::RT_Train;
	SetVisibility(bIsTrain ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
}

UFGActorRepresentation* UFCCMapTrainButton::GetRepresentation() const
{
	return Cast<UFGActorRepresentation>(FCCWidgetUtils::GetObjectProperty(FCCWidgetUtils::GetOwningUserWidget(this), TEXT("mActorRepresentation")));
}

AFGTrain* UFCCMapTrainButton::FindTrain() const
{
	UFGActorRepresentation* Representation = GetRepresentation();
	if (!Representation)
	{
		return nullptr;
	}
	if (AFGTrain* Train = Cast<AFGTrain>(Representation->GetRealActor()))
	{
		return Train;
	}

	// The real actor isn't always set on clients; match the train whose representation this is instead.
	AFGRailroadSubsystem* Railroad = AFGRailroadSubsystem::Get(GetWorld());
	AFGActorRepresentationManager* Manager = AFGActorRepresentationManager::Get(GetWorld());
	if (!Railroad || !Manager)
	{
		return nullptr;
	}
	TArray<AFGTrain*> Trains;
	Railroad->GetAllTrains(Trains);
	for (AFGTrain* Train : Trains)
	{
		if (Train && Manager->FindActorRepresentation(Train) == Representation)
		{
			return Train;
		}
	}
	return nullptr;
}

void UFCCMapTrainButton::HandleClicked()
{
	AFGTrain* Train = FindTrain();
	AFGLocomotive* Locomotive = AFCCRuleSubsystem::GetLeadLocomotive(Train);
	if (!Locomotive)
	{
		UE_LOG(LogFreightCarControl, Warning, TEXT("Map: no locomotive found for %s (train %s)"),
			Train ? *Train->GetTrainName().ToString() : TEXT("this row"), *GetNameSafe(Train));
		return;
	}
	UClass* MenuClass = Cast<UClass>(FCCWidgetUtils::GetObjectProperty(Locomotive, TEXT("mLocomotiveMenuWidgetClass")));
	if (!MenuClass)
	{
		MenuClass = FCCWidgetUtils::LoadWidgetClass(FCCWidgetUtils::LocomotiveMenuClass);
	}
	AFGPlayerController* PlayerController = Cast<AFGPlayerController>(GetOwningPlayer());
	UFGGameUI* GameUI = PlayerController ? PlayerController->GetGameUI() : nullptr;
	if (!MenuClass || !MenuClass->IsChildOf(UFGInteractWidget::StaticClass()) || !GameUI)
	{
		UE_LOG(LogFreightCarControl, Error, TEXT("Map: can't open the locomotive menu (menu class %s, game UI %s)"),
			*GetNameSafe(MenuClass), *GetNameSafe(GameUI));
		return;
	}

	UFGInteractWidget* OpenMenu = nullptr;
	for (UUserWidget* Owner = FCCWidgetUtils::GetOwningUserWidget(this); Owner; Owner = FCCWidgetUtils::GetOwningUserWidget(Owner))
	{
		if (UFGInteractWidget* AsInteract = Cast<UFGInteractWidget>(Owner))
		{
			OpenMenu = AsInteract;
		}
	}
	const bool bClosed = OpenMenu && GameUI->PopWidget(OpenMenu);
	UE_LOG(LogFreightCarControl, Log, TEXT("Map: opening the locomotive menu of %s (%s) with %s; %s %s"),
		*Train->GetTrainName().ToString(), *GetNameSafe(Locomotive), *GetNameSafe(MenuClass),
		bClosed ? TEXT("closed") : TEXT("couldn't close"), *GetNameSafe(OpenMenu));

	// Opened a tick later so the closed menu has given back input first.
	TWeakObjectPtr<UFGGameUI> WeakGameUI = GameUI;
	TWeakObjectPtr<AFGLocomotive> WeakLocomotive = Locomotive;
	TSubclassOf<UFGInteractWidget> MenuWidgetClass = MenuClass;
	PlayerController->GetWorldTimerManager().SetTimerForNextTick([WeakGameUI, WeakLocomotive, MenuWidgetClass]()
	{
		if (WeakGameUI.IsValid() && WeakLocomotive.IsValid())
		{
			WeakGameUI->OnToggleInteractWidget(MenuWidgetClass, WeakLocomotive.Get());
		}
	});
}

#undef LOCTEXT_NAMESPACE
