// Fill out your copyright notice in the Description page of Project Settings.


#include "RangeUserWidget.h"


URangeUserWidget::URangeUserWidget(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer)
{

}

void URangeUserWidget::NativeConstruct()
{
	Super::NativeConstruct();
}

// Takes in params from new Throw, populates the HUD. Intended for initial population.
void URangeUserWidget::PopulateHUD()
{
	release_stats = ADiscThrow::latest_disc_throw->get_initial_release_stats();
	cumulative_stats = ADiscThrow::latest_disc_throw->get_flight_cumulative_stats();



	
	currentdistance->SetText(FText::FromString(FString::SanitizeFloat(round(cumulative_stats->current_distance))+ " M"));
	discspeed->SetText(FText::FromString(FString::SanitizeFloat(round(cumulative_stats->current_speed))+ " M/s"));
	turnfade->SetText(FText::FromString(FString::SanitizeFloat(round(cumulative_stats->current_turnfade))+" M"));
	discspin->SetText(FText::FromString(FString::SanitizeFloat(round(cumulative_stats->current_spin))+" rotations/s"));
    //currentwobble->SetText(FText::FromString(FString::SanitizeFloat(cumulative_stats->current_wobble)));



	releasespinpercent->SetText(FText::FromString(FString::SanitizeFloat(release_stats->initial_spin_percent)+ " %"));
	releasespinrate->SetText(FText::FromString(FString::SanitizeFloat(release_stats->initial_spin_rate)+" rotations/s"));
	releasediscspeed->SetText(FText::FromString(FString::SanitizeFloat(release_stats->initial_speed)+ " M/s"));
	//releasewobble->SetText(FText::FromString(FString::SanitizeFloat(release_stats.initial_wobble)));

}

// TODO: Takes in params from new Throw, update fields in HUD. Intended to update continuously in Tick
void URangeUserWidget::UpdateHUD()
{
	
}
