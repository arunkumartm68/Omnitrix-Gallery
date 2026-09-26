// Alien Museum - a glass museum pod that holds one alien.

#include "Chamber/AlienChamber.h"
#include "Aliens/AlienCharacter.h"
#include "Data/AlienDataAsset.h"
#include "Core/MuseumAssets.h"
#include "Ben10.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/CollisionProfile.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	constexpr int32 NumWalls = 8;
	constexpr float FrameInset = 3.f; // metal rim beyond the glass radius

	FVector ShapeScale(float SizeX, float SizeY, float SizeZ)
	{
		return FVector(SizeX, SizeY, SizeZ) / 100.f; // basic shapes are 100 cm
	}

	/** Invisible collision that only blocks pawns (the alien), never the pointer ray. */
	void MakePawnBlocker(UPrimitiveComponent* Component)
	{
		Component->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		Component->SetCollisionObjectType(ECC_WorldDynamic);
		Component->SetCollisionResponseToAllChannels(ECR_Ignore);
		Component->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
		Component->SetCanEverAffectNavigation(false);
		Component->SetGenerateOverlapEvents(false);
	}

	/** Word-wraps a description for a TextRender (which has no automatic wrapping). */
	FString WrapText(const FString& In, int32 MaxChars, int32 MaxLines)
	{
		TArray<FString> Words;
		In.ParseIntoArrayWS(Words);
		TArray<FString> Lines;
		FString Line;
		for (const FString& Word : Words)
		{
			if (Line.Len() + Word.Len() + 1 > MaxChars && !Line.IsEmpty())
			{
				Lines.Add(Line);
				Line.Reset();
				if (Lines.Num() == MaxLines)
				{
					break;
				}
			}
			Line += Line.IsEmpty() ? Word : TEXT(" ") + Word;
		}
		if (Lines.Num() < MaxLines && !Line.IsEmpty())
		{
			Lines.Add(Line);
		}
		else if (Lines.Num() == MaxLines)
		{
			Lines.Last() += TEXT("...");
		}
		return FString::Join(Lines, TEXT("\n"));
	}
}

AAlienChamber::AAlienChamber()
{
	PrimaryActorTick.bCanEverTick = true;

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderFinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeFinder(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereFinder(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> ConeFinder(TEXT("/Engine/BasicShapes/Cone.Cone"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> PlaneFinder(TEXT("/Engine/BasicShapes/Plane.Plane"));
	CubeMesh = CubeFinder.Object;
	CylinderMesh = CylinderFinder.Object;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	Root->SetMobility(EComponentMobility::Movable);
	RootComponent = Root;

	auto MakeMesh = [this](const TCHAR* Name, UStaticMesh* Mesh, USceneComponent* Parent)
	{
		UStaticMeshComponent* Comp = CreateDefaultSubobject<UStaticMeshComponent>(Name);
		Comp->SetupAttachment(Parent);
		Comp->SetMobility(EComponentMobility::Movable);
		Comp->SetStaticMesh(Mesh);
		Comp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Comp->SetGenerateOverlapEvents(false);
		Comp->SetCanEverAffectNavigation(false);
		Comp->SetCastShadow(false);
		return Comp;
	};

	// Base: the alien stands on it, so it blocks everything. (Square is the default look;
	// BuildLayout swaps the meshes when Shape is Round.)
	Base = MakeMesh(TEXT("Base"), CubeFinder.Object, Root);
	Base->SetCollisionProfileName(UCollisionProfile::BlockAllDynamic_ProfileName);

	FloorGlow = MakeMesh(TEXT("FloorGlow"), CubeFinder.Object, Root);
	Glass = MakeMesh(TEXT("Glass"), CubeFinder.Object, Root);
	Glass->SetTranslucentSortPriority(1);
	TopCap = MakeMesh(TEXT("TopCap"), CubeFinder.Object, Root);
	LightPanel = MakeMesh(TEXT("LightPanel"), CubeFinder.Object, Root);

	// Anti-gravity glow under the base, shown while the chamber floats in the air.
	HoverGlow = MakeMesh(TEXT("HoverGlow"), CylinderFinder.Object, Root);
	HoverGlow->SetVisibility(false);

	FramePillars = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("FramePillars"));
	FramePillars->SetupAttachment(Root);
	FramePillars->SetMobility(EComponentMobility::Movable);
	FramePillars->SetStaticMesh(CubeFinder.Object);
	FramePillars->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	FramePillars->SetCastShadow(false);
	FramePillars->SetCanEverAffectNavigation(false);

	for (int32 i = 0; i < NumWalls; ++i)
	{
		UBoxComponent* Wall = CreateDefaultSubobject<UBoxComponent>(*FString::Printf(TEXT("ContainmentWall%d"), i));
		Wall->SetupAttachment(Root);
		Wall->SetMobility(EComponentMobility::Movable);
		MakePawnBlocker(Wall);
		ContainmentWalls.Add(Wall);
	}
	Ceiling = CreateDefaultSubobject<UBoxComponent>(TEXT("Ceiling"));
	Ceiling->SetupAttachment(Root);
	Ceiling->SetMobility(EComponentMobility::Movable);
	MakePawnBlocker(Ceiling);

	ObstacleRock = MakeMesh(TEXT("ObstacleRock"), SphereFinder.Object, Root);
	MakePawnBlocker(ObstacleRock);
	ObstacleCrystal = MakeMesh(TEXT("ObstacleCrystal"), ConeFinder.Object, Root);
	MakePawnBlocker(ObstacleCrystal);

	MovementBounds = CreateDefaultSubobject<UBoxComponent>(TEXT("MovementBounds"));
	MovementBounds->SetupAttachment(Root);
	MovementBounds->SetMobility(EComponentMobility::Movable);
	MovementBounds->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	MovementBounds->ShapeColor = FColor(80, 255, 120);
	MovementBounds->SetHiddenInGame(true);

	SpawnPoint = CreateDefaultSubobject<USceneComponent>(TEXT("SpawnPoint"));
	SpawnPoint->SetupAttachment(Root);
	SpawnPoint->SetMobility(EComponentMobility::Movable);

	// What the pointer ray hits (Visibility channel only). Property and component are named
	// "SelectBox" because older saved Blueprints hold a capsule under the previous name "SelectVolume".
	SelectBox = CreateDefaultSubobject<UBoxComponent>(TEXT("SelectBox"));
	SelectBox->SetupAttachment(Root);
	SelectBox->SetMobility(EComponentMobility::Movable);
	SelectBox->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	SelectBox->SetCollisionObjectType(ECC_WorldDynamic);
	SelectBox->SetCollisionResponseToAllChannels(ECR_Ignore);
	SelectBox->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	SelectBox->SetCanEverAffectNavigation(false);
	SelectBox->SetHiddenInGame(true);

	InteriorLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("InteriorLight"));
	InteriorLight->SetupAttachment(Root);
	InteriorLight->SetMobility(EComponentMobility::Movable);
	InteriorLight->SetCastShadows(false);
	InteriorLight->SetIntensity(15.f);

	// Holographic info panel.
	InfoRoot = CreateDefaultSubobject<USceneComponent>(TEXT("InfoRoot"));
	InfoRoot->SetupAttachment(Root);
	InfoRoot->SetMobility(EComponentMobility::Movable);

	InfoBackground = MakeMesh(TEXT("InfoBackground"), PlaneFinder.Object, InfoRoot);
	InfoBackground->SetRelativeRotation(FRotator(-90.f, 0.f, 0.f)); // plane normal +Z -> +X (towards the viewer)
	InfoBackground->SetRelativeScale3D(FVector(0.18f, 0.42f, 1.f));    // 18 cm tall, 42 cm wide
	InfoBackground->SetTranslucentSortPriority(3);

	InfoTitle = CreateDefaultSubobject<UTextRenderComponent>(TEXT("InfoTitle"));
	InfoTitle->SetupAttachment(InfoRoot);
	InfoTitle->SetMobility(EComponentMobility::Movable);
	InfoTitle->SetHorizontalAlignment(EHTA_Center);
	InfoTitle->SetVerticalAlignment(EVRTA_TextCenter);
	InfoTitle->SetWorldSize(3.0f);
	InfoTitle->SetTextRenderColor(FColor(170, 240, 255));
	InfoTitle->SetRelativeLocation(FVector(0.5f, 0.f, 5.2f));
	InfoTitle->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	InfoTitle->SetCastShadow(false);

	InfoBody = CreateDefaultSubobject<UTextRenderComponent>(TEXT("InfoBody"));
	InfoBody->SetupAttachment(InfoRoot);
	InfoBody->SetMobility(EComponentMobility::Movable);
	InfoBody->SetHorizontalAlignment(EHTA_Center);
	InfoBody->SetVerticalAlignment(EVRTA_TextCenter);
	InfoBody->SetWorldSize(1.55f);
	InfoBody->SetTextRenderColor(FColor(220, 245, 255));
	InfoBody->SetRelativeLocation(FVector(0.5f, 0.f, -2.2f));
	InfoBody->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	InfoBody->SetCastShadow(false);

	DefaultAlienClass = AAlienCharacter::StaticClass();
}

// ---------------------------------------------------------------------------------------------
// Construction / layout
// ---------------------------------------------------------------------------------------------

void AAlienChamber::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	BuildLayout();
	ApplyMaterials();
	RefreshInfoText();
	SetInfoPanelVisible(bInfoVisible);
}

void AAlienChamber::BuildLayout()
{
	const bool bSquare = Shape == EChamberShape::Square;
	const float R = Radius;
	const float TopZ = BaseHeight + GlassHeight;
	const float OuterD = 2.f * (R + FrameInset);

	// Square display case or round pod: same parts, different basic shape.
	UStaticMesh* ShellMesh = bSquare ? CubeMesh.Get() : CylinderMesh.Get();
	for (UStaticMeshComponent* Part : { Base.Get(), FloorGlow.Get(), Glass.Get(), TopCap.Get(), LightPanel.Get() })
	{
		if (ShellMesh && Part->GetStaticMesh() != ShellMesh)
		{
			Part->SetStaticMesh(ShellMesh);
		}
	}
	if (ShellMesh && FramePillars->GetStaticMesh() != ShellMesh)
	{
		FramePillars->SetStaticMesh(ShellMesh);
	}

	Base->SetRelativeLocation(FVector(0.f, 0.f, BaseHeight * 0.5f));
	Base->SetRelativeScale3D(ShapeScale(OuterD, OuterD, BaseHeight));

	FloorGlow->SetRelativeLocation(FVector(0.f, 0.f, BaseHeight + 0.2f));
	FloorGlow->SetRelativeScale3D(ShapeScale(2.f * R - 4.f, 2.f * R - 4.f, 0.4f));

	Glass->SetRelativeLocation(FVector(0.f, 0.f, BaseHeight + GlassHeight * 0.5f));
	Glass->SetRelativeScale3D(ShapeScale(2.f * R, 2.f * R, GlassHeight));

	TopCap->SetRelativeLocation(FVector(0.f, 0.f, TopZ + 3.f));
	TopCap->SetRelativeScale3D(ShapeScale(OuterD, OuterD, 6.f));

	LightPanel->SetRelativeLocation(FVector(0.f, 0.f, TopZ - 0.6f));
	LightPanel->SetRelativeScale3D(ShapeScale(2.f * R - 10.f, 2.f * R - 10.f, 1.f));

	// Four frame posts, one draw call: square corner posts or round pillars.
	if (!IsTemplate())
	{
		FramePillars->ClearInstances();
		for (int32 i = 0; i < 4; ++i)
		{
			const float Angle = FMath::DegreesToRadians(45.f + 90.f * i);
			const float Distance = bSquare ? (R + 1.5f) * UE_SQRT_2 : R + 1.5f; // square: exactly on the corners
			const FVector Location(FMath::Cos(Angle) * Distance, FMath::Sin(Angle) * Distance, BaseHeight + GlassHeight * 0.5f);
			FramePillars->AddInstance(FTransform(FQuat::Identity, Location, ShapeScale(3.f, 3.f, GlassHeight)));
		}
	}

	// Invisible walls just inside the glass: 4 for a square case, an octagon for a round pod.
	const float Apothem = R - 1.f;
	const int32 ActiveWalls = bSquare ? 4 : NumWalls;
	const float HalfSide = bSquare ? R + 1.f : Apothem * FMath::Tan(FMath::DegreesToRadians(180.f / NumWalls)) + 1.f;
	for (int32 i = 0; i < ContainmentWalls.Num(); ++i)
	{
		UBoxComponent* Wall = ContainmentWalls[i];
		const bool bActive = i < ActiveWalls;
		Wall->SetCollisionEnabled(bActive ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
		if (!bActive)
		{
			continue;
		}
		const float AngleDeg = 360.f / ActiveWalls * i;
		const float Angle = FMath::DegreesToRadians(AngleDeg);
		Wall->SetBoxExtent(FVector(1.f, HalfSide, GlassHeight * 0.5f));
		Wall->SetRelativeLocation(FVector(FMath::Cos(Angle) * Apothem, FMath::Sin(Angle) * Apothem, BaseHeight + GlassHeight * 0.5f));
		Wall->SetRelativeRotation(FRotator(0.f, AngleDeg, 0.f));
	}
	Ceiling->SetBoxExtent(FVector(R, R, 1.f));
	Ceiling->SetRelativeLocation(FVector(0.f, 0.f, TopZ));

	// Obstacles for the alien to walk around.
	ObstacleRock->SetRelativeLocation(FVector(-0.45f * R, 0.42f * R, BaseHeight + 1.5f));
	ObstacleRock->SetRelativeScale3D(ShapeScale(0.34f * R, 0.28f * R, 0.20f * R));
	ObstacleCrystal->SetRelativeLocation(FVector(0.42f * R, -0.45f * R, BaseHeight + 0.2f * R));
	ObstacleCrystal->SetRelativeScale3D(ShapeScale(0.16f * R, 0.16f * R, 0.40f * R));
	ObstacleRock->SetVisibility(bShowObstacles);
	ObstacleCrystal->SetVisibility(bShowObstacles);
	ObstacleRock->SetCollisionEnabled(bShowObstacles ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
	ObstacleCrystal->SetCollisionEnabled(bShowObstacles ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);

	const float MoveR = GetMovementRadiusLocal();
	MovementBounds->SetBoxExtent(FVector(MoveR, MoveR, GlassHeight * 0.5f));
	MovementBounds->SetRelativeLocation(FVector(0.f, 0.f, BaseHeight + GlassHeight * 0.5f));

	SpawnPoint->SetRelativeLocation(FVector(0.f, 0.f, BaseHeight + 0.5f));

	SelectBox->SetBoxExtent(FVector(R + FrameInset + 2.f, R + FrameInset + 2.f, (TopZ + 6.f) * 0.5f + 2.f));
	SelectBox->SetRelativeLocation(FVector(0.f, 0.f, (TopZ + 6.f) * 0.5f));

	HoverGlow->SetRelativeLocation(FVector(0.f, 0.f, -0.6f));
	HoverGlow->SetRelativeScale3D(ShapeScale(OuterD * 0.8f, OuterD * 0.8f, 0.6f));

	InteriorLight->SetRelativeLocation(FVector(0.f, 0.f, TopZ - 8.f));
	InteriorLight->SetAttenuationRadius(R * 2.5f);
	InteriorLight->SetLightColor(ActiveLightColor);
	InteriorLight->SetVisibility(bUseRealInteriorLight);

	InfoRoot->SetRelativeLocation(FVector(0.f, 0.f, TopZ + 22.f));
}

void AAlienChamber::ApplyMaterials()
{
	UMaterialInterface* MetalBase = MetalMaterialOverride ? MetalMaterialOverride.Get() : MuseumAssets::MetalMaterial();
	UMaterialInterface* EmissiveBase = EmissiveMaterialOverride ? EmissiveMaterialOverride.Get() : MuseumAssets::EmissiveMaterial();
	UMaterialInterface* GlassBase = GlassMaterialOverride ? GlassMaterialOverride.Get() : MuseumAssets::GlassMaterial();
	UMaterialInterface* HologramBase = HologramMaterialOverride ? HologramMaterialOverride.Get() : MuseumAssets::HologramMaterial();

	MetalMID = MetalBase ? UMaterialInstanceDynamic::Create(MetalBase, this) : nullptr;
	GlowMID = EmissiveBase ? UMaterialInstanceDynamic::Create(EmissiveBase, this) : nullptr;
	CrystalMID = EmissiveBase ? UMaterialInstanceDynamic::Create(EmissiveBase, this) : nullptr;
	GlassMID = GlassBase ? UMaterialInstanceDynamic::Create(GlassBase, this) : nullptr;
	HologramMID = HologramBase ? UMaterialInstanceDynamic::Create(HologramBase, this) : nullptr;
	HoverMID = EmissiveBase ? UMaterialInstanceDynamic::Create(EmissiveBase, this) : nullptr;
	if (HoverMID)
	{
		HoverGlow->SetMaterial(0, HoverMID);
	}

	if (MetalMID)
	{
		MetalMID->SetVectorParameterValue(MuseumAssets::Params::Color, FrameColor);
		Base->SetMaterial(0, MetalMID);
		TopCap->SetMaterial(0, MetalMID);
		FramePillars->SetMaterial(0, MetalMID);
		ObstacleRock->SetMaterial(0, MetalMID);
	}
	if (GlowMID)
	{
		FloorGlow->SetMaterial(0, GlowMID);
		LightPanel->SetMaterial(0, GlowMID);
	}
	if (CrystalMID)
	{
		CrystalMID->SetVectorParameterValue(MuseumAssets::Params::Color, FLinearColor(0.75f, 0.35f, 1.0f));
		CrystalMID->SetScalarParameterValue(MuseumAssets::Params::Intensity, 1.3f);
		ObstacleCrystal->SetMaterial(0, CrystalMID);
	}
	if (GlassMID)
	{
		Glass->SetMaterial(0, GlassMID);
	}
	if (HologramMID)
	{
		HologramMID->SetScalarParameterValue(MuseumAssets::Params::Opacity, 0.35f);
		InfoBackground->SetMaterial(0, HologramMID);
	}
	ActiveLightColor = GetDesiredLightColor();
	ApplyLightColor();
}

FLinearColor AAlienChamber::GetDesiredLightColor() const
{
	if (OccupantData && OccupantData->ChamberLightColor.A > 0.f)
	{
		const FLinearColor& C = OccupantData->ChamberLightColor;
		return FLinearColor(C.R, C.G, C.B, 1.f);
	}
	return LightColor;
}

void AAlienChamber::ApplyLightColor()
{
	if (GlassMID)
	{
		GlassMID->SetVectorParameterValue(MuseumAssets::Params::Tint, ActiveLightColor);
	}
	if (HologramMID)
	{
		HologramMID->SetVectorParameterValue(MuseumAssets::Params::Color, ActiveLightColor * 0.6f);
	}
	if (HoverMID)
	{
		HoverMID->SetVectorParameterValue(MuseumAssets::Params::Color, ActiveLightColor);
	}
	InteriorLight->SetLightColor(ActiveLightColor);
	UpdateVisualState();
}

void AAlienChamber::BeginPlay()
{
	Super::BeginPlay();
	if (!ChamberId.IsValid())
	{
		ChamberId = FGuid::NewGuid();
	}
	SetInfoPanelVisible(false);
}

void AAlienChamber::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (Occupant)
	{
		Occupant->Destroy();
		Occupant = nullptr;
	}
	Super::EndPlay(EndPlayReason);
}

// ---------------------------------------------------------------------------------------------
// Occupant
// ---------------------------------------------------------------------------------------------

AAlienCharacter* AAlienChamber::SpawnAlien(UAlienDataAsset* Data)
{
	RemoveAlien();
	UWorld* World = GetWorld();
	if (!Data || !World)
	{
		return nullptr;
	}

	TSubclassOf<AAlienCharacter> AlienClass = DefaultAlienClass;
	if (!Data->CharacterClass.IsNull())
	{
		if (UClass* Loaded = Data->CharacterClass.LoadSynchronous())
		{
			AlienClass = Loaded;
		}
	}
	if (!AlienClass)
	{
		AlienClass = AAlienCharacter::StaticClass();
	}

	const float Scale = GetChamberScale();
	const float HalfHeight = FMath::Max(Data->Height * 0.5f, 8.f) * Scale;
	FRotator Facing(0.f, GetActorRotation().Yaw, 0.f);
	if (APlayerCameraManager* Camera = UGameplayStatics::GetPlayerCameraManager(this, 0))
	{
		Facing.Yaw = (Camera->GetCameraLocation() - GetActorLocation()).Rotation().Yaw;
	}

	FActorSpawnParameters Params;
	Params.Owner = this;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	const FTransform SpawnTransform(Facing, GetAlienSpawnLocation(HalfHeight), FVector(Scale));
	AAlienCharacter* Alien = World->SpawnActor<AAlienCharacter>(AlienClass, SpawnTransform, Params);
	if (!Alien)
	{
		UE_LOG(LogAlienMuseum, Error, TEXT("%s: failed to spawn alien %s"), *GetName(), *Data->AlienId.ToString());
		return nullptr;
	}

	Alien->InitializeAlien(Data, this);
	Occupant = Alien;
	OccupantData = Data;
	ActiveLightColor = GetDesiredLightColor();
	ApplyLightColor();
	RefreshInfoText();
	SetInfoPanelVisible(true);
	PulseTime = 1.2f; // welcome light pulse
	OnOccupantChanged.Broadcast(this);
	return Alien;
}

void AAlienChamber::RemoveAlien()
{
	if (Occupant)
	{
		Occupant->Destroy();
	}
	const bool bChanged = Occupant != nullptr || OccupantData != nullptr;
	Occupant = nullptr;
	OccupantData = nullptr;
	ActiveLightColor = LightColor;
	ApplyLightColor();
	RefreshInfoText();
	if (bChanged)
	{
		OnOccupantChanged.Broadcast(this);
	}
}

// ---------------------------------------------------------------------------------------------
// Movement boundary
// ---------------------------------------------------------------------------------------------

bool AAlienChamber::IsInsideMovementBounds(const FVector& WorldLocation, float Margin) const
{
	const float Scale = GetChamberScale();
	const FVector Local = GetActorTransform().InverseTransformPosition(WorldLocation);
	const float MaxR = GetMovementRadiusLocal() - Margin / Scale;
	const bool bInsideFlat = Shape == EChamberShape::Square
		? FMath::Max(FMath::Abs(Local.X), FMath::Abs(Local.Y)) <= MaxR
		: Local.Size2D() <= MaxR;
	return bInsideFlat
		&& Local.Z >= BaseHeight - 20.f
		&& Local.Z <= BaseHeight + GlassHeight + 10.f;
}

FVector AAlienChamber::ClampToMovementBounds(const FVector& WorldLocation, float Margin) const
{
	const float Scale = GetChamberScale();
	const FTransform& T = GetActorTransform();
	FVector Local = T.InverseTransformPosition(WorldLocation);
	const float MaxR = FMath::Max(0.f, GetMovementRadiusLocal() - Margin / Scale);
	FVector2D Flat(Local.X, Local.Y);
	if (Shape == EChamberShape::Square)
	{
		Flat.X = FMath::Clamp(Flat.X, -MaxR, MaxR);
		Flat.Y = FMath::Clamp(Flat.Y, -MaxR, MaxR);
	}
	else if (Flat.Size() > MaxR)
	{
		Flat = Flat.GetSafeNormal() * MaxR;
	}
	return T.TransformPosition(FVector(Flat.X, Flat.Y, BaseHeight + 1.f));
}

bool AAlienChamber::FindRandomWanderPoint(const AAlienCharacter* Alien, FRandomStream& Rng, FVector& OutPoint) const
{
	UWorld* World = GetWorld();
	if (!Alien || !World)
	{
		return false;
	}

	const float Scale = GetChamberScale();
	const UCapsuleComponent* Capsule = Alien->GetCapsuleComponent();
	const float CapsuleR = Capsule->GetScaledCapsuleRadius();
	const float CapsuleHH = Capsule->GetScaledCapsuleHalfHeight();
	// Walkable half-size in chamber space (keeps the whole body away from the glass).
	const float MaxLocal = FMath::Max(0.f, GetMovementRadiusLocal() - (CapsuleR + 1.f * Scale) / Scale);
	const FVector Start = Alien->GetActorLocation();
	const FTransform& T = GetActorTransform();

	// Slightly thinner capsule, lifted off the floor so the sweep does not hit the base.
	const FCollisionShape SweepShape = FCollisionShape::MakeCapsule(CapsuleR * 0.9f, CapsuleHH * 0.85f);
	const FVector Lift(0.f, 0.f, 3.f * Scale);
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(AlienWander), false, Alien);

	for (int32 Attempt = 0; Attempt < 10; ++Attempt)
	{
		FVector2D Local;
		if (Shape == EChamberShape::Square)
		{
			Local = FVector2D(Rng.FRandRange(-MaxLocal, MaxLocal), Rng.FRandRange(-MaxLocal, MaxLocal));
		}
		else
		{
			const float Angle = Rng.FRandRange(0.f, 2.f * PI);
			const float Distance = MaxLocal * FMath::Sqrt(Rng.FRand());
			Local = FVector2D(FMath::Cos(Angle) * Distance, FMath::Sin(Angle) * Distance);
		}
		FVector Candidate = T.TransformPosition(FVector(Local.X, Local.Y, BaseHeight));
		Candidate.Z = Start.Z;
		if (FVector::Dist2D(Candidate, Start) < 6.f * Scale)
		{
			continue; // not worth walking to
		}

		FHitResult Hit;
		const bool bBlocked = World->SweepSingleByChannel(Hit, Start + Lift, Candidate + Lift, FQuat::Identity, ECC_Pawn, SweepShape, QueryParams);
		if (!bBlocked)
		{
			OutPoint = Candidate;
			return true;
		}
	}
	return false;
}

FVector AAlienChamber::GetAlienSpawnLocation(float CapsuleHalfHeight) const
{
	return SpawnPoint->GetComponentLocation() + FVector(0.f, 0.f, CapsuleHalfHeight + 1.f);
}

float AAlienChamber::GetFloorZ() const
{
	return GetActorTransform().TransformPosition(FVector(0.f, 0.f, BaseHeight)).Z;
}

float AAlienChamber::GetOuterRadius() const
{
	return Shape == EChamberShape::Square ? GetHalfWidth() * UE_SQRT_2 : GetHalfWidth();
}

float AAlienChamber::GetHalfWidth() const
{
	return (Radius + FrameInset) * GetChamberScale();
}

float AAlienChamber::GetTotalHeight() const
{
	return (BaseHeight + GlassHeight + 6.f) * GetChamberScale();
}

// ---------------------------------------------------------------------------------------------
// Grabbing
// ---------------------------------------------------------------------------------------------

float AAlienChamber::GetChamberScale() const
{
	return FMath::Max(0.05f, static_cast<float>(GetActorScale3D().Z));
}

void AAlienChamber::SetChamberScale(float NewScale)
{
	SetActorScale3D(FVector(FMath::Clamp(NewScale, ScaleRange.X, ScaleRange.Y)));
}

float AAlienChamber::DistanceToChamber(const FVector& WorldPoint) const
{
	const float Scale = GetChamberScale();
	const FVector Local = GetActorTransform().InverseTransformPosition(WorldPoint);
	const float TopZ = BaseHeight + GlassHeight + 6.f;
	const float Half = Radius + FrameInset;
	float Radial;
	if (Shape == EChamberShape::Square)
	{
		const float DX = FMath::Max(0.f, FMath::Abs(static_cast<float>(Local.X)) - Half);
		const float DY = FMath::Max(0.f, FMath::Abs(static_cast<float>(Local.Y)) - Half);
		Radial = FMath::Sqrt(DX * DX + DY * DY);
	}
	else
	{
		Radial = FMath::Max(0.f, static_cast<float>(Local.Size2D()) - Half);
	}
	const float Vertical = Local.Z < 0.f ? -Local.Z : FMath::Max(0.f, Local.Z - TopZ);
	return FMath::Sqrt(Radial * Radial + Vertical * Vertical) * Scale;
}

void AAlienChamber::BeginGrab()
{
	if (bGrabbed)
	{
		return;
	}
	bGrabbed = true;
	bMovedSinceGrab = false;
	GrabStartLocation = GrabTargetLocation = GetActorLocation();
	GrabStartYaw = GrabTargetYaw = GetActorRotation().Yaw;
	GrabStartScale = GrabTargetScale = GetChamberScale();
	if (Occupant)
	{
		Occupant->SetHeld(true);
	}
	UpdateVisualState();
	// OnGrabbed fires from UpdateGrab once the chamber really moves, so a simple click keeps
	// the chamber's spatial anchor.
}

void AAlienChamber::UpdateGrab(const FVector& TargetLocation, float TargetYaw, float TargetScale)
{
	GrabTargetLocation = TargetLocation;
	GrabTargetYaw = TargetYaw;
	GrabTargetScale = FMath::Clamp(TargetScale, ScaleRange.X, ScaleRange.Y);

	if (!bMovedSinceGrab)
	{
		// Small enough to feel instant, big enough to ignore hand jitter during a click.
		const bool bMoved = FVector::Dist(GrabTargetLocation, GrabStartLocation) > 3.f
			|| FMath::Abs(FRotator::NormalizeAxis(GrabTargetYaw - GrabStartYaw)) > 3.f
			|| FMath::Abs(GrabTargetScale - GrabStartScale) > 0.02f;
		if (bMoved)
		{
			bMovedSinceGrab = true;
			OnGrabbed.Broadcast(this);
		}
	}
}

void AAlienChamber::EndGrab()
{
	if (!bGrabbed)
	{
		return;
	}
	bGrabbed = false;
	bLastGrabMoved = bMovedSinceGrab;
	if (bMovedSinceGrab)
	{
		SetActorLocationAndRotation(GrabTargetLocation, FRotator(0.f, GrabTargetYaw, 0.f));
		SetChamberScale(GrabTargetScale);
	}
	else
	{
		// Just a click: put back any jitter so the chamber stays exactly on its anchor.
		SetActorLocationAndRotation(GrabStartLocation, FRotator(0.f, GrabStartYaw, 0.f));
		SetChamberScale(GrabStartScale);
	}
	// The occupant stays attached until NotifyPlaced(), so it rides along if the director
	// still has to adjust the chamber's position.
	UpdateVisualState();
}

void AAlienChamber::SetFloating(bool bInFloating)
{
	bFloating = bInFloating;
	HoverGlow->SetVisibility(bFloating);
}

void AAlienChamber::NotifyPlaced()
{
	if (Occupant && Occupant->IsHeld())
	{
		Occupant->SetHeld(false);
	}
	OnPlaced.Broadcast(this);
}

// ---------------------------------------------------------------------------------------------
// Visuals
// ---------------------------------------------------------------------------------------------

void AAlienChamber::SetHighlighted(bool bInHighlighted, bool bValidTarget)
{
	bHighlighted = bInHighlighted;
	bValidTargetHighlight = bValidTarget;
	UpdateVisualState();
}

void AAlienChamber::UpdateVisualState()
{
	const FLinearColor Invalid(1.f, 0.25f, 0.2f);
	const bool bShowInvalid = bHighlighted && !bValidTargetHighlight;
	const FLinearColor Color = bShowInvalid ? Invalid : ActiveLightColor;
	const float Intensity = bGrabbed ? 2.2f : (bHighlighted ? 2.0f : 1.0f);

	if (GlowMID)
	{
		GlowMID->SetVectorParameterValue(MuseumAssets::Params::Color, Color);
		GlowMID->SetScalarParameterValue(MuseumAssets::Params::Intensity, Intensity);
	}
	if (GlassMID)
	{
		GlassMID->SetVectorParameterValue(MuseumAssets::Params::EdgeColor, Color);
		GlassMID->SetScalarParameterValue(TEXT("EdgeGlow"), bHighlighted || bGrabbed ? 2.0f : 1.2f);
	}
	if (MetalMID)
	{
		MetalMID->SetScalarParameterValue(MuseumAssets::Params::SelfIllum, bGrabbed ? 0.45f : 0.25f);
	}
}

void AAlienChamber::SetInfoPanelVisible(bool bVisible)
{
	bInfoVisible = bVisible;
	InfoRoot->SetVisibility(bVisible, true);
}

void AAlienChamber::ToggleInfoPanel()
{
	SetInfoPanelVisible(!bInfoVisible);
}

void AAlienChamber::RefreshInfoText()
{
	FString Title;
	FString Body;
	if (OccupantData)
	{
		Title = OccupantData->DisplayName.ToString().ToUpper();
		Body = FString::Printf(TEXT("%s  |  %s\n%s"),
			*OccupantData->Species.ToString(),
			*OccupantData->HomePlanet.ToString(),
			*WrapText(OccupantData->Description.ToString(), 44, 3));
	}
	else
	{
		Title = TEXT("EMPTY CHAMBER");
		Body = TEXT("Pick an alien in the collection,\nthen point here and pull the trigger.");
	}
	InfoTitle->SetText(FText::FromString(Title));
	InfoBody->SetText(FText::FromString(Body));
}

void AAlienChamber::FaceInfoPanelToViewer(float DeltaSeconds)
{
	APlayerCameraManager* Camera = UGameplayStatics::GetPlayerCameraManager(this, 0);
	if (!Camera)
	{
		return;
	}
	const FVector ToViewer = Camera->GetCameraLocation() - InfoRoot->GetComponentLocation();
	if (ToViewer.SizeSquared2D() < 1.f)
	{
		return;
	}
	const FRotator Target(0.f, ToViewer.Rotation().Yaw, 0.f);
	InfoRoot->SetWorldRotation(FMath::RInterpTo(InfoRoot->GetComponentRotation(), Target, DeltaSeconds, 6.f));
}

void AAlienChamber::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (bGrabbed)
	{
		const FVector NewLocation = FMath::VInterpTo(GetActorLocation(), GrabTargetLocation, DeltaSeconds, GrabFollowSpeed);
		const FRotator NewRotation = FMath::RInterpTo(GetActorRotation(), FRotator(0.f, GrabTargetYaw, 0.f), DeltaSeconds, GrabFollowSpeed);
		const float NewScale = FMath::FInterpTo(GetChamberScale(), GrabTargetScale, DeltaSeconds, GrabFollowSpeed);
		SetActorLocationAndRotation(NewLocation, NewRotation);
		SetActorScale3D(FVector(NewScale));
	}

	if (bInfoVisible)
	{
		FaceInfoPanelToViewer(DeltaSeconds);
	}

	// Slow "anti-gravity" breathing under a floating chamber.
	if (bFloating && HoverMID)
	{
		HoverTime += DeltaSeconds;
		HoverMID->SetScalarParameterValue(MuseumAssets::Params::Intensity, 1.4f + 0.8f * FMath::Sin(HoverTime * 2.f * PI * 0.4f));
	}

	// Light pulse when a new alien arrives.
	if (PulseTime > 0.f)
	{
		PulseTime = FMath::Max(0.f, PulseTime - DeltaSeconds);
		if (GlowMID)
		{
			const float Pulse = FMath::Abs(FMath::Sin(PulseTime * 2.f * PI * 1.5f)) * PulseTime;
			GlowMID->SetScalarParameterValue(MuseumAssets::Params::Intensity, 1.f + 2.5f * Pulse);
		}
		if (PulseTime <= 0.f)
		{
			UpdateVisualState();
		}
	}
}

// ---------------------------------------------------------------------------------------------
// Pointer interaction
// ---------------------------------------------------------------------------------------------

void AAlienChamber::OnPointerHover(UPrimitiveComponent* HitComponent, bool bHovered)
{
	SetHighlighted(bHovered, true);
}

bool AAlienChamber::OnPointerSelect(UPrimitiveComponent* HitComponent, AMuseumPawn* Pawn)
{
	ToggleInfoPanel();
	return true;
}
