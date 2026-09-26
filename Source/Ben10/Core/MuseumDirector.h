// Alien Museum - one per level. Runs the start-up flow and owns the museum state.
//
//   BeginPlay -> Scene: passthrough + room -> Persistence: restore saved chambers
//             -> (first run) starter chamber in front of the player -> Alien Collection panel
//
// Place BP_MuseumDirector in the level and assign the chamber class, panel class and collection.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MuseumDirector.generated.h"

class AAlienChamber;
class AAlienCollectionPanel;
class ADirectionalLight;
class UAlienCollectionAsset;
class UAlienDataAsset;
class UMuseumSceneComponent;
class UMuseumPersistenceComponent;

UCLASS()
class BEN10_API AMuseumDirector : public AActor
{
	GENERATED_BODY()

public:
	AMuseumDirector();

	/** The director of the world WorldContext belongs to (nullptr if none is placed). */
	static AMuseumDirector* Get(const UObject* WorldContext);

	/** Spawns a chamber whose bottom is at FloorLocation (a surface, or mid-air when chambers float). Optionally puts an alien inside. */
	UFUNCTION(BlueprintCallable, Category = "Museum")
	AAlienChamber* SpawnChamberAt(const FVector& FloorLocation, float Yaw, UAlienDataAsset* Alien = nullptr);

	UFUNCTION(BlueprintCallable, Category = "Museum")
	void PlaceAlienInChamber(AAlienChamber* Chamber, UAlienDataAsset* Alien);

	UFUNCTION(BlueprintCallable, Category = "Museum")
	void RemoveChamber(AAlienChamber* Chamber);

	/** Removes every chamber and forgets the saved museum. */
	UFUNCTION(BlueprintCallable, Category = "Museum")
	void ClearMuseum();

	/**
	 * After a chamber was put down: keep it where it was released (floating) or drop it onto the
	 * surface below, then save/anchor it. Does nothing to a chamber that was only clicked.
	 */
	UFUNCTION(BlueprintCallable, Category = "Museum")
	void SettleChamber(AAlienChamber* Chamber);

	UFUNCTION(BlueprintCallable, Category = "Museum")
	void ToggleCollectionPanel();

	UFUNCTION(BlueprintCallable, Category = "Museum")
	void ShowCollectionPanel();

	UFUNCTION(BlueprintCallable, Category = "Museum")
	void SetStatusText(const FString& Text);

	/** Shows the scanned room as a hologram (debug) instead of invisible occluders. */
	UFUNCTION(BlueprintCallable, Category = "Museum")
	void SetShowRoomDebug(bool bShow);

	UFUNCTION(BlueprintPure, Category = "Museum")
	bool CanAddChamber() const;

	/** True if a new chamber of this radius with its bottom at FloorLocation would not overlap another chamber. */
	UFUNCTION(BlueprintPure, Category = "Museum")
	bool IsPlacementFree(const FVector& FloorLocation, float Radius, const AAlienChamber* Ignore) const;

	/** True if a case of OuterSize (depth, width, height; world cm) at FloorLocation turned by Yaw would not overlap another chamber. */
	UFUNCTION(BlueprintPure, Category = "Museum")
	bool IsFootprintFree(const FVector& FloorLocation, float Yaw, const FVector& OuterSize, const AAlienChamber* Ignore) const;

	/** Default object of ChamberClass: size and shape of the chambers that will be spawned. */
	const AAlienChamber* GetChamberTemplate() const;

	UFUNCTION(BlueprintPure, Category = "Museum")
	TArray<AAlienChamber*> GetChambers() const;

	UFUNCTION(BlueprintPure, Category = "Museum")
	bool IsMuseumReady() const { return bMuseumReady; }

	UFUNCTION(BlueprintPure, Category = "Museum")
	UMuseumSceneComponent* GetScene() const { return Scene; }

	UFUNCTION(BlueprintPure, Category = "Museum")
	UMuseumPersistenceComponent* GetPersistence() const { return Persistence; }

	UFUNCTION(BlueprintPure, Category = "Museum")
	AAlienCollectionPanel* GetPanel() const { return Panel; }

	// ---------- Designer settings ----------

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Museum")
	TSubclassOf<AAlienChamber> ChamberClass;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Museum")
	TSubclassOf<AAlienCollectionPanel> PanelClass;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Museum")
	TObjectPtr<UAlienCollectionAsset> Collection;

	/** On the very first run, put one chamber with the first alien in front of the player. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Museum")
	bool bSpawnStarterChamber = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Museum", meta = (Units = "cm"))
	float StarterChamberDistance = 110.f;

	/** Upper limit that keeps Quest performance predictable (each chamber + alien ~20 draw calls). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Museum", meta = (ClampMin = 1, ClampMax = 20))
	int32 MaxChambers = 8;

	/** Level actors with this tag are editor-only test furniture; they are removed when a headset is used. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Museum")
	FName EditorRoomTag = TEXT("MuseumEditorRoom");

	/** Wait this long after the room is ready before placing things relative to the head. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Museum")
	float StartupDelay = 0.75f;

	/** No gravity: chambers stay where they are released, even in mid-air. Off = they drop onto the surface below. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Museum")
	bool bChambersFloat = true;

	/** A floating chamber released this close above a real surface is set down onto it. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Museum", meta = (Units = "cm", ClampMin = 0))
	float SurfaceSnapDistance = 5.f;

	/** Restored chambers wait this long for their anchor pose to settle before the alien appears. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Museum", meta = (Units = "s", ClampMin = 0))
	float RestoredAlienDelay = 1.f;

	/**
	 * Turns the level's (movable) directional light so it shines from over the viewer's shoulder.
	 * Whatever the player looks at is then lit from the front - imported models have no glow of
	 * their own, and aliens turn to face the player.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Museum|Lighting")
	bool bKeyLightFollowsViewer = true;

	/** How steeply the key light shines down (degrees). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Museum|Lighting", meta = (ClampMin = 5, ClampMax = 89))
	float KeyLightElevation = 45.f;

	/** Sideways offset from the view direction (degrees), so faces get some shape. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Museum|Lighting", meta = (ClampMin = -90, ClampMax = 90))
	float KeyLightSideAngle = 25.f;

	virtual void Tick(float DeltaSeconds) override;

protected:
	virtual void BeginPlay() override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Museum")
	TObjectPtr<UMuseumSceneComponent> Scene;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Museum")
	TObjectPtr<UMuseumPersistenceComponent> Persistence;

private:
	UFUNCTION()
	void HandleSceneReady(bool bDeviceScene);

	UFUNCTION()
	void HandleChamberGrabbed(AAlienChamber* Chamber);

	UFUNCTION()
	void HandleChamberResized(AAlienChamber* Chamber);

	UFUNCTION()
	void HandleOccupantChanged(AAlienChamber* Chamber);

	void RestoreMuseum();
	void RegisterChamber(AAlienChamber* Chamber);
	void SpawnStarterChamber();
	void PushOutOfOtherChambers(AAlienChamber* Chamber) const;
	bool GetViewer(FVector& OutHead, FRotator& OutRotation) const;

	/** Z of a real surface at (or slightly into / just below) the chamber's bottom, if there is one. */
	bool FindSurfaceUnderChamber(const AAlienChamber* Chamber, float MaxGap, float& OutZ) const;

	/** Shows the anti-gravity glow when nothing real is directly under the chamber. */
	void UpdateFloatingState(AAlienChamber* Chamber) const;

	void UpdateKeyLight(float DeltaSeconds);

	TWeakObjectPtr<ADirectionalLight> KeyLight;

	UPROPERTY(Transient)
	TArray<TObjectPtr<AAlienChamber>> Chambers;

	UPROPERTY(Transient)
	TObjectPtr<AAlienCollectionPanel> Panel;

	FTimerHandle StartupTimer;
	bool bMuseumReady = false;
};
