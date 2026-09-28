// Alien Museum - a glass museum case that holds one alien.

#include "Chamber/AlienChamber.h"
#include "Chamber/ChamberHabitatComponent.h"
#include "Aliens/AlienCharacter.h"
#include "Aliens/AlienActionComponent.h"
#include "Aliens/AlienAIController.h"
#include "Data/AlienDataAsset.h"
#include "Core/MuseumAssets.h"
#include "Core/MuseumAudio.h"
#include "Data/ChamberHabitatAsset.h"
#include "Ben10.h"
#include "Components/AudioComponent.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SphereComponent.h"
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
	constexpr float FrameInset = 3.f;   // metal rim beyond the glass
	constexpr float HandleGap = 8.f;    // resize handles float this far outside the frame
	constexpr float MoveInset = 3.f;    // walkable floor stops this far inside the glass
	constexpr float HandleLingerTime = 0.6f;
	constexpr float RemoveConfirmTime = 3.f;
	const FLinearColor RemoveColor(1.f, 0.18f, 0.12f);

	FVector ShapeScale(float SizeX, float SizeY, float SizeZ)
	{
		return FVector(SizeX, SizeY, SizeZ) / 100.f; // basic shapes are 100 cm
	}

	/** Invisible collision that blocks the alien and the habitat's physics props, never the pointer ray. */
	void MakePawnBlocker(UPrimitiveComponent* Component)
	{
		Component->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		Component->SetCollisionObjectType(ECC_WorldDynamic);
		Component->SetCollisionResponseToAllChannels(ECR_Ignore);
		Component->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
		Component->SetCollisionResponseToChannel(ECC_PhysicsBody, ECR_Block);
		Component->SetCanEverAffectNavigation(false);
		Component->SetGenerateOverlapEvents(false);
	}

	/** Something the pointer ray can hit (Visibility channel only). */
	void MakePointerTarget(UPrimitiveComponent* Component)
	{
		Component->SetMobility(EComponentMobility::Movable);
		Component->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		Component->SetCollisionObjectType(ECC_WorldDynamic);
		Component->SetCollisionResponseToAllChannels(ECR_Ignore);
		Component->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
		Component->SetCanEverAffectNavigation(false);
		Component->SetGenerateOverlapEvents(false);
		Component->SetHiddenInGame(true);
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

	// Base: the alien stands on it, so it blocks everything. (Box is the default look;
	// BuildLayout swaps the meshes when Shape is Round.)
	Base = MakeMesh(TEXT("Base"), CubeFinder.Object, Root);
	Base->SetCollisionProfileName(UCollisionProfile::BlockAllDynamic_ProfileName);

	FloorGlow = MakeMesh(TEXT("FloorGlow"), CubeFinder.Object, Root);
	Glass = MakeMesh(TEXT("Glass"), CubeFinder.Object, Root);
	Glass->SetTranslucentSortPriority(1);
	TopCap = MakeMesh(TEXT("TopCap"), CubeFinder.Object, Root);
	LightPanel = MakeMesh(TEXT("LightPanel"), CubeFinder.Object, Root);

	// Anti-gravity glow under the base, shown while the chamber floats in the air.
	HoverGlow = MakeMesh(TEXT("HoverGlow"), CubeFinder.Object, Root);
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

	// The occupant's home world (ground, props, ambient effect), built when an alien moves in.
	Habitat = CreateDefaultSubobject<UChamberHabitatComponent>(TEXT("Habitat"));
	Habitat->SetupAttachment(Root);

	MovementBounds = CreateDefaultSubobject<UBoxComponent>(TEXT("MovementBounds"));
	MovementBounds->SetupAttachment(Root);
	MovementBounds->SetMobility(EComponentMobility::Movable);
	MovementBounds->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	MovementBounds->ShapeColor = FColor(80, 255, 120);
	MovementBounds->SetHiddenInGame(true);

	SpawnPoint = CreateDefaultSubobject<USceneComponent>(TEXT("SpawnPoint"));
	SpawnPoint->SetupAttachment(Root);
	SpawnPoint->SetMobility(EComponentMobility::Movable);

	// What the pointer ray hits. Property and component are named "SelectBox" because older saved
	// Blueprints hold a capsule under the previous name "SelectVolume".
	SelectBox = CreateDefaultSubobject<UBoxComponent>(TEXT("SelectBox"));
	SelectBox->SetupAttachment(Root);
	MakePointerTarget(SelectBox);

	// Resize handles: top edge = height, left/right = width, base front = depth. Each is a knob with
	// two arrows; an invisible sphere around it (bigger than the knob) is what the pointer hits.
	const EChamberResizeAxis HandleAxes[] = { EChamberResizeAxis::Height, EChamberResizeAxis::Width, EChamberResizeAxis::Width, EChamberResizeAxis::Depth };
	const float HandleSides[] = { 1.f, 1.f, -1.f, 1.f };
	for (int32 i = 0; i < static_cast<int32>(UE_ARRAY_COUNT(HandleAxes)); ++i)
	{
		FChamberResizeHandle Handle;
		Handle.Axis = HandleAxes[i];
		Handle.Side = HandleSides[i];
		Handle.Hit = CreateDefaultSubobject<USphereComponent>(*FString::Printf(TEXT("ResizeHandle%d"), i));
		Handle.Hit->SetupAttachment(Root);
		Handle.Hit->InitSphereRadius(8.f);
		MakePointerTarget(Handle.Hit);
		Handle.Hit->SetCollisionEnabled(ECollisionEnabled::NoCollision); // enabled while the handles are shown
		Handle.Knob = MakeMesh(*FString::Printf(TEXT("ResizeKnob%d"), i), SphereFinder.Object, Handle.Hit);
		Handle.Knob->SetRelativeScale3D(FVector(0.045f));
		Handle.ArrowOut = MakeMesh(*FString::Printf(TEXT("ResizeArrowOut%d"), i), ConeFinder.Object, Handle.Hit);
		Handle.ArrowIn = MakeMesh(*FString::Printf(TEXT("ResizeArrowIn%d"), i), ConeFinder.Object, Handle.Hit);
		for (UStaticMeshComponent* Arrow : { Handle.ArrowOut.Get(), Handle.ArrowIn.Get() })
		{
			Arrow->SetRelativeScale3D(ShapeScale(2.6f, 2.6f, 3.2f));
		}
		for (UStaticMeshComponent* Part : { Handle.Knob.Get(), Handle.ArrowOut.Get(), Handle.ArrowIn.Get() })
		{
			Part->SetVisibility(false);
			Part->SetTranslucentSortPriority(4);
		}
		ResizeHandles.Add(Handle);
	}

	// Remove button: a red knob with an X on the top corner, shown with the handles.
	RemoveHit = CreateDefaultSubobject<USphereComponent>(TEXT("RemoveButton"));
	RemoveHit->SetupAttachment(Root);
	RemoveHit->InitSphereRadius(7.f);
	MakePointerTarget(RemoveHit);
	RemoveHit->SetCollisionEnabled(ECollisionEnabled::NoCollision); // enabled while shown
	RemoveKnob = MakeMesh(TEXT("RemoveKnob"), SphereFinder.Object, RemoveHit);
	RemoveKnob->SetRelativeScale3D(FVector(0.055f));
	RemoveKnob->SetTranslucentSortPriority(4);
	RemoveKnob->SetVisibility(false);
	RemoveFace = CreateDefaultSubobject<USceneComponent>(TEXT("RemoveFace"));
	RemoveFace->SetupAttachment(RemoveHit);

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
	InfoBackground->SetRelativeScale3D(FVector(0.20f, 0.42f, 1.f));    // 20 cm tall, 42 cm wide
	InfoBackground->SetTranslucentSortPriority(3);

	auto MakeText = [this](const TCHAR* Name, USceneComponent* Parent, float WorldSize, const FColor& Color)
	{
		UTextRenderComponent* Text = CreateDefaultSubobject<UTextRenderComponent>(Name);
		Text->SetupAttachment(Parent);
		Text->SetMobility(EComponentMobility::Movable);
		Text->SetHorizontalAlignment(EHTA_Center);
		Text->SetVerticalAlignment(EVRTA_TextCenter);
		Text->SetWorldSize(WorldSize);
		Text->SetTextRenderColor(Color);
		Text->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Text->SetCastShadow(false);
		return Text;
	};
	InfoTitle = MakeText(TEXT("InfoTitle"), InfoRoot, 3.0f, FColor(170, 240, 255));
	InfoTitle->SetRelativeLocation(FVector(0.5f, 0.f, 6.2f));
	InfoBody = MakeText(TEXT("InfoBody"), InfoRoot, 1.55f, FColor(220, 245, 255));
	InfoBody->SetRelativeLocation(FVector(0.5f, 0.f, -2.2f));

	SizeLabel = MakeText(TEXT("SizeLabel"), Root, 3.0f, FColor(190, 250, 255));
	SizeLabel->SetVerticalAlignment(EVRTA_TextBottom);
	SizeLabel->SetVisibility(false);

	RemoveMark = MakeText(TEXT("RemoveMark"), RemoveFace, 4.2f, FColor::White);
	RemoveMark->SetText(FText::FromString(TEXT("X")));
	RemoveMark->SetRelativeLocation(FVector(3.9f, 0.f, 0.f)); // just in front of the knob, even when it grows (3.5 cm)
	RemoveMark->SetVisibility(false);
	RemoveLabel = MakeText(TEXT("RemoveLabel"), RemoveFace, 2.6f, FColor(255, 120, 100));
	RemoveLabel->SetText(FText::FromString(TEXT("REMOVE?\npress again")));
	RemoveLabel->SetVerticalAlignment(EVRTA_TextBottom);
	RemoveLabel->SetRelativeLocation(FVector(3.f, 0.f, 5.f));
	RemoveLabel->SetVisibility(false);

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

FVector2f AAlienChamber::GetInnerHalfLocal() const
{
	return Shape == EChamberShape::Round ? FVector2f(Width * 0.5f) : FVector2f(Depth * 0.5f, Width * 0.5f);
}

FVector2f AAlienChamber::GetMoveHalfLocal() const
{
	const FVector2f Half = GetInnerHalfLocal();
	return FVector2f(FMath::Max(0.f, Half.X - MoveInset), FMath::Max(0.f, Half.Y - MoveInset));
}

void AAlienChamber::BuildLayout()
{
	const bool bBox = Shape == EChamberShape::Square;
	const FVector2f Half = GetInnerHalfLocal();
	const FVector2f Inner = Half * 2.f;
	const FVector2f Outer = (Half + FVector2f(FrameInset)) * 2.f;
	const float TopZ = BaseHeight + GlassHeight;

	// Rectangular display case or round pod: same parts, different basic shape.
	UStaticMesh* ShellMesh = bBox ? CubeMesh.Get() : CylinderMesh.Get();
	for (UStaticMeshComponent* Part : { Base.Get(), FloorGlow.Get(), Glass.Get(), TopCap.Get(), LightPanel.Get(), HoverGlow.Get() })
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
	Base->SetRelativeScale3D(ShapeScale(Outer.X, Outer.Y, BaseHeight));

	FloorGlow->SetRelativeLocation(FVector(0.f, 0.f, BaseHeight + 0.2f));
	FloorGlow->SetRelativeScale3D(ShapeScale(Inner.X - 4.f, Inner.Y - 4.f, 0.4f));
	FloorGlow->SetVisibility(!Habitat->HasHabitat()); // a habitat brings its own ground

	Glass->SetRelativeLocation(FVector(0.f, 0.f, BaseHeight + GlassHeight * 0.5f));
	Glass->SetRelativeScale3D(ShapeScale(Inner.X, Inner.Y, GlassHeight));

	TopCap->SetRelativeLocation(FVector(0.f, 0.f, TopZ + 3.f));
	TopCap->SetRelativeScale3D(ShapeScale(Outer.X, Outer.Y, 6.f));

	LightPanel->SetRelativeLocation(FVector(0.f, 0.f, TopZ - 0.6f));
	LightPanel->SetRelativeScale3D(ShapeScale(Inner.X - 10.f, Inner.Y - 10.f, 1.f));

	// Four frame posts, one draw call: corner posts of the box or round pillars.
	if (!IsTemplate())
	{
		FramePillars->ClearInstances();
		for (int32 i = 0; i < 4; ++i)
		{
			FVector2f Corner;
			if (bBox)
			{
				Corner = FVector2f((i & 1) ? -(Half.X + 1.5f) : Half.X + 1.5f, (i & 2) ? -(Half.Y + 1.5f) : Half.Y + 1.5f);
			}
			else
			{
				const float Angle = FMath::DegreesToRadians(45.f + 90.f * i);
				Corner = FVector2f(FMath::Cos(Angle), FMath::Sin(Angle)) * (Half.X + 1.5f);
			}
			FramePillars->AddInstance(FTransform(FQuat::Identity, FVector(Corner.X, Corner.Y, BaseHeight + GlassHeight * 0.5f), ShapeScale(3.f, 3.f, GlassHeight)));
		}
	}

	// Invisible walls just inside the glass: one per side of the box, an octagon for a round pod. They
	// are thick and grow outwards from the same inner face: a body pushed into a thin wall can be
	// resolved out through its far side, a thick one always pushes it back into the case.
	constexpr float WallHalfThickness = 6.f;
	const int32 ActiveWalls = bBox ? 4 : NumWalls;
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
		float Apothem;
		float HalfLength;
		if (bBox)
		{
			const bool bFrontBack = (i % 2) == 0; // walls 0 / 2 face +X / -X
			Apothem = (bFrontBack ? Half.X : Half.Y) - 2.f + WallHalfThickness; // inner face 2 cm inside the glass
			HalfLength = (bFrontBack ? Half.Y : Half.X) + 2.f * WallHalfThickness;
		}
		else
		{
			const float InnerApothem = Half.X - 2.f;
			Apothem = InnerApothem + WallHalfThickness;
			HalfLength = (InnerApothem + 2.f * WallHalfThickness) * FMath::Tan(FMath::DegreesToRadians(180.f / NumWalls)) + 1.f;
		}
		const float Angle = FMath::DegreesToRadians(AngleDeg);
		Wall->SetBoxExtent(FVector(WallHalfThickness, HalfLength, GlassHeight * 0.5f));
		Wall->SetRelativeLocation(FVector(FMath::Cos(Angle) * Apothem, FMath::Sin(Angle) * Apothem, BaseHeight + GlassHeight * 0.5f));
		Wall->SetRelativeRotation(FRotator(0.f, AngleDeg, 0.f));
	}
	Ceiling->SetBoxExtent(FVector(Half.X, Half.Y, WallHalfThickness));
	Ceiling->SetRelativeLocation(FVector(0.f, 0.f, TopZ - 1.f + WallHalfThickness)); // inner face 1 cm under the lid

	// Optional obstacles for the alien to walk around.
	const float Small = FMath::Min(Half.X, Half.Y);
	ObstacleRock->SetRelativeLocation(FVector(-0.45f * Half.X, 0.42f * Half.Y, BaseHeight + 1.5f));
	ObstacleRock->SetRelativeScale3D(ShapeScale(0.34f * Small, 0.28f * Small, 0.20f * Small));
	ObstacleCrystal->SetRelativeLocation(FVector(0.42f * Half.X, -0.45f * Half.Y, BaseHeight + 0.2f * Small));
	ObstacleCrystal->SetRelativeScale3D(ShapeScale(0.16f * Small, 0.16f * Small, 0.40f * Small));
	ObstacleRock->SetVisibility(bShowObstacles);
	ObstacleCrystal->SetVisibility(bShowObstacles);
	ObstacleRock->SetCollisionEnabled(bShowObstacles ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
	ObstacleCrystal->SetCollisionEnabled(bShowObstacles ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);

	const FVector2f Move = GetMoveHalfLocal();
	MovementBounds->SetBoxExtent(FVector(Move.X, Move.Y, GlassHeight * 0.5f));
	MovementBounds->SetRelativeLocation(FVector(0.f, 0.f, BaseHeight + GlassHeight * 0.5f));

	SpawnPoint->SetRelativeLocation(FVector(0.f, 0.f, BaseHeight + 0.5f));

	SelectBox->SetBoxExtent(FVector(Half.X + FrameInset + 2.f, Half.Y + FrameInset + 2.f, (TopZ + 6.f) * 0.5f + 2.f));
	SelectBox->SetRelativeLocation(FVector(0.f, 0.f, (TopZ + 6.f) * 0.5f));

	HoverGlow->SetRelativeLocation(FVector(0.f, 0.f, -0.6f));
	HoverGlow->SetRelativeScale3D(ShapeScale(Outer.X * 0.8f, Outer.Y * 0.8f, 0.6f));

	InteriorLight->SetRelativeLocation(FVector(0.f, 0.f, TopZ - 8.f));
	InteriorLight->SetAttenuationRadius(FMath::Max(Half.X, Half.Y) * 2.5f);
	InteriorLight->SetLightColor(ActiveLightColor);
	InteriorLight->SetVisibility(bUseRealInteriorLight);

	InfoRoot->SetRelativeLocation(FVector(0.f, 0.f, TopZ + 30.f)); // clear of the height handle

	LayoutResizeHandles(Half, TopZ);
}

void AAlienChamber::LayoutResizeHandles(const FVector2f& Half, float TopZ)
{
	for (FChamberResizeHandle& Handle : ResizeHandles)
	{
		FVector Location = FVector::ZeroVector;
		FVector Direction = FVector::UpVector;
		switch (Handle.Axis)
		{
		case EChamberResizeAxis::Width:
			Direction = FVector(0.f, Handle.Side, 0.f);
			Location = FVector(0.f, Handle.Side * (Half.Y + FrameInset + HandleGap), BaseHeight + GlassHeight * 0.5f);
			break;
		case EChamberResizeAxis::Depth:
			Direction = FVector(Handle.Side, 0.f, 0.f);
			Location = FVector(Handle.Side * (Half.X + FrameInset + HandleGap), 0.f, BaseHeight * 0.5f);
			break;
		case EChamberResizeAxis::Height:
		default:
			// Just outside the top edge that faces the viewer, overlapping the case's pointer box: the
			// ray slides from the glass straight onto it (a knob above the middle of the lid cannot be
			// reached from below a tall case).
			Location = FVector(HeightHandleDir.X * (Half.X + FrameInset + 4.f), HeightHandleDir.Y * (Half.Y + FrameInset + 4.f), TopZ + 10.f);
			break;
		}
		Handle.Hit->SetRelativeLocation(Location);
		Handle.ArrowOut->SetRelativeLocationAndRotation(Direction * 5.f, FRotationMatrix::MakeFromZ(Direction).Rotator());
		Handle.ArrowIn->SetRelativeLocationAndRotation(-Direction * 5.f, FRotationMatrix::MakeFromZ(-Direction).Rotator());
	}

	// Remove button: on the same top edge as the height knob, towards the viewer's right corner
	// (like a window's close button).
	const FVector2f Right(HeightHandleDir.Y, -HeightHandleDir.X);
	const FVector2f Edge(HeightHandleDir.X * (Half.X + FrameInset + 4.f), HeightHandleDir.Y * (Half.Y + FrameInset + 4.f));
	const float Along = Shape == EChamberShape::Round ? Half.X * 0.55f
		: FMath::Max(0.f, (FMath::Abs(HeightHandleDir.X) > 0.5f ? Half.Y : Half.X) - 7.f);
	RemoveHit->SetRelativeLocation(FVector(Edge.X + Right.X * Along, Edge.Y + Right.Y * Along, TopZ + 10.f));
}

bool AAlienChamber::IsRemoveButton(const UPrimitiveComponent* Component) const
{
	return Component && Component == RemoveHit;
}

bool AAlienChamber::IsRemoveButtonNear(const FVector& WorldPoint, float MaxDistance) const
{
	return RemoveKnob->IsVisible() && FVector::Dist(RemoveHit->GetComponentLocation(), WorldPoint) <= MaxDistance;
}

bool AAlienChamber::PressRemoveButton()
{
	if (RemoveArmedTime > 0.f)
	{
		RemoveArmedTime = 0.f;
		return true;
	}
	RemoveArmedTime = RemoveConfirmTime;
	RemoveLabel->SetVisibility(true);
	FaceViewer(RemoveFace, 0.f, true);
	UpdateHandleVisuals();
	return false;
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
	HandleMID = EmissiveBase ? UMaterialInstanceDynamic::Create(EmissiveBase, this) : nullptr;
	HandleHotMID = EmissiveBase ? UMaterialInstanceDynamic::Create(EmissiveBase, this) : nullptr;
	RemoveMID = EmissiveBase ? UMaterialInstanceDynamic::Create(EmissiveBase, this) : nullptr;
	if (RemoveMID)
	{
		RemoveMID->SetVectorParameterValue(MuseumAssets::Params::Color, RemoveColor);
		RemoveMID->SetScalarParameterValue(MuseumAssets::Params::Intensity, 1.8f);
		RemoveKnob->SetMaterial(0, RemoveMID);
	}
	if (HoverMID)
	{
		HoverGlow->SetMaterial(0, HoverMID);
	}
	if (HandleMID && HandleHotMID)
	{
		HandleMID->SetVectorParameterValue(MuseumAssets::Params::Color, HandleColor);
		HandleMID->SetScalarParameterValue(MuseumAssets::Params::Intensity, 1.6f);
		HandleHotMID->SetVectorParameterValue(MuseumAssets::Params::Color, FMath::Lerp(HandleColor, FLinearColor::White, 0.5f));
		HandleHotMID->SetScalarParameterValue(MuseumAssets::Params::Intensity, 3.f);
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
	UpdateHandleVisuals();
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
	UpdateAmbience(true);
}

void AAlienChamber::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (Occupant)
	{
		Occupant->Destroy();
		Occupant = nullptr;
	}
	if (AmbienceAudio)
	{
		AmbienceAudio->Stop();
		AmbienceAudio = nullptr;
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

	// A big alien in a small case: grow the case so it fits (the clamp in SetInnerSize does that).
	SetInnerSize(GetInnerSize());

	// Its home world inside the glass. The seed keeps the layout the same for this case every time.
	if (Data->Habitat)
	{
		KeepHabitatClearOfOccupant();
		Habitat->Build(Data->Habitat, GetInnerHalfLocal(), BaseHeight, GlassHeight, static_cast<int32>(GetTypeHash(ChamberId)), Shape == EChamberShape::Round);
		if (bGrabbed)
		{
			Habitat->SetFrozen(true);
		}
	}
	FloorGlow->SetVisibility(!Habitat->HasHabitat());

	// Stand on the (new) ground with the real capsule: a wide model gets a bigger capsule than its
	// data height suggests, and it must not start inside the floor.
	Alien->SetActorLocation(GetAlienSpawnLocation(Alien->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()), false, nullptr, ETeleportType::TeleportPhysics);

	ActiveLightColor = GetDesiredLightColor();
	ApplyLightColor();
	RefreshInfoText();
	SetInfoPanelVisible(true);
	PulseTime = 1.2f; // welcome light pulse
	UpdateAmbience(true);
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
	Habitat->Clear();
	FloorGlow->SetVisibility(true);
	ActiveLightColor = LightColor;
	ApplyLightColor();
	RefreshInfoText();
	if (bChanged)
	{
		if (HasActorBegunPlay())
		{
			UpdateAmbience(true);
		}
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
	const FVector2f Move = GetMoveHalfLocal() - FVector2f(Margin / Scale);
	const bool bInsideFlat = Shape == EChamberShape::Square
		? FMath::Abs(Local.X) <= Move.X && FMath::Abs(Local.Y) <= Move.Y
		: Local.Size2D() <= Move.X;
	return bInsideFlat
		&& Local.Z >= BaseHeight - 20.f
		&& Local.Z <= BaseHeight + GlassHeight + 10.f;
}

FVector AAlienChamber::ClampToMovementBounds(const FVector& WorldLocation, float Margin) const
{
	const float Scale = GetChamberScale();
	const FTransform& T = GetActorTransform();
	const FVector Local = T.InverseTransformPosition(WorldLocation);
	const FVector2f Full = GetMoveHalfLocal();
	const FVector2f Move(FMath::Max(0.f, Full.X - Margin / Scale), FMath::Max(0.f, Full.Y - Margin / Scale));
	FVector2f Flat(static_cast<float>(Local.X), static_cast<float>(Local.Y));
	if (Shape == EChamberShape::Square)
	{
		Flat.X = FMath::Clamp(Flat.X, -Move.X, Move.X);
		Flat.Y = FMath::Clamp(Flat.Y, -Move.Y, Move.Y);
	}
	else if (Flat.Size() > Move.X)
	{
		Flat = Flat.GetSafeNormal() * Move.X;
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
	const float Keep = (CapsuleR + 1.f * Scale) / Scale;
	const FVector2f Move = GetMoveHalfLocal();
	const FVector2f Range(FMath::Max(0.f, Move.X - Keep), FMath::Max(0.f, Move.Y - Keep));
	const FVector Start = Alien->GetActorLocation();
	const FTransform& T = GetActorTransform();

	// Slightly thinner capsule, lifted off the floor so the sweep does not hit the base.
	const FCollisionShape SweepShape = FCollisionShape::MakeCapsule(CapsuleR * 0.9f, CapsuleHH * 0.85f);
	const FVector Lift(0.f, 0.f, 3.f * Scale);
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(AlienWander), false, Alien);
	// Loose habitat props don't count as obstacles: the alien walks into them and pushes them around.
	FCollisionResponseParams Responses;
	Responses.CollisionResponse.SetResponse(ECC_PhysicsBody, ECR_Ignore);

	for (int32 Attempt = 0; Attempt < 10; ++Attempt)
	{
		FVector2f Local;
		if (Shape == EChamberShape::Square)
		{
			Local = FVector2f(Rng.FRandRange(-Range.X, Range.X), Rng.FRandRange(-Range.Y, Range.Y));
		}
		else
		{
			const float Angle = Rng.FRandRange(0.f, 2.f * PI);
			const float Distance = Range.X * FMath::Sqrt(Rng.FRand());
			Local = FVector2f(FMath::Cos(Angle) * Distance, FMath::Sin(Angle) * Distance);
		}
		FVector Candidate = T.TransformPosition(FVector(Local.X, Local.Y, BaseHeight));
		Candidate.Z = Start.Z;
		if (FVector::Dist2D(Candidate, Start) < 6.f * Scale)
		{
			continue; // not worth walking to
		}

		FHitResult Hit;
		const bool bBlocked = World->SweepSingleByChannel(Hit, Start + Lift, Candidate + Lift, FQuat::Identity, ECC_Pawn, SweepShape, QueryParams, Responses);
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
	const float Ground = Habitat->HasHabitat() ? Habitat->GetWalkZ() - BaseHeight : 0.f;
	return SpawnPoint->GetComponentLocation() + FVector(0.f, 0.f, CapsuleHalfHeight + 1.f + Ground * GetChamberScale());
}

float AAlienChamber::GetFloorZ() const
{
	const float WalkZ = Habitat->HasHabitat() ? Habitat->GetWalkZ() : BaseHeight;
	return GetActorTransform().TransformPosition(FVector(0.f, 0.f, WalkZ)).Z;
}

FVector2D AAlienChamber::GetWalkHalfSize() const
{
	return FVector2D(GetMoveHalfLocal());
}

// ---------------------------------------------------------------------------------------------
// Size
// ---------------------------------------------------------------------------------------------

void AAlienChamber::SetInnerSize(const FVector& NewSize)
{
	auto Fit = [this](EChamberResizeAxis Axis, float Value)
	{
		return FMath::Clamp(Value, FMath::Max(SizeRange.X, GetMinSizeAlong(Axis)), FMath::Max(SizeRange.X, SizeRange.Y));
	};
	const FVector OldSize = GetInnerSize();
	Depth = Fit(EChamberResizeAxis::Depth, NewSize.X);
	Width = Fit(EChamberResizeAxis::Width, NewSize.Y);
	GlassHeight = Fit(EChamberResizeAxis::Height, NewSize.Z);
	BuildLayout();

	// A live drag rebuilds the habitat once, when the handle is let go.
	if (!IsBeingResized() && !GetInnerSize().Equals(OldSize, 0.1))
	{
		RebuildHabitat();
	}
}

void AAlienChamber::RebuildHabitat()
{
	if (Habitat->HasHabitat())
	{
		KeepHabitatClearOfOccupant();
		Habitat->Rebuild(GetInnerHalfLocal(), BaseHeight, GlassHeight);
	}
}

void AAlienChamber::UpdateAmbience(bool bRestart)
{
	UMuseumAudio* Audio = UMuseumAudio::Get(this);
	if (!Audio)
	{
		return;
	}
	FVector Listener;
	const bool bBehindGlass = !(Audio->GetListener(Listener) && DistanceToChamber(Listener) <= 0.f);
	if (!bRestart)
	{
		if (AmbienceAudio && bBehindGlass != bAmbienceBehindGlass)
		{
			const UMuseumSoundLibrary* Library = Audio->GetLibrary();
			const float Quieter = Library ? FMath::Max(0.01f, Library->GlassVolume) : 1.f;
			Audio->SetThroughGlass(AmbienceAudio, bBehindGlass, AmbienceAudio->VolumeMultiplier / (bAmbienceBehindGlass ? Quieter : 1.f));
			bAmbienceBehindGlass = bBehindGlass;
		}
		return;
	}

	if (AmbienceAudio)
	{
		AmbienceAudio->FadeOut(0.4f, 0.f);
		AmbienceAudio = nullptr;
	}
	FName Name = TEXT("Case.Hum");
	if (const UChamberHabitatAsset* HomeWorld = OccupantData ? OccupantData->Habitat.Get() : nullptr)
	{
		switch (HomeWorld->Ambient)
		{
		case EHabitatAmbient::Bubbles: Name = TEXT("Amb.Bubbles"); break;
		case EHabitatAmbient::Embers: Name = TEXT("Amb.Embers"); break;
		case EHabitatAmbient::Mist: Name = TEXT("Amb.Mist"); break;
		case EHabitatAmbient::Sparkles: Name = TEXT("Amb.Sparkles"); break;
		case EHabitatAmbient::Spores: Name = TEXT("Amb.Spores"); break;
		case EHabitatAmbient::Pulses: Name = TEXT("Amb.Pulses"); break;
		default: break;
		}
	}
	UMuseumAudio::FPlay How;
	How.AttachTo = GetRootComponent();
	How.Location = GetActorTransform().TransformPosition(FVector(0.f, 0.f, BaseHeight + GlassHeight * 0.35f)); // low in the case
	How.bThroughGlass = bBehindGlass;
	AmbienceAudio = Audio->Play(Name, How);
	bAmbienceBehindGlass = bBehindGlass;
}

// ---------------------------------------------------------------------------------------------
// The glass: taps, ripples, marks
// ---------------------------------------------------------------------------------------------

bool AAlienChamber::GetGlassWallDistance(const FVector& WorldPoint, float& OutDistance, FVector& OutNormal, FVector& OutOnGlass) const
{
	const FTransform& T = GetActorTransform();
	const FVector Local = T.InverseTransformPosition(WorldPoint); // chamber space: cm at scale 1
	if (Local.Z < BaseHeight || Local.Z > BaseHeight + GlassHeight)
	{
		return false; // below the base or above the lid: no glass there
	}
	const FVector2f Half = GetInnerHalfLocal();
	FVector LocalNormal;
	FVector LocalOnGlass = Local;
	float LocalDistance;
	if (Shape == EChamberShape::Round)
	{
		const FVector2D Flat(Local.X, Local.Y);
		const double Radius = Flat.Size();
		const FVector2D Dir = Radius > UE_KINDA_SMALL_NUMBER ? Flat / Radius : FVector2D(1.0, 0.0);
		LocalDistance = static_cast<float>(Radius) - Half.X;
		LocalNormal = FVector(Dir.X, Dir.Y, 0.0);
		LocalOnGlass = FVector(Dir.X * Half.X, Dir.Y * Half.X, Local.Z);
	}
	else
	{
		// The wall the point is closest to going outwards; a point past a corner is beside no wall.
		const float OutX = static_cast<float>(FMath::Abs(Local.X)) - Half.X;
		const float OutY = static_cast<float>(FMath::Abs(Local.Y)) - Half.Y;
		const double SideX = Local.X >= 0.0 ? 1.0 : -1.0;
		const double SideY = Local.Y >= 0.0 ? 1.0 : -1.0;
		if (OutX >= OutY)
		{
			if (OutY > 2.f)
			{
				return false;
			}
			LocalDistance = OutX;
			LocalNormal = FVector(SideX, 0.0, 0.0);
			LocalOnGlass = FVector(SideX * Half.X, FMath::Clamp(Local.Y, -static_cast<double>(Half.Y), static_cast<double>(Half.Y)), Local.Z);
		}
		else
		{
			if (OutX > 2.f)
			{
				return false;
			}
			LocalDistance = OutY;
			LocalNormal = FVector(0.0, SideY, 0.0);
			LocalOnGlass = FVector(FMath::Clamp(Local.X, -static_cast<double>(Half.X), static_cast<double>(Half.X)), SideY * Half.Y, Local.Z);
		}
	}
	OutDistance = LocalDistance * GetChamberScale();
	OutNormal = T.TransformVectorNoScale(LocalNormal).GetSafeNormal();
	OutOnGlass = T.TransformPosition(LocalOnGlass);
	return true;
}

void AAlienChamber::TapGlass(const FVector& WorldPoint, float Strength)
{
	Strength = FMath::Clamp(Strength, 0.f, 1.f);
	float Distance = 0.f;
	FVector Normal;
	FVector OnGlass;
	if (!GetGlassWallDistance(WorldPoint, Distance, Normal, OnGlass))
	{
		OnGlass = WorldPoint;
		Normal = (WorldPoint - GetActorLocation()).GetSafeNormal2D();
	}
	RippleGlass(OnGlass, Normal, 0.35f + 0.65f * Strength, FMath::Lerp(ActiveLightColor, FLinearColor::White, 0.55f));
	if (UMuseumAudio* Audio = UMuseumAudio::Get(this))
	{
		// The glass itself rings: heard as it is, never muffled.
		Audio->PlayAt(Strength >= 0.55f ? TEXT("Glass.Knock") : TEXT("Glass.Tap"), OnGlass, 0.45f + 0.55f * Strength);
	}
	if (Occupant)
	{
		if (AAlienAIController* Brain = Cast<AAlienAIController>(Occupant->GetController()))
		{
			Brain->NotifyGlassTap(OnGlass, Normal, Strength);
		}
	}
	UE_LOG(LogAlienMuseum, Log, TEXT("%s: glass tapped (strength %.2f)"), *GetName(), Strength);
}

int32 AAlienChamber::GetGlassEffectSlot(bool bMark)
{
	int32 Oldest = INDEX_NONE;
	for (int32 i = 0; i < GlassEffects.Num(); ++i)
	{
		if (GlassEffects[i].bMark != bMark)
		{
			continue;
		}
		if (GlassEffects[i].Life < 0.f)
		{
			return i;
		}
		if (Oldest == INDEX_NONE || GlassEffects[i].Age / GlassEffects[i].Life > GlassEffects[Oldest].Age / GlassEffects[Oldest].Life)
		{
			Oldest = i;
		}
	}
	// A few of each are plenty: a new one takes over the one nearest its end.
	const int32 Count = GlassEffects.FilterByPredicate([bMark](const FGlassEffect& Effect) { return Effect.bMark == bMark; }).Num();
	if (Count >= (bMark ? 3 : 9))
	{
		return Oldest;
	}
	UMaterialInterface* EffectMaterial = bMark ? MuseumAssets::FXGlowMaterial() : MuseumAssets::FXRingMaterial();
	UStaticMeshComponent* Mesh = NewObject<UStaticMeshComponent>(this, MakeUniqueObjectName(this, UStaticMeshComponent::StaticClass(), bMark ? TEXT("GlassMark") : TEXT("GlassRing")));
	Mesh->SetStaticMesh(bMark ? MuseumAssets::SphereMesh() : MuseumAssets::PlaneMesh());
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Mesh->SetCastShadow(false);
	Mesh->SetCanEverAffectNavigation(false);
	Mesh->SetupAttachment(GetRootComponent());
	Mesh->RegisterComponent();
	UMaterialInstanceDynamic* MID = EffectMaterial ? UMaterialInstanceDynamic::Create(EffectMaterial, this) : nullptr;
	if (MID)
	{
		Mesh->SetMaterial(0, MID);
	}
	Mesh->SetVisibility(false);
	GlassEffectMeshes.Add(Mesh);
	GlassEffectMIDs.Add(MID);
	FGlassEffect Effect;
	Effect.bMark = bMark;
	return GlassEffects.Add(Effect);
}

void AAlienChamber::RippleGlass(const FVector& WorldPoint, const FVector& Normal, float Strength, const FLinearColor& Color)
{
	const FTransform& T = GetActorTransform();
	const float Scale = GetChamberScale();
	// Three rings, one after another, spreading wider the harder the knock.
	for (int32 Ring = 0; Ring < 3; ++Ring)
	{
		const int32 Slot = GetGlassEffectSlot(false);
		if (Slot == INDEX_NONE)
		{
			return;
		}
		FGlassEffect& Effect = GlassEffects[Slot];
		Effect.LocalPoint = T.InverseTransformPosition(WorldPoint + Normal * 0.4f * Scale); // just off the glass, on the side it came from
		Effect.LocalNormal = T.InverseTransformVectorNoScale(Normal).GetSafeNormal();
		Effect.Age = 0.f;
		Effect.Delay = 0.09f * Ring;
		Effect.Life = 0.55f + 0.15f * Strength;
		Effect.Size0 = 2.f;
		Effect.Size1 = (8.f + 26.f * Strength) * (1.f - 0.22f * Ring);
		Effect.Opacity = (0.45f + 0.4f * Strength) * (1.f - 0.25f * Ring);
		if (UMaterialInstanceDynamic* MID = GlassEffectMIDs[Slot])
		{
			MID->SetVectorParameterValue(MuseumAssets::Params::Color, Color);
			MID->SetScalarParameterValue(MuseumAssets::Params::Intensity, 2.2f);
			MID->SetScalarParameterValue(MuseumAssets::Params::Opacity, 0.f);
		}
	}
}

void AAlienChamber::MarkGlass(const FVector& WorldPoint, const FVector& Normal, float Size, const FLinearColor& Color, float Life, float Opacity)
{
	const int32 Slot = GetGlassEffectSlot(true);
	if (Slot == INDEX_NONE)
	{
		return;
	}
	const FTransform& T = GetActorTransform();
	FGlassEffect& Effect = GlassEffects[Slot];
	Effect.LocalPoint = T.InverseTransformPosition(WorldPoint - Normal * 0.4f * GetChamberScale()); // on the inside of the glass
	Effect.LocalNormal = T.InverseTransformVectorNoScale(Normal).GetSafeNormal();
	Effect.Age = 0.f;
	Effect.Delay = 0.f;
	Effect.Life = FMath::Max(0.5f, Life);
	Effect.Size0 = Effect.Size1 = Size / FMath::Max(0.01f, GetChamberScale());
	Effect.Opacity = FMath::Clamp(Opacity, 0.f, 1.f);
	if (UMaterialInstanceDynamic* MID = GlassEffectMIDs[Slot])
	{
		MID->SetVectorParameterValue(MuseumAssets::Params::Color, Color);
		MID->SetScalarParameterValue(MuseumAssets::Params::Intensity, 0.9f);
		MID->SetScalarParameterValue(MuseumAssets::Params::Softness, 1.2f);
		MID->SetScalarParameterValue(MuseumAssets::Params::Opacity, Effect.Opacity);
	}
}

void AAlienChamber::UpdateGlassEffects(float DeltaSeconds)
{
	for (int32 i = 0; i < GlassEffects.Num(); ++i)
	{
		FGlassEffect& Effect = GlassEffects[i];
		UStaticMeshComponent* Mesh = GlassEffectMeshes[i];
		if (Effect.Life < 0.f || !Mesh)
		{
			continue;
		}
		Effect.Age += DeltaSeconds;
		const float Time = Effect.Age - Effect.Delay;
		if (Time >= Effect.Life)
		{
			Effect.Life = -1.f;
			Mesh->SetVisibility(false);
			continue;
		}
		if (Time < 0.f)
		{
			continue; // a later ring of the ripple, not started yet
		}
		const float A = Time / Effect.Life;
		const FQuat Facing = FRotationMatrix::MakeFromZ(Effect.LocalNormal).ToQuat(); // the plane / flattened sphere lies on the glass
		FVector LocalScale;
		float Opacity;
		if (Effect.bMark)
		{
			LocalScale = FVector(Effect.Size0, Effect.Size0 * 0.8f, Effect.Size0 * 0.08f) / 100.f;
			Opacity = Effect.Opacity * FMath::Min(1.f, (1.f - A) / 0.4f); // stays, then fades
		}
		else
		{
			// Rings are drawn at 80% of a 100 cm plane: a ring's size is its diameter.
			const float Size = FMath::Lerp(Effect.Size0, Effect.Size1, 1.f - (1.f - A) * (1.f - A));
			LocalScale = FVector(Size / 80.f, Size / 80.f, 1.f);
			Opacity = Effect.Opacity * FMath::Min(1.f, A / 0.1f) * (1.f - A);
		}
		Mesh->SetRelativeTransform(FTransform(Facing, Effect.LocalPoint, LocalScale));
		Mesh->SetVisibility(true);
		if (UMaterialInstanceDynamic* MID = GlassEffectMIDs[i])
		{
			MID->SetScalarParameterValue(MuseumAssets::Params::Opacity, Opacity);
		}
	}
}

void AAlienChamber::KeepHabitatClearOfOccupant()
{
	FVector2f Center = FVector2f::ZeroVector;
	float Radius = 0.f;
	if (Occupant)
	{
		const FVector Local = GetActorTransform().InverseTransformPosition(Occupant->GetActorLocation());
		Center = FVector2f(static_cast<float>(Local.X), static_cast<float>(Local.Y));
		Radius = Occupant->GetCapsuleComponent()->GetUnscaledCapsuleRadius() + 2.f;
	}
	Habitat->SetKeepClear(Center, Radius);
}

FVector AAlienChamber::GetOuterSize() const
{
	const FVector2f Half = GetInnerHalfLocal();
	return FVector(2.f * (Half.X + FrameInset), 2.f * (Half.Y + FrameInset), BaseHeight + GlassHeight + 6.f) * GetChamberScale();
}

FVector2D AAlienChamber::GetFootprintHalfSize() const
{
	return FVector2D((GetInnerHalfLocal() + FVector2f(FrameInset)) * GetChamberScale());
}

float AAlienChamber::GetOuterRadius() const
{
	const FVector2D Half = GetFootprintHalfSize();
	return static_cast<float>(Shape == EChamberShape::Round ? Half.X : Half.Size());
}

float AAlienChamber::GetTotalHeight() const
{
	return (BaseHeight + GlassHeight + 6.f) * GetChamberScale();
}

float AAlienChamber::GetSizeAlong(EChamberResizeAxis Axis) const
{
	switch (Axis)
	{
	case EChamberResizeAxis::Width: return Width;
	case EChamberResizeAxis::Depth: return Depth;
	case EChamberResizeAxis::Height: return GlassHeight;
	default: return 0.f;
	}
}

float AAlienChamber::GetMinSizeAlong(EChamberResizeAxis Axis) const
{
	if (!Occupant)
	{
		return 0.f;
	}
	// The alien is scaled together with the chamber, so its unscaled capsule is in chamber space.
	const UCapsuleComponent* Capsule = Occupant->GetCapsuleComponent();
	return Axis == EChamberResizeAxis::Height
		? 2.f * Capsule->GetUnscaledCapsuleHalfHeight() + 8.f
		: 2.f * (Capsule->GetUnscaledCapsuleRadius() + MoveInset) + 6.f;
}

void AAlienChamber::SetSizeAlong(EChamberResizeAxis Axis, float NewSize)
{
	FVector Size = GetInnerSize();
	switch (Axis)
	{
	case EChamberResizeAxis::Width: Size.Y = NewSize; break;
	case EChamberResizeAxis::Depth: Size.X = NewSize; break;
	case EChamberResizeAxis::Height: Size.Z = NewSize; break;
	default: return;
	}
	SetInnerSize(Size);

	// Walls moved in: keep the alien inside the glass.
	if (Occupant && !Occupant->IsHeld())
	{
		const float Radius = Occupant->GetCapsuleComponent()->GetScaledCapsuleRadius();
		const FVector Location = Occupant->GetActorLocation();
		if (!IsInsideMovementBounds(Location, Radius))
		{
			FVector Target = ClampToMovementBounds(Location, Radius);
			Target.Z = Location.Z;
			Occupant->SetActorLocation(Target);
		}
	}
}

// ---------------------------------------------------------------------------------------------
// Resize handles
// ---------------------------------------------------------------------------------------------

const FChamberResizeHandle* AAlienChamber::FindHandle(const UPrimitiveComponent* Component) const
{
	if (!Component)
	{
		return nullptr;
	}
	return ResizeHandles.FindByPredicate([Component](const FChamberResizeHandle& Handle) { return Handle.Hit == Component; });
}

EChamberResizeAxis AAlienChamber::GetResizeAxis(const UPrimitiveComponent* Component) const
{
	const FChamberResizeHandle* Handle = FindHandle(Component);
	return Handle ? Handle->Axis : EChamberResizeAxis::None;
}

UPrimitiveComponent* AAlienChamber::FindResizeHandleNear(const FVector& WorldPoint, float MaxDistance) const
{
	if (bGrabbed)
	{
		return nullptr;
	}
	UPrimitiveComponent* Best = nullptr;
	float BestDistance = MaxDistance;
	for (const FChamberResizeHandle& Handle : ResizeHandles)
	{
		if (Handle.Axis == EChamberResizeAxis::Depth && Shape == EChamberShape::Round)
		{
			continue;
		}
		const float Distance = FVector::Dist(Handle.Hit->GetComponentLocation(), WorldPoint);
		if (Distance <= BestDistance)
		{
			Best = Handle.Hit;
			BestDistance = Distance;
		}
	}
	return Best;
}

FVector AAlienChamber::GetResizeDirection(const UPrimitiveComponent* Handle) const
{
	const FChamberResizeHandle* Found = FindHandle(Handle);
	if (!Found)
	{
		return FVector::ZeroVector;
	}
	FVector Local = FVector::UpVector;
	if (Found->Axis == EChamberResizeAxis::Width)
	{
		Local = FVector(0.f, Found->Side, 0.f);
	}
	else if (Found->Axis == EChamberResizeAxis::Depth)
	{
		Local = FVector(Found->Side, 0.f, 0.f);
	}
	return GetActorTransform().TransformVectorNoScale(Local).GetSafeNormal();
}

void AAlienChamber::BeginResize(const UPrimitiveComponent* Handle)
{
	const FChamberResizeHandle* Found = FindHandle(Handle);
	if (!Found || bGrabbed || IsBeingResized())
	{
		return;
	}
	if (Occupant)
	{
		Occupant->GetActions()->StopAction(); // the walls are about to move
	}
	ResizeAxis = Found->Axis;
	ResizeHandle = Handle;
	ResizeStartSize = GetSizeAlong(ResizeAxis);
	SizeLabel->SetVisibility(true);
	RefreshSizeLabel();
	UpdateHandleVisuals();
}

void AAlienChamber::UpdateResize(float DragDistance)
{
	if (!IsBeingResized())
	{
		return;
	}
	const float Local = DragDistance / GetChamberScale();
	// Width and depth grow on both sides, so the handle stays under the hand; height grows upwards.
	SetSizeAlong(ResizeAxis, ResizeStartSize + (ResizeAxis == EChamberResizeAxis::Height ? Local : 2.f * Local));
	RefreshSizeLabel();
}

void AAlienChamber::EndResize()
{
	if (!IsBeingResized())
	{
		return;
	}
	const bool bChanged = FMath::Abs(GetSizeAlong(ResizeAxis) - ResizeStartSize) > 0.5f;
	ResizeAxis = EChamberResizeAxis::None;
	ResizeHandle = nullptr;
	SizeLabel->SetVisibility(false);
	HandleLinger = HandleLingerTime;
	UpdateHandleVisuals();
	if (bChanged)
	{
		RebuildHabitat();
		OnResized.Broadcast(this);
	}
}

void AAlienChamber::AimHeightHandle()
{
	APlayerCameraManager* Camera = UGameplayStatics::GetPlayerCameraManager(this, 0);
	if (!Camera)
	{
		return;
	}
	const FVector Local = GetActorTransform().InverseTransformPosition(Camera->GetCameraLocation());
	const FVector2f Half = GetInnerHalfLocal();
	FVector2f Dir;
	if (Shape == EChamberShape::Round)
	{
		Dir = FVector2f(static_cast<float>(Local.X), static_cast<float>(Local.Y)).GetSafeNormal();
		if (Dir.IsNearlyZero())
		{
			Dir = FVector2f(1.f, 0.f);
		}
	}
	else
	{
		// The middle of the side the viewer looks at (relative to the box's proportions).
		const float SideX = FMath::Abs(static_cast<float>(Local.X)) / FMath::Max(1.f, Half.X);
		const float SideY = FMath::Abs(static_cast<float>(Local.Y)) / FMath::Max(1.f, Half.Y);
		Dir = SideX >= SideY ? FVector2f(Local.X >= 0.0 ? 1.f : -1.f, 0.f) : FVector2f(0.f, Local.Y >= 0.0 ? 1.f : -1.f);
	}
	if (!Dir.Equals(HeightHandleDir, 0.02f))
	{
		HeightHandleDir = Dir;
		LayoutResizeHandles(Half, BaseHeight + GlassHeight);
	}
}

void AAlienChamber::UpdateHandleVisuals()
{
	const bool bResizing = IsBeingResized();
	const bool bShow = bResizing || ((HoverCount > 0 || HandleLinger > 0.f || RemoveArmedTime > 0.f) && !bGrabbed);

	// Remove button: with the handles, but not while a handle is being dragged.
	const bool bShowRemove = bShow && !bResizing;
	const bool bRemoveHot = HotHandle.Get() == RemoveHit || RemoveArmedTime > 0.f;
	RemoveHit->SetCollisionEnabled(bShowRemove ? ECollisionEnabled::QueryOnly : ECollisionEnabled::NoCollision);
	RemoveKnob->SetVisibility(bShowRemove);
	RemoveMark->SetVisibility(bShowRemove);
	RemoveLabel->SetVisibility(bShowRemove && RemoveArmedTime > 0.f);
	RemoveKnob->SetRelativeScale3D(FVector(bRemoveHot ? 0.07f : 0.055f));
	for (FChamberResizeHandle& Handle : ResizeHandles)
	{
		if (!Handle.Hit)
		{
			continue;
		}
		const bool bUsed = !(Handle.Axis == EChamberResizeAxis::Depth && Shape == EChamberShape::Round);
		const bool bActive = bResizing && ResizeHandle.Get() == Handle.Hit;
		const bool bVisible = bUsed && bShow && (!bResizing || bActive);
		const bool bHot = bActive || (!bResizing && HotHandle.Get() == Handle.Hit);
		Handle.Hit->SetCollisionEnabled(bVisible && !bResizing ? ECollisionEnabled::QueryOnly : ECollisionEnabled::NoCollision);
		UMaterialInterface* Material = bHot ? HandleHotMID.Get() : HandleMID.Get();
		for (UStaticMeshComponent* Part : { Handle.Knob.Get(), Handle.ArrowOut.Get(), Handle.ArrowIn.Get() })
		{
			Part->SetVisibility(bVisible);
			if (Material)
			{
				Part->SetMaterial(0, Material);
			}
		}
		Handle.Knob->SetRelativeScale3D(FVector(bHot ? 0.06f : 0.045f));
	}
}

void AAlienChamber::RefreshSizeLabel()
{
	const TCHAR* Name = ResizeAxis == EChamberResizeAxis::Width ? TEXT("WIDTH")
		: ResizeAxis == EChamberResizeAxis::Depth ? TEXT("DEPTH")
		: TEXT("HEIGHT");
	// Real-world centimetres (the uniform exhibit scale included).
	const float Scale = GetChamberScale();
	SizeLabel->SetText(FText::FromString(FString::Printf(TEXT("%s %.0f cm\n%.0f x %.0f x %.0f cm"),
		Name, GetSizeAlong(ResizeAxis) * Scale, Width * Scale,
		(Shape == EChamberShape::Round ? Width : Depth) * Scale, GlassHeight * Scale)));
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
	const FVector2f Half = GetInnerHalfLocal() + FVector2f(FrameInset);
	float Radial;
	if (Shape == EChamberShape::Square)
	{
		const float DX = FMath::Max(0.f, FMath::Abs(static_cast<float>(Local.X)) - Half.X);
		const float DY = FMath::Max(0.f, FMath::Abs(static_cast<float>(Local.Y)) - Half.Y);
		Radial = FMath::Sqrt(DX * DX + DY * DY);
	}
	else
	{
		Radial = FMath::Max(0.f, static_cast<float>(Local.Size2D()) - Half.X);
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
	EndResize();
	bGrabbed = true;
	bMovedSinceGrab = false;
	HandleLinger = 0.f;
	Habitat->SetFrozen(true); // loose props ride along with the case
	GrabStartLocation = GrabTargetLocation = GetActorLocation();
	GrabStartYaw = GrabTargetYaw = GetActorRotation().Yaw;
	GrabStartScale = GrabTargetScale = GetChamberScale();
	if (Occupant)
	{
		Occupant->SetHeld(true);
	}
	UpdateVisualState();
	UpdateHandleVisuals();
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
	UpdateHandleVisuals();
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
	Habitat->SetFrozen(false);
	// The alien rode along while attached; start tracking unaided moves from here.
	LastTransform = GetActorTransform();
	bHasLastTransform = true;
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
		if (!OccupantData->ModelCredit.IsEmpty())
		{
			Body += TEXT("\n") + WrapText(OccupantData->ModelCredit.ToString(), 52, 1);
		}
	}
	else
	{
		Title = TEXT("EMPTY CHAMBER");
		Body = TEXT("Pick an alien in the collection,\nthen point here and pull the trigger.");
	}
	InfoTitle->SetText(FText::FromString(Title));
	InfoBody->SetText(FText::FromString(Body));
}

void AAlienChamber::CarryOccupantAlong()
{
	const FTransform Current = GetActorTransform();
	// While carried the alien and the loose props are attached and ride along by themselves. Otherwise
	// the case can still be moved without a hand - spatial-anchor corrections, settling - and the
	// walking alien and the simulating props must go with it.
	const bool bMoved = bHasLastTransform
		&& (!Current.GetLocation().Equals(LastTransform.GetLocation(), 0.05)
			|| !Current.GetRotation().Equals(LastTransform.GetRotation(), 1.e-4));
	if (bMoved && Occupant && !Occupant->IsHeld())
	{
		const FVector Local = LastTransform.InverseTransformPositionNoScale(Occupant->GetActorLocation());
		const float YawDelta = FRotator::NormalizeAxis(Current.Rotator().Yaw - LastTransform.Rotator().Yaw);
		Occupant->SetActorLocationAndRotation(Current.TransformPositionNoScale(Local),
			FRotator(0.f, Occupant->GetActorRotation().Yaw + YawDelta, 0.f), false, nullptr, ETeleportType::TeleportPhysics);
	}
	if (bMoved && !bGrabbed)
	{
		Habitat->CarryLooseProps(LastTransform, Current);
	}
	LastTransform = Current;
	bHasLastTransform = true;
}

void AAlienChamber::FaceViewer(USceneComponent* Component, float DeltaSeconds, bool bInstant) const
{
	APlayerCameraManager* Camera = UGameplayStatics::GetPlayerCameraManager(this, 0);
	if (!Camera || !Component)
	{
		return;
	}
	const FVector ToViewer = Camera->GetCameraLocation() - Component->GetComponentLocation();
	if (ToViewer.SizeSquared2D() < 1.f)
	{
		return;
	}
	const FRotator Target(0.f, ToViewer.Rotation().Yaw, 0.f);
	Component->SetWorldRotation(bInstant ? Target : FMath::RInterpTo(Component->GetComponentRotation(), Target, DeltaSeconds, 6.f));
}

void AAlienChamber::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	CarryOccupantAlong();

	AmbienceTimer -= DeltaSeconds;
	if (AmbienceTimer <= 0.f)
	{
		AmbienceTimer = 0.25f;
		UpdateAmbience(false);
	}
	UpdateGlassEffects(DeltaSeconds);

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
		FaceViewer(InfoRoot, DeltaSeconds);
	}

	if (HandleLinger > 0.f)
	{
		HandleLinger -= DeltaSeconds;
		if (HandleLinger <= 0.f)
		{
			HandleLinger = 0.f;
			UpdateHandleVisuals();
		}
	}

	// The remove button reads from anywhere; armed, it pulses until confirmed or it times out.
	if (RemoveKnob->IsVisible())
	{
		FaceViewer(RemoveFace, DeltaSeconds, true);
	}
	if (RemoveArmedTime > 0.f)
	{
		RemoveArmedTime -= DeltaSeconds;
		if (RemoveMID)
		{
			RemoveMID->SetScalarParameterValue(MuseumAssets::Params::Intensity, 2.f + 2.f * FMath::Abs(FMath::Sin(RemoveArmedTime * 2.f * PI * 1.5f)));
		}
		if (RemoveArmedTime <= 0.f)
		{
			RemoveArmedTime = 0.f;
			if (RemoveMID)
			{
				RemoveMID->SetScalarParameterValue(MuseumAssets::Params::Intensity, 1.8f);
			}
			UpdateHandleVisuals();
		}
	}

	// Size read-out follows the handle being dragged.
	if (IsBeingResized())
	{
		if (const UPrimitiveComponent* Handle = ResizeHandle.Get())
		{
			SizeLabel->SetWorldLocation(Handle->GetComponentLocation() + FVector(0.f, 0.f, 9.f * GetChamberScale()));
			FaceViewer(SizeLabel, DeltaSeconds, true);
		}
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
	// Both hands may point at the chamber; the handles stay while at least one does, and a moment
	// longer so the pointer can cross the gap between the glass and a knob.
	const bool bWereShown = HoverCount > 0 || HandleLinger > 0.f;
	HoverCount = FMath::Max(0, HoverCount + (bHovered ? 1 : -1));
	if (HoverCount == 0 && !bHovered)
	{
		HandleLinger = HandleLingerTime;
	}
	else if (HoverCount > 0 && !bWereShown && !IsBeingResized())
	{
		AimHeightHandle();
	}
	if (GetResizeAxis(HitComponent) != EChamberResizeAxis::None || IsRemoveButton(HitComponent))
	{
		if (bHovered)
		{
			HotHandle = HitComponent;
		}
		else if (HotHandle.Get() == HitComponent)
		{
			HotHandle = nullptr;
		}
	}
	SetHighlighted(HoverCount > 0, true);
	UpdateHandleVisuals();
}

bool AAlienChamber::OnPointerSelect(UPrimitiveComponent* HitComponent, AMuseumPawn* Pawn)
{
	ToggleInfoPanel();
	return true;
}
