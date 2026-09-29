// Alien Museum - an autonomous alien that lives inside an AAlienChamber.

#include "Aliens/AlienCharacter.h"
#include "Aliens/AlienAppearanceComponent.h"
#include "Aliens/AlienActionComponent.h"
#include "Aliens/AlienAIController.h"
#include "Aliens/AlienSoundComponent.h"
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
#include "Camera/PlayerCameraManager.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
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
	Sounds = CreateDefaultSubobject<UAlienSoundComponent>(TEXT("Sounds"));

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

	const FVector2D Capsule = GetCapsuleSize(CollisionRadius, ModelHeight, Appearance->IsModel());
	GetCapsuleComponent()->SetCapsuleSize(Capsule.X, Capsule.Y);
	CapsuleHalfHeightLocal = Capsule.Y;

	// Contact shadow: a soft disc just above the floor, a bit wider than the feet area.
	ContactShadowScale = FVector(ShadowRadius * 2.6f / 100.f, ShadowRadius * 2.6f / 100.f, 1.f);
	ContactShadow->SetRelativeScale3D(ContactShadowScale);
	if (UMaterialInterface* ShadowMaterial = MuseumAssets::BlobShadowMaterial())
	{
		ContactShadow->SetMaterial(0, ShadowMaterial);
	}

	ApplyScaleDependentSettings();
	Actions->Setup(AlienData);
	Sounds->Setup(AlienData);

	UE_LOG(LogAlienMuseum, Log, TEXT("Alien %s initialised (height %.0f cm, chamber %s)"),
		*AlienData->AlienId.ToString(), ModelHeight, InChamber ? *InChamber->GetName() : TEXT("none"));
}

FVector2D AAlienCharacter::GetCapsuleSize(float CollisionRadius, float BodyHeight, bool bModel)
{
	// Imported models can be wide (tails, spread arms - Wildmutt's reach out over half again his height): allow a
	// bigger capsule so they stay inside the glass.
	const float Radius = FMath::Clamp(CollisionRadius, 6.f, bModel ? 60.f : 20.f);
	return FVector2D(Radius, FMath::Max(Radius, BodyHeight * 0.5f));
}

FVector2D AAlienCharacter::GetCapsuleSizeFor(const UAlienDataAsset* Data)
{
	// A shape-built body only knows its size once built; an imported model's comes from its mesh.
	const UStaticMesh* Mesh = Data && Data->HasModel() ? Data->ModelMesh.LoadSynchronous() : nullptr;
	return Mesh ? GetCapsuleSize(UAlienAppearanceComponent::GetModelCollisionRadius(Data, Mesh), Data->Height, true) : FVector2D::ZeroVector;
}

float AAlienCharacter::GetCapsuleOverhang() const
{
	return AlienData ? FMath::Max(0.f, 2.f * GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight() - AlienData->Height) : 0.f;
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
	PlaceBodyOnFloor();
}

void AAlienCharacter::PlaceBodyOnFloor()
{
	// The movement component keeps the capsule about 2 cm above the floor (world cm, whatever the
	// scale); the feet and the shadow go down by that much so the alien really stands on the ground.
	const float FloorGap = 0.5f * (UCharacterMovementComponent::MIN_FLOOR_DIST + UCharacterMovementComponent::MAX_FLOOR_DIST) / GetScaleFactor();
	const float Bottom = -CapsuleHalfHeightLocal - FloorGap;
	Appearance->SetBaseLocation(FVector(0.f, 0.f, Bottom));
	GetMesh()->SetRelativeLocation(FVector(0.f, 0.f, Bottom));
	ContactShadow->SetRelativeLocation(FVector(0.f, 0.f, Bottom + 0.3f));
	if (TargetRing)
	{
		TargetRing->SetRelativeLocation(FVector(0.f, 0.f, Bottom + 0.5f));
	}
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
	if (IsHeld())
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
	if (!IsHeld() && Move->IsMovingOnGround())
	{
		LaunchCharacter(FVector(0.f, 0.f, Move->JumpZVelocity), false, true);
	}
}

void AAlienCharacter::SetHeld(bool bHeld)
{
	if (bIsHeld == bHeld || IsOutOfCase())
	{
		return; // out of its case (in the player's hand or flying home): the case moves without it
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

	if (bExamined)
	{
		UpdateExamine(DeltaSeconds);
	}
	else if (bReturning)
	{
		UpdateReturn(DeltaSeconds);
	}
	if (TargetRing && TargetRing->IsVisible())
	{
		TargetTime += DeltaSeconds;
		const float Pulse = 0.5f + 0.5f * FMath::Sin(TargetTime * 6.f);
		if (TargetRingMaterial)
		{
			TargetRingMaterial->SetScalarParameterValue(MuseumAssets::Params::Opacity, 0.45f + 0.4f * Pulse);
		}
		TargetRing->SetRelativeScale3D(ContactShadowScale * 1.25f * (1.f + 0.06f * Pulse));
	}

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
	if (!bHasMoveTarget || IsHeld())
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
	if (!bHasDesiredYaw || bHasMoveTarget || IsHeld())
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
	if (!Chamber || IsHeld() || Chamber->IsBeingGrabbed())
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

// ---------------------------------------------------------------------------------------------
// Taken out of the case by the player
// ---------------------------------------------------------------------------------------------

float AAlienCharacter::GetCaseScale() const
{
	const AAlienChamber* Chamber = HomeChamber.Get();
	return Chamber ? Chamber->GetChamberScale() : GetScaleFactor();
}

void AAlienCharacter::BeginExamine()
{
	if (bExamined)
	{
		return;
	}
	if (bIsHeld)
	{
		SetHeld(false);
	}
	Actions->StopAction();
	StopMoving();
	bReturning = false;
	bExamined = true;
	bCelebrateOnLanding = false;
	UCharacterMovementComponent* Move = GetCharacterMovement();
	Move->StopMovementImmediately();
	Move->DisableMovement();
	GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	ContactShadow->SetVisibility(false);
	SetTargeted(false);
	SetExcited(true);
	ExamineLocation = GetActorLocation();
	ExamineRotation = GetActorQuat();
	ExamineZoom = 1.f;
	if (AAlienAIController* Brain = Cast<AAlienAIController>(GetController()))
	{
		Brain->NotifyHeld(true);
	}
	Sounds->PlayEffect(TEXT("Alien.PickUp"), 0.8f);
	Sounds->PlayVoice(EAlienVoice::Held);
}

void AAlienCharacter::SetExamineTarget(const FVector& Location, const FQuat& Rotation, float Zoom)
{
	ExamineLocation = Location;
	ExamineRotation = Rotation;
	ExamineZoom = FMath::Clamp(Zoom, 0.25f, 5.f);
}

void AAlienCharacter::UpdateExamine(float DeltaSeconds)
{
	// Follow the hand smoothly (a ray pick-up flies to the hand this way).
	const float Alpha = 1.f - FMath::Exp(-16.f * DeltaSeconds);
	SetActorLocationAndRotation(FMath::Lerp(GetActorLocation(), ExamineLocation, Alpha),
		FQuat::Slerp(GetActorQuat(), ExamineRotation, Alpha), false, nullptr, ETeleportType::TeleportPhysics);
	// In the hand it is a figure of itself: a life-size giant shrinks to at most HeldHeight (it grows back in its case).
	const float Model = AlienData ? FMath::Max(1.f, AlienData->Height) : 40.f;
	const float Held = FMath::Min(GetCaseScale(), HeldHeight / Model) * ExamineZoom;
	SetActorScale3D(FVector(FMath::Lerp(GetScaleFactor(), Held, Alpha)));
	// It looks back at the person holding it.
	if (APlayerCameraManager* Camera = UGameplayStatics::GetPlayerCameraManager(this, 0))
	{
		SetLookTarget(Camera->GetCameraLocation());
	}
}

void AAlienCharacter::EndExamine()
{
	if (!bExamined)
	{
		return;
	}
	AAlienChamber* Chamber = HomeChamber.Get();
	const FVector Location = GetActorLocation();
	// Over its case (or reaching into it): straight back in. Anywhere else it floats home.
	if (!Chamber || Chamber->IsInsideMovementBounds(Location, -15.f * Chamber->GetChamberScale()))
	{
		SettleInCase(Location);
		return;
	}
	bExamined = false;
	bReturning = true;
	Sounds->PlayEffect(TEXT("Alien.Return"));
	ReturnFrom = GetActorTransform();
	ReturnTime = 0.f;
	ReturnDuration = FMath::Clamp(static_cast<float>(FVector::Dist(Location, GetReturnPoint())) / 120.f, 0.6f, 1.6f);
}

FVector AAlienCharacter::GetReturnPoint() const
{
	const AAlienChamber* Chamber = HomeChamber.Get();
	if (!Chamber)
	{
		return GetActorLocation();
	}
	const float Scale = Chamber->GetChamberScale();
	return Chamber->GetAlienSpawnLocation(CapsuleHalfHeightLocal * Scale) + FVector(0.f, 0.f, Chamber->GlassHeight * Scale * 0.2f);
}

void AAlienCharacter::UpdateReturn(float DeltaSeconds)
{
	if (!HomeChamber.IsValid())
	{
		SettleInCase(GetActorLocation());
		return;
	}
	ReturnTime += DeltaSeconds;
	const float T = FMath::Clamp(ReturnTime / ReturnDuration, 0.f, 1.f);
	const float Ease = FMath::InterpEaseInOut(0.f, 1.f, T, 2.f);
	const FVector From = ReturnFrom.GetLocation();
	const FVector To = GetReturnPoint(); // the case may be moving
	const float Arc = FMath::Clamp(static_cast<float>(FVector::Dist(From, To)) * 0.25f, 10.f, 40.f);
	const FQuat Upright = FRotator(0.f, ReturnFrom.Rotator().Yaw, 0.f).Quaternion();
	SetActorLocationAndRotation(FMath::Lerp(From, To, Ease) + FVector(0.f, 0.f, FMath::Sin(T * PI) * Arc),
		FQuat::Slerp(ReturnFrom.GetRotation(), Upright, Ease), false, nullptr, ETeleportType::TeleportPhysics);
	SetActorScale3D(FVector(FMath::Lerp(static_cast<float>(ReturnFrom.GetScale3D().Z), GetCaseScale(), Ease)));
	if (T >= 1.f)
	{
		SettleInCase(To);
	}
}

void AAlienCharacter::SettleInCase(const FVector& WorldLocation)
{
	bExamined = false;
	bReturning = false;
	AAlienChamber* Chamber = HomeChamber.Get();
	SetActorScale3D(FVector(GetCaseScale()));
	FVector Drop = WorldLocation;
	if (Chamber)
	{
		// Inside the glass, between the floor and the lid; it falls to the floor from there.
		const float Scale = Chamber->GetChamberScale();
		const float Radius = GetCapsuleComponent()->GetScaledCapsuleRadius();
		const float HalfHeight = GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
		const FVector Inside = Chamber->ClampToMovementBounds(WorldLocation, Radius + 1.f * Scale);
		Drop.X = Inside.X;
		Drop.Y = Inside.Y;
		const float Floor = Chamber->GetFloorZ() + HalfHeight + 1.f * Scale;
		const float Lid = static_cast<float>(Chamber->GetActorTransform().TransformPosition(FVector(0.f, 0.f, Chamber->BaseHeight + Chamber->GlassHeight)).Z) - HalfHeight - 2.f * Scale;
		Drop.Z = FMath::Clamp(WorldLocation.Z, static_cast<double>(Floor), static_cast<double>(FMath::Max(Floor, Lid)));
	}
	SetActorLocationAndRotation(Drop, FRotator(0.f, GetActorRotation().Yaw, 0.f), false, nullptr, ETeleportType::TeleportPhysics);
	GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	UCharacterMovementComponent* Move = GetCharacterMovement();
	Move->SetMovementMode(MOVE_Falling);
	Move->Velocity = FVector::ZeroVector;
	ApplyScaleDependentSettings();
	ContactShadow->SetVisibility(true);
	SetExcited(false);
	ClearLookTarget();
	bCelebrateOnLanding = true; // a happy hop once it lands
	if (AAlienAIController* Brain = Cast<AAlienAIController>(GetController()))
	{
		Brain->NotifyHeld(false);
	}
	// The case is being carried right now: ride along like the rest of its contents.
	if (Chamber && Chamber->IsBeingGrabbed())
	{
		SetHeld(true);
	}
}

void AAlienCharacter::Landed(const FHitResult& Hit)
{
	Super::Landed(Hit);
	if (!Actions->IsPerforming())
	{
		Sounds->PlayFootstep(1.3f);
	}
	if (bCelebrateOnLanding)
	{
		bCelebrateOnLanding = false;
		Hop();
	}
}

void AAlienCharacter::SetTargeted(bool bInTargeted)
{
	if (bTargeted == bInTargeted)
	{
		return;
	}
	bTargeted = bInTargeted;
	if (bTargeted && !TargetRing)
	{
		TargetRing = NewObject<UStaticMeshComponent>(this, TEXT("PickUpRing"));
		TargetRing->SetStaticMesh(MuseumAssets::PlaneMesh());
		TargetRing->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		TargetRing->SetCastShadow(false);
		TargetRing->SetCanEverAffectNavigation(false);
		TargetRing->SetupAttachment(GetCapsuleComponent());
		if (UMaterialInterface* Ring = MuseumAssets::FXRingMaterial())
		{
			TargetRingMaterial = UMaterialInstanceDynamic::Create(Ring, this);
			TargetRingMaterial->SetVectorParameterValue(MuseumAssets::Params::Color, FLinearColor(1.f, 0.85f, 0.3f));
			TargetRingMaterial->SetScalarParameterValue(MuseumAssets::Params::Intensity, 2.2f);
			TargetRing->SetMaterial(0, TargetRingMaterial);
		}
		TargetRing->RegisterComponent();
		PlaceBodyOnFloor();
	}
	if (TargetRing)
	{
		TargetRing->SetVisibility(bTargeted);
		TargetTime = 0.f;
	}
}
