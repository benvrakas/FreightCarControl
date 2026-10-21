#include "UI/FCCWidgetUtils.h"
#include "FreightCarControl.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "FGRailroadTimeTable.h"
#include "FGTrainStationIdentifier.h"
#include "UObject/UnrealType.h"

namespace FCCWidgetUtils
{
	const TCHAR* StandardButtonClass = TEXT("/Game/FactoryGame/Interface/UI/InGame/-Shared/Widget_StandardButton.Widget_StandardButton_C");
	const TCHAR* TrainMenuLabelClass = TEXT("/Game/FactoryGame/Buildable/Vehicle/Train/Locomotive/UI/BPW_TrainMenuLabel.BPW_TrainMenuLabel_C");
	const TCHAR* StationSettingsClass = TEXT("/Game/FactoryGame/Buildable/Vehicle/Train/Locomotive/UI/TimeTable/BPW_Train_TimeTable_StationSettings.BPW_Train_TimeTable_StationSettings_C");
	const TCHAR* TimeTableSetupClass = TEXT("/Game/FactoryGame/Buildable/Vehicle/Train/Locomotive/UI/TimeTable/BPW_Train_TimeTable_Setup.BPW_Train_TimeTable_Setup_C");
	const TCHAR* SetupStationClassName = TEXT("BPW_Train_TimeTable_SetupStation_C");
	const TCHAR* FreightWagonWidgetClass = TEXT("/Game/FactoryGame/Buildable/Vehicle/Train/Wagon/Widget_FreightWagon.Widget_FreightWagon_C");
}

UClass* FCCWidgetUtils::LoadWidgetClass(const TCHAR* Path)
{
	static TMap<FString, TWeakObjectPtr<UClass>> Cache;
	if (const TWeakObjectPtr<UClass>* Found = Cache.Find(Path))
	{
		if (Found->IsValid())
		{
			return Found->Get();
		}
	}
	UClass* Class = LoadClass<UUserWidget>(nullptr, Path);
	if (!Class)
	{
		UE_LOG(LogFreightCarControl, Warning, TEXT("Widget class %s not found"), Path);
	}
	Cache.Add(Path, Class);
	return Class;
}

UUserWidget* FCCWidgetUtils::CreateStandardButton(UWidgetTree* Tree, const FText& Text)
{
	UClass* Class = Tree ? LoadWidgetClass(StandardButtonClass) : nullptr;
	UUserWidget* Button = Class ? Tree->ConstructWidget<UUserWidget>(Class) : nullptr;
	if (Button)
	{
		SetWidgetText(Button, Text);
	}
	return Button;
}

UUserWidget* FCCWidgetUtils::CreateMenuLabel(UWidgetTree* Tree, const FText& Text)
{
	UClass* Class = Tree ? LoadWidgetClass(TrainMenuLabelClass) : nullptr;
	UUserWidget* Label = Class ? Tree->ConstructWidget<UUserWidget>(Class) : nullptr;
	if (Label)
	{
		SetWidgetText(Label, Text);
	}
	return Label;
}

bool FCCWidgetUtils::SetWidgetText(UUserWidget* Widget, const FText& Text)
{
	UFunction* Function = Widget ? Widget->FindFunction(TEXT("SetText")) : nullptr;
	if (Function)
	{
		FTextProperty* TextParam = nullptr;
		for (TFieldIterator<FProperty> It(Function); It && It->HasAnyPropertyFlags(CPF_Parm); ++It)
		{
			if (FTextProperty* AsText = CastField<FTextProperty>(*It))
			{
				TextParam = AsText;
				break;
			}
		}
		if (TextParam)
		{
			uint8* Params = static_cast<uint8*>(FMemory_Alloca(FMath::Max<int32>(Function->ParmsSize, 1)));
			FMemory::Memzero(Params, Function->ParmsSize);
			for (TFieldIterator<FProperty> It(Function); It && It->HasAnyPropertyFlags(CPF_Parm); ++It)
			{
				It->InitializeValue_InContainer(Params);
			}
			TextParam->SetPropertyValue_InContainer(Params, Text);
			Widget->ProcessEvent(Function, Params);
			for (TFieldIterator<FProperty> It(Function); It && It->HasAnyPropertyFlags(CPF_Parm); ++It)
			{
				It->DestroyValue_InContainer(Params);
			}
			return true;
		}
	}
	if (UTextBlock* TextObject = Cast<UTextBlock>(GetObjectProperty(Widget, TEXT("mTextObject"))))
	{
		TextObject->SetText(Text);
		return true;
	}
	UE_LOG(LogFreightCarControl, Warning, TEXT("%s has no SetText; text \"%s\" not shown"), *GetNameSafe(Widget), *Text.ToString());
	return false;
}

UButton* FCCWidgetUtils::GetInnerButton(UUserWidget* StandardButton)
{
	return Cast<UButton>(GetObjectProperty(StandardButton, TEXT("mButton")));
}

bool FCCWidgetUtils::BindEventDispatcher(UObject* Source, FName DelegateName, UObject* Target, FName FunctionName)
{
	FMulticastDelegateProperty* Property = Source ? FindFProperty<FMulticastDelegateProperty>(Source->GetClass(), DelegateName) : nullptr;
	if (!Property || !Target || !Target->FindFunction(FunctionName))
	{
		UE_LOG(LogFreightCarControl, Warning, TEXT("Can't bind %s.%s to %s.%s"), *GetNameSafe(Source), *DelegateName.ToString(), *GetNameSafe(Target), *FunctionName.ToString());
		return false;
	}
	FScriptDelegate Delegate;
	Delegate.BindUFunction(Target, FunctionName);
	Property->AddDelegate(Delegate, Source);
	return true;
}

void FCCWidgetUtils::UnbindEventDispatcher(UObject* Source, FName DelegateName, UObject* Target, FName FunctionName)
{
	FMulticastDelegateProperty* Property = Source ? FindFProperty<FMulticastDelegateProperty>(Source->GetClass(), DelegateName) : nullptr;
	if (Property && Target)
	{
		FScriptDelegate Delegate;
		Delegate.BindUFunction(Target, FunctionName);
		Property->RemoveDelegate(Delegate, Source);
	}
}

UObject* FCCWidgetUtils::GetObjectProperty(const UObject* Owner, FName PropertyName)
{
	FObjectPropertyBase* Property = Owner ? FindFProperty<FObjectPropertyBase>(Owner->GetClass(), PropertyName) : nullptr;
	return Property ? Property->GetObjectPropertyValue_InContainer(Owner) : nullptr;
}

AFGTrainStationIdentifier* FCCWidgetUtils::GetSetupStationStation(const UObject* SetupStationWidget)
{
	if (!SetupStationWidget)
	{
		return nullptr;
	}
	FProperty* Property = FindFProperty<FProperty>(SetupStationWidget->GetClass(), TEXT("mStation"));
	if (FStructProperty* AsStruct = CastField<FStructProperty>(Property))
	{
		if (AsStruct->Struct == FTimeTableStop::StaticStruct())
		{
			return AsStruct->ContainerPtrToValuePtr<FTimeTableStop>(SetupStationWidget)->Station;
		}
	}
	if (FObjectPropertyBase* AsObject = CastField<FObjectPropertyBase>(Property))
	{
		return Cast<AFGTrainStationIdentifier>(AsObject->GetObjectPropertyValue_InContainer(SetupStationWidget));
	}
	static bool bWarned = false;
	if (!bWarned)
	{
		bWarned = true;
		UE_LOG(LogFreightCarControl, Warning, TEXT("%s: mStation is %s, expected FTimeTableStop or a station"),
			*GetNameSafe(SetupStationWidget->GetClass()), Property ? *Property->GetCPPType() : TEXT("missing"));
	}
	return nullptr;
}

TArray<UUserWidget*> FCCWidgetUtils::FindNestedWidgetsByClassName(UUserWidget* Root, const TCHAR* ClassName)
{
	TArray<UUserWidget*> Found;
	TFunction<void(UUserWidget*, int32)> Visit = [&](UUserWidget* Widget, int32 Depth)
	{
		if (!Widget || !Widget->WidgetTree || Depth > 6)
		{
			return;
		}
		Widget->WidgetTree->ForEachWidget([&](UWidget* Child)
		{
			if (UUserWidget* AsUser = Cast<UUserWidget>(Child))
			{
				if (AsUser->GetClass()->GetName() == ClassName)
				{
					Found.Add(AsUser);
				}
				else
				{
					Visit(AsUser, Depth + 1);
				}
			}
		});
	};
	Visit(Root, 0);
	return Found;
}

void FCCWidgetUtils::CopyTextStyle(UTextBlock* Target, UUserWidget* Root, FName SourceName, int32 MaxSize)
{
	if (!Target || !Root || !Root->WidgetTree)
	{
		return;
	}
	UTextBlock* Source = SourceName.IsNone() ? nullptr : Cast<UTextBlock>(Root->WidgetTree->FindWidget(SourceName));
	if (!Source)
	{
		Root->WidgetTree->ForEachWidget([&](UWidget* Child)
		{
			if (!Source && Child != Target)
			{
				Source = Cast<UTextBlock>(Child);
			}
		});
	}
	if (Source)
	{
		FSlateFontInfo Font = Source->GetFont();
		if (MaxSize > 0 && Font.Size > MaxSize)
		{
			Font.Size = MaxSize;
		}
		Target->SetFont(Font);
		Target->SetColorAndOpacity(Source->GetColorAndOpacity());
	}
	else if (MaxSize > 0)
	{
		FSlateFontInfo Font = Target->GetFont();
		Font.Size = FMath::Min<float>(Font.Size, MaxSize);
		Target->SetFont(Font);
	}
}

UUserWidget* FCCWidgetUtils::GetOwningUserWidget(const UWidget* Widget)
{
	for (UObject* Outer = Widget ? Widget->GetOuter() : nullptr; Outer; Outer = Outer->GetOuter())
	{
		if (UUserWidget* AsUser = Cast<UUserWidget>(Outer))
		{
			return AsUser;
		}
	}
	return nullptr;
}
