#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "HoloCharacterSwitchManager.generated.h"

class AHoloCharacterStand;
class APawn;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UMeshComponent;
class UPointLightComponent;
class UPrimitiveComponent;

UENUM(BlueprintType)
enum class EHoloSwitchPhase : uint8
{
    Idle,
    OverlayEngulf,
    DissolveOut,
    MaterializeIn,
    OverlayFadeOut
};

USTRUCT()
struct FHoloSavedMeshMaterials
{
    GENERATED_BODY()

    UPROPERTY()
    TWeakObjectPtr<UMeshComponent> MeshComp;

    FName CompName = NAME_None;

    UPROPERTY()
    TArray<TObjectPtr<UMaterialInterface>> OriginalOverrideMaterials;

    int32 NumMaterialSlots = 0;
};

USTRUCT()
struct FHoloSavedGroomVisibility
{
    GENERATED_BODY()

    UPROPERTY()
    TWeakObjectPtr<UPrimitiveComponent> Comp;

    bool bWasVisible = true;
    bool bWasHiddenInGame = false;
};

/**
 * Coordinates proximity PBZ F-key prompts across all AHoloCharacterStand pedestals in the level,
 * listens for F-key (or Gamepad Face-Left) input, and executes a smooth 3-phase holographic
 * fade-out -> seamless pawn swap -> holographic materialize-in transition preserving camera & PlumPole control.
 */
UCLASS(Blueprintable)
class ZEROANIM_API AHoloCharacterSwitchManager : public AActor
{
    GENERATED_BODY()

public:
    AHoloCharacterSwitchManager();

    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;

    UFUNCTION(BlueprintCallable, Category = "Hologram Switch")
    bool TriggerSwitchToStand(AHoloCharacterStand* TargetStand);

    UFUNCTION(BlueprintPure, Category = "Hologram Switch")
    bool IsSwitching() const { return SwitchPhase != EHoloSwitchPhase::Idle; }

    UFUNCTION(BlueprintPure, Category = "Hologram Switch")
    EHoloSwitchPhase GetSwitchPhase() const { return SwitchPhase; }

    UFUNCTION(BlueprintPure, Category = "Hologram Switch")
    AHoloCharacterStand* GetHighlightedStand() const { return HighlightedStand.Get(); }

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hologram Switch|FX")
    TObjectPtr<UMaterialInterface> HologramMaterial;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hologram Switch|Timing", meta = (ClampMin = "0.05"))
    float OverlayEngulfDuration = 0.14f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hologram Switch|Timing", meta = (ClampMin = "0.10"))
    float DissolveOutDuration = 0.30f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hologram Switch|Timing", meta = (ClampMin = "0.10"))
    float MaterializeInDuration = 0.30f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hologram Switch|Timing", meta = (ClampMin = "0.05"))
    float OverlayFadeOutDuration = 0.18f;

private:
    void RefreshStandsList();
    void UpdateStandPromptsAndInput(float DeltaSeconds);
    void TickSwitchTransition(float DeltaSeconds);
    void ExecuteAtomicPawnSwap();
    void FinishSwitchTransition();

    void CollectVisibleMeshesAndGrooms(APawn* Pawn, TArray<FHoloSavedMeshMaterials>& OutMeshes, TArray<FHoloSavedGroomVisibility>& OutGrooms) const;
    void ApplyOverlayMaterial(const TArray<FHoloSavedMeshMaterials>& Meshes, UMaterialInterface* Mat) const;
    void ApplyReplacementMaterial(const TArray<FHoloSavedMeshMaterials>& Meshes, UMaterialInterface* Mat) const;
    void RestoreOriginalMaterials(const TArray<FHoloSavedMeshMaterials>& Meshes) const;
    void SetGroomsVisible(const TArray<FHoloSavedGroomVisibility>& Grooms, bool bShow) const;
    void ResetClothOnPawn(APawn* Pawn) const;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components", meta = (AllowPrivateAccess = "true"))
    TObjectPtr<UPointLightComponent> TransitionFlashLight;

    UPROPERTY(Transient)
    TArray<TWeakObjectPtr<AHoloCharacterStand>> Stands;

    TWeakObjectPtr<AHoloCharacterStand> HighlightedStand;
    TWeakObjectPtr<AHoloCharacterStand> ActiveSwitchStand;
    TWeakObjectPtr<APawn> TransitionPawn;

    UPROPERTY(Transient)
    TObjectPtr<UMaterialInstanceDynamic> BodyHoloMID;

    UPROPERTY(Transient)
    TObjectPtr<UMaterialInstanceDynamic> OverlayHoloMID;

    UPROPERTY(Transient)
    TArray<FHoloSavedMeshMaterials> CachedMeshes;

    UPROPERTY(Transient)
    TArray<FHoloSavedGroomVisibility> CachedGrooms;

    EHoloSwitchPhase SwitchPhase = EHoloSwitchPhase::Idle;
    float PhaseElapsed = 0.0f;
    FLinearColor TransitionColor = FLinearColor(0.02f, 0.52f, 1.0f, 1.0f);
    bool bGroomsHiddenDuringDissolve = false;
    bool bMoveInputLocked = false;
};
