#pragma once

#include "CoreMinimal.h"
#include "PlumPoleInteractPromptWidget.h"
#include "HoloSwitchPromptWidget.generated.h"

class SBorder;
class STextBlock;
class SVerticalBox;

/**
 * PBZ-styled F-key character switch prompt inheriting exact 5-state MovieScene transitions,
 * ring morph (T_PBZ_Interact_Ellipse), and keycap scale (1.5x) from UPlumPoleInteractPromptWidget.
 * Displays a crisp matching PBZ 'F' keycap plus a subtle dark-ink character name label underneath.
 */
UCLASS(Blueprintable)
class ZEROANIM_API UHoloSwitchPromptWidget : public UPlumPoleInteractPromptWidget
{
    GENERATED_BODY()

public:
    UHoloSwitchPromptWidget(const FObjectInitializer& ObjectInitializer);

    UFUNCTION(BlueprintCallable, Category = "Hologram Switch|UI")
    void SetPromptLabel(const FText& InLabel);

    void RefreshLabelVisibility();

    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Hologram Switch|UI")
    FText PromptLabel;

protected:
    virtual TSharedRef<SWidget> RebuildWidget() override;
    virtual void ReleaseSlateResources(bool bReleaseChildren) override;
    virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

private:
    void SyncLabelOpacity();

    TSharedPtr<SVerticalBox> WrapperBox;
    TSharedPtr<SBorder> LabelPillBorder;
    TSharedPtr<STextBlock> LabelTextBlock;
};
