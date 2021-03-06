// Fill out your copyright notice in the Description page of Project Settings.


#include "DebugWidget.h"

UDebugWidget::UDebugWidget(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer)
{
	
}

// Initialize global variables for DAero Debug HUD fields.
void UDebugWidget::InitializeDAero()
{
	DebugAero1->SetText(FText::FromString(FString::SanitizeFloat(gv_aero_debug0)));
	DebugAero2->SetText(FText::FromString(FString::SanitizeFloat(gv_aero_debug1)));
	DebugAero3->SetText(FText::FromString(FString::SanitizeFloat(gv_aero_debug2)));
	DebugAero4->SetText(FText::FromString(FString::SanitizeFloat(gv_aero_debug3)));
	DebugAero5->SetText(FText::FromString(FString::SanitizeFloat(gv_aero_debug4)));
	DebugAero6->SetText(FText::FromString(FString::SanitizeFloat(gv_aero_debug5)));

	DebugAero1Text->SetText(FText::FromString(FString(gv_aero_label_debug0.c_str())));
	DebugAero2Text->SetText(FText::FromString(FString(gv_aero_label_debug1.c_str())));
	DebugAero3Text->SetText(FText::FromString(FString(gv_aero_label_debug2.c_str())));
	DebugAero4Text->SetText(FText::FromString(FString(gv_aero_label_debug3.c_str())));
	DebugAero5Text->SetText(FText::FromString(FString(gv_aero_label_debug4.c_str())));
	DebugAero6Text->SetText(FText::FromString(FString(gv_aero_label_debug5.c_str())));

	int i;
	for (i = 0; i < DfisX::disc_object_array.size(); i++)
	{
		DebugDiscMoldDropDown->AddOption(DfisX::disc_object_array[i].mold_name);
	}
	
}

// Override of Native Construct. This can be used to enact listeners for Button Clicks/Input field changes.
void UDebugWidget::NativeConstruct()
{
	Super::NativeConstruct();
	GenDebugThrowButton->OnClicked.AddDynamic(this, &UDebugWidget::OnClick);
}

void UDebugWidget::OnClick()
{
	
}

// Getters for Input fields of Gen Throw Debug HUD.
// Currently Hyzer
double UDebugWidget::GetGenThrow1Input()
{
	return FCString::Atof(*GenDebugHyzerInput->GetText().ToString());
}
// Currently NoseUp
double UDebugWidget::GetGenThrow2Input()
{
	return FCString::Atof(*GenDebugNoseUpInput->GetText().ToString());
}
// Currently Speed
double UDebugWidget::GetGenThrow3Input()
{
	return FCString::Atof(*GenDebugSpeedInput->GetText().ToString());
}
// Currently Direction
double UDebugWidget::GetGenThrow4Input()
{
	return FCString::Atof(*GenDebugDirectionInput->GetText().ToString());
}
// Currently Loft
double UDebugWidget::GetGenThrow5Input()
{
	return FCString::Atof(*GenDebugLoftInput->GetText().ToString());
}
// Currently Spin Percent
double UDebugWidget::GetGenThrow6Input()
{
	return FCString::Atof(*GenDebugSpinPercentInput->GetText().ToString());
}
// Currently Wobble
double UDebugWidget::GetGenThrow7Input()
{
	return FCString::Atof(*GenDebugWobbleInput->GetText().ToString());
}
// Currently Disc Mold Drop Down
int32 UDebugWidget::GetGenThrow8Input()
{
	return DebugDiscMoldDropDown->GetSelectedIndex();
}


// Getters for Text/Input fields of DAero Debug HUD.
FString UDebugWidget::GetAero1Text()
{
	return FString(DebugAero1Text->GetText().ToString());
}
double UDebugWidget::GetAero1Input()
{
	return FCString::Atof(*DebugAero1->GetText().ToString());
}
FString UDebugWidget::GetAero2Text()
{
	return FString(DebugAero2Text->GetText().ToString());
}
double UDebugWidget::GetAero2Input()
{
	return FCString::Atof(*DebugAero2->GetText().ToString());
}
FString UDebugWidget::GetAero3Text()
{
	return FString(DebugAero3Text->GetText().ToString());
}
double UDebugWidget::GetAero3Input()
{
	return FCString::Atof(*DebugAero3->GetText().ToString());
}
FString UDebugWidget::GetAero4Text()
{
	return FString(DebugAero4Text->GetText().ToString());
}
double UDebugWidget::GetAero4Input()
{
	return FCString::Atof(*DebugAero4->GetText().ToString());
}
FString UDebugWidget::GetAero5Text()
{
	return FString(DebugAero5Text->GetText().ToString());
}
double UDebugWidget::GetAero5Input()
{
	return FCString::Atof(*DebugAero5->GetText().ToString());
}
FString UDebugWidget::GetAero6Text()
{
	return FString(DebugAero6Text->GetText().ToString());
}
double UDebugWidget::GetAero6Input()
{
	return FCString::Atof(*DebugAero6->GetText().ToString());
}


//Setters for Text/Input fields of DAero Debug HUD.
void UDebugWidget::SetAero1Text(FString aero1)
{
	DebugAero1Text->SetText(FText::FromString(aero1));
}
void UDebugWidget::SetAero1Input(double aero1in)
{
	DebugAero1->SetText(FText::FromString(FString::SanitizeFloat(aero1in)));
}
void UDebugWidget::SetAero2Text(FString aero2)
{
	DebugAero2Text->SetText(FText::FromString(aero2));
}
void UDebugWidget::SetAero2Input(double aero2in)
{
	DebugAero1->SetText(FText::FromString(FString::SanitizeFloat(aero2in)));
}
void UDebugWidget::SetAero3Text(FString aero3)
{
	DebugAero3Text->SetText(FText::FromString(aero3));
}
void UDebugWidget::SetAero3Input(double aero3in)
{
	DebugAero1->SetText(FText::FromString(FString::SanitizeFloat(aero3in)));
}
void UDebugWidget::SetAero4Text(FString aero4)
{
	DebugAero4Text->SetText(FText::FromString(aero4));
}
void UDebugWidget::SetAero4Input(double aero4in)
{
	DebugAero1->SetText(FText::FromString(FString::SanitizeFloat(aero4in)));
}
void UDebugWidget::SetAero5Text(FString aero5)
{
	DebugAero5Text->SetText(FText::FromString(aero5));
}
void UDebugWidget::SetAero5Input(double aero5in)
{
	DebugAero1->SetText(FText::FromString(FString::SanitizeFloat(aero5in)));
}
void UDebugWidget::SetAero6Text(FString aero6)
{
	DebugAero6Text->SetText(FText::FromString(aero6));
}
void UDebugWidget::SetAero6Input(double aero6in)
{
	DebugAero1->SetText(FText::FromString(FString::SanitizeFloat(aero6in)));
}