// Alien Museum - builds an alien's home-world diorama inside its display case.
//
//   ground layer (sand, lava, water pool, ...)
//   fixed props      one instanced mesh per shape/colour (one draw call each); some block the alien, plants sway
//   loose props      real physics: they fall, roll, get pushed by the alien and bounce off the glass
//   ambient effect   embers, bubbles, mist, sparkles, spores or tech pulses (one instanced mesh)
//
// Everything lives in the chamber's local space (the chamber's uniform scale applies). Loose props
// simulate in world space, so the chamber freezes them while it is carried and moves them along
// when a spatial anchor corrects its pose.

#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "ChamberHabitatComponent.generated.h"

class UChamberHabitatAsset;
class UStaticMesh;
class UStaticMeshComponent;
class UInstancedStaticMeshComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;
struct FHabitatProp;

UCLASS(ClassGroup = (AlienMuseum))
class BEN10_API UChamberHabitatComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	UChamberHabitatComponent();

	/**
	 * Builds the diorama. Half = inside half size (X depth, Y width; a round pod uses its radius twice) and
	 * FloorZ = floor height, both in the chamber's local space. The same seed gives the same layout.
	 */
	void Build(UChamberHabitatAsset* InHabitat, const FVector2f& InHalf, float InFloorZ, float InGlassHeight, int32 InSeed, bool bInRound = false);

	/** Same diorama, fitted to a new case size (after a resize). */
	void Rebuild(const FVector2f& InHalf, float InFloorZ, float InGlassHeight);

	void Clear();

	/** Keeps new props away from this spot (the alien), chamber space. Set before Build / Rebuild. */
	void SetKeepClear(const FVector2f& Center, float Radius) { KeepClearCenter = Center; KeepClearRadius = Radius; }

	bool HasHabitat() const { return Habitat != nullptr; }

	const UChamberHabitatAsset* GetAsset() const { return Habitat; }

	/** Loose props stop simulating and ride along while the case is carried. */
	void SetFrozen(bool bInFrozen);

	/** Moves the simulating props when the case moved without a hand (spatial-anchor corrections). */
	void CarryLooseProps(const FTransform& From, const FTransform& To);

	/** The nearest loose prop that is currently in play (for Upchuck to eat), or nullptr. */
	UStaticMeshComponent* FindNearestLooseProp(const FVector& WorldPoint) const;

	/** Takes a loose prop out of play (eaten); it comes back after RespawnSeconds. */
	void ConsumeProp(UStaticMeshComponent* Prop, float RespawnSeconds = 8.f);

	/**
	 * Throws the loose props within Radius of WorldCenter (a stomp, a bounce, a sonic blast): each gets
	 * up to Speed cm/s away from the centre, or along Direction (only props in front) when it is set.
	 * Returns how many props were thrown.
	 */
	int32 Blast(const FVector& WorldCenter, float Radius, float Speed, const FVector& Direction = FVector::ZeroVector, float UpBias = 0.35f);

	/** Local Z of the ground surface (the water surface for pools). */
	float GetGroundTopZ() const { return FloorZ + GroundDepth; }

	/** Local Z the alien stands on (the pool bed for water). */
	float GetWalkZ() const { return IsWater() ? FloorZ + 0.6f : FloorZ + GroundDepth; }

	/** True for pools: effects landing here splash instead of raising dust. */
	bool IsWater() const;

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	struct FLooseProp
	{
		TWeakObjectPtr<UStaticMeshComponent> Comp;
		FTransform Home;            // local to this component
		float RespawnTimer = -1.f;  // > 0 while eaten
	};

	struct FSwayInstance
	{
		TWeakObjectPtr<UInstancedStaticMeshComponent> Instancer;
		int32 Index = 0;
		FTransform Base;            // local, pivot at the bottom of the prop
		float Height = 10.f;
		float Phase = 0.f;
	};

	struct FParticle
	{
		FVector Location = FVector::ZeroVector;
		FVector Velocity = FVector::ZeroVector;
		FVector Size = FVector::OneVector;   // cm
		float Age = 0.f;
		float Life = 1.f;
		float Phase = 0.f;
	};

	void BuildGround();
	void BuildProps();
	void BuildAmbient();
	void UpdateAmbient(float DeltaTime);
	void UpdateSway();
	void KeepLoosePropsInside();
	void ResetLooseProp(FLooseProp& Prop);
	void RespawnParticle(FParticle& Particle, bool bFirstTime);
	FTransform ParticleTransform(const FParticle& Particle) const;
	bool PickSpot(const FHabitatProp& Prop, FRandomStream& Rng, float Radius, FVector2f& OutSpot);

	/** True if a circle of this radius at Spot is inside the floor (box or round pod). */
	bool IsInsideFloor(const FVector2f& Spot, float Radius) const;

	UStaticMesh* ShapeMesh(uint8 Shape) const;
	UMaterialInterface* LookMaterial(uint8 Look, const FLinearColor& Color, float Roughness = 0.85f);
	UStaticMeshComponent* MakeMeshComponent(const TCHAR* Name, UStaticMesh* Mesh, UMaterialInterface* Material, const FTransform& Local);
	UInstancedStaticMeshComponent* GetInstancer(const FHabitatProp& Prop);

	UPROPERTY(Transient)
	TObjectPtr<UChamberHabitatAsset> Habitat;

	/** Everything created for the current diorama (destroyed by Clear). */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UPrimitiveComponent>> Created;

	UPROPERTY(Transient)
	TMap<FString, TObjectPtr<UInstancedStaticMeshComponent>> Instancers;

	UPROPERTY(Transient)
	TMap<FString, TObjectPtr<UMaterialInstanceDynamic>> Materials;

	UPROPERTY(Transient)
	TObjectPtr<UInstancedStaticMeshComponent> AmbientInstances;

	TArray<FLooseProp> LooseProps;
	TArray<FSwayInstance> SwayInstances;
	TArray<FParticle> Particles;
	TArray<FVector> Occupied; // x, y, radius of placed props

	FRandomStream AmbientRng;
	FVector2f Half = FVector2f(30.f, 30.f);
	float FloorZ = 10.f;
	float GlassHeight = 100.f;
	float GroundDepth = 0.f;
	int32 Seed = 0;
	bool bRound = false;
	FVector2f KeepClearCenter = FVector2f::ZeroVector;
	float KeepClearRadius = 0.f;
	float Time = 0.f;
	float KeepInsideTimer = 0.f;
	bool bFrozen = false;
};
