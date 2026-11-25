#include "PlumPoleInteractPromptWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

TSharedRef<SWidget> UPlumPoleInteractPromptWidget::RebuildWidget()
{
    // If a Blueprint child provides its own Designer WidgetTree, honor it directly.
    if (WidgetTree && WidgetTree->RootWidget)
    {
        return Super::RebuildWidget();
    }

    HaloBrush = FSlateRoundedBoxBrush(FLinearColor(0.01f, 0.01f, 0.015f, 0.65f), 11.0f);
    KeycapBrush = FSlateRoundedBoxBrush(
        FLinearColor(0.96f, 0.95f, 0.92f, 0.99f),
        7.0f,
        FLinearColor(0.05f, 0.05f, 0.05f, 0.90f),
        2.5f);

    TSharedRef<SWidget> Built = SNew(SBox)
        .HAlign(HAlign_Center)
        .VAlign(VAlign_Center)
        [
            SNew(SVerticalBox)
            + SVerticalBox::Slot()
            .AutoHeight()
            .HAlign(HAlign_Center)
            [
                SAssignNew(HaloBorder, SBorder)
                .BorderImage(&HaloBrush)
                .Padding(FMargin(4.0f))
                [
                    SAssignNew(KeycapBorder, SBorder)
                    .BorderImage(&KeycapBrush)
                    .HAlign(HAlign_Center)
                    .VAlign(VAlign_Center)
                    .Padding(FMargin(18.0f, 6.0f))
                    [
                        SAssignNew(KeyTextBlock, STextBlock)
                        .Text(KeyboardKeyLabel)
                        .Font(FCoreStyle::GetDefaultFontStyle("Bold", 26))
                        .ColorAndOpacity(FSlateColor(FLinearColor(0.012f, 0.012f, 0.015f, 1.0f)))
                        .Justification(ETextJustify::Center)
                    ]
                ]
            ]
            + SVerticalBox::Slot()
            .AutoHeight()
            .HAlign(HAlign_Center)
            .Padding(FMargin(0.0f, 4.0f, 0.0f, 0.0f))
            [
                SAssignNew(PipTextBlock, STextBlock)
                .Text(FText::FromString(TEXT("\u25C6")))
                .Font(FCoreStyle::GetDefaultFontStyle("Bold", 14))
                .ColorAndOpacity(FSlateColor(FLinearColor(0.95f, 0.94f, 0.90f, 0.92f)))
                .ShadowOffset(FVector2D(1.5f, 1.5f))
                .ShadowColorAndOpacity(FLinearColor(0.0f, 0.0f, 0.0f, 0.75f))
                .Justification(ETextJustify::Center)
            ]
        ];

    RootContainer = Built;
    RefreshPromptVisuals();
    return Built;
}

void UPlumPoleInteractPromptWidget::ReleaseSlateResources(bool bReleaseChildren)
{
    Super::ReleaseSlateResources(bReleaseChildren);
    RootContainer.Reset();
    HaloBorder.Reset();
    KeycapBorder.Reset();
    KeyTextBlock.Reset();
    PipTextBlock.Reset();
}

void UPlumPoleInteractPromptWidget::SetUsingGamepad(bool bInUsingGamepad)
{
    if (bUsingGamepad == bInUsingGamepad)
    {
        return;
    }
    bUsingGamepad = bInUsingGamepad;
    RefreshPromptVisuals();
    OnInputDeviceUpdated(bUsingGamepad);
}

void UPlumPoleInteractPromptWidget::SetPromptAlpha(float InAlpha)
{
    PromptAlpha = FMath::Clamp(InAlpha, 0.0f, 1.0f);
    SetRenderOpacity(PromptAlpha);
}

void UPlumPoleInteractPromptWidget::RefreshPromptVisuals()
{
    if (!KeycapBorder.IsValid() || !KeyTextBlock.IsValid() || !HaloBorder.IsValid())
    {
        return;
    }

    if (bUsingGamepad)
    {
        HaloBrush = FSlateRoundedBoxBrush(FLinearColor(0.01f, 0.01f, 0.015f, 0.68f), 32.0f);
        KeycapBrush = FSlateRoundedBoxBrush(
            FLinearColor(0.96f, 0.95f, 0.92f, 0.99f),
            28.0f,
            FLinearColor(0.10f, 0.44f, 0.20f, 0.98f),
            4.0f);
        KeycapBorder->SetPadding(FMargin(18.0f, 6.0f));
        KeyTextBlock->SetText(GamepadKeyLabel);
        KeyTextBlock->SetFont(FCoreStyle::GetDefaultFontStyle("Bold", 28));
    }
    else
    {
        HaloBrush = FSlateRoundedBoxBrush(FLinearColor(0.01f, 0.01f, 0.015f, 0.65f), 11.0f);
        KeycapBrush = FSlateRoundedBoxBrush(
            FLinearColor(0.96f, 0.95f, 0.92f, 0.99f),
            7.0f,
            FLinearColor(0.05f, 0.05f, 0.05f, 0.90f),
            2.5f);
        KeycapBorder->SetPadding(FMargin(18.0f, 6.0f));
        KeyTextBlock->SetText(KeyboardKeyLabel);
        KeyTextBlock->SetFont(FCoreStyle::GetDefaultFontStyle("Bold", 26));
    }
    HaloBorder->SetBorderImage(&HaloBrush);
    KeycapBorder->SetBorderImage(&KeycapBrush);
}
