// Alien Museum - an autonomous alien that lives inside an AAlienChamber.

#include "Aliens/AlienCharacter.h"
#include "Aliens/AlienAppearanceComponent.h"
#include "Aliens/AlienActionComponent.h"
#include "Aliens/AlienAIController.h"
#include "Chamber/AlienChamber.h"
#include "Data/AlienDataAsset.h"
#include "Ben10.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Core/MuseumAssets.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/StaticMesh.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	// Loose habitat props weigh grams, not the ~100 kg bodies the engine defaults are tuned for.
	// They overlap the alien instead of blocking it (a prop wedged against a body that nearly fills
	// the case could otherwise push it through the glass); the repulsion force shoves them away.
	constexpr float PushImpulse = 6.f;   // blocking bodies: first touch (applied as a one-frame force x30)
	constexpr float PushForce = 120.f;   // blocking bodies: while pushing
	constexpr float Repulsion = 0.6f;    // overlapping props: x the movement component's Mass (100) = force
}

AAlienCharacter::AAlienCharacter()
{
	PrimaryActorTick.bCanEverTick = true;

	UCapsuleComponent* Capsule = GetCapsuleComponent();
	Capsule->InitCapsuleSize(10.f, 16.f);
	Capsule->SetCollisionProfileName(UCollisionProfile::Pawn_ProfileName);
	Capsule->SetCollisionResponseToChannel(ECC_PhysicsBody, ECR_Overlap); // loose props: pushed, never wedged
	Capsule->SetGenerateOverlapEvents(true);
	Capsule->SetCanEverAffectNavigation(false);

	// The placeholder uses static-mesh parts; the skeletal mesh slot stays free for real alien art.
	GetMesh()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	GetMesh()->SetRelativeLocation(FVector(0.f, 0.f, -16.f));

	Appearance = CreateDefaultSubobject<UAlienAppearanceComponent>(TEXT("Appearance"));
	Appearance->SetupAttachment(Capsule);
	Appearance->SetRelativeLocation(FVector(0.f, 0.f, -16.f));

	Actions = CreateDefaultSubobject<UAlienActionComponent>(TEXT("Actions"));

	static ConstructorHelpers::FObjectFinder<UStaticMesh> PlaneFinder(TEXT("/Engine/BasicShapes/Plane.Plane"));
	ContactShadow = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ContactShadow"));
	ContactShadow->SetupAttachment(Capsule);
	ContactShadow->SetStaticMesh(PlaneFinder.Object);
	ContactShadow->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	ContactShadow->SetCastShadow(false);
	ContactShadow->SetCanEverAffectNavigation(false);
	ContactShadow->SetRelativeLocation(FVector(0.f, 0.f, -15.7f));
	ContactShadow->SetRelativeScale3D(FVector(0.2f));

	bUseControllerRotationYaw = false;
	bUseControllerRotationPitch = false;
	bUseControllerRotationRoll = false;

	UCharacterMovementComponent* Move = GetCharacterMovement();
	Move->bOrientRotationToMovement = true;
	Move->RotationRate = FRotator(0.f, 300.f, 0.f);
	Move->MaxWalkSpeed = 22.f;
	Move->MaxAcceleration = 250.f;
	Move->BrakingDecelerationWalking = 400.f;
	Move->bCanWalkOffLedges = false;
	Move->MaxStepHeight = 3.f;
	Move->JumpZVelocity = 160.f;
	Move->AirControl = 0.2f;
	Move->bUseRVOAvoidance = false;
	Move->SetCanEverAffectNavigation(false);

	// Walking into loose props pushes them (real physics), gently enough for pebbles and fruit.
	Move->bEnablePhysicsInteraction = true;
	Move->bPushForceScaledToMass = false;
	Move->InitialPushForceFactor = PushImpulse;
	Move->PushForceFactor = PushForce;
	Move->RepulsionForce = Repulsion;
	Move->StandingDownwardForceScale = 0.01f;

	// Engine default: pushed out of an overlap by up to 5 m in one go, e.g. when a prop gets wedged
	// against it - enough to pop the alien through the glass. A few cm at a time is plenty in a case.
	Move->MaxDepenetrationWithGeometry = 8.f;
	Move->MaxDepenetrationWithGeometryAsProxy = 8.f;
	Move->MaxDepenetrationWithPawn = 8.f;

	AIControllerClass = AAlienAIController::StaticClass();
	AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;
}

void AAlienCharacter::BeginPlay()
{
	Super::BeginPlay();

	// Aliens placed by hand in a level (not spawned by a chamber) still build their body.
	if (!bAppearanceBuilt && AlienData)
	{
		InitializeAlien(AlienData, HomeChamber.Get());
	}
}

void AAlienCharacter::InitializeAlien(UAlienDataAsset* InData, AAlienChamber* InChamber)
{
	AlienData = InData;
	HomeChamber = InChamber;
	if (!AlienData)
	{
		UE_LOG(LogAlienMuseum, Warning, TEXT("%s: InitializeAlien called without data"), *GetName());
		return;
	}

	// Build the placeholder only when no real skeletal mesh was assigned in a Blueprint subclass.
	float ModelHeight = AlienData->Height;
	float CollisionRadius = AlienData->Height * 0.24f;
	float ShadowRadius = AlienData->Height * 0.3f;
	if (!GetMesh()->GetSkeletalMeshAsset())
	{
		Appearance->BuildAppearance(AlienData);
		Appearance->PlayMaterialize(0.6f);
		ModelHeight = Appearance->GetModelHeight();
		CollisionRadius = Appearance->GetCollisionRadius();
		ShadowRadius = Appearance->GetShadowRadius();
	}
	bAppearanceBuilt = true;

	// Imported models can be wide (tails, spread arms): allow a bigger capsule so they stay inside the glass.
	const float Radius = FMath::Clamp(CollisionRadius, 6.f, Appearance->IsModel() ? 30.f : 20.f);
	const float HalfHeight = FMath::Max(Radius, ModelHeight * 0.5f);
	GetCapsuleComponent()->SetCapsuleSize(Radius, HalfHeight);
	Appearance->SetBaseLocation(FVector(0.f, 0.f, -HalfHeight));
	GetMesh()->SetRelativeLocation(FVector(0.f, 0.f, -HalfHeight));

	// Contact shadow: a soft disc just above the floor, a bit wider than the feet area.
	ContactShadowScale = FVector(ShadowRadius * 2.6f / 100.f, ShadowRadius * 2.6f / 100.f, 1.f);
	ContactShadow->SetRelativeLocation(FVector(0.f, 0.f, -HalfHeight + 0.3f));
	ContactShadow->SetRelativeScale3D(ContactShadowScale);
	if (UMaterialInterface* ShadowMaterial = MuseumAssets::BlobShadowMaterial())
	{
		ContactShadow->SetMaterial(0, ShadowMaterial);
	}

	ApplyScaleDependentSettings();
	Actions->Setup(AlienData);

	UE_LOG(LogAlienMuseum, Log, TEXT("Alien %s initialised (height %.0f cm, chamber %s)"),
		*AlienData->AlienId.ToString(), ModelHeight, InChamber ? *InChamber->GetName() : TEXT("none"));
}

float AAlienCharacter::GetScaleFactor() const
{
	return FMath::Max(0.1f, static_cast<float>(GetActorScale3D().Z));
}

void AAlienCharacter::ApplyScaleDependentSettings()
{
	const float Scale = GetScaleFactor();
	UCharacterMovementComponent* Move = GetCharacterMovement();
	const float BaseSpeed = AlienData ? AlienData->WalkSpeed : 22.f;
	Move->MaxWalkSpeed = BaseSpeed * Scale * SpeedMultiplier;
	Move->MaxAcceleration = 250.f * Scale * SpeedMultiplier;
	Move->BrakingDecelerationWalking = 400.f * Scale * SpeedMultiplier;
	Move->RotationRate = FRotator(0.f, FMath::Min(300.f * SpeedMultiplier, 1080.f), 0.f);
	Move->MaxStepHeight = 3.f * Scale;
	Move->JumpZVelocity = 160.f * FMath::Sqrt(Scale);
}

void AAlienCharacter::SetSpeedMultiplier(float Multiplier)
{
	SpeedMultiplier = FMath::Clamp(Multiplier, 0.1f, 20.f);
	ApplyScaleDependentSettings();
}

void AAlienCharacter::SetPushStrength(float Multiplier)
{
	UCharacterMovementComponent* Move = GetCharacterMovement();
	Move->InitialPushForceFactor = PushImpulse * Multiplier;
	Move->PushForceFactor = PushForce * Multiplier;
	Move->RepulsionForce = Repulsion * Multiplier;
}

void AAlienCharacter::SetContactShadowScale(float Factor)
{
	ContactShadow->SetRelativeScale3D(FVector(ContactShadowScale.X * Factor, ContactShadowScale.Y * Factor, 1.f));
	ContactShadow->SetVisibility(Factor > 0.05f);
}

void AAlienCharacter::MoveToPoint(const FVector& WorldTarget, float SpeedScale)
{
	if (bIsHeld)
	{
		return;
	}
	MoveTarget = WorldTarget;
	MoveSpeedScale = FMath::Clamp(SpeedScale, 0.1f, 1.f);
	bHasMoveTarget = true;
	bHasDesiredYaw = false;
	bStuck = false;
	StuckTime = 0.f;
	ProgressTimer = 0.f;
	LastDistance = FVector::Dist2D(GetActorLocation(), WorldTarget);
}

void AAlienCharacter::StopMoving()
{
	bHasMoveTarget = false;
	bStuck = false;
	StuckTime = 0.f;
}

void AAlienCharacter::TurnToward(const FVector& WorldPoint)
{
	const FVector To = WorldPoint - GetActorLocation();
	if (To.SizeSquared2D() > 1.f)
	{
		DesiredYaw = To.Rotation().Yaw;
		bHasDesiredYaw = true;
	}
}

void AAlienCharacter::SetLookTarget(const FVector& WorldPoint)
{
	LookTarget = WorldPoint;
	bHasLookTarget = true;
}

void AAlienCharacter::ClearLookTarget()
{
	bHasLookTarget = false;
}

void AAlienCharacter::Hop()
{
	UCharacterMovementComponent* Move = GetCharacterMovement();
	if (!bIsHeld && Move->IsMovingOnGround())
	{
		LaunchCharacter(FVector(0.f, 0.f, Move->JumpZVelocity), false, true);
	}
}

void AAlienCharacter::SetHeld(bool bHeld)
{
	if (bIsHeld == bHeld)
	{
		return;
	}
	bIsHeld = bHeld;
	UCharacterMovementComponent* Move = GetCharacterMovement();

	if (bHeld)
	{
		Actions->StopAction();
		StopMoving();
		Move->StopMovementImmediately();
		Move->DisableMovement();
		if (AAlienChamber* Chamber = HomeChamber.Get())
		{
			AttachToActor(Chamber, FAttachmentTransformRules::KeepWorldTransform);
		}
	}
	else
	{
		DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
		SetActorRotation(FRotator(0.f, GetActorRotation().Yaw, 0.f));
		Move->SetMovementMode(MOVE_Walking);
		ApplyScaleDependentSettings();
	}

	if (AAlienAIController* Brain = Cast<AAlienAIController>(GetController()))
	{
		Brain->NotifyHeld(bHeld);
	}
}

void AAlienCharacter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	UpdateMovement(DeltaSeconds);
	UpdateTurning(DeltaSeconds);
	UpdateContainment(DeltaSeconds);

	if (Appearance)
	{
		Appearance->UpdateMaterialize(DeltaSeconds);
	}

	// Only animate when someone can see it (the procedural animation touches several components).
	if (Appearance && Appearance->HasParts() && WasRecentlyRendered(0.25f))
	{
		const float MaxSpeed = FMath::Max(1.f, GetCharacterMovement()->MaxWalkSpeed);
		const float SpeedAlpha = GetVelocity().Size2D() / MaxSpeed;
		Appearance->UpdateAnimation(DeltaSeconds, SpeedAlpha, bHasLookTarget ? &LookTarget : nullptr, bExcited);
	}
}

void AAlienCharacter::UpdateMovement(float DeltaSeconds)
{
	if (!bHasMoveTarget || bIsHeld)
	{
		return;
	}

	FVector ToTarget = MoveTarget - GetActorLocation();
	ToTarget.Z = 0.f;
	const float Distance = ToTarget.Size();
	const float AcceptRadius = FMath::Max(2.f, GetCapsuleComponent()->GetScaledCapsuleRadius() * 0.4f);
	if (Distance <= AcceptRadius)
	{
		StopMoving();
		return;
	}

	AddMovementInput(ToTarget / Distance, MoveSpeedScale);

	// Stuck detection: less than 1 cm progress per half second for 1.5 s.
	ProgressTimer += DeltaSeconds;
	if (ProgressTimer >= 0.5f)
	{
		StuckTime = (LastDistance - Distance < 1.f * GetScaleFactor()) ? StuckTime + ProgressTimer : 0.f;
		LastDistance = Distance;
		ProgressTimer = 0.f;
		bStuck = StuckTime >= 1.5f;
	}
}

void AAlienCharacter::UpdateTurning(float DeltaSeconds)
{
	if (!bHasDesiredYaw || bHasMoveTarget || bIsHeld)
	{
		return;
	}
	const FRotator Current = GetActorRotation();
	const FRotator Target(0.f, DesiredYaw, 0.f);
	const FRotator NewRot = FMath::RInterpConstantTo(Current, Target, DeltaSeconds, 160.f);
	SetActorRotation(NewRot);
	if (FMath::Abs(FRotator::NormalizeAxis(NewRot.Yaw - DesiredYaw)) < 1.f)
	{
		bHasDesiredYaw = false;
	}
}

void AAlienCharacter::UpdateContainment(float DeltaSeconds)
{
	ContainmentTimer += DeltaSeconds;
	if (ContainmentTimer < 0.5f)
	{
		return;
	}
	ContainmentTimer = 0.f;

	AAlienChamber* Chamber = HomeChamber.Get();
	if (!Chamber || bIsHeld || Chamber->IsBeingGrabbed())
	{
		return;
	}

	const FVector Location = GetActorLocation();
	const float Scale = GetScaleFactor();
	const bool bOutside = !Chamber->IsInsideMovementBounds(Location, -8.f * Scale);
	const bool bFellThrough = Location.Z < Chamber->GetFloorZ() - 25.f * Scale;
	if (bOutside || bFellThrough)
	{
		const FVector Local = Chamber->GetActorTransform().InverseTransformPosition(Location);
		UE_LOG(LogAlienMuseum, Warning, TEXT("%s (%s) %s its chamber at local %s (inside %s cm), returning it to the spawn point"),
			*GetName(), AlienData ? *AlienData->AlienId.ToString() : TEXT("?"), bFellThrough ? TEXT("fell through") : TEXT("left"),
			*Local.ToCompactString(), *Chamber->GetInnerSize().ToCompactString());
		StopMoving();
		GetCharacterMovement()->StopMovementImmediately();
		TeleportTo(Chamber->GetAlienSpawnLocation(GetCapsuleComponent()->GetScaledCapsuleHalfHeight()), FRotator(0.f, GetActorRotation().Yaw, 0.f));
	}
}
