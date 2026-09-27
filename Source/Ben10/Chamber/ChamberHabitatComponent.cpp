// Alien Museum - builds an alien's home-world diorama inside its display case.

#include "Chamber/ChamberHabitatComponent.h"
#include "Data/ChamberHabitatAsset.h"
#include "Core/MuseumAssets.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInstanceDynamic.h"

namespace
{
	/** Light props (g/cm3), so the alien can push them around. */
	constexpr float PropDensity = 0.6f;

	float ShapeVolume(EAlienPartShape Shape, const FVector& Size)
	{
		const float RX = Size.X * 0.5f;
		const float RY = Size.Y * 0.5f;
		switch (Shape)
		{
		case EAlienPartShape::Cube: return Size.X * Size.Y * Size.Z;
		case EAlienPartShape::Cylinder: return PI * RX * RY * Size.Z;
		case EAlienPartShape::Cone: return PI * RX * RY * Size.Z / 3.f;
		case EAlienPartShape::Sphere:
		default: return 4.f / 3.f * PI * RX * RY * Size.Z * 0.5f;
		}
	}

	FString ColorKey(const FLinearColor& C)
	{
		return FString::Printf(TEXT("%.2f_%.2f_%.2f"), C.R, C.G, C.B);
	}

	/**
	 * Loose props: real physics against the case, the ground, the rocks and each other; never the
	 * pointer ray. The alien overlaps them and its movement component's repulsion shoves them away.
	 */
	void MakeLoose(UStaticMeshComponent* Component, bool bUnderwater)
	{
		Component->SetCollisionProfileName(UCollisionProfile::PhysicsActor_ProfileName);
		Component->SetCollisionResponseToChannel(ECC_Visibility, ECR_Ignore);
		Component->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
		Component->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
		Component->SetGenerateOverlapEvents(true);
		Component->SetUseCCD(true); // small and fast: don't tunnel through the glass
		Component->CanCharacterStepUpOn = ECB_No; // the alien pushes pebbles instead of climbing them
		// Water drag: things sink and drift slowly in a pool.
		Component->SetLinearDamping(bUnderwater ? 3.f : 0.35f);
		Component->SetAngularDamping(bUnderwater ? 2.5f : 0.8f);
	}

	/** Solid ground and fixed props: the alien stands on / walks around them, loose props land on them. */
	void MakeSolid(UPrimitiveComponent* Component)
	{
		Component->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		Component->SetCollisionObjectType(ECC_WorldDynamic);
		Component->SetCollisionResponseToAllChannels(ECR_Ignore);
		Component->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
		Component->SetCollisionResponseToChannel(ECC_PhysicsBody, ECR_Block);
	}
}

UChamberHabitatComponent::UChamberHabitatComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
}

bool UChamberHabitatComponent::IsWater() const
{
	return Habitat && Habitat->Ground == EHabitatGround::Water;
}

// ---------------------------------------------------------------------------------------------
// Building
// ---------------------------------------------------------------------------------------------

void UChamberHabitatComponent::Build(UChamberHabitatAsset* InHabitat, const FVector2f& InHalf, float InFloorZ, float InGlassHeight, int32 InSeed, bool bInRound)
{
	Clear();
	Habitat = InHabitat;
	Half = InHalf;
	FloorZ = InFloorZ;
	GlassHeight = InGlassHeight;
	Seed = InSeed;
	bRound = bInRound;
	if (!Habitat)
	{
		return;
	}
	GroundDepth = Habitat->Ground == EHabitatGround::None ? 0.f : FMath::Min(Habitat->GroundDepth, GlassHeight * 0.6f);
	BuildGround();
	BuildProps();
	BuildAmbient();
	SetComponentTickEnabled(true);
}

void UChamberHabitatComponent::Rebuild(const FVector2f& InHalf, float InFloorZ, float InGlassHeight)
{
	if (Habitat)
	{
		const bool bWasFrozen = bFrozen;
		Build(Habitat, InHalf, InFloorZ, InGlassHeight, Seed, bRound);
		SetFrozen(bWasFrozen);
	}
}

void UChamberHabitatComponent::Clear()
{
	for (UPrimitiveComponent* Component : Created)
	{
		if (Component)
		{
			Component->DestroyComponent();
		}
	}
	Created.Reset();
	Instancers.Reset();
	Materials.Reset();
	AmbientInstances = nullptr;
	LooseProps.Reset();
	SwayInstances.Reset();
	Particles.Reset();
	Occupied.Reset();
	Habitat = nullptr;
	GroundDepth = 0.f;
	bFrozen = false;
	SetComponentTickEnabled(false);
}

UStaticMesh* UChamberHabitatComponent::ShapeMesh(uint8 Shape) const
{
	switch (static_cast<EAlienPartShape>(Shape))
	{
	case EAlienPartShape::Cylinder: return MuseumAssets::CylinderMesh();
	case EAlienPartShape::Cone: return MuseumAssets::ConeMesh();
	case EAlienPartShape::Cube: return MuseumAssets::CubeMesh();
	case EAlienPartShape::Sphere:
	default: return MuseumAssets::SphereMesh();
	}
}

UMaterialInterface* UChamberHabitatComponent::LookMaterial(uint8 Look, const FLinearColor& Color, float Roughness)
{
	const FString Key = FString::Printf(TEXT("%d_%s_%.2f"), Look, *ColorKey(Color), Roughness);
	if (TObjectPtr<UMaterialInstanceDynamic>* Found = Materials.Find(Key))
	{
		return *Found;
	}

	UMaterialInstanceDynamic* MID = nullptr;
	switch (static_cast<EHabitatPropLook>(Look))
	{
	case EHabitatPropLook::Glow:
		if (UMaterialInterface* Base = MuseumAssets::EmissiveMaterial())
		{
			MID = UMaterialInstanceDynamic::Create(Base, this);
			MID->SetScalarParameterValue(MuseumAssets::Params::Intensity, 1.6f);
		}
		break;
	case EHabitatPropLook::Crystal:
		if (UMaterialInterface* Base = MuseumAssets::FXGlowMaterial())
		{
			MID = UMaterialInstanceDynamic::Create(Base, this);
			MID->SetScalarParameterValue(MuseumAssets::Params::Intensity, 1.4f);
			MID->SetScalarParameterValue(MuseumAssets::Params::Opacity, 0.8f);
			MID->SetScalarParameterValue(MuseumAssets::Params::Softness, 0.7f);
		}
		break;
	case EHabitatPropLook::Lit:
	default:
		if (UMaterialInterface* Base = MuseumAssets::EnvLitMaterial())
		{
			MID = UMaterialInstanceDynamic::Create(Base, this);
			MID->SetScalarParameterValue(MuseumAssets::Params::Roughness, Roughness);
		}
		break;
	}
	if (MID)
	{
		MID->SetVectorParameterValue(MuseumAssets::Params::Color, Color);
		Materials.Add(Key, MID);
	}
	return MID;
}

UStaticMeshComponent* UChamberHabitatComponent::MakeMeshComponent(const TCHAR* Name, UStaticMesh* Mesh, UMaterialInterface* Material, const FTransform& Local)
{
	AActor* Owner = GetOwner();
	UStaticMeshComponent* Component = NewObject<UStaticMeshComponent>(Owner, MakeUniqueObjectName(Owner, UStaticMeshComponent::StaticClass(), Name));
	Component->SetMobility(EComponentMobility::Movable);
	Component->SetStaticMesh(Mesh);
	Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Component->SetGenerateOverlapEvents(false);
	Component->SetCanEverAffectNavigation(false);
	Component->SetCastShadow(false);
	Component->SetupAttachment(this);
	Component->SetRelativeTransform(Local);
	if (Material)
	{
		Component->SetMaterial(0, Material);
	}
	Component->RegisterComponent();
	Created.Add(Component);
	return Component;
}

UInstancedStaticMeshComponent* UChamberHabitatComponent::GetInstancer(const FHabitatProp& Prop)
{
	const FString Key = FString::Printf(TEXT("%d_%d_%s_%d"), static_cast<int32>(Prop.Shape), static_cast<int32>(Prop.Look), *ColorKey(Prop.Color), Prop.bBlocksAlien ? 1 : 0);
	if (TObjectPtr<UInstancedStaticMeshComponent>* Found = Instancers.Find(Key))
	{
		return *Found;
	}

	AActor* Owner = GetOwner();
	UInstancedStaticMeshComponent* Instancer = NewObject<UInstancedStaticMeshComponent>(Owner, MakeUniqueObjectName(Owner, UInstancedStaticMeshComponent::StaticClass(), TEXT("HabitatProps")));
	Instancer->SetMobility(EComponentMobility::Movable);
	Instancer->SetStaticMesh(ShapeMesh(static_cast<uint8>(Prop.Shape)));
	Instancer->SetMaterial(0, LookMaterial(static_cast<uint8>(Prop.Look), Prop.Color));
	Instancer->SetCastShadow(false);
	Instancer->SetCanEverAffectNavigation(false);
	Instancer->SetGenerateOverlapEvents(false);
	if (Prop.bBlocksAlien)
	{
		MakeSolid(Instancer); // rocks and crystals the alien walks around and loose props bounce off
	}
	else
	{
		Instancer->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}
	Instancer->SetupAttachment(this);
	Instancer->RegisterComponent();
	Created.Add(Instancer);
	Instancers.Add(Key, Instancer);
	return Instancer;
}

void UChamberHabitatComponent::BuildGround()
{
	const EHabitatGround Ground = Habitat->Ground;
	if (Ground == EHabitatGround::None)
	{
		return;
	}
	UStaticMesh* SlabMesh = bRound ? MuseumAssets::CylinderMesh() : MuseumAssets::CubeMesh();
	const FVector2f Inner = Half * 2.f - FVector2f(0.6f); // just inside the glass
	auto Slab = [this, SlabMesh, &Inner](const TCHAR* Name, float Bottom, float Thickness, UMaterialInterface* Material, bool bSolid = true)
	{
		UStaticMeshComponent* Component = MakeMeshComponent(Name, SlabMesh, Material,
			FTransform(FQuat::Identity, FVector(0.f, 0.f, Bottom + Thickness * 0.5f), FVector(Inner.X, Inner.Y, Thickness) / 100.f));
		if (bSolid)
		{
			MakeSolid(Component);
		}
		return Component;
	};

	switch (Ground)
	{
	case EHabitatGround::Lava:
	{
		UMaterialInstanceDynamic* Lava = nullptr;
		if (UMaterialInterface* Base = MuseumAssets::EnvLavaMaterial())
		{
			Lava = UMaterialInstanceDynamic::Create(Base, this);
			Lava->SetVectorParameterValue(MuseumAssets::Params::Color, Habitat->GroundColor);
			Lava->SetVectorParameterValue(MuseumAssets::Params::GlowColor, Habitat->GroundGlowColor);
			Lava->SetScalarParameterValue(MuseumAssets::Params::Tiling, FMath::Max(Half.X, Half.Y) / 40.f); // same crack size in any case
		}
		Slab(TEXT("HabitatLava"), FloorZ, GroundDepth, Lava);
		break;
	}
	case EHabitatGround::Water:
	{
		Slab(TEXT("HabitatPoolBed"), FloorZ, 0.6f, LookMaterial(static_cast<uint8>(EHabitatPropLook::Lit), Habitat->GroundColor, 0.9f));
		UMaterialInstanceDynamic* Water = nullptr;
		if (UMaterialInterface* Base = MuseumAssets::EnvWaterMaterial())
		{
			Water = UMaterialInstanceDynamic::Create(Base, this);
			Water->SetVectorParameterValue(MuseumAssets::Params::Color, Habitat->GroundGlowColor);
			Water->SetScalarParameterValue(MuseumAssets::Params::Opacity, 0.42f);
		}
		// The water itself doesn't collide: the alien walks on the pool bed, props sink to it.
		Slab(TEXT("HabitatWater"), FloorZ + 0.6f, FMath::Max(1.f, GroundDepth - 0.6f), Water, false);
		break;
	}
	default:
	{
		// Crystal and tech floors are glossy and glow faintly in the glow colour.
		const bool bGlossy = Ground == EHabitatGround::Crystal || Ground == EHabitatGround::Tech;
		UMaterialInstanceDynamic* Surface = nullptr;
		if (UMaterialInterface* Base = MuseumAssets::EnvLitMaterial())
		{
			Surface = UMaterialInstanceDynamic::Create(Base, this);
			Surface->SetVectorParameterValue(MuseumAssets::Params::Color, bGlossy ? FMath::Lerp(Habitat->GroundColor, Habitat->GroundGlowColor, 0.12f) : Habitat->GroundColor);
			Surface->SetScalarParameterValue(MuseumAssets::Params::Roughness, bGlossy ? 0.3f : 0.92f);
			Surface->SetScalarParameterValue(MuseumAssets::Params::SelfIllum, bGlossy ? 0.28f : 0.12f);
			Surface->SetScalarParameterValue(MuseumAssets::Params::Tiling, FMath::Max(Half.X, Half.Y) / 15.f); // same grain size in any case
		}
		Slab(TEXT("HabitatGround"), FloorZ, GroundDepth, Surface);
		break;
	}
	}
}

bool UChamberHabitatComponent::IsInsideFloor(const FVector2f& Spot, float Radius) const
{
	return bRound
		? Spot.Size() <= Half.X - Radius - 1.5f
		: FMath::Abs(Spot.X) <= Half.X - Radius - 1.5f && FMath::Abs(Spot.Y) <= Half.Y - Radius - 1.5f;
}

bool UChamberHabitatComponent::PickSpot(const FHabitatProp& Prop, FRandomStream& Rng, float Radius, FVector2f& OutSpot)
{
	const FVector2f Room(FMath::Max(0.f, Half.X - Radius - 1.5f), FMath::Max(0.f, Half.Y - Radius - 1.5f));
	const float ClearRadius = 0.32f * FMath::Min(Half.X, Half.Y) + Radius; // the middle stays free for the alien
	for (int32 Attempt = 0; Attempt < 16; ++Attempt)
	{
		FVector2f Spot;
		switch (Prop.Placement)
		{
		case EHabitatPlacement::Edges:
		{
			const int32 Side = Rng.RandRange(0, 3);
			const float Along = Rng.FRandRange(-1.f, 1.f);
			Spot = Side == 0 ? FVector2f(Room.X, Along * Room.Y)
				: Side == 1 ? FVector2f(-Room.X, Along * Room.Y)
				: Side == 2 ? FVector2f(Along * Room.X, Room.Y)
				: FVector2f(Along * Room.X, -Room.Y);
			break;
		}
		case EHabitatPlacement::Corners:
			Spot = FVector2f(Rng.FRand() < 0.5f ? -Room.X : Room.X, Rng.FRand() < 0.5f ? -Room.Y : Room.Y) * Rng.FRandRange(0.8f, 1.f);
			break;
		case EHabitatPlacement::Back: // the case faces the viewer with +X when placed
			Spot = FVector2f(-Room.X * Rng.FRandRange(0.75f, 1.f), Rng.FRandRange(-Room.Y, Room.Y));
			break;
		case EHabitatPlacement::Scatter:
		default:
			Spot = FVector2f(Rng.FRandRange(-Room.X, Room.X), Rng.FRandRange(-Room.Y, Room.Y));
			if (Spot.Size() < ClearRadius)
			{
				continue;
			}
			break;
		}

		if (bRound && Prop.Placement != EHabitatPlacement::Scatter)
		{
			// Edges, corners and back all hug the round glass.
			const float Angle = Prop.Placement == EHabitatPlacement::Back ? Rng.FRandRange(0.6f, 1.4f) * PI : Rng.FRandRange(0.f, 2.f * PI);
			Spot = FVector2f(FMath::Cos(Angle), FMath::Sin(Angle)) * Room.X * 0.98f;
		}
		if (!IsInsideFloor(Spot, Radius))
		{
			continue;
		}
		if (FVector2f::Distance(Spot, KeepClearCenter) < KeepClearRadius + Radius)
		{
			continue; // not on top of the alien
		}

		bool bFree = true;
		for (const FVector& Other : Occupied)
		{
			if (FVector2f::Distance(Spot, FVector2f(Other.X, Other.Y)) < Other.Z + Radius + 0.5f)
			{
				bFree = false;
				break;
			}
		}
		if (bFree)
		{
			Occupied.Add(FVector(Spot.X, Spot.Y, Radius));
			OutSpot = Spot;
			return true;
		}
	}
	return false;
}

void UChamberHabitatComponent::BuildProps()
{
	FRandomStream Rng(Seed);
	const float FixedBase = Habitat->Ground == EHabitatGround::Water ? FloorZ + 0.6f : FloorZ + GroundDepth;
	for (const FHabitatProp& Prop : Habitat->Props)
	{
		for (int32 i = 0; i < Prop.Count; ++i)
		{
			const float Size = Rng.FRandRange(Prop.Size.X, FMath::Max(Prop.Size.X, Prop.Size.Y));
			const FVector Dimensions = Prop.Stretch * Size;
			const float Radius = 0.5f * FMath::Max(Dimensions.X, Dimensions.Y);
			FVector2f Spot;
			if (!PickSpot(Prop, Rng, Radius, Spot))
			{
				continue;
			}
			const FRotator Rotation(Rng.FRandRange(-Prop.Tilt, Prop.Tilt), Rng.FRandRange(0.f, 360.f), Rng.FRandRange(-Prop.Tilt, Prop.Tilt));
			const FVector Scale = Dimensions / 100.f; // basic shapes are 100 cm

			if (Prop.bPhysics)
			{
				// Dropped in from just above the ground; it settles by itself.
				const FTransform Local(Rotation, FVector(Spot.X, Spot.Y, GetGroundTopZ() + Dimensions.Z * 0.5f + 1.f), Scale);
				UStaticMeshComponent* Component = MakeMeshComponent(TEXT("HabitatLoose"), ShapeMesh(static_cast<uint8>(Prop.Shape)),
					LookMaterial(static_cast<uint8>(Prop.Look), Prop.Color), Local);
				MakeLoose(Component, IsWater());
				Component->SetMassOverrideInKg(NAME_None, FMath::Clamp(PropDensity * ShapeVolume(Prop.Shape, Dimensions) / 1000.f, 0.02f, 3.f), true);
				Component->GetBodyInstance()->SetMaxDepenetrationVelocity(25.f); // overlapping props ease apart, never pop
				if (!bFrozen)
				{
					Component->SetSimulatePhysics(true);
				}
				FLooseProp Loose;
				Loose.Comp = Component;
				Loose.Home = Local;
				LooseProps.Add(Loose);
			}
			else
			{
				// Slightly sunk into the ground so it looks planted.
				const FTransform Local(Rotation, FVector(Spot.X, Spot.Y, FixedBase + Dimensions.Z * 0.44f), Scale);
				UInstancedStaticMeshComponent* Instancer = GetInstancer(Prop);
				const int32 Index = Instancer->AddInstance(Local, false);
				if (Prop.bSway)
				{
					FSwayInstance Sway;
					Sway.Instancer = Instancer;
					Sway.Index = Index;
					Sway.Base = Local;
					Sway.Height = Dimensions.Z;
					Sway.Phase = Rng.FRandRange(0.f, 2.f * PI);
					SwayInstances.Add(Sway);
				}
			}
		}
	}
}

void UChamberHabitatComponent::BuildAmbient()
{
	Particles.Reset();
	AmbientRng.Initialize(Seed ^ 0x5EED);
	if (Habitat->Ambient == EHabitatAmbient::None || Habitat->AmbientCount <= 0)
	{
		return;
	}

	const EHabitatAmbient Ambient = Habitat->Ambient;
	const bool bMist = Ambient == EHabitatAmbient::Mist;
	UMaterialInstanceDynamic* MID = nullptr;
	if (UMaterialInterface* Base = bMist ? MuseumAssets::EnvMistMaterial() : MuseumAssets::FXGlowMaterial())
	{
		MID = UMaterialInstanceDynamic::Create(Base, this);
		MID->SetVectorParameterValue(MuseumAssets::Params::Color, Habitat->AmbientColor);
		if (bMist)
		{
			MID->SetScalarParameterValue(MuseumAssets::Params::Opacity, 0.3f);
		}
		else
		{
			const float Intensity = Ambient == EHabitatAmbient::Embers ? 3.f
				: Ambient == EHabitatAmbient::Bubbles ? 0.9f
				: Ambient == EHabitatAmbient::Spores ? 1.2f
				: 2.4f;
			MID->SetScalarParameterValue(MuseumAssets::Params::Intensity, Intensity);
			MID->SetScalarParameterValue(MuseumAssets::Params::Opacity, Ambient == EHabitatAmbient::Bubbles ? 0.4f : 0.9f);
		}
	}

	AActor* Owner = GetOwner();
	AmbientInstances = NewObject<UInstancedStaticMeshComponent>(Owner, MakeUniqueObjectName(Owner, UInstancedStaticMeshComponent::StaticClass(), TEXT("HabitatAmbient")));
	AmbientInstances->SetMobility(EComponentMobility::Movable);
	AmbientInstances->SetStaticMesh(Ambient == EHabitatAmbient::Pulses ? MuseumAssets::CubeMesh() : MuseumAssets::SphereMesh());
	if (MID)
	{
		AmbientInstances->SetMaterial(0, MID);
	}
	AmbientInstances->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	AmbientInstances->SetCastShadow(false);
	AmbientInstances->SetCanEverAffectNavigation(false);
	AmbientInstances->SetupAttachment(this);
	AmbientInstances->RegisterComponent();
	Created.Add(AmbientInstances);

	Particles.SetNum(Habitat->AmbientCount);
	for (FParticle& Particle : Particles)
	{
		RespawnParticle(Particle, true);
		AmbientInstances->AddInstance(ParticleTransform(Particle), false);
	}
}

// ---------------------------------------------------------------------------------------------
// Ambient effect
// ---------------------------------------------------------------------------------------------

void UChamberHabitatComponent::RespawnParticle(FParticle& P, bool bFirstTime)
{
	const FVector2f Room = Half - FVector2f(2.f);
	FVector Floor(AmbientRng.FRandRange(-Room.X, Room.X), AmbientRng.FRandRange(-Room.Y, Room.Y), 0.f);
	if (bRound)
	{
		const float Angle = AmbientRng.FRandRange(0.f, 2.f * PI);
		Floor = FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.f) * Room.X * FMath::Sqrt(AmbientRng.FRand());
	}
	P.Age = 0.f;
	P.Phase = AmbientRng.FRandRange(0.f, 2.f * PI);
	P.Velocity = FVector::ZeroVector;

	switch (Habitat->Ambient)
	{
	case EHabitatAmbient::Embers:
		P.Location = Floor + FVector(0.f, 0.f, GetGroundTopZ() + 0.5f);
		P.Velocity = FVector(0.f, 0.f, AmbientRng.FRandRange(5.f, 11.f));
		P.Life = AmbientRng.FRandRange(2.f, 4.5f);
		P.Size = FVector(AmbientRng.FRandRange(0.5f, 1.1f));
		break;
	case EHabitatAmbient::Bubbles:
	{
		P.Location = Floor + FVector(0.f, 0.f, FloorZ + 1.f);
		P.Velocity = FVector(0.f, 0.f, AmbientRng.FRandRange(5.f, 10.f));
		const float Rise = IsWater() ? FMath::Max(2.f, GroundDepth - 1.5f) : GlassHeight * 0.4f;
		P.Life = Rise / P.Velocity.Z;
		P.Size = FVector(AmbientRng.FRandRange(0.5f, 1.4f));
		break;
	}
	case EHabitatAmbient::Mist:
		P.Location = Floor + FVector(0.f, 0.f, GetGroundTopZ() + AmbientRng.FRandRange(1.f, 5.f));
		P.Velocity = FVector(AmbientRng.FRandRange(-2.5f, 2.5f), AmbientRng.FRandRange(-2.5f, 2.5f), 0.f);
		P.Life = AmbientRng.FRandRange(6.f, 10.f);
		P.Size = FVector(AmbientRng.FRandRange(14.f, 26.f), AmbientRng.FRandRange(14.f, 26.f), AmbientRng.FRandRange(3.f, 6.f));
		break;
	case EHabitatAmbient::Sparkles:
		P.Location = Floor + FVector(0.f, 0.f, FloorZ + AmbientRng.FRandRange(2.f, GlassHeight * 0.6f));
		P.Life = AmbientRng.FRandRange(1.2f, 2.6f);
		P.Size = FVector(AmbientRng.FRandRange(0.6f, 1.3f));
		break;
	case EHabitatAmbient::Spores:
		P.Location = Floor + FVector(0.f, 0.f, FloorZ + AmbientRng.FRandRange(2.f, GlassHeight * 0.5f));
		P.Velocity = FVector(AmbientRng.FRandRange(-1.f, 1.f), AmbientRng.FRandRange(-1.f, 1.f), AmbientRng.FRandRange(0.8f, 2.2f));
		P.Life = AmbientRng.FRandRange(5.f, 9.f);
		P.Size = FVector(AmbientRng.FRandRange(0.3f, 0.65f));
		break;
	case EHabitatAmbient::Pulses:
	{
		const float Length = AmbientRng.FRandRange(8.f, 20.f);
		P.Location = Floor + FVector(0.f, 0.f, GetGroundTopZ() + 0.15f);
		P.Size = AmbientRng.FRand() < 0.5f ? FVector(Length, 0.5f, 0.25f) : FVector(0.5f, Length, 0.25f);
		P.Life = AmbientRng.FRandRange(1.f, 2.2f);
		break;
	}
	default:
		P.Life = 1.f;
		break;
	}
	if (bFirstTime)
	{
		P.Age = AmbientRng.FRandRange(0.f, P.Life); // spread the cycle out
	}
}

FTransform UChamberHabitatComponent::ParticleTransform(const FParticle& P) const
{
	const float T = P.Life > 0.f ? FMath::Clamp(P.Age / P.Life, 0.f, 1.f) : 1.f;
	float Fade = 1.f;
	switch (Habitat ? Habitat->Ambient : EHabitatAmbient::None)
	{
	case EHabitatAmbient::Embers: Fade = 1.f - T; break;
	case EHabitatAmbient::Bubbles: Fade = T < 0.9f ? 1.f : (1.f - T) * 10.f; break;
	case EHabitatAmbient::Sparkles: Fade = FMath::Pow(FMath::Sin(T * PI), 3.f); break;
	default: Fade = FMath::Sin(T * PI); break; // mist, spores, pulses grow in and out
	}
	return FTransform(FQuat::Identity, P.Location, P.Size * FMath::Max(Fade, 0.001f) / 100.f);
}

void UChamberHabitatComponent::UpdateAmbient(float DeltaTime)
{
	if (!AmbientInstances || Particles.Num() == 0)
	{
		return;
	}
	const EHabitatAmbient Ambient = Habitat->Ambient;
	for (int32 i = 0; i < Particles.Num(); ++i)
	{
		FParticle& P = Particles[i];
		P.Age += DeltaTime;
		if (P.Age >= P.Life)
		{
			RespawnParticle(P, false);
		}

		FVector Drift = FVector::ZeroVector;
		switch (Ambient)
		{
		case EHabitatAmbient::Embers:
			Drift = FVector(FMath::Sin(Time * 2.f + P.Phase), FMath::Cos(Time * 1.7f + P.Phase), 0.f) * 1.5f;
			break;
		case EHabitatAmbient::Bubbles:
			Drift = FVector(FMath::Sin(Time * 6.f + P.Phase), FMath::Cos(Time * 5.f + P.Phase), 0.f) * 1.2f;
			break;
		case EHabitatAmbient::Spores:
			Drift = FVector(FMath::Sin(Time * 0.8f + P.Phase), FMath::Cos(Time * 0.6f + P.Phase), 0.f);
			break;
		default:
			break;
		}
		P.Location += (P.Velocity + Drift) * DeltaTime;

		// Mist turns back at the glass instead of leaving the case.
		if (Ambient == EHabitatAmbient::Mist)
		{
			if (FMath::Abs(P.Location.X) > Half.X - 4.f) { P.Velocity.X = -FMath::Sign(P.Location.X) * FMath::Abs(P.Velocity.X); }
			if (FMath::Abs(P.Location.Y) > Half.Y - 4.f) { P.Velocity.Y = -FMath::Sign(P.Location.Y) * FMath::Abs(P.Velocity.Y); }
		}
		AmbientInstances->UpdateInstanceTransform(i, ParticleTransform(P), false, i == Particles.Num() - 1, true);
	}
}

void UChamberHabitatComponent::UpdateSway()
{
	for (int32 i = 0; i < SwayInstances.Num(); ++i)
	{
		const FSwayInstance& Sway = SwayInstances[i];
		UInstancedStaticMeshComponent* Instancer = Sway.Instancer.Get();
		if (!Instancer)
		{
			continue;
		}
		// Bend around the bottom of the plant.
		const FQuat BaseRotation = Sway.Base.GetRotation();
		const FVector Bottom = Sway.Base.GetLocation() - BaseRotation.GetUpVector() * Sway.Height * 0.5f;
		const float Angle = FMath::DegreesToRadians(FMath::Sin(Time * 1.3f + Sway.Phase) * 6.f + FMath::Sin(Time * 2.9f + Sway.Phase * 2.f) * 2.f);
		const FQuat Rotation = FQuat(FVector::ForwardVector, Angle) * BaseRotation;
		const FVector Center = Bottom + Rotation.GetUpVector() * Sway.Height * 0.5f;
		Instancer->UpdateInstanceTransform(Sway.Index, FTransform(Rotation, Center, Sway.Base.GetScale3D()), false, true, true);
	}
}

// ---------------------------------------------------------------------------------------------
// Loose props
// ---------------------------------------------------------------------------------------------

void UChamberHabitatComponent::ResetLooseProp(FLooseProp& Prop)
{
	UStaticMeshComponent* Component = Prop.Comp.Get();
	if (!Component)
	{
		return;
	}
	Component->SetSimulatePhysics(false);
	Component->AttachToComponent(this, FAttachmentTransformRules::KeepWorldTransform);
	Component->SetRelativeTransform(Prop.Home);
	Component->SetVisibility(true);
	Component->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Prop.RespawnTimer = -1.f;
	if (!bFrozen)
	{
		Component->SetSimulatePhysics(true);
		Component->WakeRigidBody();
	}
}

void UChamberHabitatComponent::SetFrozen(bool bInFrozen)
{
	if (bFrozen == bInFrozen)
	{
		return;
	}
	bFrozen = bInFrozen;
	for (FLooseProp& Prop : LooseProps)
	{
		UStaticMeshComponent* Component = Prop.Comp.Get();
		if (!Component || Prop.RespawnTimer > 0.f)
		{
			continue;
		}
		if (bFrozen)
		{
			Component->SetSimulatePhysics(false);
			Component->AttachToComponent(this, FAttachmentTransformRules::KeepWorldTransform);
		}
		else
		{
			Component->SetSimulatePhysics(true);
			Component->WakeRigidBody();
		}
	}
}

void UChamberHabitatComponent::CarryLooseProps(const FTransform& From, const FTransform& To)
{
	if (bFrozen)
	{
		return;
	}
	const float YawDelta = FRotator::NormalizeAxis(To.Rotator().Yaw - From.Rotator().Yaw);
	for (const FLooseProp& Prop : LooseProps)
	{
		UStaticMeshComponent* Component = Prop.Comp.Get();
		if (!Component || !Component->IsSimulatingPhysics())
		{
			continue;
		}
		const FVector Local = From.InverseTransformPositionNoScale(Component->GetComponentLocation());
		Component->SetWorldLocationAndRotation(To.TransformPositionNoScale(Local),
			Component->GetComponentRotation() + FRotator(0.f, YawDelta, 0.f), false, nullptr, ETeleportType::TeleportPhysics);
	}
}

void UChamberHabitatComponent::KeepLoosePropsInside()
{
	const FTransform& Transform = GetComponentTransform();
	for (FLooseProp& Prop : LooseProps)
	{
		UStaticMeshComponent* Component = Prop.Comp.Get();
		if (!Component || Prop.RespawnTimer > 0.f || !Component->IsSimulatingPhysics())
		{
			continue;
		}
		const FVector Local = Transform.InverseTransformPosition(Component->GetComponentLocation());
		const bool bOutsideFlat = bRound
			? Local.Size2D() > Half.X + 3.f
			: FMath::Abs(Local.X) > Half.X + 3.f || FMath::Abs(Local.Y) > Half.Y + 3.f;
		const bool bOutside = bOutsideFlat || Local.Z < FloorZ - 6.f || Local.Z > FloorZ + GlassHeight + 6.f;
		if (bOutside)
		{
			ResetLooseProp(Prop);
		}
	}
}

UStaticMeshComponent* UChamberHabitatComponent::FindNearestLooseProp(const FVector& WorldPoint) const
{
	UStaticMeshComponent* Best = nullptr;
	float BestDistance = TNumericLimits<float>::Max();
	for (const FLooseProp& Prop : LooseProps)
	{
		UStaticMeshComponent* Component = Prop.Comp.Get();
		if (!Component || Prop.RespawnTimer > 0.f || !Component->IsVisible())
		{
			continue;
		}
		const float Distance = FVector::Dist(Component->GetComponentLocation(), WorldPoint);
		if (Distance < BestDistance)
		{
			Best = Component;
			BestDistance = Distance;
		}
	}
	return Best;
}

void UChamberHabitatComponent::ConsumeProp(UStaticMeshComponent* Component, float RespawnSeconds)
{
	for (FLooseProp& Prop : LooseProps)
	{
		if (Prop.Comp.Get() == Component)
		{
			Component->SetSimulatePhysics(false);
			Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Component->SetVisibility(false);
			Prop.RespawnTimer = FMath::Max(0.1f, RespawnSeconds);
			return;
		}
	}
}

int32 UChamberHabitatComponent::Blast(const FVector& WorldCenter, float Radius, float Speed, const FVector& Direction, float UpBias)
{
	int32 Thrown = 0;
	const FVector Along = Direction.GetSafeNormal2D();
	for (const FLooseProp& Prop : LooseProps)
	{
		UStaticMeshComponent* Component = Prop.Comp.Get();
		if (!Component || Prop.RespawnTimer > 0.f || !Component->IsSimulatingPhysics())
		{
			continue;
		}
		const FVector To = Component->GetComponentLocation() - WorldCenter;
		const float Distance = static_cast<float>(To.Size());
		if (Distance > Radius)
		{
			continue;
		}
		FVector Push = To.GetSafeNormal2D();
		if (!Along.IsNearlyZero())
		{
			if (FVector::DotProduct(Push, Along) < 0.2f)
			{
				continue; // behind the blast
			}
			Push = Along;
		}
		if (Push.IsNearlyZero())
		{
			Push = FVector(AmbientRng.FRandRange(-1.f, 1.f), AmbientRng.FRandRange(-1.f, 1.f), 0.f).GetSafeNormal();
		}
		// Weaker further out, and heavy props (boulders) fly less far than pebbles.
		const float Falloff = FMath::Sqrt(FMath::Max(0.f, 1.f - Distance / Radius));
		const float MassFactor = FMath::Clamp(0.3f / FMath::Sqrt(FMath::Max(0.01f, Component->GetMass())), 0.35f, 1.2f);
		Component->WakeRigidBody();
		Component->AddImpulse((Push + FVector(0.f, 0.f, UpBias)) * Speed * Falloff * MassFactor, NAME_None, true);
		++Thrown;
	}
	return Thrown;
}

void UChamberHabitatComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (!Habitat)
	{
		return;
	}
	Time += DeltaTime;

	for (FLooseProp& Prop : LooseProps)
	{
		if (Prop.RespawnTimer > 0.f)
		{
			Prop.RespawnTimer -= DeltaTime;
			if (Prop.RespawnTimer <= 0.f)
			{
				ResetLooseProp(Prop);
			}
		}
	}

	// Props are light: a hard shove (or a slow frame, where pushes add up) must not turn them into
	// bullets. Cap their speed.
	if (!bFrozen)
	{
		const float MaxSpeed = 250.f * static_cast<float>(GetComponentScale().X);
		for (const FLooseProp& Prop : LooseProps)
		{
			UStaticMeshComponent* Component = Prop.Comp.Get();
			if (Component && Prop.RespawnTimer <= 0.f && Component->IsSimulatingPhysics())
			{
				const FVector Velocity = Component->GetPhysicsLinearVelocity();
				if (Velocity.SizeSquared() > FMath::Square(MaxSpeed))
				{
					Component->SetPhysicsLinearVelocity(Velocity.GetClampedToMaxSize(MaxSpeed));
				}
			}
		}
	}

	KeepInsideTimer += DeltaTime;
	if (KeepInsideTimer >= 0.5f)
	{
		KeepInsideTimer = 0.f;
		KeepLoosePropsInside();
	}

	// Purely visual: only while someone can see the case.
	if (GetOwner() && GetOwner()->WasRecentlyRendered(0.25f))
	{
		UpdateAmbient(DeltaTime);
		UpdateSway();
	}
}
