// Alien Museum - signature moves (Cannonbolt rolls, XLR8 dashes, Four Arms flexes...) and their effects.
//
// Each move is a short timeline run from TickComponent. It steers the alien through AAlienCharacter's
// movement API, bends the body through UAlienAppearanceComponent's action transform, and spawns cheap
// effects: glowing basic shapes and rings from a small pool, the rolling ball, clones, vines, a bouncing
// physics ball. Moves also throw the case's loose habitat props, so the diorama reacts physically.
//
// Always-on extras from the data asset: flames on the head (Heatblast) and after-images when running
// fast (XLR8). The AI decides when to perform (AAlienAIController).

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Data/AlienDataAsset.h"
#include "AlienActionComponent.generated.h"

class AAlienCharacter;
class AAlienChamber;
class UAlienAppearanceComponent;
class UChamberHabitatComponent;
class UStaticMesh;
class UStaticMeshComponent;
class USceneComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class UPhysicalMaterial;

UCLASS(ClassGroup = (AlienMuseum))
class BEN10_API UAlienActionComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UAlienActionComponent();

	/** Reads the alien's moves and always-on effects. Call after the body has been built. */
	void Setup(const UAlienDataAsset* Data);

	UFUNCTION(BlueprintPure, Category = "Alien|Actions")
	bool HasActions() const { return Actions.Num() > 0; }

	/** Starts a move. False if the alien can't do it right now (held, no room, no model to clone...). */
	UFUNCTION(BlueprintCallable, Category = "Alien|Actions")
	bool StartAction(EAlienAction Action);

	/** A random one of the alien's moves, or its show-off move (the first in its list). */
	UFUNCTION(BlueprintCallable, Category = "Alien|Actions")
	bool StartRandomAction(bool bShowOff);

	/** Ends the current move at once and puts the body back to normal (e.g. the case is picked up). */
	UFUNCTION(BlueprintCallable, Category = "Alien|Actions")
	void StopAction();

	UFUNCTION(BlueprintPure, Category = "Alien|Actions")
	bool IsPerforming() const { return bPerforming; }

	UFUNCTION(BlueprintPure, Category = "Alien|Actions")
	EAlienAction GetCurrentAction() const { return Current; }

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

protected:
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	enum class EFx : uint8 { Sphere, Cone, Cylinder, Cube, Ring };

	/** One pooled effect: a glowing shape or ring that moves, grows and fades by itself. */
	struct FFxState
	{
		float Age = 0.f;
		float Life = -1.f;              // < 0: free
		FVector Velocity = FVector::ZeroVector;
		float Gravity = 0.f;             // cm/s2 downwards (negative = rises faster and faster)
		FVector Scale0 = FVector::OneVector;
		FVector Scale1 = FVector::OneVector;
		float Opacity = 0.9f;
		bool bRing = false;
		bool bPop = false;               // grows fast, holds, shrinks (crystals) instead of growing and fading
		bool bFromBase = false;          // grows out of Base along its up axis (spikes out of the ground)
		FVector Base = FVector::ZeroVector;
	};

	// ---- moves (one tick function each) ----
	void TickRoll(float Dt);
	void TickDash(float Dt);
	void TickFlare(float Dt);
	void TickFlex(float Dt);
	void TickCrystalBurst(float Dt);
	void TickPhase(float Dt);
	void TickScream(float Dt);
	void TickClone(float Dt);
	void TickSpit(float Dt);
	void TickPounce(float Dt);
	void TickVines(float Dt);
	void TickMelt(float Dt);
	void TickScurry(float Dt);
	void TickFly(float Dt);

	/** True on the first tick of a step (each move's steps set themselves up then). */
	bool Entering();
	void NextStep();
	void GoToStep(int32 NewStep);
	void RestartStep();

	// ---- movement helpers ----
	void AimRoll();
	void BounceRoll();
	bool MoveToFarPoint(float MinFraction);
	FVector GlassNormalAt(const FVector& WorldPoint) const;
	float DistanceToGlass(const FVector& From, const FVector& Direction, float Margin) const;
	bool GetViewer(FVector& OutLocation) const;
	FVector TowardViewer() const;
	void FaceViewer();
	void SetGhost(bool bGhost);

	/** Rolling: the capsule becomes a ball of this (world) radius, feet staying on the ground. */
	void ShrinkCapsule(float WorldRadius);
	/** Back to the full capsule, stepping away from the glass first if it would not fit there. */
	void RestoreCapsule();

	// ---- body measurements (world cm) ----
	float WorldHeight() const;
	float CapsuleRadius() const;
	FVector Feet() const;
	FVector Forward() const;
	AAlienChamber* GetChamber() const;
	UChamberHabitatComponent* GetHabitat() const;
	float WaterSurfaceZ() const; // world Z of a pool's surface, or the feet when there is no water

	/** World distance from a point to the nearest glass of the case. */
	float RoomAround(const FVector& WorldPoint) const;
	/** An effect's diameter, limited so it stays inside the glass around At. */
	float FitInCase(const FVector& At, float Diameter) const;

	// ---- effects ----
	int32 SpawnFx(EFx Shape, const FVector& Location, const FQuat& Rotation, const FVector& Size0, const FVector& Size1,
		const FLinearColor& Color, float Life, float Opacity = 0.9f, float Intensity = 2.f);
	void UpdateFx(float Dt);
	void ClearFx();
	void Burst(const FVector& At, int32 Count, float Size, float Speed, const FLinearColor& Color, float Life, float Gravity, float UpBias = 0.5f);
	void DustPuff(const FVector& At, float Size, int32 Count);
	void GroundRing(const FVector& At, float From, float To, const FLinearColor& Color, float Life, float Opacity = 0.75f);
	FLinearColor DustColor() const;
	void Blast(const FVector& Center, float Radius, float Speed, const FVector& Direction = FVector::ZeroVector, float UpBias = 0.35f);

	void BuildFlames();
	void DestroyFlames();
	void UpdateFlames(float Dt);

	void SpawnAfterImage(float Opacity, float Life);
	void UpdateAfterImages(float Dt);

	void EnsureBall();
	void EnsureClones();
	void EnsureVines();
	void UpdateVines(float Grow);
	void EnsurePuddle();
	void LaunchPhysicsBall(const FVector& From, const FVector& Velocity, float Diameter, const FLinearColor& Color, float Life, float PopRadius, float PopSpeed);
	void UpdatePhysicsBall(float Dt);
	void PopPhysicsBall();
	void FinishEating();

	UStaticMeshComponent* MakeMesh(const TCHAR* Name, UStaticMesh* Mesh, UMaterialInterface* Material, USceneComponent* Parent, bool bAbsolute);
	UMaterialInstanceDynamic* MakeMaterial(UMaterialInterface* Base, const FLinearColor& Color, float Intensity = -1.f, float Opacity = -1.f);
	UMaterialInstanceDynamic* MakeLitMaterial(const FLinearColor& Color, float Roughness, float SelfIllum);

	// ---- data ----
	UPROPERTY(Transient)
	TObjectPtr<AAlienCharacter> Alien;

	UPROPERTY(Transient)
	TObjectPtr<UAlienAppearanceComponent> Body;

	TArray<EAlienAction> Actions;
	TSoftObjectPtr<UStaticMesh> PoseMesh;
	TSoftObjectPtr<UStaticMesh> BallForm;
	FLinearColor ActionColor = FLinearColor(0.3f, 1.f, 0.4f);
	float Height = 40.f;        // data height (cm at scale 1)
	float WalkSpeed = 20.f;     // data walk speed (cm/s at scale 1)
	bool bSpeedTrail = false;

	// ---- current move ----
	EAlienAction Current = EAlienAction::Roll;
	bool bPerforming = false;
	int32 Step = 0;
	int32 EnteredStep = -1;
	float StepTime = 0.f;
	float ActionTime = 0.f;
	int32 Counter = 0;
	float Timer = 0.f;
	float EventTime = 0.f;
	FVector MoveDir = FVector::ForwardVector;
	FVector TargetPoint = FVector::ZeroVector;
	FVector SavedPoint = FVector::ZeroVector;
	float Amount = 0.f;
	float LastYaw = 0.f;
	bool bGhostMode = false;
	bool bTempFlames = false;
	FRandomStream Rng;

	// ---- pooled effects ----
	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> FxComponents;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInstanceDynamic>> FxGlowMaterials;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInstanceDynamic>> FxRingMaterials;

	TArray<FFxState> FxStates;

	// ---- head flames ----
	UPROPERTY(Transient)
	TObjectPtr<USceneComponent> FlameRoot;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> Flames;

	TArray<FTransform> FlameBase;
	TArray<float> FlamePhase;
	float FlameTime = 0.f;
	float FlameBoost = 1.f;
	float EmberTimer = 0.f;

	// ---- after-images ----
	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> AfterImages;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInstanceDynamic>> AfterImageMaterials;

	TArray<float> AfterImageAge;
	TArray<float> AfterImageLife;
	TArray<float> AfterImageOpacity;
	int32 NextAfterImage = 0;
	float TrailTimer = 0.f;

	// ---- move props ----
	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> Ball;

	FQuat BallSpin = FQuat::Identity;
	float BallRadius = 10.f;      // world cm
	float BallGrow = 0.f;         // 0..1 while curling up / uncurling
	FVector BallMeshCenter = FVector::ZeroVector; // bounds centre and radius of the ball mesh (its own units)
	float BallMeshRadius = 50.f;

	bool bCapsuleShrunk = false;
	float SavedCapsuleRadius = 0.f;
	float SavedCapsuleHalfHeight = 0.f;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> Clones;

	TArray<FVector> CloneOffsets;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> Vines;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> VineBuds;

	TArray<FVector> VineTargets;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> Puddle;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> PhysicsBall;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> PhysicsBallMaterial;

	UPROPERTY(Transient)
	TObjectPtr<UPhysicalMaterial> BouncyMaterial;

	bool bPhysicsBallActive = false;
	float PhysicsBallLife = 0.f;
	float PhysicsBallPopRadius = 20.f;
	float PhysicsBallPopSpeed = 80.f;
	float PhysicsBallTrail = 0.f;
	FLinearColor PhysicsBallColor = FLinearColor::Green;

	/** A loose prop being eaten (pulled into the mouth). */
	TWeakObjectPtr<UStaticMeshComponent> EatenProp;
	FVector EatenStart = FVector::ZeroVector;
	FVector EatenScale = FVector::OneVector;
};
