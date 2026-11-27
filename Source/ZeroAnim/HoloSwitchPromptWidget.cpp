#include "HoloSwitchPromptWidget.h"

#include "Engine/Texture2D.h"
#include "Fonts/SlateFontInfo.h"
#include "Styling/CoreStyle.h"
#include "UObject/ConstructorHelpers.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

UHoloSwitchPromptWidget::UHoloSwitchPromptWidget(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
{
    static ConstructorHelpers::FObjectFinder<UTexture2D> FKeyFinder(
        TEXT("/Game/CharacterShowcase/UI/T_PBZ_Key_PC_F.T_PBZ_Key_PC_F"));
    if (FKeyFinder.Succeeded())
    {
        KeyboardSpaceTexture = FKeyFinder.Object;
    }

    // Match PBZ Space height (32x32 * 1.5 RenderScale = 48x48 crisp square F keycap)
    KeyboardSpaceBaseSize = FVector2D(32.0, 32.0);
    PromptLabel = FText::FromString(TEXT("切换角色"));
}

void UHoloSwitchPromptWidget::SetPromptLabel(const FText& InLabel)
{
    PromptLabel = InLabel;
    if (LabelTextBlock.IsValid())
    {
        LabelTextBlock->SetText(PromptLabel);
    }
}

void UHoloSwitchPromptWidget::RefreshLabelVisibility()
{
    SyncLabelOpacity();
}

TSharedRef<SWidget> UHoloSwitchPromptWidget::RebuildWidget()
{
    if (!KeyboardSpaceTexture)
    {
        KeyboardSpaceTexture = LoadObject<UTexture2D>(
            nullptr, TEXT("/Game/CharacterShowcase/UI/T_PBZ_Key_PC_F.T_PBZ_Key_PC_F"));
    }

    TSharedRef<SWidget> BaseIconWidget = Super::RebuildWidget();

    FSlateFontInfo LabelFont = FCoreStyle::GetDefaultFontStyle("Bold", 12);

    SAssignNew(WrapperBox, SVerticalBox)
        + SVerticalBox::Slot()
        .AutoHeight()
        .HAlign(HAlign_Center)
        [
            BaseIconWidget
        ]
        + SVerticalBox::Slot()
        .AutoHeight()
        .HAlign(HAlign_Center)
        .Padding(FMargin(0.0f, 14.0f, 0.0f, 0.0f))
        [
            SAssignNew(LabelPillBorder, SBorder)
            .BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
            .BorderBackgroundColor(FLinearColor(0.03f, 0.04f, 0.06f, 0.78f))
            .Padding(FMargin(10.0f, 3.0f))
            [
                SAssignNew(LabelTextBlock, STextBlock)
                .Text(PromptLabel)
                .Font(LabelFont)
                .ColorAndOpacity(FSlateColor(FLinearColor(0.96f, 0.96f, 0.96f, 1.0f)))
                .ShadowOffset(FVector2D(1.0f, 1.0f))
                .ShadowColorAndOpacity(FLinearColor(0.0f, 0.0f, 0.0f, 0.85f))
                .Justification(ETextJustify::Center)
            ]
        ];

    SyncLabelOpacity();
    return WrapperBox.ToSharedRef();
}

void UHoloSwitchPromptWidget::ReleaseSlateResources(bool bReleaseChildren)
{
    Super::ReleaseSlateResources(bReleaseChildren);
    LabelTextBlock.Reset();
    LabelPillBorder.Reset();
    WrapperBox.Reset();
}

void UHoloSwitchPromptWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
    Super::NativeTick(MyGeometry, InDeltaTime);
    SyncLabelOpacity();
}

void UHoloSwitchPromptWidget::SyncLabelOpacity()
{
    if (!LabelPillBorder.IsValid())
    {
        return;
    }

    // Show the character name pill ONLY when in InputAction (F keycap visible), fading smoothly with it.
    const EPlumPoleInteractUIState State = GetTargetUIState();
    const bool bInInputState = (State == EPlumPoleInteractUIState::InputAction
        || State == EPlumPoleInteractUIState::InputActionScreen);
    const float LabelAlpha = bInInputState ? GetVisibleAlpha() : 0.0f;

    LabelPillBorder->SetRenderOpacity(LabelAlpha);
    LabelPillBorder->SetVisibility(LabelAlpha > 0.02f ? EVisibility::SelfHitTestInvisible : EVisibility::Collapsed);
}
