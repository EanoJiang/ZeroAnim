#include "PlumPoleCharacter.h"

#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"
#include "Framework/Application/IInputProcessor.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/PlayerController.h"
#include "InputAction.h"
#include "InputMappingContext.h"

class FPlumPoleInputDeviceProcessor final : public IInputProcessor
{
public:
    explicit FPlumPoleInputDeviceProcessor(APlumPoleCharacter* InOwner)
        : Owner(InOwner)
    {
    }

    virtual void Tick(const float DeltaTime, FSlateApplication& SlateApp, TSharedRef<ICursor> Cursor) override {}

    virtual bool HandleKeyDownEvent(FSlateApplication& SlateApp, const FKeyEvent& InKeyEvent) override
    {
        if (APlumPoleCharacter* Char = Owner.Get())
        {
            Char->SetUsingGamepadForPrompt(InKeyEvent.GetKey().IsGamepadKey());
        }
        return false;
    }

    virtual bool HandleAnalogInputEvent(FSlateApplication& SlateApp, const FAnalogInputEvent& InAnalogInputEvent) override
    {
        if (InAnalogInputEvent.GetKey().IsGamepadKey() && FMath::Abs(InAnalogInputEvent.GetAnalogValue()) > 0.25f)
        {
            if (APlumPoleCharacter* Char = Owner.Get())
            {
                Char->SetUsingGamepadForPrompt(true);
            }
        }
        return false;
    }

    virtual bool HandleMouseButtonDownEvent(FSlateApplication& SlateApp, const FPointerEvent& MouseEvent) override
    {
        if (APlumPoleCharacter* Char = Owner.Get())
        {
            Char->SetUsingGamepadForPrompt(false);
        }
        return false;
    }

    virtual bool HandleMouseMoveEvent(FSlateApplication& SlateApp, const FPointerEvent& MouseEvent) override
    {
        if (MouseEvent.GetCursorDelta().SizeSquared() > 4.0f)
        {
            if (APlumPoleCharacter* Char = Owner.Get())
            {
                Char->SetUsingGamepadForPrompt(false);
            }
        }
        return false;
    }

private:
    TWeakObjectPtr<APlumPoleCharacter> Owner;
};

void APlumPoleCharacter::RegisterInputDeviceDetector()
{
    if (InputDeviceProcessor.IsValid() || !FSlateApplication::IsInitialized())
    {
        return;
    }
    InputDeviceProcessor = MakeShared<FPlumPoleInputDeviceProcessor>(this);
    FSlateApplication::Get().RegisterInputPreProcessor(InputDeviceProcessor);
}

void APlumPoleCharacter::UnregisterInputDeviceDetector()
{
    if (InputDeviceProcessor.IsValid())
    {
        if (FSlateApplication::IsInitialized())
        {
            FSlateApplication::Get().UnregisterInputPreProcessor(InputDeviceProcessor);
        }
        InputDeviceProcessor.Reset();
    }
}

void APlumPoleCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
    Super::SetupPlayerInputComponent(PlayerInputComponent);
    UEnhancedInputComponent* EnhancedInput = Cast<UEnhancedInputComponent>(PlayerInputComponent);
    APlayerController* Player = Cast<APlayerController>(GetController());
    if (Player && Player->IsLocalPlayerController())
    {
        RegisterInputDeviceDetector();
    }
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
    UnregisterInputDeviceDetector();
    Super::NotifyControllerChanged();
}

void APlumPoleCharacter::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    RemovePoleInputContext();
    UnregisterInputDeviceDetector();
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
