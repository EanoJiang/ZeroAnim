#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Styling/SlateBrush.h"
#include "PlumPoleInteractPromptWidget.generated.h"

class SBorder;
class STextBlock;
class SWidget;

/**
 * PBZ-style minimalist floating interact keycap prompt (ivory rounded keycap + diamond pip),
 * switching automatically between Keyboard ("Space") and Gamepad ("A") glyphs.
 * Modders can subclass this in UMG (/Game/PlumPole/UI/WBP_PlumPoleInteractPrompt) and handle OnInputDeviceUpdated.
 */
UCLASS(Blueprintable)
class ZEROANIM_API UPlumPoleInteractPromptWidget : public UUserWidget
{
    GENERATED_BODY()

public:
    UFUNCTION(BlueprintCallable, Category = "Plum Pole|UI")
    void SetUsingGamepad(bool bInUsingGamepad);

    UFUNCTION(BlueprintPure, Category = "Plum Pole|UI")
    bool IsUsingGamepad() const { return bUsingGamepad; }

    UFUNCTION(BlueprintCallable, Category = "Plum Pole|UI")
    void SetPromptAlpha(float InAlpha);

    UFUNCTION(BlueprintImplementableEvent, Category = "Plum Pole|UI")
    void OnInputDeviceUpdated(bool bGamepad);

protected:
    virtual TSharedRef<SWidget> RebuildWidget() override;
    virtual void ReleaseSlateResources(bool bReleaseChildren) override;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Plum Pole|UI")
    FText KeyboardKeyLabel = FText::FromString(TEXT("Space"));

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Plum Pole|UI")
    FText GamepadKeyLabel = FText::FromString(TEXT("A"));

private:
    void RefreshPromptVisuals();

    TSharedPtr<SWidget> RootContainer;
    TSharedPtr<SBorder> HaloBorder;
    TSharedPtr<SBorder> KeycapBorder;
    TSharedPtr<STextBlock> KeyTextBlock;
    TSharedPtr<STextBlock> PipTextBlock;

    FSlateBrush HaloBrush;
    FSlateBrush KeycapBrush;
    bool bUsingGamepad = false;
    float PromptAlpha = 1.0f;
};
