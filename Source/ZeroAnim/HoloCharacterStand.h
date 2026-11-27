#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "PlumPoleInteractPromptWidget.h"
#include "HoloCharacterStand.generated.h"

class ACharacter;
class UAnimInstance;
class UAnimSequence;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UPointLightComponent;
class USceneComponent;
class USkeletalMesh;
class USkeletalMeshComponent;
class UStaticMeshComponent;
class UWidgetComponent;
class UHoloSwitchPromptWidget;

USTRUCT(BlueprintType)
struct ZEROANIM_API FHoloMeshPart
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hologram")
    TObjectPtr<USkeletalMesh> Mesh;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hologram")
    TObjectPtr<UAnimSequence> IdleAnim;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hologram")
    TSubclassOf<UAnimInstance> AnimClass;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hologram")
    FVector RelativeOffset = FVector::ZeroVector;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hologram")
    FRotator RelativeRotation = FRotator(0.0f, -90.0f, 0.0f);

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hologram")
    bool bCopyPoseFromFirstPart = false;

    /** Invisible pose driver mesh (e.g. Manny playing MM_Idle for IK-retargeted ABP_CopyPose characters). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hologram")
    bool bHiddenPoseDriver = false;

    /** Attach directly to the hidden pose driver mesh (used for ABP_*_CopyPose Body meshes). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hologram")
    bool bAttachToPoseDriver = false;
};

/**
 * Ground holographic character showcase pedestal.
 * Renders a full multi-part skeletal mesh character using a translucent scanline Fresnel hologram shader,
 * projects a glowing ground pad ring, and displays a PBZ-styled F-key prompt when the player approaches.
 */
UCLASS(Blueprintable)
class ZEROANIM_API AHoloCharacterStand : public AActor
{
    GENERATED_BODY()

public:
    AHoloCharacterStand();

    virtual void OnConstruction(const FTransform& Transform) override;
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;

    UFUNCTION(BlueprintCallable, Category = "Hologram Switch")
    void SetPromptState(EPlumPoleInteractUIState NewState, float DeltaSeconds, bool bUsingGamepad);

    UFUNCTION(BlueprintCallable, Category = "Hologram Switch")
    void SetIsActiveCharacterStand(bool bInActive);

    UFUNCTION(BlueprintCallable, Category = "Hologram Switch")
    void TriggerSwitchPulse();

    UFUNCTION(BlueprintPure, Category = "Hologram Switch")
    bool IsActiveCharacterStand() const { return bIsActiveCharacter; }

    UFUNCTION(BlueprintPure, Category = "Hologram Switch")
    FVector GetPromptAnchorWorldLocation() const;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hologram Switch")
    FName CharacterId = NAME_None;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hologram Switch")
    FText CharacterDisplayName;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hologram Switch")
    TSubclassOf<ACharacter> TargetCharacterClass;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hologram Switch|Visuals")
    TArray<FHoloMeshPart> PreviewParts;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hologram Switch|Visuals")
    TObjectPtr<UMaterialInterface> HologramMaterial;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hologram Switch|Visuals")
    TObjectPtr<UMaterialInterface> PadMaterial;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hologram Switch|Visuals")
    TObjectPtr<UMaterialInterface> LightConeMaterial;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hologram Switch|Visuals")
    TObjectPtr<UMaterialInterface> EmitterOrbMaterial;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hologram Switch|Visuals")
    FLinearColor HologramColor = FLinearColor(0.02f, 0.52f, 1.0f, 1.0f);

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hologram Switch|Visuals")
    FLinearColor ActiveStandbyColor = FLinearColor(0.02f, 0.52f, 1.0f, 1.0f);

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hologram Switch|Proximity", meta = (ClampMin = "50"))
    float VirtualAwarenessRadius = 520.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hologram Switch|Proximity", meta = (ClampMin = "50"))
    float InteractRadius = 175.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hologram Switch|Proximity")
    float PromptHeightZ = 228.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hologram Switch|Visuals")
    float HologramHoverHeight = 46.0f;

    /** Continuous turntable yaw rotation speed in degrees per second (matching reference RotateAroundYAxis). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hologram Switch|Visuals")
    float HologramRotationSpeed = 24.0f;

    /** Directional pseudo-random vertex glitch intensity (0 = off, 1 = reference default). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hologram Switch|Visuals", meta = (ClampMin = "0"))
    float GlitchStrength = 1.0f;

private:
    void RebuildPreviewMeshes();
    void ApplyHologramMaterials();
    UHoloSwitchPromptWidget* EnsurePromptWidget();

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components", meta = (AllowPrivateAccess = "true"))
    TObjectPtr<USceneComponent> SceneRoot;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components", meta = (AllowPrivateAccess = "true"))
    TObjectPtr<USceneComponent> PreviewPivot;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components", meta = (AllowPrivateAccess = "true"))
    TObjectPtr<UStaticMeshComponent> GlowPadMesh;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components", meta = (AllowPrivateAccess = "true"))
    TObjectPtr<UStaticMeshComponent> LightConeMesh;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components", meta = (AllowPrivateAccess = "true"))
    TObjectPtr<UStaticMeshComponent> EmitterInnerSphere;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components", meta = (AllowPrivateAccess = "true"))
    TObjectPtr<UStaticMeshComponent> EmitterOuterSphere;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components", meta = (AllowPrivateAccess = "true"))
    TObjectPtr<UPointLightComponent> HoloPointLight;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components", meta = (AllowPrivateAccess = "true"))
    TObjectPtr<UWidgetComponent> PromptWidgetComponent;

    UPROPERTY(Transient)
    TArray<TObjectPtr<USkeletalMeshComponent>> SpawnedPreviewMeshes;

    UPROPERTY(Transient)
    TObjectPtr<UMaterialInstanceDynamic> HoloBodyMID;

    UPROPERTY(Transient)
    TObjectPtr<UMaterialInstanceDynamic> HoloPadMID;

    UPROPERTY(Transient)
    TObjectPtr<UMaterialInstanceDynamic> LightConeMID;

    UPROPERTY(Transient)
    TObjectPtr<UMaterialInstanceDynamic> EmitterInnerMID;

    UPROPERTY(Transient)
    TObjectPtr<UMaterialInstanceDynamic> EmitterOuterMID;

    bool bIsActiveCharacter = false;
    float ElapsedTime = 0.0f;
    float SwitchPulseAlpha = 0.0f;
    EPlumPoleInteractUIState CurrentPromptState = EPlumPoleInteractUIState::None;
};
