#include "PlumPoleCharacter.h"

#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/PlayerController.h"
#include "InputAction.h"
#include "InputMappingContext.h"

void APlumPoleCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
    Super::SetupPlayerInputComponent(PlayerInputComponent);
    UEnhancedInputComponent* EnhancedInput = Cast<UEnhancedInputComponent>(PlayerInputComponent);
    APlayerController* Player = Cast<APlayerController>(GetController());
    if (!EnhancedInput || !Player || !PoleInputMappingContext || !PoleFlipInputAction)
    {
        return;
    }

    UEnhancedInputLocalPlayerSubsystem* Input = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(Player->GetLocalPlayer());
    if (!Input)
    {
        return;
    }

    if (!Input->HasMappingContext(PoleInputMappingContext))
    {
        Input->AddMappingContext(PoleInputMappingContext, 0);
        PoleInputOwner = Input;
    }
    EnhancedInput->BindAction(PoleFlipInputAction, ETriggerEvent::Started, this, &APlumPoleCharacter::HandlePoleFlipInput);
}

void APlumPoleCharacter::NotifyControllerChanged()
{
    RemovePoleInputContext();
    Super::NotifyControllerChanged();
}

void APlumPoleCharacter::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    RemovePoleInputContext();
    Super::EndPlay(EndPlayReason);
}

void APlumPoleCharacter::RemovePoleInputContext()
{
    if (UEnhancedInputLocalPlayerSubsystem* Input = PoleInputOwner.Get())
    {
        Input->RemoveMappingContext(PoleInputMappingContext);
    }
    PoleInputOwner.Reset();
}

void APlumPoleCharacter::HandlePoleFlipInput()
{
    PlumPoleTraversal->TryPoleFlip();
}
