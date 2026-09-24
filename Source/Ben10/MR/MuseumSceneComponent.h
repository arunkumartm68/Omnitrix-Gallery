// Alien Museum - everything about the real room: passthrough, scene permission, MRUK room
// loading, surface raycasts for placement and real-world occluders.
//
// On a Quest it uses MR Utility Kit (floor, walls, tables, couches...). In the editor or on PC
// without a headset it falls back to ordinary collision traces against the level (plus an
// infinite floor at FallbackFloorZ), so the whole museum can be tested with Play-In-Editor.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Core/MuseumTypes.h"
#include "MuseumSceneComponent.generated.h"

class AMRUKAnchor;
class AMRUKRoom;
class UMaterialInterface;
class UProceduralMeshComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnMuseumSceneReady, bool, bUsingDeviceScene);

UENUM(BlueprintType)
enum class EMuseumSceneState : uint8
{
	NotStarted,
	RequestingPermission,
	LoadingScene,
	CapturingScene,
	Ready
};

UCLASS(ClassGroup = (AlienMuseum), meta = (BlueprintSpawnableComponent))
class BEN10_API UMuseumSceneComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UMuseumSceneComponent();

	/** Starts passthrough and room loading. OnSceneReady fires when placement can begin. */
	UFUNCTION(BlueprintCallable, Category = "Museum|Scene")
	void StartScene();

	/** Casts a placement ray against real surfaces (or the editor fallback). */
	UFUNCTION(BlueprintCallable, Category = "Museum|Scene")
	FMuseumSurfaceHit RaycastSurface(const FVector& Origin, const FVector& Direction, float MaxDistance = 600.f) const;

	/** Finds the surface directly below a point (used to settle a chamber that was put down). */
	UFUNCTION(BlueprintCallable, Category = "Museum|Scene")
	FMuseumSurfaceHit FindSurfaceBelow(const FVector& Location, float MaxDrop = 300.f) const;

	/** World Z of the floor (MRUK floor anchor, or FallbackFloorZ). */
	UFUNCTION(BlueprintPure, Category = "Museum|Scene")
	float GetFloorZ() const;

	UFUNCTION(BlueprintPure, Category = "Museum|Scene")
	bool IsUsingDeviceScene() const { return bUsingDeviceScene; }

	UFUNCTION(BlueprintPure, Category = "Museum|Scene")
	EMuseumSceneState GetSceneState() const { return SceneState; }

	/** Shows the scanned room as a hologram instead of invisible occluders (debugging). */
	UFUNCTION(BlueprintCallable, Category = "Museum|Scene")
	void SetShowRoomDebug(bool bShow);

	UPROPERTY(BlueprintAssignable, Category = "Museum|Scene")
	FOnMuseumSceneReady OnSceneReady;

	// ---------- Settings ----------

	/** Start the Meta passthrough layer (the real room behind the virtual museum). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Museum|Passthrough")
	bool bEnablePassthrough = true;

	/** Ask the user to scan the room if no scene model exists yet. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Museum|Scene")
	bool bLaunchSceneCaptureIfMissing = true;

	/** Build invisible occluders from the room model so real furniture hides aliens behind it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Museum|Occlusion")
	bool bEnableSceneOccluders = true;

	/** MRUK labels that become occluders. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Museum|Occlusion")
	TArray<FString> OccluderLabels;

	/** Pushes occluder surfaces towards the viewer a little (cm) to avoid flicker against aliens. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Museum|Occlusion")
	float OccluderInflate = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Museum|Occlusion")
	TObjectPtr<UMaterialInterface> OccluderMaterialOverride;

	/** Surfaces a chamber may stand on. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Museum|Placement")
	TArray<FString> PlaceableLabels;

	/** Editor / no-headset fallback floor height. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Museum|Placement")
	float FallbackFloorZ = 0.f;

protected:
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	void StartPassthrough();
	void RequestPermissionThenLoad();
	void LoadSceneFromDevice();
	void FinishScene(bool bDeviceScene);

	UFUNCTION()
	void HandleSceneLoaded(bool bSuccess);

	UFUNCTION()
	void HandleCaptureComplete(bool bSuccess);

	UFUNCTION()
	void HandlePermissionsResult(const TArray<FString>& Permissions, const TArray<bool>& GrantResults);

	void BuildOccluders();
	void ClearOccluders();
	EMuseumSurfaceType ClassifyLabels(const TArray<FString>& Labels) const;
	class UMRUKSubsystem* GetMRUK() const;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UProceduralMeshComponent>> Occluders;

	EMuseumSceneState SceneState = EMuseumSceneState::NotStarted;
	bool bUsingDeviceScene = false;
	bool bCaptureAttempted = false;
	bool bShowRoomDebug = false;
};
