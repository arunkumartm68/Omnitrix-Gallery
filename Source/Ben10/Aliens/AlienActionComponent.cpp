// Alien Museum - signature moves and their effects.

#include "Aliens/AlienActionComponent.h"
#include "Aliens/AlienAppearanceComponent.h"
#include "Aliens/AlienCharacter.h"
#include "Chamber/AlienChamber.h"
#include "Chamber/ChamberHabitatComponent.h"
#include "Data/ChamberHabitatAsset.h"
#include "Core/MuseumAssets.h"
#include "Core/MuseumAudio.h"
#include "Ben10.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "PhysicalMaterials/PhysicalMaterial.h"

namespace
{
	constexpr int32 MaxFx = 40;
	constexpr int32 MaxAfterImages = 3;
	const FLinearColor EmberColor(1.f, 0.42f, 0.07f);
	const FLinearColor SparkColor(1.f, 0.8f, 0.3f);

	float EaseOut(float T)
	{
		T = FMath::Clamp(T, 0.f, 1.f);
		return 1.f - (1.f - T) * (1.f - T);
	}

	float EaseInOut(float T)
	{
		return FMath::InterpEaseInOut(0.f, 1.f, FMath::Clamp(T, 0.f, 1.f), 2.f);
	}

	/** 0 -> 1 with a little overshoot. */
	float EaseOutBack(float T)
	{
		T = FMath::Clamp(T, 0.f, 1.f);
		const float C1 = 1.70158f;
		const float C3 = C1 + 1.f;
		return 1.f + C3 * FMath::Pow(T - 1.f, 3.f) + C1 * FMath::Pow(T - 1.f, 2.f);
	}

	/** 0 -> 1 -> 0. */
	float Bell(float T)
	{
		return FMath::Sin(FMath::Clamp(T, 0.f, 1.f) * PI);
	}

	/** Rotation whose up axis (+Z: cone tips, cylinder axes, ring normals) points along Direction. */
	FQuat UpAlong(const FVector& Direction)
	{
		return FRotationMatrix::MakeFromZ(Direction.GetSafeNormal()).ToQuat();
	}

	FVector RandomFlat(FRandomStream& Rng)
	{
		const float Angle = Rng.FRandRange(0.f, 2.f * PI);
		return FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.f);
	}
}

UAlienActionComponent::UAlienActionComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	Rng.GenerateNewSeed();
}

// ---------------------------------------------------------------------------------------------
// Setup / start / stop
// ---------------------------------------------------------------------------------------------

void UAlienActionComponent::Setup(const UAlienDataAsset* Data)
{
	StopAction();
	DestroyFlames();
	Alien = Cast<AAlienCharacter>(GetOwner());
	Body = Alien ? Alien->GetAppearance() : nullptr;
	Actions.Reset();
	if (!Data || !Alien || !Body)
	{
		return;
	}
	Actions = Data->SignatureActions;
	if (!Body->GetModelComponent())
	{
		Actions.Remove(EAlienAction::Clone); // clones copy the imported model
	}
	PoseMesh = Data->PoseMesh;
	BallForm = Data->BallMesh;
	ActionColor = Data->ActionColor;
	Height = Data->Height;
	WalkSpeed = Data->WalkSpeed;
	bSpeedTrail = Data->bSpeedTrail;
	bHovers = Data->bHovers;
	TapStyle = Data->TapStyle;
	VineColor = Data->VineColor;
	bVineSeedPod = Data->bVineSeedPod;
	if (Data->bHeadFlames)
	{
		BuildFlames();
	}
}

bool UAlienActionComponent::StartRandomAction(bool bShowOff)
{
	if (Actions.Num() == 0)
	{
		return false;
	}
	if (bShowOff && StartAction(Actions[0]))
	{
		return true;
	}
	const int32 First = Rng.RandRange(0, Actions.Num() - 1);
	for (int32 i = 0; i < Actions.Num(); ++i)
	{
		if (StartAction(Actions[(First + i) % Actions.Num()]))
		{
			return true;
		}
	}
	return false;
}

bool UAlienActionComponent::StartAction(EAlienAction Action)
{
	AAlienChamber* Case = GetChamber();
	if (!Alien || !Body || !Case || Alien->IsHeld() || !Alien->GetCharacterMovement()->IsMovingOnGround())
	{
		return false;
	}
	if (Action == EAlienAction::Clone && !Body->GetModelComponent())
	{
		return false;
	}
	if (Action == EAlienAction::Fly && Case->GetInnerSize().Z * Case->GetChamberScale() - WorldHeight() < 15.f)
	{
		return false; // no head room
	}
	StopAction();

	Current = Action;
	bPerforming = true;
	Step = 0;
	EnteredStep = -1;
	StepTime = 0.f;
	ActionTime = 0.f;
	Counter = 0;
	Timer = 0.f;
	EventTime = 0.f;
	Amount = 0.f;
	LastYaw = static_cast<float>(Alien->GetActorRotation().Yaw);
	Alien->StopMoving();
	UE_LOG(LogAlienMuseum, Log, TEXT("%s performs %s"), *GetOwner()->GetName(), *UEnum::GetDisplayValueAsText(Action).ToString());
	return true;
}

void UAlienActionComponent::StopAction()
{
	if (Alien && Alien->IsHeld())
	{
		// The case is being carried: nothing may keep flying around in it.
		PopPhysicsBall();
		ClearFx();
	}
	if (!bPerforming)
	{
		return;
	}
	bPerforming = false;
	FinishEating();
	RestoreCapsule();
	if (Alien)
	{
		Alien->StopMoving();
		Alien->SetSpeedMultiplier(1.f);
		Alien->SetPushStrength(1.f);
		Alien->SetContactShadowScale(1.f);
		Alien->SetExcited(false);
	}
	SetGhost(false);
	if (Body)
	{
		Body->EndPose();
		Body->ClearActionTransform();
		Body->ClearFrontReach();
		Body->SetExtraLift(0.f);
		Body->SetBodyVisible(true);
		Body->StopClip(0.2f);
	}
	if (UAlienSoundComponent* Sounds = Alien ? Alien->GetSounds() : nullptr)
	{
		Sounds->StopMoveLoop();
		Sounds->SetLoopBoost(1.f, 1.f);
	}
	FlameBoost = 1.f;
	if (bTempFlames)
	{
		DestroyFlames();
		bTempFlames = false;
	}
	if (Ball)
	{
		Ball->SetVisibility(false, true);
	}
	for (UStaticMeshComponent* Part : Clones)
	{
		Part->SetVisibility(false);
	}
	for (UStaticMeshComponent* Part : Vines)
	{
		Part->SetVisibility(false);
	}
	for (UStaticMeshComponent* Part : VineBuds)
	{
		Part->SetVisibility(false);
	}
	if (Puddle)
	{
		Puddle->SetVisibility(false);
	}
}

void UAlienActionComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	StopAction();
	Super::EndPlay(EndPlayReason);
}

bool UAlienActionComponent::Entering()
{
	if (EnteredStep == Step)
	{
		return false;
	}
	EnteredStep = Step;
	return true;
}

void UAlienActionComponent::NextStep()
{
	GoToStep(Step + 1);
}

void UAlienActionComponent::GoToStep(int32 NewStep)
{
	Step = NewStep;
	StepTime = 0.f;
}

void UAlienActionComponent::RestartStep()
{
	EnteredStep = -1;
	StepTime = 0.f;
}

// ---------------------------------------------------------------------------------------------
// Tick
// ---------------------------------------------------------------------------------------------

void UAlienActionComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (!Alien || !Body)
	{
		return;
	}

	if (bPerforming)
	{
		ActionTime += DeltaTime;
		StepTime += DeltaTime;
		switch (Current)
		{
		case EAlienAction::Roll: TickRoll(DeltaTime); break;
		case EAlienAction::Dash: TickDash(DeltaTime); break;
		case EAlienAction::Flare: TickFlare(DeltaTime); break;
		case EAlienAction::Flex: TickFlex(DeltaTime); break;
		case EAlienAction::CrystalBurst: TickCrystalBurst(DeltaTime); break;
		case EAlienAction::Phase: TickPhase(DeltaTime); break;
		case EAlienAction::Scream: TickScream(DeltaTime); break;
		case EAlienAction::Clone: TickClone(DeltaTime); break;
		case EAlienAction::Spit: TickSpit(DeltaTime); break;
		case EAlienAction::Pounce: TickPounce(DeltaTime); break;
		case EAlienAction::Vines: TickVines(DeltaTime); break;
		case EAlienAction::Melt: TickMelt(DeltaTime); break;
		case EAlienAction::Scurry: TickScurry(DeltaTime); break;
		case EAlienAction::Fly: TickFly(DeltaTime); break;
		case EAlienAction::Howl: TickHowl(DeltaTime); break;
		case EAlienAction::GlassReact: TickGlassReact(DeltaTime); break;
		}
		if (bPerforming && ActionTime > 15.f)
		{
			StopAction(); // safety net: a move never takes this long
		}

		// Safety net: whatever a move does (fast rolls, leaps, a capsule growing back), the body stays
		// inside the glass. Normal contact with the glass leaves it well within this check.
		AAlienChamber* Case = GetChamber();
		const float Radius = CapsuleRadius();
		if (bPerforming && Case && !Case->IsInsideMovementBounds(Alien->GetActorLocation(), Radius - 2.f * Alien->GetScaleFactor()))
		{
			FVector Inside = Case->ClampToMovementBounds(Alien->GetActorLocation(), Radius);
			Inside.Z = Alien->GetActorLocation().Z;
			UE_LOG(LogAlienMuseum, Warning, TEXT("%s: kept inside the glass during %s (step %d)"),
				*GetOwner()->GetName(), *UEnum::GetDisplayValueAsText(Current).ToString(), Step);
			Alien->SetActorLocation(Inside, false, nullptr, ETeleportType::TeleportPhysics);
			Alien->GetCharacterMovement()->StopMovementImmediately(); // the move steers it on from here
		}
	}

	// Purely visual extras only while someone can see the alien.
	if (Alien->WasRecentlyRendered(0.3f))
	{
		UpdateFlames(DeltaTime);
		UpdateFootsteps(DeltaTime);

		// XLR8: after-images whenever it runs fast (the dash makes its own).
		const bool bDashing = bPerforming && Current == EAlienAction::Dash;
		const float FastSpeed = 45.f * Alien->GetScaleFactor();
		if (bSpeedTrail && !bDashing && Body->IsBodyVisible() && Alien->GetVelocity().Size2D() > FastSpeed)
		{
			TrailTimer += DeltaTime;
			if (TrailTimer >= 0.06f)
			{
				TrailTimer = 0.f;
				SpawnAfterImage(0.35f, 0.2f);
			}
		}
	}
	UpdateAfterImages(DeltaTime);
	UpdatePhysicsBall(DeltaTime);
	UpdateFx(DeltaTime);
}

// ---------------------------------------------------------------------------------------------
// Roll (Cannonbolt): curls into an armoured ball, rolls fast and bounces off the glass
// ---------------------------------------------------------------------------------------------

void UAlienActionComponent::TickRoll(float Dt)
{
	const bool bEnter = Entering();
	constexpr float CurlTime = 0.35f;
	EnsureBall();

	switch (Step)
	{
	case 0: // curl up
	{
		if (bEnter)
		{
			// A ball about the size of the body. The capsule shrinks to it, so it can roll around the case.
			BallRadius = FMath::Max(4.f, FMath::Min(CapsuleRadius(), 0.42f * WorldHeight()));
			BallSpin = Alien->GetActorQuat();
			Ball->SetVisibility(true, true);
			MoveDir = !ForcedDirection.IsNearlyZero() ? ForcedDirection : (Rng.FRand() < 0.5f ? TowardViewer() : RandomFlat(Rng));
			ForcedDirection = FVector::ZeroVector;
			Sfx(TEXT("Move.Roll.Curl"));
		}
		const float A = FMath::Min(1.f, StepTime / CurlTime);
		BallGrow = FMath::Max(0.05f, EaseOutBack(A));
		Body->SetActionTransform(FVector(FMath::Lerp(1.f, 0.45f, EaseInOut(A))), FRotator(-35.f * A, 0.f, 0.f));
		if (A >= 0.7f && Body->IsBodyVisible())
		{
			Body->SetBodyVisible(false);
		}
		if (A >= 1.f)
		{
			ShrinkCapsule(BallRadius);
			Alien->SetSpeedMultiplier(FMath::Max(6.f, 90.f / FMath::Max(5.f, WalkSpeed)));
			Alien->SetPushStrength(3.f); // bowls the props over
			Counter = 0;
			NextStep();
			AimRoll();
			if (UAlienSoundComponent* Sounds = Alien->GetSounds())
			{
				Sounds->StartMoveLoop(TEXT("Move.Roll.Loop"));
			}
		}
		break;
	}
	case 1: // roll, bouncing off the glass
		BallGrow = 1.f;
		if (Alien->IsStuck())
		{
			// Blocked by a rock: back off in a new direction.
			MoveDir = (-MoveDir).RotateAngleAxis(Rng.FRandRange(-50.f, 50.f), FVector::UpVector).GetSafeNormal2D();
			AimRoll();
		}
		else if (!Alien->IsMoving())
		{
			BounceRoll();
		}
		if (StepTime > 3.6f || Counter >= 6)
		{
			Alien->StopMoving();
			Alien->SetSpeedMultiplier(1.f);
			Alien->SetPushStrength(1.f);
			NextStep();
		}
		break;
	case 2: // uncurl
	{
		if (bEnter)
		{
			RestoreCapsule();
			if (UAlienSoundComponent* Sounds = Alien->GetSounds())
			{
				Sounds->StopMoveLoop(0.25f);
			}
			Sfx(TEXT("Move.Roll.Uncurl"));
		}
		const float A = FMath::Min(1.f, StepTime / CurlTime);
		BallGrow = FMath::Max(0.05f, 1.f - EaseInOut(A));
		if (A >= 0.25f && !Body->IsBodyVisible())
		{
			Body->SetBodyVisible(true);
		}
		Body->SetActionTransform(FVector(FMath::Lerp(0.45f, 1.f, EaseOutBack(A))), FRotator(-35.f * (1.f - A), 0.f, 0.f));
		if (A >= 1.f)
		{
			StopAction();
			return;
		}
		break;
	}
	default:
		StopAction();
		return;
	}

	// The ball sits on the ground under the alien and spins with the distance it rolls.
	const FVector Velocity = Alien->GetVelocity();
	const float Speed = static_cast<float>(Velocity.Size2D());
	if (Speed > 1.f)
	{
		const FVector Axis = FVector::CrossProduct(FVector::UpVector, Velocity.GetSafeNormal2D());
		BallSpin = FQuat(Axis, Speed * Dt / BallRadius) * BallSpin;
	}
	const float MeshScale = BallRadius * BallGrow / BallMeshRadius;
	const FVector Center = Feet() + FVector(0.f, 0.f, BallRadius * BallGrow);
	Ball->SetWorldTransform(FTransform(BallSpin, Center - BallSpin.RotateVector(BallMeshCenter * MeshScale), FVector(MeshScale)));
}

void UAlienActionComponent::AimRoll()
{
	const FVector From = Alien->GetActorLocation();
	float Distance = DistanceToGlass(From, MoveDir, CapsuleRadius() + 1.f);
	if (Distance < 4.f)
	{
		MoveDir = -MoveDir;
		Distance = DistanceToGlass(From, MoveDir, CapsuleRadius() + 1.f);
	}
	Alien->MoveToPoint(From + MoveDir * FMath::Max(Distance, 1.f), 1.f);
}

void UAlienActionComponent::BounceRoll()
{
	++Counter;
	const FVector Normal = GlassNormalAt(Alien->GetActorLocation()); // into the case

	// Mirror off the glass, with a little randomness so it never repeats the same line.
	FVector Dir = MoveDir - 2.f * FVector::DotProduct(MoveDir, Normal) * Normal;
	Dir = Dir.RotateAngleAxis(Rng.FRandRange(-25.f, 25.f), FVector::UpVector).GetSafeNormal2D();
	if (FVector::DotProduct(Dir, Normal) < 0.3f)
	{
		Dir = (Dir + Normal).GetSafeNormal2D();
	}
	MoveDir = Dir;

	// Impact: a ring on the glass, dust, the props nearby jump, and the ball hops off.
	const float S = Alien->GetScaleFactor();
	const FVector Contact = Feet() + FVector(0.f, 0.f, BallRadius) - Normal * BallRadius;
	SpawnFx(EFx::Ring, Contact - Normal * 0.5f, UpAlong(Normal), FVector(BallRadius * 0.8f), FVector(BallRadius * 3.f), ActionColor, 0.4f, 0.8f, 2.5f);
	SfxAt(TEXT("Move.Roll.Bounce"), Contact);
	DustPuff(Feet() - Normal * BallRadius * 0.6f, BallRadius * 0.9f, 4);
	Blast(Contact, BallRadius * 3.5f, 110.f * S, FVector::ZeroVector, 0.6f);
	const float Speed = Alien->GetCharacterMovement()->MaxWalkSpeed * 0.8f;
	Alien->LaunchCharacter(MoveDir * Speed + FVector(0.f, 0.f, 110.f * FMath::Sqrt(S)), true, true);
	AimRoll();
}

// ---------------------------------------------------------------------------------------------
// Dash (XLR8): super-speed zig-zag with after-images and speed streaks
// ---------------------------------------------------------------------------------------------

void UAlienActionComponent::TickDash(float Dt)
{
	const bool bEnter = Entering();
	const float H = WorldHeight();
	switch (Step)
	{
	case 0: // crouch
	{
		const float A = FMath::Min(1.f, StepTime / 0.25f);
		Body->SetActionTransform(FVector(1.f + 0.04f * A, 1.f + 0.04f * A, 1.f - 0.1f * A), FRotator(-8.f * A, 0.f, 0.f));
		if (A >= 1.f)
		{
			Alien->SetSpeedMultiplier(FMath::Max(3.f, 200.f / FMath::Max(5.f, WalkSpeed)));
			Alien->SetPushStrength(3.f);
			Counter = 0;
			NextStep();
			Voice(EAlienVoice::Effort);
		}
		break;
	}
	case 1: // one leg of the zig-zag
	{
		if (bEnter)
		{
			const FVector From = Alien->GetActorLocation();
			if (!MoveToFarPoint(0.6f))
			{
				GoToStep(2);
				break;
			}
			++Counter;
			DustPuff(Feet(), H * 0.35f, 3);
			Sfx(TEXT("Move.Dash.Zip"));
			const FVector To = TargetPoint;
			const float Length = static_cast<float>(FVector::Dist(From, To));
			SpawnFx(EFx::Cylinder, (From + To) * 0.5f, UpAlong(To - From), FVector(H * 0.06f, H * 0.06f, Length),
				FVector(H * 0.01f, H * 0.01f, Length), ActionColor, 0.45f, 0.6f, 2.5f);
		}
		Body->SetActionTransform(FVector::OneVector, FRotator(-16.f, 0.f, 0.f));
		Timer += Dt;
		if (Timer >= 0.045f)
		{
			Timer = 0.f;
			SpawnAfterImage(0.45f, 0.2f);
		}
		if (!Alien->IsMoving() || Alien->IsStuck() || StepTime > 1.5f)
		{
			if (Counter >= 4)
			{
				Alien->StopMoving();
				NextStep();
			}
			else
			{
				RestartStep(); // next leg
			}
		}
		break;
	}
	case 2: // skid to a stop
	{
		if (bEnter)
		{
			Alien->StopMoving();
			Alien->SetSpeedMultiplier(1.f);
			Alien->SetPushStrength(1.f);
			DustPuff(Feet() + Forward() * CapsuleRadius(), H * 0.45f, 5);
			Sfx(TEXT("Move.Dash.Skid"));
		}
		const float A = FMath::Min(1.f, StepTime / 0.4f);
		Body->SetActionTransform(FVector::OneVector, FRotator(10.f * (1.f - A), 0.f, 0.f));
		if (A >= 1.f)
		{
			StopAction();
		}
		break;
	}
	default:
		StopAction();
		break;
	}
}

// ---------------------------------------------------------------------------------------------
// Flare (Heatblast): the head flames surge, embers burst and a fireball hits the glass
// ---------------------------------------------------------------------------------------------

void UAlienActionComponent::TickFlare(float Dt)
{
	const bool bEnter = Entering();
	if (bEnter)
	{
		if (Flames.Num() == 0)
		{
			BuildFlames();
			bTempFlames = true;
		}
		FaceViewer();
		Sfx(TEXT("Move.Flare.Surge"));
	}
	const float H = WorldHeight();
	const float S = Alien->GetScaleFactor();

	// Flames surge, hold and calm down again.
	const float Envelope = FMath::Min(1.f, ActionTime / 0.3f) * (1.f - FMath::Clamp((ActionTime - 1.7f) / 0.6f, 0.f, 1.f));
	FlameBoost = 1.f + 1.2f * Envelope;
	Body->SetActionTransform(FVector(1.f + 0.04f * Envelope), FRotator(6.f * Envelope, 0.f, 0.f));

	FVector TopOffset = FVector::ZeroVector;
	USceneComponent* Head = Body->GetHeadTop(TopOffset);
	const FVector HeadTop = Head ? Head->GetComponentTransform().TransformPosition(TopOffset) : Alien->GetActorLocation() + FVector(0.f, 0.f, H * 0.5f);

	if (Counter == 0 && ActionTime >= 0.35f)
	{
		Counter = 1;
		Burst(HeadTop, 14, H * 0.03f, H * 1.2f, EmberColor, 1.1f, -H * 0.4f, 1.2f);
	}
	else if (Counter == 1 && ActionTime >= 0.9f)
	{
		// A fireball flies to the glass on the visitor's side.
		Counter = 2;
		MoveDir = TowardViewer();
		Sfx(TEXT("Move.Flare.Fireball"));
		const FVector From = HeadTop + MoveDir * CapsuleRadius() - FVector(0.f, 0.f, H * 0.1f);
		const float Distance = FMath::Max(5.f, DistanceToGlass(From, MoveDir, 0.f) + 2.f * S);
		const float Speed = 120.f * S;
		const float Life = FMath::Max(0.15f, Distance / Speed);
		const int32 Index = SpawnFx(EFx::Sphere, From, FQuat::Identity, FVector(H * 0.12f), FVector(H * 0.2f), EmberColor, Life, 0.95f, 4.f);
		if (FxStates.IsValidIndex(Index))
		{
			FxStates[Index].Velocity = MoveDir * Speed;
		}
		SavedPoint = From;
		TargetPoint = From + MoveDir * Distance;
		EventTime = ActionTime + Life;
	}
	else if (Counter == 2)
	{
		// Sparks trail behind the fireball.
		Timer += Dt;
		const float Flight = FMath::Clamp(1.f - (EventTime - ActionTime) / FMath::Max(0.05f, EventTime - 0.9f), 0.f, 1.f);
		if (Timer >= 0.05f)
		{
			Timer = 0.f;
			const int32 Index = SpawnFx(EFx::Sphere, FMath::Lerp(SavedPoint, TargetPoint, Flight), FQuat::Identity, FVector(H * 0.05f), FVector(H * 0.01f), SparkColor, 0.35f, 0.9f, 3.5f);
			if (FxStates.IsValidIndex(Index))
			{
				FxStates[Index].Gravity = -H * 0.5f;
			}
		}
		if (ActionTime >= EventTime)
		{
			Counter = 3;
			SpawnFx(EFx::Ring, TargetPoint, UpAlong(MoveDir), FVector(H * 0.15f), FVector(H * 0.7f), EmberColor, 0.5f, 0.85f, 3.f);
			SfxAt(TEXT("Move.Flare.Impact"), TargetPoint);
			Burst(TargetPoint, 10, H * 0.025f, H * 1.4f, EmberColor, 0.6f, H * 2.f, 0.3f);
			Blast(TargetPoint, H * 0.9f, 80.f * S, FVector::ZeroVector, 0.4f);
		}
	}
	else if (Counter == 3 && ActionTime >= 2.4f)
	{
		StopAction();
	}
}

// ---------------------------------------------------------------------------------------------
// Flex (Four Arms): faces the visitor, strikes a strength pose, pumps, and stomps a shockwave
// ---------------------------------------------------------------------------------------------

void UAlienActionComponent::TickFlex(float Dt)
{
	const bool bEnter = Entering();
	const float H = WorldHeight();
	const float S = Alien->GetScaleFactor();
	switch (Step)
	{
	case 0: // turn to the visitor
	{
		if (bEnter)
		{
			FaceViewer();
		}
		const float A = FMath::Min(1.f, StepTime / 0.6f);
		Body->SetActionTransform(FVector(1.f, 1.f, 1.f - 0.03f * A));
		if (A >= 1.f)
		{
			if (UStaticMesh* Pose = PoseMesh.LoadSynchronous())
			{
				Body->ShowPose(Pose); // double biceps
			}
			GroundRing(Feet(), H * 0.3f, H * 1.4f, ActionColor, 0.5f, 0.6f);
			Counter = 0;
			NextStep();
		}
		break;
	}
	case 1: // hold the pose and pump the muscles three times
	{
		const float Pump = Bell(FMath::Fmod(StepTime, 0.65f) / 0.65f);
		Body->SetActionTransform(FVector(1.f + 0.06f * Pump, 1.f + 0.06f * Pump, 1.f + 0.025f * Pump), FRotator(3.f * Pump, 0.f, 0.f));
		const int32 Peaks = FMath::FloorToInt((StepTime + 0.325f) / 0.65f);
		if (Peaks > Counter && Counter < 3)
		{
			Counter = Peaks;
			const FVector Chest = Feet() + FVector(0.f, 0.f, H * 0.55f);
			const float Aura = FitInCase(Chest, H * 1.3f);
			SpawnFx(EFx::Ring, Chest, FQuat::Identity, FVector(Aura * 0.35f), FVector(Aura), ActionColor, 0.45f, 0.55f, 2.5f);
			Voice(EAlienVoice::Effort, 0.9f);
			Sfx(TEXT("Move.Flex.Aura"), 0.6f);
		}
		if (StepTime >= 1.95f)
		{
			Body->EndPose();
			Alien->LaunchCharacter(FVector(0.f, 0.f, 170.f * FMath::Sqrt(S)), false, true); // jump for the stomp
			NextStep();
		}
		break;
	}
	case 2: // stomp: wait for the landing
		Body->SetActionTransform(FVector(0.97f, 0.97f, 1.05f));
		if ((StepTime > 0.12f && Alien->GetCharacterMovement()->IsMovingOnGround()) || StepTime > 1.f)
		{
			const FVector At = Feet();
			GroundRing(At, H * 0.3f, H * 3.2f, DustColor(), 0.7f, 0.8f);
			GroundRing(At, H * 0.2f, H * 2.f, ActionColor, 0.5f, 0.6f);
			DustPuff(At, H * 0.5f, 8);
			Blast(At, H * 3.5f, 150.f * S, FVector::ZeroVector, 0.7f); // everything nearby jumps
			Sfx(TEXT("Move.Stomp"));
			NextStep();
		}
		break;
	case 3: // absorb the landing
	{
		const float A = FMath::Min(1.f, StepTime / 0.35f);
		Body->SetActionTransform(FVector(1.f + 0.06f * (1.f - A), 1.f + 0.06f * (1.f - A), FMath::Lerp(0.84f, 1.f, EaseOutBack(A))));
		if (A >= 1.f)
		{
			StopAction();
		}
		break;
	}
	default:
		StopAction();
		break;
	}
}

// ---------------------------------------------------------------------------------------------
// Crystal burst (Diamondhead): crystal spikes burst out of the ground around it
// ---------------------------------------------------------------------------------------------

void UAlienActionComponent::TickCrystalBurst(float Dt)
{
	const bool bEnter = Entering();
	const float H = WorldHeight();
	switch (Step)
	{
	case 0: // wind up
	{
		if (bEnter)
		{
			Sfx(TEXT("Move.Crystal.Charge"));
		}
		const float A = FMath::Min(1.f, StepTime / 0.4f);
		Body->SetActionTransform(FVector(1.f + 0.03f * A, 1.f + 0.03f * A, 1.f - 0.1f * A), FRotator(-8.f * A, 0.f, 0.f));
		if (A >= 1.f)
		{
			NextStep();
		}
		break;
	}
	case 1: // slam: the crystals grow, stay a while and sink back
	{
		if (bEnter)
		{
			AAlienChamber* Case = GetChamber();
			const FVector Center = Feet();
			const float R = CapsuleRadius();
			const float Start = Rng.FRandRange(0.f, 360.f);
			for (int32 i = 0; i < 7; ++i)
			{
				const float Angle = FMath::DegreesToRadians(Start + i * 360.f / 7.f + Rng.FRandRange(-12.f, 12.f));
				const FVector Out(FMath::Cos(Angle), FMath::Sin(Angle), 0.f);
				FVector Base = Center + Out * (R * 1.25f + Rng.FRandRange(0.f, R * 0.5f));
				if (Case && !Case->IsInsideMovementBounds(Base, H * 0.06f))
				{
					Base = Case->ClampToMovementBounds(Base, H * 0.06f);
					Base.Z = Center.Z;
				}
				const FVector Axis = (FVector::UpVector + Out * Rng.FRandRange(0.35f, 0.7f)).GetSafeNormal();
				const float Tall = H * Rng.FRandRange(0.35f, 0.75f);
				const float Wide = Tall * Rng.FRandRange(0.22f, 0.32f);
				const int32 Index = SpawnFx(EFx::Cone, Base, UpAlong(Axis), FVector(Wide, Wide, Tall), FVector(Wide, Wide, Tall), ActionColor, 2.4f, 0.85f, 2.2f);
				if (FxStates.IsValidIndex(Index))
				{
					FxStates[Index].bPop = true;
					FxStates[Index].bFromBase = true;
					FxStates[Index].Base = Base - Axis * Tall * 0.1f; // rooted a little below the surface
				}
			}
			Burst(Center + FVector(0.f, 0.f, H * 0.2f), 10, H * 0.025f, H * 1.1f, ActionColor, 0.9f, H * 0.6f, 1.f);
			GroundRing(Center, H * 0.3f, H * 2.2f, ActionColor, 0.5f, 0.6f);
			Blast(Center, H * 3.f, 110.f * Alien->GetScaleFactor(), FVector::ZeroVector, 0.8f);
			Sfx(TEXT("Move.Crystal.Burst"));
			Voice(EAlienVoice::Effort, 0.8f);
		}
		const float A = FMath::Min(1.f, StepTime / 0.3f);
		const float Width = FMath::Lerp(1.03f, 1.f, A);
		Body->SetActionTransform(FVector(Width, Width, FMath::Lerp(0.9f, 1.04f, EaseOutBack(A))), FRotator(6.f * A, 0.f, 0.f));
		if (StepTime >= 2.4f)
		{
			StopAction();
		}
		break;
	}
	default:
		StopAction();
		break;
	}
}

// ---------------------------------------------------------------------------------------------
// Phase (Ghostfreak): fades into mist, drifts unseen through everything, reappears elsewhere
// ---------------------------------------------------------------------------------------------

void UAlienActionComponent::TickPhase(float Dt)
{
	const bool bEnter = Entering();
	const float H = WorldHeight();
	const FLinearColor Mist = FMath::Lerp(ActionColor, FLinearColor::White, 0.3f);
	auto Puff = [this, H, &Mist]()
	{
		for (int32 i = 0; i < 6; ++i)
		{
			const FVector At = Feet() + RandomFlat(Rng) * CapsuleRadius() * 0.6f + FVector(0.f, 0.f, H * Rng.FRandRange(0.2f, 0.8f));
			const float Size = FitInCase(At, H * 0.6f);
			SpawnFx(EFx::Sphere, At, FQuat::Identity, FVector(Size * 0.4f), FVector(Size), Mist, Rng.FRandRange(0.8f, 1.2f), 0.35f, 0.9f);
		}
	};
	switch (Step)
	{
	case 0: // fade into mist
		if (bEnter)
		{
			Puff();
			SpawnAfterImage(0.6f, 0.5f);
			SetGhost(true); // passes through loose props
			Sfx(TEXT("Move.Phase.Out"));
		}
		if (StepTime >= 0.1f && Body->IsBodyVisible())
		{
			Body->SetBodyVisible(false);
		}
		if (StepTime >= 0.5f)
		{
			Alien->SetSpeedMultiplier(2.5f);
			if (MoveToFarPoint(0.6f))
			{
				NextStep();
			}
			else
			{
				GoToStep(2);
			}
		}
		break;
	case 1: // drift unseen, leaving wisps - and whispering from wherever he is
		if (bEnter)
		{
			Voice(EAlienVoice::Call, 0.8f);
		}
		Timer += Dt;
		if (Timer >= 0.12f)
		{
			Timer = 0.f;
			SpawnFx(EFx::Sphere, Alien->GetActorLocation() + FVector(0.f, 0.f, H * Rng.FRandRange(-0.2f, 0.2f)), FQuat::Identity,
				FVector(H * 0.12f), FVector(H * 0.3f), Mist, 0.6f, 0.25f, 0.8f);
		}
		if (!Alien->IsMoving() || Alien->IsStuck() || StepTime > 3.f)
		{
			Alien->StopMoving();
			Alien->SetSpeedMultiplier(1.f);
			FaceViewer();
			NextStep();
		}
		break;
	case 2: // reappear
		if (bEnter)
		{
			Puff();
			Sfx(TEXT("Move.Phase.In"));
		}
		if (StepTime >= 0.3f && !Body->IsBodyVisible())
		{
			Body->SetBodyVisible(true);
			SpawnAfterImage(0.7f, 0.45f);
		}
		if (StepTime >= 0.7f)
		{
			StopAction();
		}
		break;
	default:
		StopAction();
		break;
	}
}

// ---------------------------------------------------------------------------------------------
// Scream (Echo Echo): sonic rings blast out and push the props in front away
// ---------------------------------------------------------------------------------------------

void UAlienActionComponent::TickScream(float Dt)
{
	const bool bEnter = Entering();
	const float H = WorldHeight();
	const float S = Alien->GetScaleFactor();
	const FLinearColor Sound = FMath::Lerp(ActionColor, FLinearColor::White, 0.35f);
	switch (Step)
	{
	case 0: // draw breath, facing the visitor
	{
		if (bEnter)
		{
			FaceViewer();
		}
		const float A = FMath::Min(1.f, StepTime / 0.45f);
		Body->SetActionTransform(FVector(1.f + 0.03f * A), FRotator(10.f * A, 0.f, 0.f));
		if (A >= 1.f)
		{
			Counter = 0;
			Timer = 0.2f; // first ring right away
			NextStep();
			Sfx(TEXT("Move.Scream"));
		}
		break;
	}
	case 1: // four rings
	{
		Timer += Dt;
		const float Kick = Bell(FMath::Min(1.f, Timer / 0.2f));
		Body->SetActionTransform(FVector(1.f + 0.03f * Kick), FRotator(-6.f * Kick, 0.f, 0.f));
		if (Timer >= 0.2f && Counter < 4)
		{
			Timer = 0.f;
			++Counter;
			const FVector Dir = Forward();
			const FVector From = Feet() + FVector(0.f, 0.f, H * 0.7f) + Dir * (CapsuleRadius() * 0.8f);
			const float Speed = 95.f * S;
			const float Distance = DistanceToGlass(From, Dir, 0.f) + 3.f * S;
			const float Life = FMath::Clamp(Distance / Speed, 0.2f, 0.9f);
			const int32 Index = SpawnFx(EFx::Ring, From, UpAlong(Dir), FVector(H * 0.15f), FVector(H * 0.95f), Sound, Life, 0.9f, 2.6f);
			if (FxStates.IsValidIndex(Index))
			{
				FxStates[Index].Velocity = Dir * Speed;
			}
			if (Counter == 1)
			{
				Blast(From, Distance + H, 90.f * S, Dir, 0.25f);
			}
		}
		if (Counter >= 4 && Timer >= 0.35f)
		{
			NextStep();
		}
		break;
	}
	case 2:
		Body->ClearActionTransform();
		if (StepTime >= 0.3f)
		{
			StopAction();
		}
		break;
	default:
		StopAction();
		break;
	}
}

// ---------------------------------------------------------------------------------------------
// Howl (Benwolf): winds up, opens his four-way jaw and blasts sonic rings at the visitor. A model with
// its own clips plays its wind-up and holds its howl loop; any other alien rears back and howls.
// ---------------------------------------------------------------------------------------------

void UAlienActionComponent::TickHowl(float Dt)
{
	const bool bEnter = Entering();
	const float H = WorldHeight();
	const float S = Alien->GetScaleFactor();
	const FLinearColor Sound = FMath::Lerp(ActionColor, FLinearColor::White, 0.3f);
	switch (Step)
	{
	case 0: // face the visitor and wind up
	{
		if (bEnter)
		{
			FaceViewer();
			Sfx(TEXT("Move.HowlStart"));
			Amount = Body->PlayClip(EAlienClip::SpecialStart, false, 0.2f);
			if (Amount <= 0.f)
			{
				Amount = 0.5f;
			}
		}
		if (!Body->HasClips())
		{
			const float A = FMath::Min(1.f, StepTime / Amount);
			Body->SetActionTransform(FVector(1.f + 0.04f * A), FRotator(14.f * A, 0.f, 0.f));
		}
		if (StepTime >= Amount)
		{
			Counter = 0;
			Timer = 0.12f; // the first ring right away
			NextStep();
		}
		break;
	}
	case 1: // the howl: rings pour out of the open jaw
	{
		if (bEnter)
		{
			Body->PlayClip(EAlienClip::SpecialLoop, true, 0.1f);
			Alien->SetExcited(true);
			Sfx(TEXT("Move.Howl"));
		}
		Timer += Dt;
		if (!Body->HasClips())
		{
			const float Kick = Bell(FMath::Min(1.f, Timer / 0.18f));
			Body->SetActionTransform(FVector(1.f + 0.03f * Kick), FRotator(-8.f * Kick, 0.f, 0.f));
		}
		if (Timer >= 0.18f && Counter < 8)
		{
			Timer = 0.f;
			++Counter;
			const FVector Dir = Forward();
			FVector Head;
			const FVector From = Body->GetHeadBoneLocation(Head)
				? Head + Dir * (0.08f * H)
				: Feet() + FVector(0.f, 0.f, H * 0.75f) + Dir * (CapsuleRadius() * 0.8f);
			const float Speed = 110.f * S;
			const float Distance = DistanceToGlass(From, Dir, 0.f) + 3.f * S;
			const float Life = FMath::Clamp(Distance / Speed, 0.2f, 0.9f);
			const int32 Index = SpawnFx(EFx::Ring, From, UpAlong(Dir), FVector(H * 0.1f), FVector(H * 0.8f), Sound, Life, 0.85f, 2.6f);
			if (FxStates.IsValidIndex(Index))
			{
				FxStates[Index].Velocity = Dir * Speed;
			}
			if (Counter == 2)
			{
				Blast(From, Distance + H, 110.f * S, Dir, 0.3f);
			}
		}
		if (Counter >= 8 && Timer >= 0.2f)
		{
			NextStep();
		}
		break;
	}
	case 2: // the jaw closes, back to prowling
		if (bEnter)
		{
			Body->StopClip(0.35f);
			Alien->SetExcited(false);
		}
		Body->ClearActionTransform();
		if (StepTime >= 0.4f)
		{
			StopAction();
		}
		break;
	default:
		StopAction();
		break;
	}
}

// ---------------------------------------------------------------------------------------------
// Glass reaction: someone tapped on the glass. It turns to the sound, comes over and answers in
// character (TapStyle): Wildmutt sniffs and paws at it, Four Arms bangs back, Ripjaws snaps, Upchuck
// smears his face on it... A hard knock startles it first; tapped again and again, it answers with
// its show-off move.
// ---------------------------------------------------------------------------------------------

bool UAlienActionComponent::StartGlassReaction(const FVector& GlassPoint, const FVector& InGlassNormal, float Strength, EGlassMood Mood)
{
	if (!StartAction(EAlienAction::GlassReact))
	{
		return false;
	}
	SavedPoint = GlassPoint;
	const FVector Flat = InGlassNormal.GetSafeNormal2D();
	GlassNormal = Flat.IsNearlyZero() ? -Forward() : Flat;
	GlassMood = Mood;
	Amount = FMath::Clamp(Strength, 0.f, 1.f);
	return true;
}

FVector UAlienActionComponent::GlassPointAtHeight(float Fraction) const
{
	const float H = WorldHeight();
	const FVector Probe = Feet() + FVector(0.f, 0.f, H * Fraction) + GlassNormal * (CapsuleRadius() + 30.f * Alien->GetScaleFactor());
	float Distance = 0.f;
	FVector Normal;
	FVector OnGlass;
	const AAlienChamber* Case = GetChamber();
	if (Case && Case->GetGlassWallDistance(Probe, Distance, Normal, OnGlass))
	{
		return OnGlass;
	}
	return FVector(SavedPoint.X, SavedPoint.Y, Feet().Z + H * Fraction);
}

void UAlienActionComponent::HitGlass(const FVector& Point, float Strength)
{
	if (AAlienChamber* Case = GetChamber())
	{
		Case->RippleGlass(Point, -GlassNormal, Strength, FMath::Lerp(ActionColor, FLinearColor::White, 0.3f)); // rings on the inside
	}
	if (UMuseumAudio* Audio = UMuseumAudio::Get(this))
	{
		Audio->PlayAt(Strength >= 0.6f ? TEXT("Glass.Knock") : TEXT("Glass.Tap"), Point, 0.35f + 0.6f * Strength); // the glass rings unmuffled
	}
}

float UAlienActionComponent::GapToGlass(float Fraction) const
{
	const float S = Alien->GetScaleFactor();
	const float Ahead = static_cast<float>(FVector::DotProduct(GlassPointAtHeight(Fraction) - Alien->GetActorLocation(), GlassNormal));
	return FMath::Max(0.f, Ahead - Body->GetModelFront() * S - 1.f * S);
}

float UAlienActionComponent::FaceGapToGlass(FVector& OutOnGlass) const
{
	const float H = WorldHeight();
	FVector Head;
	if (Body->GetHeadBoneLocation(Head))
	{
		// A rigged head: the spot on the glass straight in front of it (a head need not sit over the body's
		// centre), and the face (a snout) a little in front of its bone.
		const FVector Ahead = GlassPointAtHeight(FMath::Clamp(static_cast<float>(Head.Z - Feet().Z) / FMath::Max(1.f, H), 0.f, 1.2f));
		const float Distance = static_cast<float>(FVector::DotProduct(Ahead - Head, GlassNormal));
		OutOnGlass = Head + GlassNormal * Distance;
		return FMath::Max(0.f, Distance - 0.12f * H);
	}
	OutOnGlass = GlassPointAtHeight(0.8f);
	return GapToGlass(0.8f);
}

float UAlienActionComponent::PawAtGlass(bool bEnter, const FVector& Along)
{
	// Blind Wildmutt finds the tap with his nose, then he is up on the glass like a dog at a window: both
	// paws land on it, then scratch at it in turn, and he whines to be let out (snarls when annoyed).
	const float H = WorldHeight();
	const bool bAnnoyed = GlassMood == EGlassMood::Annoyed;
	const int32 Cycles = bAnnoyed ? 2 : 1;       // scratches per paw
	const float Period = bAnnoyed ? 0.42f : 0.5f; // one paw: off the glass, up, and down it again
	const float UpTime = 0.6f;                    // both paws on the glass (after the sniff)
	const float ScratchFrom = UpTime + 0.15f;
	const float DownTime = ScratchFrom + Period * (Cycles + 0.5f);
	if (bEnter)
	{
		Body->Sniff();
		Sfx(TEXT("Move.Pounce.Sniff"), 0.8f);
		Alien->SetExcited(true); // panting
	}

	// Up on the hind legs, and back down on all fours at the end.
	float Reach = 0.f;
	if (StepTime >= UpTime - 0.25f)
	{
		Reach = StepTime < UpTime ? EaseOut((StepTime - (UpTime - 0.25f)) / 0.25f)
			: 1.f - EaseInOut(FMath::Clamp((StepTime - DownTime) / 0.3f, 0.f, 1.f));
	}
	// Each paw in turn comes off the glass, goes up and lands on it again, scratching down it.
	const FVector Centre = GlassPointAtHeight(0.95f);
	FVector Paws[2];
	for (int32 Side = 0; Side < 2; ++Side)
	{
		const float U = (StepTime - ScratchFrom - Side * Period * 0.5f) / Period;
		float Off = 0.f;
		float Rise = -1.f;
		if (U > 0.f && U < Cycles)
		{
			const float Cycle = FMath::Frac(U);
			Off = Cycle < 0.5f ? FMath::Sin(Cycle * 2.f * PI) : 0.f;
			Rise = -FMath::Cos(Cycle * 2.f * PI);
		}
		Paws[Side] = Centre + Along * ((Side == 0 ? 0.2f : -0.2f) * H) - GlassNormal * (0.14f * H * Off) + FVector(0.f, 0.f, 0.06f * H * Rise);
	}
	Body->SetFrontReach(Paws[0], Paws[1], Reach);

	// The glass hears every paw: both landing, then each scratch.
	const int32 Taps = 2 + 2 * Cycles;
	while (Counter < Taps)
	{
		const float When = Counter < 2 ? UpTime + 0.06f * Counter : ScratchFrom + (Counter - 1) * Period * 0.5f;
		if (StepTime < When)
		{
			break;
		}
		const int32 Side = Counter < 2 ? Counter : (Counter - 2) % 2;
		HitGlass(Paws[Side], Counter < 2 ? 0.35f : bAnnoyed ? 0.4f : 0.25f);
		if (Counter == 1)
		{
			Voice(bAnnoyed ? EAlienVoice::Alert : EAlienVoice::Held, bAnnoyed ? 1.f : 0.8f); // a snarl, or a whine
		}
		++Counter;
	}
	if (Counter == Taps && StepTime >= DownTime + 0.28f)
	{
		++Counter;
		if (UAlienSoundComponent* Sounds = Alien->GetSounds())
		{
			Sounds->PlayFootstep(0.9f); // front paws back on the ground
		}
	}
	return DownTime + 0.45f;
}

void UAlienActionComponent::TickGlassReact(float Dt)
{
	const bool bEnter = Entering();
	const float H = WorldHeight();
	const float S = Alien->GetScaleFactor();
	AAlienChamber* Case = GetChamber();
	UAlienSoundComponent* Sounds = Alien->GetSounds();
	if (!Case)
	{
		StopAction();
		return;
	}
	const bool bAnnoyed = GlassMood == EGlassMood::Annoyed;
	const FVector Along = FVector::CrossProduct(GlassNormal, FVector::UpVector).GetSafeNormal(); // across the glass

	// Stinkfly flies up to where the knock was.
	auto RiseToTap = [this, H, S, Case, Dt]()
	{
		const float Room = FMath::Max(0.f, static_cast<float>(Case->GetInnerSize().Z) * Case->GetChamberScale() - H - 4.f * S);
		const float Lift = FMath::Clamp(static_cast<float>(SavedPoint.Z - Feet().Z) - H * 0.5f, 0.f, Room);
		Body->SetExtraLift(FMath::FInterpTo(Body->GetExtraLift(), Lift / S, Dt, 4.f));
	};

	switch (Step)
	{
	case 0: // the head snaps to the sound
	{
		const bool bStartled = GlassMood == EGlassMood::Startled;
		const float Duration = bStartled ? 0.6f : 0.35f;
		if (bEnter)
		{
			Alien->SetLookTarget(SavedPoint);
			Alien->TurnToward(SavedPoint);
			if (bStartled)
			{
				Voice(EAlienVoice::Alert);
				Body->PlayClip(EAlienClip::Hit, false, 0.08f); // a model with a flinch of its own (Benwolf) uses it
				if (!bHovers)
				{
					// Grey Matter jumps out of his skin; the others flinch with a little hop.
					const float Jump = TapStyle == EAlienTapStyle::Inspect ? 170.f : 90.f;
					Alien->LaunchCharacter(FVector(0.f, 0.f, Jump * FMath::Sqrt(S)), false, true);
				}
			}
			else if (bAnnoyed)
			{
				Voice(EAlienVoice::Effort);
			}
			else if (Rng.FRand() < 0.6f)
			{
				Voice(EAlienVoice::Alert, 0.6f);
			}
		}
		const float A = FMath::Min(1.f, StepTime / Duration);
		if (bStartled)
		{
			const float Flinch = Bell(A);
			Body->SetActionTransform(FVector(1.f + 0.06f * Flinch, 1.f + 0.06f * Flinch, 1.f - 0.12f * Flinch), FRotator(8.f * Flinch, 0.f, 0.f));
		}
		else
		{
			Body->SetActionTransform(FVector::OneVector, FRotator(0.f, 0.f, 10.f * Bell(A))); // a curious tilt
		}
		if (A < 1.f)
		{
			break;
		}
		Body->ClearActionTransform();
		const bool bOwnAnswer = TapStyle == EAlienTapStyle::Paw || TapStyle == EAlienTapStyle::Bang
			|| TapStyle == EAlienTapStyle::Snap || TapStyle == EAlienTapStyle::Bump;
		if (bAnnoyed && !bOwnAnswer && Actions.Num() > 0)
		{
			// Enough: its show-off move (Echo Echo screams, Diamondhead bursts crystals, Ghostfreak phases...).
			const EAlienAction Answer = Actions[0];
			StopAction();
			StartAction(Answer);
			return;
		}
		if (TapStyle == EAlienTapStyle::Growl && GlassMood != EGlassMood::Curious)
		{
			StopAction();
			StartAction(EAlienAction::Howl); // a hard knock gets a howl back
			return;
		}
		if (TapStyle == EAlienTapStyle::Roll)
		{
			ForcedDirection = (SavedPoint - Alien->GetActorLocation()).GetSafeNormal2D();
			StopAction();
			if (!StartAction(EAlienAction::Roll))
			{
				ForcedDirection = FVector::ZeroVector;
			}
			return;
		}
		NextStep();
		break;
	}
	case 1: // over to the glass where it was tapped
	{
		if (bEnter)
		{
			TargetPoint = Case->ClampToMovementBounds(SavedPoint, CapsuleRadius() + 1.5f * S);
			TargetPoint.Z = Alien->GetActorLocation().Z;
			float Speed = 1.4f;
			if (TapStyle == EAlienTapStyle::Rush)
			{
				Speed = FMath::Max(4.f, 200.f / FMath::Max(5.f, WalkSpeed)); // XLR8 is simply there
			}
			else if (TapStyle == EAlienTapStyle::Paw || TapStyle == EAlienTapStyle::Snap || TapStyle == EAlienTapStyle::Bang)
			{
				Speed = bAnnoyed ? 2.2f : 1.8f;
			}
			else if (TapStyle == EAlienTapStyle::Face)
			{
				Speed = 2.f; // a ghost glides straight over
			}
			Alien->SetSpeedMultiplier(Speed);
			Alien->MoveToPoint(TargetPoint, 1.f);
			if (TapStyle == EAlienTapStyle::Bump)
			{
				SetGhost(true); // flies over the props
				if (Sounds)
				{
					Sounds->SetLoopBoost(2.2f, 1.15f);
				}
			}
		}
		Alien->SetLookTarget(SavedPoint);
		if (TapStyle == EAlienTapStyle::Rush)
		{
			Timer += Dt;
			if (Timer >= 0.045f)
			{
				Timer = 0.f;
				SpawnAfterImage(0.45f, 0.2f);
			}
		}
		if (TapStyle == EAlienTapStyle::Bump)
		{
			RiseToTap();
		}
		const bool bThere = FVector::Dist2D(Alien->GetActorLocation(), TargetPoint) <= FMath::Max(3.f * S, CapsuleRadius() * 0.35f);
		if (bThere || !Alien->IsMoving() || Alien->IsStuck() || StepTime > 3.f)
		{
			Alien->StopMoving();
			Alien->SetSpeedMultiplier(1.f);
			Alien->TurnToward(SavedPoint);
			Counter = 0;
			Timer = 0.f;
			NextStep();
		}
		break;
	}
	case 2: // at the glass: its answer
	{
		Alien->SetLookTarget(SavedPoint);
		float Length = 1.4f;
		switch (TapStyle)
		{
		case EAlienTapStyle::Paw: // Wildmutt: a sniff, then up on his hind legs with his front paws on the glass
			if (Body->CanReachFront())
			{
				Length = PawAtGlass(bEnter, Along);
				break;
			}
			[[fallthrough]]; // a body that can't reach up lunges at the glass instead
		case EAlienTapStyle::Bang: // Four Arms: bangs it back and the glass shudders
		case EAlienTapStyle::Snap: // Ripjaws: lunges and snaps his jaws
		{
			const bool bPaw = TapStyle == EAlienTapStyle::Paw;
			const int32 Strikes = bPaw ? (bAnnoyed ? 3 : 2) : (bAnnoyed ? 2 : 1);
			const float First = bPaw ? 0.55f : 0.35f;
			const float Gap = bPaw ? 0.32f : 0.5f;
			const float StrikeHeight = bPaw ? 0.45f : TapStyle == EAlienTapStyle::Snap ? 0.85f : 0.72f; // paw, jaws, fists
			// Lunging at the glass: a body that tips forward from its feet gets part of the way by leaning.
			const float Tip = bPaw ? 16.f : -8.f;
			const float LeanReach = Body->IsRigged() || bPaw ? 0.f : StrikeHeight * H * FMath::Sin(FMath::DegreesToRadians(-Tip));
			const float MaxLunge = 0.5f * H;
			if (bEnter)
			{
				if (bPaw)
				{
					Sfx(TEXT("Move.Pounce.Sniff"), 0.8f);
				}
				else
				{
					Voice(TapStyle == EAlienTapStyle::Snap ? EAlienVoice::Alert : EAlienVoice::Effort);
				}
				Body->PlayClip(EAlienClip::Attack, false, 0.1f);
				GlassGap = FMath::Max(0.f, GapToGlass(StrikeHeight) - LeanReach);
			}
			// Every strike: fast into the glass (the hit at the peak), slower back.
			const float Start = First - Gap * 0.5f;
			const bool bStriking = StepTime >= Start && StepTime < Start + Gap * Strikes;
			const float Phase = bStriking ? FMath::Fmod(StepTime - Start, Gap) / Gap : 0.f;
			const float Reach = !bStriking ? 0.f : Phase < 0.5f ? FMath::Square(Phase / 0.5f) : 1.f - EaseInOut((Phase - 0.5f) / 0.5f);
			Body->SetActionTransform(FVector(1.f, 1.f, 1.f + 0.04f * Reach), FRotator(Tip * Reach, 0.f, 0.f), FVector(FMath::Min(GlassGap, MaxLunge) * Reach, 0.f, 0.f));
			if (Counter < Strikes && StepTime >= First + Gap * Counter)
			{
				++Counter;
				const float Force = TapStyle == EAlienTapStyle::Bang ? (bAnnoyed ? 1.f : 0.85f) : TapStyle == EAlienTapStyle::Snap ? 0.5f : 0.35f;
				if (GlassGap <= MaxLunge) // blocked further back, it strikes at the air
				{
					HitGlass(GlassPointAtHeight(StrikeHeight) + Along * (Rng.FRandRange(-0.08f, 0.08f) * H), Force);
				}
				if (TapStyle == EAlienTapStyle::Snap)
				{
					Voice(EAlienVoice::Alert, 0.8f);
				}
				else if (TapStyle == EAlienTapStyle::Bang)
				{
					Blast(Feet(), H * 2.f, 90.f * S, FVector::ZeroVector, 0.5f); // the props jump
				}
			}
			Length = First + Gap * Strikes + 0.3f;
			break;
		}
		case EAlienTapStyle::Face:  // Ghostfreak: his face right up to the glass, whispering
		case EAlienTapStyle::Smear: // Upchuck: squishes his face on it and leaves a smear
		{
			const bool bSmear = TapStyle == EAlienTapStyle::Smear;
			if (bEnter)
			{
				GlassGap = FaceGapToGlass(TargetPoint);
			}
			const float MaxLean = 0.4f * H;
			const bool bReaches = GlassGap <= MaxLean;
			const float Duration = bSmear ? 2.1f : 2.4f;
			const float T = StepTime / Duration;
			// In until the face is on the glass - a ghost's comes on through it - a moment there, then back.
			const float Lean = T < 0.3f ? EaseInOut(T / 0.3f) : T < 0.72f ? 1.f : 1.f - EaseInOut((T - 0.72f) / 0.28f);
			const float Through = bReaches && !bSmear ? 0.1f * H : 0.f;
			const float Front = Body->GetModelFront() * S;
			if (bSmear)
			{
				// Squashed flat on it (shorter front to back, a bit wider), rubbing it about.
				const float Rub = bReaches && T >= 0.3f && T < 0.72f ? FMath::Sin((StepTime - 0.3f * Duration) * 2.f * PI * 2.f) : 0.f;
				Body->SetActionTransform(FVector(1.f - 0.08f * Lean, 1.f + 0.05f * Lean, 1.f - 0.03f * Lean), FRotator(0.f, 6.f * Rub, 0.f),
					FVector((FMath::Min(GlassGap, MaxLean) + (bReaches ? 0.08f * Front : 0.f)) * Lean, 0.f, 0.f));
			}
			else
			{
				Body->SetActionTransform(FVector::OneVector, FRotator::ZeroRotator, FVector((FMath::Min(GlassGap, MaxLean) + Through) * Lean, 0.f, 0.f));
			}
			if (Counter == 0 && T >= 0.3f)
			{
				Counter = 1;
				if (bReaches)
				{
					HitGlass(TargetPoint, 0.15f);
					if (bSmear)
					{
						// Upchuck's slime, Heatblast's scorch: in the colour of its effects.
						Case->MarkGlass(TargetPoint, GlassNormal, H * 0.32f, FMath::Lerp(ActionColor, FLinearColor::White, 0.15f), 9.f, 0.6f);
					}
				}
				Voice(EAlienVoice::Call, 0.8f); // a whisper, a laugh; a gurgle
			}
			Length = Duration;
			break;
		}
		case EAlienTapStyle::Inspect: // Grey Matter: studies the spot, head tilting one way, then the other
		{
			if (bEnter)
			{
				Voice(EAlienVoice::Call, 0.8f);
			}
			const float Tilt = FMath::Sin(StepTime / 1.8f * 2.f * PI) * Bell(FMath::Min(1.f, StepTime / 1.8f));
			Body->SetActionTransform(FVector::OneVector, FRotator(-6.f * Bell(FMath::Min(1.f, StepTime / 1.8f)), 0.f, 14.f * Tilt));
			Length = 1.8f;
			break;
		}
		case EAlienTapStyle::Rush: // XLR8: taps back, twice, before you can blink
		{
			if (Counter < 2 && StepTime >= 0.2f + 0.14f * Counter)
			{
				++Counter;
				// Right where you tapped, if he can reach that high: he mirrors you.
				const float Reachable = FMath::Clamp(static_cast<float>(SavedPoint.Z - Feet().Z) / FMath::Max(1.f, H), 0.35f, 0.9f);
				const FVector Spot = GlassPointAtHeight(Reachable);
				const FVector Yours = SavedPoint - GlassNormal * static_cast<float>(FVector::DotProduct(SavedPoint - Spot, GlassNormal));
				HitGlass(FVector(Yours.X, Yours.Y, Spot.Z) + Along * (Counter == 1 ? -0.03f : 0.03f) * H, 0.2f);
			}
			if (Counter == 2 && StepTime >= 0.55f)
			{
				Counter = 3;
				Voice(EAlienVoice::Call, 0.8f);
			}
			Length = 1.f;
			break;
		}
		case EAlienTapStyle::Circuits: // Upgrade: green circuit lines race across the glass from the spot
		{
			if (bEnter)
			{
				Voice(EAlienVoice::Call);
			}
			if (Counter == 0 && StepTime >= 0.25f)
			{
				Counter = 1;
				const FVector From = SavedPoint - GlassNormal * 0.6f * S;
				Case->RippleGlass(SavedPoint, -GlassNormal, 0.6f, ActionColor);
				for (int32 k = 0; k < 10; ++k)
				{
					const float Angle = FMath::DegreesToRadians(k * 36.f + Rng.FRandRange(-12.f, 12.f));
					const FVector Dir = (Along * FMath::Cos(Angle) + FVector::UpVector * FMath::Sin(Angle)).GetSafeNormal();
					const float LineLength = Rng.FRandRange(6.f, 15.f) * S;
					const int32 Index = SpawnFx(EFx::Cube, From, FRotationMatrix::MakeFromZX(Dir, GlassNormal).ToQuat(),
						FVector(0.5f * S, 0.25f * S, 0.5f * S), FVector(0.5f * S, 0.25f * S, LineLength), ActionColor, 1.4f, 0.9f, 2.4f);
					if (FxStates.IsValidIndex(Index))
					{
						FxStates[Index].bFromBase = true; // grows out from the tapped spot
						FxStates[Index].Base = From;
					}
				}
			}
			Length = 1.6f;
			break;
		}
		case EAlienTapStyle::Bump: // Stinkfly: buzzes against the glass like a bug at a window
		{
			RiseToTap();
			const int32 Bumps = bAnnoyed ? 5 : 3;
			const float Gap = bAnnoyed ? 0.24f : 0.35f;
			const float Phase = FMath::Fmod(FMath::Max(0.f, StepTime - 0.15f), Gap) / Gap;
			const bool bBumping = StepTime >= 0.15f && StepTime < 0.15f + Gap * Bumps;
			Body->SetActionTransform(FVector::OneVector, FRotator(bBumping ? -14.f * FMath::Sin(Phase * PI) : 0.f, 0.f, 0.f));
			if (Counter < Bumps && StepTime >= 0.15f + Gap * (Counter + 0.5f))
			{
				++Counter;
				HitGlass(FVector(SavedPoint.X, SavedPoint.Y, Feet().Z + Body->GetExtraLift() * S + H * 0.55f) + Along * Rng.FRandRange(-0.1f, 0.1f) * H, 0.2f);
			}
			Length = 0.15f + Gap * Bumps + 0.2f;
			break;
		}
		case EAlienTapStyle::Echo: // Echo Echo: a little sonic ring against the glass
		{
			if (bEnter)
			{
				Voice(EAlienVoice::Alert);
				const FVector From = Feet() + FVector(0.f, 0.f, H * 0.7f);
				const FVector Dir = (GlassPointAtHeight(0.7f) - From).GetSafeNormal();
				const float Distance = static_cast<float>(FVector::Dist(GlassPointAtHeight(0.7f), From));
				const float Speed = 90.f * S;
				EventTime = FMath::Clamp(Distance / Speed, 0.1f, 0.8f);
				const int32 Index = SpawnFx(EFx::Ring, From, UpAlong(Dir), FVector(H * 0.1f), FVector(H * 0.5f),
					FMath::Lerp(ActionColor, FLinearColor::White, 0.35f), EventTime, 0.85f, 2.6f);
				if (FxStates.IsValidIndex(Index))
				{
					FxStates[Index].Velocity = Dir * Speed;
				}
			}
			if (Counter == 0 && StepTime >= EventTime)
			{
				Counter = 1;
				HitGlass(GlassPointAtHeight(0.7f), 0.45f);
			}
			Length = 1.2f;
			break;
		}
		case EAlienTapStyle::Crystal: // Diamondhead: a little crystal grows on the glass where you tapped
		{
			if (Counter == 0 && StepTime >= 0.3f)
			{
				Counter = 1;
				Sfx(TEXT("Move.Crystal.Charge"), 0.7f);
				Case->RippleGlass(SavedPoint, -GlassNormal, 0.4f, ActionColor);
				for (int32 k = 0; k < 3; ++k)
				{
					const FVector Axis = (-GlassNormal + FVector(0.f, 0.f, Rng.FRandRange(-0.4f, 0.6f)) + Along * Rng.FRandRange(-0.5f, 0.5f)).GetSafeNormal();
					const float Tall = H * Rng.FRandRange(0.1f, 0.2f);
					const float Wide = Tall * 0.32f;
					const FVector Base = SavedPoint - GlassNormal * 0.5f * S + Along * Rng.FRandRange(-2.f, 2.f) * S + FVector(0.f, 0.f, Rng.FRandRange(-2.f, 2.f) * S);
					const int32 Index = SpawnFx(EFx::Cone, Base, UpAlong(Axis), FVector(Wide, Wide, Tall), FVector(Wide, Wide, Tall), ActionColor, 2.2f, 0.85f, 2.2f);
					if (FxStates.IsValidIndex(Index))
					{
						FxStates[Index].bPop = true;
						FxStates[Index].bFromBase = true;
						FxStates[Index].Base = Base;
					}
				}
			}
			Length = 1.6f;
			break;
		}
		case EAlienTapStyle::Vine: // Wildvine: a vine reaches out and taps back
		{
			if (bEnter)
			{
				EnsureVines();
				VineTargets.Reset();
				VineTargets.Add(SavedPoint - GlassNormal * 0.8f * S);
			}
			const float Grow = StepTime < 0.4f ? EaseOut(StepTime / 0.4f) : StepTime < 0.9f ? 1.f : 1.f - EaseInOut((StepTime - 0.9f) / 0.4f);
			UpdateVines(Grow);
			if (Counter == 0 && StepTime >= 0.42f)
			{
				Counter = 1;
				HitGlass(SavedPoint, 0.3f);
				Sfx(TEXT("Move.Vine.Lash"), 0.6f);
			}
			Length = 1.35f;
			break;
		}
		case EAlienTapStyle::Clones: // Ditto: and here come his clones too
			StopAction();
			StartAction(EAlienAction::Clone);
			return;
		case EAlienTapStyle::Growl:   // Benwolf (a light tap): ears up, a low growl
		case EAlienTapStyle::Curious:
		default:
		{
			const bool bGrowl = TapStyle == EAlienTapStyle::Growl;
			if (bEnter)
			{
				GlassGap = FaceGapToGlass(TargetPoint);
				if (bGrowl || Rng.FRand() < 0.5f)
				{
					Voice(EAlienVoice::Call, 0.8f); // Benwolf: a low growl, or a sniff
				}
			}
			// Nose up to the glass (a curious one only halfway), a moment, back.
			const float MaxLean = 0.35f * H;
			const float Duration = bGrowl ? 2.f : 1.5f;
			const float T = StepTime / Duration;
			const float Lean = T < 0.3f ? EaseInOut(T / 0.3f) : T < 0.7f ? 1.f : 1.f - EaseInOut((T - 0.7f) / 0.3f);
			const float Reach = FMath::Min(GlassGap, MaxLean) * (bGrowl ? 1.f : 0.5f);
			Body->SetActionTransform(FVector::OneVector, FRotator(-6.f * Lean, 0.f, 0.f), FVector(Reach * Lean, 0.f, 0.f));
			if (bGrowl && Counter == 0 && T >= 0.3f)
			{
				Counter = 1;
				if (GlassGap <= MaxLean)
				{
					Case->MarkGlass(TargetPoint, GlassNormal, 0.2f * H, FLinearColor(0.6f, 0.65f, 0.7f), 3.5f, 0.28f); // his breath fogs it
				}
			}
			Length = Duration;
			break;
		}
		}
		if (StepTime >= Length)
		{
			Body->ClearActionTransform();
			NextStep();
		}
		break;
	}
	case 3: // a look at whoever tapped, then back to its day
	{
		if (bEnter)
		{
			FVector Viewer;
			if (GetViewer(Viewer))
			{
				Alien->SetLookTarget(Viewer);
			}
			Body->ClearActionTransform();
			if (Sounds)
			{
				Sounds->SetLoopBoost(1.f, 1.f);
			}
		}
		if (TapStyle == EAlienTapStyle::Bump)
		{
			Body->SetExtraLift(FMath::FInterpTo(Body->GetExtraLift(), 0.f, Dt, 3.f));
		}
		if (StepTime >= 1.2f)
		{
			StopAction();
		}
		break;
	}
	default:
		StopAction();
		break;
	}
}

// ---------------------------------------------------------------------------------------------
// Clone (Ditto, Echo Echo): two copies step out and merge back
// ---------------------------------------------------------------------------------------------

void UAlienActionComponent::TickClone(float Dt)
{
	const bool bEnter = Entering();
	UStaticMeshComponent* Model = Body->GetModelComponent();
	if (!Model)
	{
		StopAction();
		return;
	}
	const float H = WorldHeight();
	const FLinearColor Flash = FMath::Lerp(ActionColor, FLinearColor::White, 0.5f);
	float Out = 0.f;
	switch (Step)
	{
	case 0: // split
		if (bEnter)
		{
			EnsureClones();
			// Step out sideways, or forwards / backwards when the glass is too close.
			CloneOffsets.Reset();
			const FVector From = Alien->GetActorLocation();
			const float Gap = CapsuleRadius() * 2.2f;
			for (int32 Side = -1; Side <= 1; Side += 2)
			{
				FVector Offset = Alien->GetActorRightVector() * (Side * Gap);
				if (DistanceToGlass(From, Offset, CapsuleRadius()) < Gap)
				{
					Offset = Forward() * (Side * Gap);
					if (DistanceToGlass(From, Offset, CapsuleRadius()) < Gap)
					{
						Offset *= 0.5f;
					}
				}
				CloneOffsets.Add(Offset);
			}
			for (UStaticMeshComponent* Clone : Clones)
			{
				Clone->SetStaticMesh(Model->GetStaticMesh());
				Clone->SetVisibility(true);
			}
			const FVector Middle = Feet() + FVector(0.f, 0.f, H * 0.5f);
			const float Size = FitInCase(Middle, H * 1.1f);
			SpawnFx(EFx::Sphere, Middle, FQuat::Identity, FVector(Size * 0.3f), FVector(Size), Flash, 0.35f, 0.7f, 3.f);
			Alien->SetExcited(true);
			Sfx(TEXT("Move.Clone.Split"));
		}
		Out = EaseOut(StepTime / 0.45f);
		if (StepTime >= 0.45f)
		{
			NextStep();
		}
		break;
	case 1: // stand together
		Out = 1.f;
		if (StepTime >= 1.8f)
		{
			NextStep();
		}
		break;
	case 2: // merge
		if (bEnter)
		{
			Sfx(TEXT("Move.Clone.Merge"));
		}
		Out = 1.f - EaseInOut(StepTime / 0.45f);
		if (StepTime >= 0.45f)
		{
			const FVector Middle = Feet() + FVector(0.f, 0.f, H * 0.5f);
			const float Size = FitInCase(Middle, H * 1.2f);
			SpawnFx(EFx::Sphere, Middle, FQuat::Identity, FVector(Size * 0.3f), FVector(Size), Flash, 0.3f, 0.7f, 3.f);
			StopAction();
			return;
		}
		break;
	default:
		StopAction();
		return;
	}

	// The copies mirror the body's pose every frame, with a little life of their own.
	const FTransform Source = Model->GetComponentTransform();
	for (int32 i = 0; i < Clones.Num() && i < CloneOffsets.Num(); ++i)
	{
		const float Wiggle = Step == 1 ? FMath::Sin(StepTime * 3.f + i * 2.f) : 0.f;
		FTransform Copy = Source;
		Copy.SetLocation(Source.GetLocation() + CloneOffsets[i] * Out + FVector(0.f, 0.f, FMath::Abs(Wiggle) * H * 0.02f));
		Copy.SetRotation(FQuat(FVector::UpVector, FMath::DegreesToRadians(12.f * Wiggle)) * Source.GetRotation());
		Copy.SetScale3D(Source.GetScale3D() * FMath::Lerp(0.85f, 1.f, Out));
		Clones[i]->SetWorldTransform(Copy);
	}
}

// ---------------------------------------------------------------------------------------------
// Spit (Upchuck): eats a loose prop and spits a bouncing energy ball
// ---------------------------------------------------------------------------------------------

void UAlienActionComponent::TickSpit(float Dt)
{
	const bool bEnter = Entering();
	const float H = WorldHeight();
	const float S = Alien->GetScaleFactor();
	const FVector Mouth = Feet() + FVector(0.f, 0.f, H * 0.7f) + Forward() * CapsuleRadius() * 0.6f;
	switch (Step)
	{
	case 0: // walk to the nearest loose prop
	{
		UChamberHabitatComponent* Home = GetHabitat();
		UStaticMeshComponent* Food = Home ? Home->FindNearestLooseProp(Alien->GetActorLocation()) : nullptr;
		if (!Food)
		{
			GoToStep(2); // nothing to eat: just spit
			break;
		}
		const FVector FoodAt = Food->GetComponentLocation();
		const float Reach = CapsuleRadius() + static_cast<float>(Food->Bounds.SphereRadius) + 3.f * S;
		if (FVector::Dist2D(FoodAt, Alien->GetActorLocation()) <= Reach)
		{
			Alien->StopMoving();
			Alien->SetSpeedMultiplier(1.f);
			Alien->TurnToward(FoodAt);
			EatenProp = Food;
			NextStep();
			break;
		}
		if (bEnter)
		{
			Alien->SetSpeedMultiplier(1.6f);
		}
		Timer -= Dt;
		if (Timer <= 0.f)
		{
			Timer = 0.4f; // it may roll away: aim again now and then
			const FVector Approach = (FoodAt - Alien->GetActorLocation()).GetSafeNormal2D();
			Alien->MoveToPoint(FoodAt - Approach * (Reach - 2.f * S), 1.f);
		}
		if (StepTime > 5.f || Alien->IsStuck())
		{
			Alien->StopMoving();
			Alien->SetSpeedMultiplier(1.f);
			GoToStep(2);
		}
		break;
	}
	case 1: // pull it into the mouth and swallow
	{
		UStaticMeshComponent* Food = EatenProp.Get();
		if (!Food)
		{
			GoToStep(2);
			break;
		}
		if (bEnter)
		{
			Food->SetSimulatePhysics(false);
			Food->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Sfx(TEXT("Move.Spit.Chomp"));
			EatenStart = Food->GetComponentLocation();
			EatenScale = Food->GetComponentScale();
		}
		const float A = FMath::Min(1.f, StepTime / 0.4f);
		Food->SetWorldLocation(FMath::Lerp(EatenStart, Mouth, EaseInOut(A)));
		Food->SetWorldScale3D(EatenScale * FMath::Lerp(1.f, 0.15f, A));
		Body->SetActionTransform(FVector(1.f + 0.05f * A), FRotator(-10.f * Bell(A), 0.f, 0.f));
		if (A >= 1.f)
		{
			FinishEating();
			Sfx(TEXT("Move.Spit.Gulp"));
			NextStep();
		}
		break;
	}
	case 2: // turn to the visitor while the belly swells
	{
		if (bEnter)
		{
			FaceViewer();
			Sfx(TEXT("Move.Spit.Swell"));
		}
		const float A = FMath::Min(1.f, StepTime / 0.7f);
		const float Gulp = Bell(FMath::Min(1.f, StepTime / 0.3f)) * 0.08f;
		const float Width = 1.05f + 0.1f * A + Gulp;
		Body->SetActionTransform(FVector(Width, Width, 1.f - 0.03f * A), FRotator(4.f * A, 0.f, 0.f));
		if (A >= 1.f)
		{
			NextStep();
		}
		break;
	}
	case 3: // spit
	{
		if (bEnter)
		{
			const FVector Dir = Forward();
			LaunchPhysicsBall(Mouth + Dir * H * 0.08f, (Dir * 160.f + FVector(0.f, 0.f, 120.f)) * S, H * 0.2f, ActionColor, 3.5f, H * 0.9f, 100.f * S);
			Burst(Mouth, 6, H * 0.03f, H * 1.5f, ActionColor, 0.5f, H * 3.f, 0.3f);
			Sfx(TEXT("Move.Spit.Spit"));
		}
		const float A = FMath::Min(1.f, StepTime / 0.35f);
		const float Width = FMath::Lerp(1.15f, 1.f, EaseOut(A));
		Body->SetActionTransform(FVector(Width, Width, FMath::Lerp(0.97f, 1.f, A)), FRotator(-12.f * (1.f - A), 0.f, 0.f));
		if (A >= 1.f)
		{
			StopAction();
		}
		break;
	}
	default:
		StopAction();
		break;
	}
}

void UAlienActionComponent::FinishEating()
{
	if (UStaticMeshComponent* Food = EatenProp.Get())
	{
		if (UChamberHabitatComponent* Home = GetHabitat())
		{
			Home->ConsumeProp(Food, 9.f); // it grows back in its spot later
		}
		else
		{
			Food->SetVisibility(false);
		}
	}
	EatenProp = nullptr;
}

// ---------------------------------------------------------------------------------------------
// Pounce (Wildmutt, Ripjaws): sniffs, crouches and leaps across the case
// ---------------------------------------------------------------------------------------------

void UAlienActionComponent::TickPounce(float Dt)
{
	const bool bEnter = Entering();
	const float H = WorldHeight();
	const float S = Alien->GetScaleFactor();
	UChamberHabitatComponent* Home = GetHabitat();
	const bool bWater = Home && Home->IsWater();
	switch (Step)
	{
	case 0: // sniff the air, turned towards the target
	{
		if (bEnter)
		{
			AAlienChamber* Case = GetChamber();
			FVector Viewer;
			if (Case && GetViewer(Viewer) && Rng.FRand() < 0.6f)
			{
				TargetPoint = Case->ClampToMovementBounds(Viewer, CapsuleRadius() + 2.f * S); // at the visitor
			}
			else if (!Case || !Case->FindRandomWanderPoint(Alien, Rng, TargetPoint))
			{
				StopAction();
				return;
			}
			Alien->TurnToward(TargetPoint);
			Sfx(TEXT("Move.Pounce.Sniff"), 0.8f);
		}
		const float Sniff = FMath::Sin(StepTime * 16.f) * Bell(StepTime / 1.2f);
		Body->SetActionTransform(FVector::OneVector, FRotator(-5.f + 6.f * Sniff, 0.f, 0.f));
		Timer += Dt;
		if (Timer >= 0.3f)
		{
			// Scent drifting into the nose.
			Timer = 0.f;
			const FVector Nose = Feet() + FVector(0.f, 0.f, H * 0.6f) + Forward() * CapsuleRadius();
			const int32 Index = SpawnFx(EFx::Sphere, Nose + Forward() * H * 0.25f, FQuat::Identity, FVector(H * 0.05f), FVector(H * 0.01f),
				FLinearColor(0.8f, 0.85f, 0.9f), 0.4f, 0.35f, 0.8f);
			if (FxStates.IsValidIndex(Index))
			{
				FxStates[Index].Velocity = -Forward() * H * 0.6f;
			}
		}
		if (StepTime >= 1.2f)
		{
			NextStep();
		}
		break;
	}
	case 1: // crouch, then leap
	{
		if (bEnter)
		{
			Voice(EAlienVoice::Effort);
		}
		const float A = FMath::Min(1.f, StepTime / 0.35f);
		Body->SetActionTransform(FVector(1.f + 0.05f * A, 1.f + 0.05f * A, 1.f - 0.18f * A), FRotator(-10.f * A, 0.f, 0.f));
		if (A >= 1.f)
		{
			// A ballistic arc that lands on the target (or short of the glass).
			FVector To = TargetPoint - Alien->GetActorLocation();
			To.Z = 0.f;
			const float Distance = FMath::Min(static_cast<float>(To.Size()), DistanceToGlass(Alien->GetActorLocation(), To, CapsuleRadius()));
			const float Gravity = FMath::Abs(Alien->GetCharacterMovement()->GetGravityZ());
			const float Flight = FMath::Clamp(Distance / (110.f * S), 0.35f, 0.7f);
			Alien->LaunchCharacter(To.GetSafeNormal() * (Distance / Flight) + FVector(0.f, 0.f, 0.5f * Gravity * Flight), true, true);
			Sfx(TEXT("Move.Pounce.Leap"));
			if (bWater)
			{
				GroundRing(FVector(Feet().X, Feet().Y, WaterSurfaceZ()), H * 0.3f, H * 1.4f, DustColor(), 0.5f, 0.6f);
			}
			else
			{
				DustPuff(Feet(), H * 0.35f, 4);
			}
			NextStep();
		}
		break;
	}
	case 2: // in the air: nose up while rising, down while falling
	{
		const float Pitch = FMath::Clamp(static_cast<float>(Alien->GetVelocity().Z) / 250.f * 12.f, -12.f, 12.f);
		Body->SetActionTransform(FVector(0.95f, 0.95f, 1.08f), FRotator(Pitch, 0.f, 0.f));
		if ((StepTime > 0.12f && Alien->GetCharacterMovement()->IsMovingOnGround()) || StepTime > 1.3f)
		{
			const FVector At = Feet();
			if (bWater)
			{
				// Splash where it breaks the surface.
				const FVector Surface(At.X, At.Y, WaterSurfaceZ());
				GroundRing(Surface, H * 0.3f, H * 1.8f, DustColor(), 0.6f, 0.7f);
				Burst(Surface, 10, H * 0.03f, H * 1.6f, DustColor(), 0.6f, 900.f * S, 1.6f);
				SfxAt(TEXT("Move.Splash"), Surface);
			}
			else
			{
				GroundRing(At, H * 0.3f, H * 1.6f, DustColor(), 0.5f, 0.7f);
				DustPuff(At, H * 0.4f, 6);
				Sfx(TEXT("Move.Land"));
			}
			Blast(At, CapsuleRadius() * 3.f + H, 120.f * S, FVector::ZeroVector, 0.5f);
			NextStep();
		}
		break;
	}
	case 3: // absorb the landing
	{
		const float A = FMath::Min(1.f, StepTime / 0.35f);
		Body->SetActionTransform(FVector(1.f + 0.06f * (1.f - A), 1.f + 0.06f * (1.f - A), FMath::Lerp(0.82f, 1.f, EaseOutBack(A))));
		if (A >= 1.f)
		{
			StopAction();
		}
		break;
	}
	default:
		StopAction();
		break;
	}
}

// ---------------------------------------------------------------------------------------------
// Vines (Wildvine; Benmummy's bandages): vines lash out to the glass, and an exploding seed pod is thrown
// ---------------------------------------------------------------------------------------------

void UAlienActionComponent::TickVines(float Dt)
{
	const bool bEnter = Entering();
	const float H = WorldHeight();
	const float S = Alien->GetScaleFactor();
	switch (Step)
	{
	case 0: // tense up and pick where the vines go
	{
		if (bEnter)
		{
			EnsureVines();
			VineTargets.Reset();
			const FVector Chest = Feet() + FVector(0.f, 0.f, H * 0.55f);
			const float Start = Rng.FRandRange(0.f, 360.f);
			for (int32 i = 0; i < Vines.Num(); ++i)
			{
				const FVector Dir = FVector::ForwardVector.RotateAngleAxis(Start + i * 360.f / Vines.Num() + Rng.FRandRange(-20.f, 20.f), FVector::UpVector);
				const float Distance = DistanceToGlass(Chest, Dir, 0.f) + 2.5f * S;
				VineTargets.Add(Chest + Dir * Distance + FVector(0.f, 0.f, H * Rng.FRandRange(-0.25f, 0.4f)));
			}
		}
		const float A = FMath::Min(1.f, StepTime / 0.3f);
		Body->SetActionTransform(FVector(1.f + 0.05f * A));
		if (A >= 1.f)
		{
			NextStep();
		}
		break;
	}
	case 1: // lash out
	{
		if (bEnter)
		{
			Sfx(TEXT("Move.Vine.Lash"));
			Voice(EAlienVoice::Effort, 0.8f);
		}
		const float A = FMath::Min(1.f, StepTime / 0.45f);
		UpdateVines(EaseOut(A));
		if (A >= 1.f)
		{
			for (const FVector& Tip : VineTargets)
			{
				const FVector Normal = GlassNormalAt(Tip);
				SpawnFx(EFx::Ring, Tip + Normal * 0.5f, UpAlong(Normal), FVector(H * 0.05f), FVector(H * 0.35f), ActionColor, 0.45f, 0.7f, 2.f);
			}
			NextStep();
		}
		break;
	}
	case 2: // hold and sway; throw a seed pod
		UpdateVines(1.f);
		if (bVineSeedPod && Counter == 0 && StepTime >= 0.3f)
		{
			Counter = 1;
			const FVector Throw = RandomFlat(Rng) * Rng.FRandRange(40.f, 80.f) * S + FVector(0.f, 0.f, 170.f * S);
			LaunchPhysicsBall(Feet() + FVector(0.f, 0.f, H * 0.8f), Throw, H * 0.12f, FLinearColor(0.45f, 0.35f, 0.08f), 1.6f, H * 1.1f, 140.f * S);
			Sfx(TEXT("Move.Seed.Throw"));
		}
		if (StepTime >= 1.4f)
		{
			NextStep();
		}
		break;
	case 3: // pull back
	{
		if (bEnter)
		{
			Sfx(TEXT("Move.Vine.Retract"));
		}
		const float A = FMath::Min(1.f, StepTime / 0.45f);
		UpdateVines(1.f - EaseInOut(A));
		Body->SetActionTransform(FVector(1.05f - 0.05f * A));
		if (A >= 1.f)
		{
			StopAction();
		}
		break;
	}
	default:
		StopAction();
		break;
	}
}

void UAlienActionComponent::UpdateVines(float Grow)
{
	const float H = WorldHeight();
	const FVector Chest = Feet() + FVector(0.f, 0.f, H * 0.55f);
	const float Thick = H * 0.045f;
	for (int32 i = 0; i < Vines.Num() && i < VineTargets.Num(); ++i)
	{
		const FVector Sway = FVector(FMath::Sin(ActionTime * 3.1f + i), FMath::Cos(ActionTime * 2.7f + i * 1.3f), FMath::Sin(ActionTime * 2.f + i * 0.7f)) * H * 0.02f * Grow;
		const FVector Full = VineTargets[i] + Sway - Chest;
		const float Length = FMath::Max(0.5f, static_cast<float>(Full.Size()) * Grow);
		const FVector Dir = Full.GetSafeNormal();
		const FVector Tip = Chest + Dir * Length;
		const bool bShow = Grow > 0.02f;
		Vines[i]->SetVisibility(bShow);
		VineBuds[i]->SetVisibility(bShow);
		Vines[i]->SetWorldTransform(FTransform(UpAlong(Dir), (Chest + Tip) * 0.5f, FVector(Thick, Thick, Length) / 100.f));
		VineBuds[i]->SetWorldTransform(FTransform(FQuat::Identity, Tip, FVector(Thick * 2.2f / 100.f)));
	}
}

// ---------------------------------------------------------------------------------------------
// Melt (Upgrade): melts into a puddle, slides away leaving circuit lines, re-forms
// ---------------------------------------------------------------------------------------------

void UAlienActionComponent::TickMelt(float Dt)
{
	const bool bEnter = Entering();
	const float H = WorldHeight();
	const float R = CapsuleRadius();
	auto PlacePuddle = [this, H, R]()
	{
		Puddle->SetWorldTransform(FTransform(FQuat::Identity, Feet() + FVector(0.f, 0.f, H * 0.02f), FVector(R * 2.6f, R * 2.6f, H * 0.07f) / 100.f));
	};
	switch (Step)
	{
	case 0: // melt into the floor
	{
		if (bEnter)
		{
			EnsurePuddle();
			Sfx(TEXT("Move.Melt.Down"));
		}
		const float A = FMath::Min(1.f, StepTime / 0.5f);
		const float M = A * A;
		Body->SetActionTransform(FVector(1.f + 0.5f * M, 1.f + 0.5f * M, 1.f - 0.92f * M));
		if (A >= 0.85f && Body->IsBodyVisible())
		{
			Body->SetBodyVisible(false);
			Puddle->SetVisibility(true);
			PlacePuddle();
			GroundRing(Feet(), R, R * 3.f, ActionColor, 0.5f, 0.8f);
		}
		if (A >= 1.f)
		{
			SetGhost(true);
			Alien->SetSpeedMultiplier(2.2f);
			if (MoveToFarPoint(0.6f))
			{
				NextStep();
			}
			else
			{
				GoToStep(2);
			}
		}
		break;
	}
	case 1: // slide across the floor
	{
		PlacePuddle();
		Timer += Dt;
		if (Timer >= 0.09f)
		{
			// Glowing circuit lines left behind.
			Timer = 0.f;
			const FVector Velocity = Alien->GetVelocity();
			const FQuat Along = FRotationMatrix::MakeFromX(Velocity.SizeSquared2D() > 1.f ? Velocity.GetSafeNormal2D() : Forward()).ToQuat();
			const FVector Side = Along.GetRightVector() * Rng.FRandRange(-R, R) * 0.6f;
			const FVector Size(R * 0.8f, H * 0.012f, H * 0.006f);
			SpawnFx(EFx::Cube, Feet() + Side + FVector(0.f, 0.f, 0.3f), Along, Size, Size, ActionColor, 0.9f, 0.85f, 2.2f);
		}
		EventTime += Dt;
		if (EventTime >= 0.35f)
		{
			EventTime = 0.f;
			GroundRing(Feet() + FVector(0.f, 0.f, 0.4f), R * 1.6f, R * 2.4f, ActionColor, 0.5f, 0.5f);
		}
		if (!Alien->IsMoving() || Alien->IsStuck() || StepTime > 3.5f)
		{
			Alien->StopMoving();
			Alien->SetSpeedMultiplier(1.f);
			FaceViewer();
			NextStep();
		}
		break;
	}
	case 2: // re-form
	{
		if (bEnter)
		{
			if (Puddle)
			{
				Puddle->SetVisibility(false);
			}
			Body->SetBodyVisible(true);
			GroundRing(Feet(), R, R * 3.5f, ActionColor, 0.5f, 0.8f);
			Sfx(TEXT("Move.Melt.Up"));
		}
		const float A = FMath::Min(1.f, StepTime / 0.6f);
		const float Width = FMath::Lerp(1.5f, 1.f, EaseOut(A));
		Body->SetActionTransform(FVector(Width, Width, FMath::Lerp(0.08f, 1.f, EaseOutBack(A))));
		if (A >= 1.f)
		{
			StopAction();
		}
		break;
	}
	default:
		StopAction();
		break;
	}
}

// ---------------------------------------------------------------------------------------------
// Scurry (Grey Matter): quick little zig-zag dashes with hops
// ---------------------------------------------------------------------------------------------

void UAlienActionComponent::TickScurry(float Dt)
{
	const bool bEnter = Entering();
	if (bEnter)
	{
		if (Step == 0)
		{
			Alien->SetSpeedMultiplier(FMath::Max(3.f, 70.f / FMath::Max(5.f, WalkSpeed)));
			Alien->SetExcited(true);
		}
		if (!MoveToFarPoint(0.3f))
		{
			StopAction();
			return;
		}
		++Counter;
		Alien->LaunchCharacter(FVector(0.f, 0.f, 95.f * FMath::Sqrt(Alien->GetScaleFactor())), false, true);
		Sfx(TEXT("Move.Scurry.Hop"), 0.8f);
		if (Counter == 1)
		{
			Voice(EAlienVoice::Effort);
		}
		DustPuff(Feet(), WorldHeight() * 0.4f, 3);
	}
	Body->SetActionTransform(FVector(1.f, 1.f, 1.f + 0.05f * FMath::Sin(ActionTime * 30.f)), FRotator(-12.f, 0.f, 0.f));
	if (!Alien->IsMoving() || Alien->IsStuck() || StepTime > 1.2f)
	{
		if (Counter >= 6)
		{
			StopAction();
		}
		else
		{
			NextStep(); // each leg is a step
		}
	}
}

// ---------------------------------------------------------------------------------------------
// Fly (Stinkfly): takes off, circles around the case banking into the turns, lands
// ---------------------------------------------------------------------------------------------

void UAlienActionComponent::TickFly(float Dt)
{
	const bool bEnter = Entering();
	const float H = WorldHeight();
	const float S = Alien->GetScaleFactor();
	AAlienChamber* Case = GetChamber();
	if (!Case)
	{
		StopAction();
		return;
	}
	const float Room = static_cast<float>(Case->GetInnerSize().Z) * Case->GetChamberScale() - H;
	const float Cruise = FMath::Max(0.f, FMath::Min(Room * 0.55f, Room - 6.f * S));

	// Bank into the turns.
	const float Yaw = static_cast<float>(Alien->GetActorRotation().Yaw);
	const float YawRate = Dt > 0.f ? FRotator::NormalizeAxis(Yaw - LastYaw) / Dt : 0.f;
	LastYaw = Yaw;
	Amount = FMath::FInterpTo(Amount, FMath::Clamp(YawRate * 0.12f, -28.f, 28.f), Dt, 4.f);

	switch (Step)
	{
	case 0: // take off
	{
		if (bEnter)
		{
			SetGhost(true); // flies over the props
			Alien->SetExcited(true);
			GroundRing(Feet(), H * 0.3f, H * 1.4f, DustColor(), 0.6f, 0.6f);
			Sfx(TEXT("Move.Fly.TakeOff"));
			if (UAlienSoundComponent* Sounds = Alien->GetSounds())
			{
				Sounds->SetLoopBoost(2.4f, 1.18f);
			}
			DustPuff(Feet(), H * 0.4f, 5);
		}
		const float A = FMath::Min(1.f, StepTime / 0.8f);
		Body->SetExtraLift(Cruise * EaseInOut(A) / S);
		Alien->SetContactShadowScale(1.f - 0.55f * A);
		Body->SetActionTransform(FVector::OneVector, FRotator(-6.f * A, 0.f, 0.f));
		if (A >= 1.f)
		{
			Counter = 0;
			NextStep();
		}
		break;
	}
	case 1: // circle around the middle of the case
	{
		if (bEnter)
		{
			const FVector Local = Case->GetActorTransform().InverseTransformPosition(Alien->GetActorLocation());
			EventTime = FMath::RadiansToDegrees(FMath::Atan2(static_cast<float>(Local.Y), static_cast<float>(Local.X)));
			Timer = Rng.FRand() < 0.5f ? 1.f : -1.f; // which way round
			Alien->SetSpeedMultiplier(1.8f);
		}
		if (!Alien->IsMoving() || Alien->IsStuck())
		{
			EventTime += 55.f * Timer;
			++Counter;
			const FVector2D Half = Case->GetWalkHalfSize() * 0.55;
			const float Angle = FMath::DegreesToRadians(EventTime);
			Alien->MoveToPoint(Case->GetActorTransform().TransformPosition(FVector(FMath::Cos(Angle) * Half.X, FMath::Sin(Angle) * Half.Y, 0.0)), 1.f);
		}
		const float Bob = FMath::Sin(ActionTime * 2.4f) * H * 0.04f;
		Body->SetExtraLift((Cruise + Bob) / S);
		Body->SetActionTransform(FVector::OneVector, FRotator(-8.f, 0.f, Amount));
		if (Counter >= 8 || StepTime > 6.f)
		{
			Alien->StopMoving();
			Alien->SetSpeedMultiplier(1.f);
			NextStep();
		}
		break;
	}
	case 2: // land
	{
		if (bEnter)
		{
			SavedPoint.Z = Body->GetExtraLift();
			Sfx(TEXT("Move.Fly.Land"));
			if (UAlienSoundComponent* Sounds = Alien->GetSounds())
			{
				Sounds->SetLoopBoost(1.f, 1.f);
			}
		}
		const float A = FMath::Min(1.f, StepTime / 0.8f);
		Body->SetExtraLift(static_cast<float>(SavedPoint.Z) * (1.f - EaseInOut(A)));
		Alien->SetContactShadowScale(0.45f + 0.55f * A);
		Body->SetActionTransform(FVector::OneVector, FRotator(-8.f * (1.f - A), 0.f, Amount * (1.f - A)));
		if (A >= 1.f)
		{
			DustPuff(Feet(), H * 0.4f, 5);
			StopAction();
		}
		break;
	}
	default:
		StopAction();
		break;
	}
}

// ---------------------------------------------------------------------------------------------
// Movement and geometry helpers
// ---------------------------------------------------------------------------------------------

bool UAlienActionComponent::MoveToFarPoint(float MinFraction)
{
	AAlienChamber* Case = GetChamber();
	if (!Case)
	{
		return false;
	}
	const FVector From = Alien->GetActorLocation();
	const float MinDistance = MinFraction * static_cast<float>((Case->GetWalkHalfSize() * Case->GetChamberScale()).Size());
	FVector Best = From;
	float BestDistance = -1.f;
	for (int32 Attempt = 0; Attempt < 6; ++Attempt)
	{
		FVector Point;
		if (Case->FindRandomWanderPoint(Alien, Rng, Point))
		{
			const float Distance = static_cast<float>(FVector::Dist2D(Point, From));
			if (Distance > BestDistance)
			{
				Best = Point;
				BestDistance = Distance;
			}
			if (Distance >= MinDistance)
			{
				break;
			}
		}
	}
	if (BestDistance < 0.f)
	{
		return false;
	}
	TargetPoint = Best;
	Alien->MoveToPoint(Best, 1.f);
	return true;
}

FVector UAlienActionComponent::GlassNormalAt(const FVector& WorldPoint) const
{
	const AAlienChamber* Case = GetChamber();
	if (!Case)
	{
		return -Forward();
	}
	const FTransform& T = Case->GetActorTransform();
	const FVector Local = T.InverseTransformPosition(WorldPoint);
	FVector Normal;
	if (Case->Shape == EChamberShape::Round)
	{
		Normal = -FVector(Local.X, Local.Y, 0.0).GetSafeNormal();
	}
	else
	{
		// The side whose glass is nearest, relative to the case's proportions.
		const FVector2D Half = Case->GetWalkHalfSize();
		const double SideX = FMath::Abs(Local.X) / FMath::Max(1.0, Half.X);
		const double SideY = FMath::Abs(Local.Y) / FMath::Max(1.0, Half.Y);
		Normal = SideX >= SideY ? FVector(Local.X > 0.0 ? -1.0 : 1.0, 0.0, 0.0) : FVector(0.0, Local.Y > 0.0 ? -1.0 : 1.0, 0.0);
	}
	return Normal.IsNearlyZero() ? -Forward() : T.TransformVectorNoScale(Normal).GetSafeNormal2D();
}

float UAlienActionComponent::DistanceToGlass(const FVector& From, const FVector& Direction, float Margin) const
{
	const AAlienChamber* Case = GetChamber();
	const FVector Flat = Direction.GetSafeNormal2D();
	if (!Case || Flat.IsNearlyZero())
	{
		return 0.f;
	}
	// Work in chamber space (cm at scale 1), against the walkable area inside the glass.
	const FTransform& T = Case->GetActorTransform();
	const float Scale = Case->GetChamberScale();
	const FVector P = T.InverseTransformPosition(From);
	const FVector D = T.InverseTransformVectorNoScale(Flat).GetSafeNormal2D();
	const FVector2D Half = Case->GetWalkHalfSize() - FVector2D(Margin / Scale);
	double Best = 0.0;
	if (Case->Shape == EChamberShape::Round)
	{
		const double Radius = FMath::Max(0.0, Half.X);
		const double B = P.X * D.X + P.Y * D.Y;
		const double C = P.X * P.X + P.Y * P.Y - Radius * Radius;
		const double Disc = B * B - C;
		Best = Disc > 0.0 ? -B + FMath::Sqrt(Disc) : 0.0;
	}
	else
	{
		Best = TNumericLimits<double>::Max();
		if (FMath::Abs(D.X) > KINDA_SMALL_NUMBER)
		{
			Best = FMath::Min(Best, ((D.X > 0.0 ? Half.X : -Half.X) - P.X) / D.X);
		}
		if (FMath::Abs(D.Y) > KINDA_SMALL_NUMBER)
		{
			Best = FMath::Min(Best, ((D.Y > 0.0 ? Half.Y : -Half.Y) - P.Y) / D.Y);
		}
	}
	return static_cast<float>(FMath::Max(0.0, Best) * Scale);
}

bool UAlienActionComponent::GetViewer(FVector& OutLocation) const
{
	if (APlayerCameraManager* Camera = UGameplayStatics::GetPlayerCameraManager(this, 0))
	{
		OutLocation = Camera->GetCameraLocation();
		return true;
	}
	return false;
}

FVector UAlienActionComponent::TowardViewer() const
{
	FVector Viewer;
	if (GetViewer(Viewer))
	{
		const FVector To = (Viewer - Alien->GetActorLocation()).GetSafeNormal2D();
		if (!To.IsNearlyZero())
		{
			return To;
		}
	}
	return Forward();
}

void UAlienActionComponent::FaceViewer()
{
	FVector Viewer;
	if (GetViewer(Viewer))
	{
		Alien->TurnToward(Viewer);
	}
}

void UAlienActionComponent::SetGhost(bool bGhost)
{
	if (!Alien || bGhostMode == bGhost)
	{
		return;
	}
	bGhostMode = bGhost;
	Alien->GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_PhysicsBody, bGhost ? ECR_Ignore : ECR_Overlap);
}

float UAlienActionComponent::WorldHeight() const
{
	return (Body ? Body->GetModelHeight() : Height) * (Alien ? Alien->GetScaleFactor() : 1.f);
}

float UAlienActionComponent::CapsuleRadius() const
{
	return Alien->GetCapsuleComponent()->GetScaledCapsuleRadius();
}

FVector UAlienActionComponent::Feet() const
{
	return Alien->GetActorLocation() - FVector(0.f, 0.f, Alien->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
}

FVector UAlienActionComponent::Forward() const
{
	return Alien->GetActorForwardVector().GetSafeNormal2D();
}

AAlienChamber* UAlienActionComponent::GetChamber() const
{
	return Alien ? Alien->GetHomeChamber() : nullptr;
}

UChamberHabitatComponent* UAlienActionComponent::GetHabitat() const
{
	AAlienChamber* Case = GetChamber();
	return Case ? Case->GetHabitat() : nullptr;
}

float UAlienActionComponent::WaterSurfaceZ() const
{
	const AAlienChamber* Case = GetChamber();
	const UChamberHabitatComponent* Home = GetHabitat();
	if (Case && Home && Home->IsWater())
	{
		return static_cast<float>(Case->GetActorTransform().TransformPosition(FVector(0.f, 0.f, Home->GetGroundTopZ())).Z);
	}
	return static_cast<float>(Feet().Z);
}

void UAlienActionComponent::ShrinkCapsule(float WorldRadius)
{
	if (!Alien || bCapsuleShrunk)
	{
		return;
	}
	UCapsuleComponent* Capsule = Alien->GetCapsuleComponent();
	const float S = Alien->GetScaleFactor();
	SavedCapsuleRadius = Capsule->GetUnscaledCapsuleRadius();
	SavedCapsuleHalfHeight = Capsule->GetUnscaledCapsuleHalfHeight();
	const float Radius = FMath::Min(SavedCapsuleRadius, WorldRadius / S);
	Capsule->SetCapsuleSize(Radius, Radius);
	// Keep the bottom on the ground.
	Alien->SetActorLocation(Alien->GetActorLocation() - FVector(0.f, 0.f, (SavedCapsuleHalfHeight - Radius) * S), false, nullptr, ETeleportType::TeleportPhysics);
	bCapsuleShrunk = true;
}

void UAlienActionComponent::RestoreCapsule()
{
	if (!Alien || !bCapsuleShrunk)
	{
		return;
	}
	bCapsuleShrunk = false;
	UCapsuleComponent* Capsule = Alien->GetCapsuleComponent();
	const float S = Alien->GetScaleFactor();
	FVector Location = Alien->GetActorLocation();
	if (const AAlienChamber* Case = GetChamber())
	{
		// The full body needs more room than the ball: step back from the glass if necessary.
		const FVector Inside = Case->ClampToMovementBounds(Location, (SavedCapsuleRadius + 1.f) * S);
		Location.X = Inside.X;
		Location.Y = Inside.Y;
	}
	Location.Z += (SavedCapsuleHalfHeight - Capsule->GetUnscaledCapsuleHalfHeight()) * S;
	Capsule->SetCapsuleSize(SavedCapsuleRadius, SavedCapsuleHalfHeight);
	Alien->SetActorLocation(Location, false, nullptr, ETeleportType::TeleportPhysics);
}

float UAlienActionComponent::RoomAround(const FVector& WorldPoint) const
{
	const AAlienChamber* Case = GetChamber();
	if (!Case)
	{
		return 1000.f;
	}
	const FVector P = Case->GetActorTransform().InverseTransformPosition(WorldPoint);
	const FVector2D Half = Case->GetGlassHalfSize();
	const double Room = Case->Shape == EChamberShape::Round
		? Half.X - FVector2D(P.X, P.Y).Size()
		: FMath::Min(Half.X - FMath::Abs(P.X), Half.Y - FMath::Abs(P.Y));
	return FMath::Max(0.f, static_cast<float>(Room) * Case->GetChamberScale());
}

float UAlienActionComponent::FitInCase(const FVector& At, float Diameter) const
{
	return FMath::Min(Diameter, 2.f * RoomAround(At));
}

// ---------------------------------------------------------------------------------------------
// Pooled effects
// ---------------------------------------------------------------------------------------------

int32 UAlienActionComponent::SpawnFx(EFx Shape, const FVector& Location, const FQuat& Rotation, const FVector& Size0, const FVector& Size1,
	const FLinearColor& Color, float Life, float Opacity, float Intensity)
{
	if (Life <= 0.f)
	{
		return INDEX_NONE;
	}
	// A free slot, a new one, or else the one closest to its end.
	int32 Index = FxStates.IndexOfByPredicate([](const FFxState& State) { return State.Life < 0.f; });
	if (Index == INDEX_NONE && FxStates.Num() < MaxFx)
	{
		UStaticMeshComponent* Component = MakeMesh(TEXT("ActionFx"), MuseumAssets::SphereMesh(), nullptr, nullptr, true);
		if (!Component)
		{
			return INDEX_NONE;
		}
		Index = FxStates.Add(FFxState());
		FxComponents.Add(Component);
		FxGlowMaterials.Add(nullptr);
		FxRingMaterials.Add(nullptr);
	}
	if (Index == INDEX_NONE)
	{
		float MostDone = -1.f;
		for (int32 i = 0; i < FxStates.Num(); ++i)
		{
			const float Done = FxStates[i].Age / FMath::Max(0.01f, FxStates[i].Life);
			if (Done > MostDone)
			{
				MostDone = Done;
				Index = i;
			}
		}
	}

	UStaticMeshComponent* Component = FxComponents[Index];
	const bool bRing = Shape == EFx::Ring;
	UStaticMesh* Mesh = bRing ? MuseumAssets::PlaneMesh()
		: Shape == EFx::Cone ? MuseumAssets::ConeMesh()
		: Shape == EFx::Cylinder ? MuseumAssets::CylinderMesh()
		: Shape == EFx::Cube ? MuseumAssets::CubeMesh()
		: MuseumAssets::SphereMesh();
	if (Component->GetStaticMesh() != Mesh)
	{
		Component->SetStaticMesh(Mesh);
	}
	TObjectPtr<UMaterialInstanceDynamic>& Material = bRing ? FxRingMaterials[Index] : FxGlowMaterials[Index];
	if (!Material)
	{
		Material = MakeMaterial(bRing ? MuseumAssets::FXRingMaterial() : MuseumAssets::FXGlowMaterial(), Color);
	}
	if (Material)
	{
		Material->SetVectorParameterValue(MuseumAssets::Params::Color, Color);
		Material->SetScalarParameterValue(MuseumAssets::Params::Intensity, Intensity);
		Material->SetScalarParameterValue(MuseumAssets::Params::Opacity, 0.f);
		Component->SetMaterial(0, Material);
	}

	// Rings are drawn at 80% of a 100 cm plane, so a ring's Size is its diameter.
	auto ToScale = [bRing](const FVector& Size) { return bRing ? FVector(Size.X / 80.f, Size.Y / 80.f, 1.f) : Size / 100.f; };
	FFxState& State = FxStates[Index];
	State = FFxState();
	State.Life = Life;
	State.Scale0 = ToScale(Size0);
	State.Scale1 = ToScale(Size1);
	State.Opacity = Opacity;
	State.bRing = bRing;
	State.Base = Location;
	Component->SetWorldTransform(FTransform(Rotation, Location, State.Scale0));
	Component->SetVisibility(true);
	return Index;
}

void UAlienActionComponent::UpdateFx(float Dt)
{
	for (int32 i = 0; i < FxStates.Num(); ++i)
	{
		FFxState& State = FxStates[i];
		if (State.Life < 0.f)
		{
			continue;
		}
		UStaticMeshComponent* Component = FxComponents[i];
		State.Age += Dt;
		if (!Component || State.Age >= State.Life)
		{
			State.Life = -1.f;
			if (Component)
			{
				Component->SetVisibility(false);
			}
			continue;
		}

		const float T = State.Age / State.Life;
		FVector Scale;
		float Alpha;
		if (State.bPop)
		{
			const float Grow = EaseOutBack(T / 0.12f);
			const float Shrink = T > 0.8f ? 1.f - (T - 0.8f) / 0.2f : 1.f;
			Scale = State.Scale1 * FMath::Max(0.01f, Grow * Shrink);
			Alpha = State.Opacity;
		}
		else
		{
			Scale = FMath::Lerp(State.Scale0, State.Scale1, EaseOut(T));
			Alpha = State.Opacity * FMath::Min(1.f, T / 0.08f) * FMath::Min(1.f, (1.f - T) / 0.45f);
		}

		State.Velocity.Z -= State.Gravity * Dt;
		const FQuat Rotation = Component->GetComponentQuat();
		FVector Location;
		if (State.bFromBase)
		{
			State.Base += State.Velocity * Dt;
			Location = State.Base + Rotation.GetUpVector() * (Scale.Z * 50.f); // grows out of its base
		}
		else
		{
			Location = Component->GetComponentLocation() + State.Velocity * Dt;
		}
		Component->SetWorldTransform(FTransform(Rotation, Location, Scale));
		if (UMaterialInstanceDynamic* Material = State.bRing ? FxRingMaterials[i].Get() : FxGlowMaterials[i].Get())
		{
			Material->SetScalarParameterValue(MuseumAssets::Params::Opacity, Alpha);
		}
	}
}

void UAlienActionComponent::ClearFx()
{
	for (int32 i = 0; i < FxStates.Num(); ++i)
	{
		FxStates[i].Life = -1.f;
		if (FxComponents[i])
		{
			FxComponents[i]->SetVisibility(false);
		}
	}
}

void UAlienActionComponent::Burst(const FVector& At, int32 Count, float Size, float Speed, const FLinearColor& Color, float Life, float Gravity, float UpBias)
{
	for (int32 i = 0; i < Count; ++i)
	{
		const FVector Dir = (RandomFlat(Rng) + FVector(0.f, 0.f, UpBias * Rng.FRandRange(0.5f, 1.5f))).GetSafeNormal();
		const int32 Index = SpawnFx(EFx::Sphere, At, FQuat::Identity, FVector(Size), FVector(Size * 0.3f), Color, Life * Rng.FRandRange(0.7f, 1.2f), 0.95f, 3.f);
		if (FxStates.IsValidIndex(Index))
		{
			FxStates[Index].Velocity = Dir * Speed * Rng.FRandRange(0.5f, 1.f);
			FxStates[Index].Gravity = Gravity;
		}
	}
}

void UAlienActionComponent::DustPuff(const FVector& At, float Size, int32 Count)
{
	const FLinearColor Dust = DustColor();
	for (int32 i = 0; i < Count; ++i)
	{
		const FVector Out = RandomFlat(Rng);
		const int32 Index = SpawnFx(EFx::Sphere, At + Out * Size * 0.3f + FVector(0.f, 0.f, Size * 0.15f), FQuat::Identity,
			FVector(Size * 0.25f), FVector(Size * Rng.FRandRange(0.55f, 0.8f)), Dust, Rng.FRandRange(0.55f, 0.85f), 0.4f, 0.7f);
		if (FxStates.IsValidIndex(Index))
		{
			FxStates[Index].Velocity = (Out * 0.9f + FVector(0.f, 0.f, 0.35f)) * Size;
		}
	}
}

void UAlienActionComponent::GroundRing(const FVector& At, float From, float To, const FLinearColor& Color, float Life, float Opacity)
{
	To = FitInCase(At, To);
	From = FMath::Min(From, To * 0.4f);
	SpawnFx(EFx::Ring, At + FVector(0.f, 0.f, 0.3f), FQuat::Identity, FVector(From), FVector(To), Color, Life, Opacity, 2.f);
}

FLinearColor UAlienActionComponent::DustColor() const
{
	if (const UChamberHabitatComponent* Home = GetHabitat())
	{
		if (const UChamberHabitatAsset* Asset = Home->GetAsset())
		{
			switch (Asset->Ground)
			{
			case EHabitatGround::Water: return FMath::Lerp(Asset->GroundGlowColor, FLinearColor::White, 0.55f);
			case EHabitatGround::Lava: return EmberColor;
			case EHabitatGround::None: break;
			default: return FMath::Lerp(Asset->GroundColor, FLinearColor::White, 0.35f);
			}
		}
	}
	return FLinearColor(0.75f, 0.78f, 0.82f);
}

void UAlienActionComponent::Blast(const FVector& Center, float Radius, float Speed, const FVector& Direction, float UpBias)
{
	if (UChamberHabitatComponent* Home = GetHabitat())
	{
		Home->Blast(Center, Radius, Speed, Direction, UpBias);
	}
}

// ---------------------------------------------------------------------------------------------
// Head flames (Heatblast)
// ---------------------------------------------------------------------------------------------

void UAlienActionComponent::BuildFlames()
{
	DestroyFlames();
	FVector TopOffset = FVector::ZeroVector;
	USceneComponent* Parent = Body ? Body->GetHeadTop(TopOffset) : nullptr;
	AActor* Owner = GetOwner();
	if (!Parent || !Owner)
	{
		return;
	}
	FlameRoot = NewObject<USceneComponent>(Owner, MakeUniqueObjectName(Owner, USceneComponent::StaticClass(), TEXT("HeadFlames")));
	FlameRoot->SetupAttachment(Parent);
	FlameRoot->SetRelativeLocation(TopOffset);
	FlameRoot->RegisterComponent();

	// Five soft glowing cones: a big outer flame, two side tongues, one leaning back and a bright core.
	struct FFlameSpec
	{
		FVector Offset;   // height units
		FVector Axis;
		FVector Size;     // height units
		FLinearColor Color;
		float Opacity;
	};
	const FFlameSpec Specs[] = {
		{ FVector(0.f, 0.f, 0.12f), FVector(0.f, 0.f, 1.f), FVector(0.20f, 0.20f, 0.36f), FLinearColor(1.f, 0.30f, 0.04f), 0.55f },
		{ FVector(0.02f, 0.06f, 0.09f), FVector(0.f, 0.42f, 1.f), FVector(0.11f, 0.11f, 0.24f), FLinearColor(1.f, 0.50f, 0.08f), 0.65f },
		{ FVector(0.02f, -0.06f, 0.09f), FVector(0.f, -0.42f, 1.f), FVector(0.11f, 0.11f, 0.24f), FLinearColor(1.f, 0.50f, 0.08f), 0.65f },
		{ FVector(-0.05f, 0.f, 0.10f), FVector(-0.5f, 0.f, 1.f), FVector(0.13f, 0.13f, 0.26f), FLinearColor(1.f, 0.20f, 0.03f), 0.5f },
		{ FVector(0.01f, 0.f, 0.08f), FVector(0.f, 0.f, 1.f), FVector(0.10f, 0.10f, 0.20f), FLinearColor(1.f, 0.85f, 0.40f), 0.8f },
	};
	const float H = Body->GetModelHeight(); // local units: the flames scale with the alien
	for (const FFlameSpec& Spec : Specs)
	{
		UMaterialInstanceDynamic* Material = MakeMaterial(MuseumAssets::FXGlowMaterial(), Spec.Color, 2.4f, Spec.Opacity * 0.85f);
		if (Material)
		{
			Material->SetScalarParameterValue(MuseumAssets::Params::Softness, 1.5f); // soft edges read as fire
		}
		UStaticMeshComponent* Flame = MakeMesh(TEXT("HeadFlame"), MuseumAssets::ConeMesh(), Material, FlameRoot, false);
		const FTransform Base(UpAlong(Spec.Axis), Spec.Offset * H, Spec.Size * H / 100.f);
		Flame->SetRelativeTransform(Base);
		Flames.Add(Flame);
		FlameBase.Add(Base);
		FlamePhase.Add(Rng.FRandRange(0.f, 2.f * PI));
	}
}

void UAlienActionComponent::DestroyFlames()
{
	for (UStaticMeshComponent* Flame : Flames)
	{
		if (Flame)
		{
			Flame->DestroyComponent();
		}
	}
	if (FlameRoot)
	{
		FlameRoot->DestroyComponent();
	}
	Flames.Reset();
	FlameBase.Reset();
	FlamePhase.Reset();
	FlameRoot = nullptr;
	FlameBoost = 1.f;
}

void UAlienActionComponent::UpdateFlames(float Dt)
{
	if (Flames.Num() == 0)
	{
		return;
	}
	FlameTime += Dt;
	for (int32 i = 0; i < Flames.Num(); ++i)
	{
		UStaticMeshComponent* Flame = Flames[i];
		if (!Flame)
		{
			continue;
		}
		// Layered sines read as a flicker; the tongues also wobble a little.
		const float P = FlamePhase[i];
		const float Noise = 0.55f * FMath::Sin(FlameTime * 11.f + P) + 0.3f * FMath::Sin(FlameTime * 23.f + P * 2.3f) + 0.15f * FMath::Sin(FlameTime * 37.f + P * 0.7f);
		const FTransform& Base = FlameBase[i];
		const FVector BaseScale = Base.GetScale3D();
		const FVector Scale = BaseScale * FVector(1.f - 0.08f * Noise, 1.f - 0.08f * Noise, 1.f + 0.25f * Noise) * FlameBoost;
		const FQuat Wobble = FQuat(FVector::ForwardVector, 0.08f * FMath::Sin(FlameTime * 7.f + P)) * FQuat(FVector::RightVector, 0.08f * FMath::Sin(FlameTime * 5.3f + P * 1.7f));
		const FQuat Rotation = Wobble * Base.GetRotation();
		// Taller flames grow upwards from their base instead of sinking into the head.
		const FVector Location = Base.GetLocation() + Rotation.GetUpVector() * ((Scale.Z - BaseScale.Z) * 50.f);
		Flame->SetRelativeTransform(FTransform(Rotation, Location, Scale));
	}

	// Embers drift up out of the fire.
	EmberTimer += Dt * FlameBoost;
	if (EmberTimer >= 0.16f && FlameRoot)
	{
		EmberTimer = 0.f;
		const float H = WorldHeight();
		const FVector At = FlameRoot->GetComponentLocation() + RandomFlat(Rng) * H * 0.05f + FVector(0.f, 0.f, H * 0.08f);
		const int32 Index = SpawnFx(EFx::Sphere, At, FQuat::Identity, FVector(H * 0.018f), FVector(H * 0.005f),
			Rng.FRand() < 0.5f ? EmberColor : SparkColor, Rng.FRandRange(0.7f, 1.2f), 0.95f, 4.f);
		if (FxStates.IsValidIndex(Index))
		{
			FxStates[Index].Velocity = FVector(Rng.FRandRange(-0.15f, 0.15f), Rng.FRandRange(-0.15f, 0.15f), Rng.FRandRange(0.5f, 0.9f)) * H;
			FxStates[Index].Gravity = -H * 0.3f;
		}
	}
}

// ---------------------------------------------------------------------------------------------
// Footsteps
// ---------------------------------------------------------------------------------------------

void UAlienActionComponent::UpdateFootsteps(float Dt)
{
	const UChamberHabitatComponent* Home = GetHabitat();
	const UChamberHabitatAsset* HomeWorld = Home ? Home->GetAsset() : nullptr;
	const UCharacterMovementComponent* Move = Alien->GetCharacterMovement();
	const float Speed = static_cast<float>(Alien->GetVelocity().Size2D());
	if (!HomeWorld || bHovers || Alien->IsHeld() || !Body->IsBodyVisible() || !Move->IsMovingOnGround()
		|| Speed < 0.25f * Move->MaxWalkSpeed || (bPerforming && Current != EAlienAction::Scurry && Current != EAlienAction::Dash))
	{
		FootstepDistance = 0.f;
		return;
	}
	const float H = WorldHeight();
	FootstepDistance += Speed * Dt;
	if (FootstepDistance < 0.35f * H)
	{
		return;
	}
	FootstepDistance = 0.f;
	bLeftFoot = !bLeftFoot;
	if (UAlienSoundComponent* Sounds = Alien->GetSounds())
	{
		Sounds->PlayFootstep();
	}
	const FVector Foot = Feet() + Alien->GetActorRightVector() * ((bLeftFoot ? -0.12f : 0.12f) * H);
	switch (HomeWorld->Ground)
	{
	case EHabitatGround::Water:
		GroundRing(FVector(Foot.X, Foot.Y, WaterSurfaceZ()), H * 0.1f, H * 0.45f, DustColor(), 0.7f, 0.45f);
		break;
	case EHabitatGround::Lava:
		Burst(Foot + FVector(0.f, 0.f, H * 0.02f), 2, H * 0.015f, H * 0.6f, EmberColor, 0.5f, -H * 0.3f, 1.5f);
		break;
	case EHabitatGround::Tech:
	case EHabitatGround::Crystal:
	case EHabitatGround::None:
		break;
	default:
	{
		// Soft dust, lower and fainter than a move's puff.
		const int32 Index = SpawnFx(EFx::Sphere, Foot + FVector(0.f, 0.f, H * 0.02f), FQuat::Identity, FVector(H * 0.04f),
			FVector(H * 0.12f, H * 0.12f, H * 0.06f), DustColor(), 0.6f, 0.25f, 0.6f);
		if (FxStates.IsValidIndex(Index))
		{
			FxStates[Index].Velocity = (FVector(0.f, 0.f, 0.15f) - Alien->GetVelocity().GetSafeNormal2D() * 0.25f) * H;
		}
		break;
	}
	}
}

// ---------------------------------------------------------------------------------------------
// Sounds
// ---------------------------------------------------------------------------------------------

void UAlienActionComponent::Sfx(FName Name, float Volume, float Pitch)
{
	if (UAlienSoundComponent* Sounds = Alien ? Alien->GetSounds() : nullptr)
	{
		Sounds->PlayEffect(Name, Volume, Pitch);
	}
}

void UAlienActionComponent::SfxAt(FName Name, const FVector& At, float Volume)
{
	if (UAlienSoundComponent* Sounds = Alien ? Alien->GetSounds() : nullptr)
	{
		Sounds->PlayEffectAt(Name, At, Volume);
	}
}

void UAlienActionComponent::Voice(EAlienVoice Kind, float Volume)
{
	if (UAlienSoundComponent* Sounds = Alien ? Alien->GetSounds() : nullptr)
	{
		Sounds->PlayVoice(Kind, Volume);
	}
}

// ---------------------------------------------------------------------------------------------
// After-images (XLR8, Ghostfreak)
// ---------------------------------------------------------------------------------------------

void UAlienActionComponent::SpawnAfterImage(float Opacity, float Life)
{
	UStaticMeshComponent* Model = Body ? Body->GetModelComponent() : nullptr;
	if (!Model || !Model->GetStaticMesh() || !Body->IsBodyVisible()) // a rigged model's static twin is hidden but placed
	{
		return;
	}
	if (AfterImages.Num() < MaxAfterImages)
	{
		UMaterialInstanceDynamic* Material = MakeMaterial(MuseumAssets::FXGlowMaterial(), ActionColor, 1.6f, 0.f);
		if (Material)
		{
			Material->SetScalarParameterValue(MuseumAssets::Params::Softness, 0.6f);
		}
		UStaticMeshComponent* Ghost = MakeMesh(TEXT("AfterImage"), nullptr, nullptr, nullptr, true);
		if (!Ghost)
		{
			return;
		}
		AfterImages.Add(Ghost);
		AfterImageMaterials.Add(Material);
		AfterImageAge.Add(0.f);
		AfterImageLife.Add(-1.f);
		AfterImageOpacity.Add(0.f);
	}
	const int32 Index = NextAfterImage % AfterImages.Num();
	NextAfterImage = Index + 1;
	UStaticMeshComponent* Ghost = AfterImages[Index];
	if (Ghost->GetStaticMesh() != Model->GetStaticMesh())
	{
		Ghost->SetStaticMesh(Model->GetStaticMesh());
		for (int32 Slot = 0; Slot < Ghost->GetNumMaterials(); ++Slot)
		{
			Ghost->SetMaterial(Slot, AfterImageMaterials[Index]);
		}
	}
	Ghost->SetWorldTransform(Model->GetComponentTransform());
	Ghost->SetVisibility(true);
	AfterImageAge[Index] = 0.f;
	AfterImageLife[Index] = Life;
	AfterImageOpacity[Index] = Opacity;
	if (AfterImageMaterials[Index])
	{
		AfterImageMaterials[Index]->SetScalarParameterValue(MuseumAssets::Params::Opacity, Opacity);
	}
}

void UAlienActionComponent::UpdateAfterImages(float Dt)
{
	for (int32 i = 0; i < AfterImages.Num(); ++i)
	{
		if (AfterImageLife[i] < 0.f)
		{
			continue;
		}
		AfterImageAge[i] += Dt;
		const float T = AfterImageAge[i] / FMath::Max(0.01f, AfterImageLife[i]);
		if (T >= 1.f)
		{
			AfterImageLife[i] = -1.f;
			AfterImages[i]->SetVisibility(false);
			continue;
		}
		if (AfterImageMaterials[i])
		{
			AfterImageMaterials[i]->SetScalarParameterValue(MuseumAssets::Params::Opacity, AfterImageOpacity[i] * (1.f - T));
		}
	}
}

// ---------------------------------------------------------------------------------------------
// Move props (created the first time a move needs them)
// ---------------------------------------------------------------------------------------------

void UAlienActionComponent::EnsureBall()
{
	if (Ball)
	{
		return;
	}
	if (UStaticMesh* Form = BallForm.LoadSynchronous())
	{
		// The alien's own rolled-up model (Cannonbolt's ball). It spins around its bounds centre.
		Ball = MakeMesh(TEXT("ActionBall"), Form, nullptr, nullptr, true);
		const FBoxSphereBounds Bounds = Form->GetBounds();
		BallMeshCenter = Bounds.Origin;
		BallMeshRadius = FMath::Max(0.1f, static_cast<float>(Bounds.BoxExtent.GetMax()));
	}
	else
	{
		// Built-in armoured ball: shell in the action colour with two dark seams.
		Ball = MakeMesh(TEXT("ActionBall"), MuseumAssets::SphereMesh(), MakeLitMaterial(ActionColor, 0.35f, 0.15f), nullptr, true);
		UMaterialInstanceDynamic* Seam = MakeLitMaterial(FLinearColor(0.06f, 0.06f, 0.07f), 0.5f, 0.02f);
		for (int32 i = 0; i < 2; ++i)
		{
			UStaticMeshComponent* Band = MakeMesh(TEXT("ActionBallSeam"), MuseumAssets::CylinderMesh(), Seam, Ball, false);
			Band->SetRelativeRotation(i == 0 ? FRotator(90.f, 0.f, 0.f) : FRotator(0.f, 0.f, 90.f));
			Band->SetRelativeScale3D(FVector(1.03f, 1.03f, 0.16f));
		}
		BallMeshCenter = FVector::ZeroVector;
		BallMeshRadius = 50.f; // engine sphere
	}
	Ball->SetVisibility(false, true);
}

void UAlienActionComponent::EnsureClones()
{
	while (Clones.Num() < 2)
	{
		UStaticMeshComponent* Clone = MakeMesh(TEXT("ActionClone"), nullptr, nullptr, nullptr, true);
		if (!Clone)
		{
			return;
		}
		Clone->SetVisibility(false);
		Clones.Add(Clone);
	}
}

void UAlienActionComponent::EnsureVines()
{
	if (Vines.Num() > 0)
	{
		return;
	}
	UMaterialInstanceDynamic* Stem = MakeLitMaterial(VineColor, 0.6f, 0.1f);
	UMaterialInstanceDynamic* Bud = MakeLitMaterial(ActionColor, 0.5f, 0.3f);
	for (int32 i = 0; i < 4; ++i)
	{
		UStaticMeshComponent* Vine = MakeMesh(TEXT("ActionVine"), MuseumAssets::CylinderMesh(), Stem, nullptr, true);
		UStaticMeshComponent* Tip = MakeMesh(TEXT("ActionVineBud"), MuseumAssets::SphereMesh(), Bud, nullptr, true);
		if (!Vine || !Tip)
		{
			return;
		}
		Vine->SetVisibility(false);
		Tip->SetVisibility(false);
		Vines.Add(Vine);
		VineBuds.Add(Tip);
	}
}

void UAlienActionComponent::EnsurePuddle()
{
	if (!Puddle)
	{
		// Liquid metal: black and glossy.
		Puddle = MakeMesh(TEXT("ActionPuddle"), MuseumAssets::SphereMesh(), MakeLitMaterial(FLinearColor(0.015f, 0.02f, 0.015f), 0.12f, 0.05f), nullptr, true);
		if (Puddle)
		{
			Puddle->SetVisibility(false);
		}
	}
}

void UAlienActionComponent::LaunchPhysicsBall(const FVector& From, const FVector& Velocity, float Diameter, const FLinearColor& Color, float Life, float PopRadius, float PopSpeed)
{
	PopPhysicsBall();
	if (!PhysicsBall)
	{
		BouncyMaterial = NewObject<UPhysicalMaterial>(this);
		BouncyMaterial->Restitution = 0.7f;
		BouncyMaterial->Friction = 0.4f;
		BouncyMaterial->bOverrideRestitutionCombineMode = true;
		BouncyMaterial->RestitutionCombineMode = EFrictionCombineMode::Max;

		PhysicsBallMaterial = MakeMaterial(MuseumAssets::FXGlowMaterial(), Color, 2.5f, 0.95f);
		PhysicsBall = MakeMesh(TEXT("ActionEnergyBall"), MuseumAssets::SphereMesh(), PhysicsBallMaterial, nullptr, false);
		if (!PhysicsBall)
		{
			return;
		}
		// Bounces off the glass, the ground and the props; flies straight out of the alien's mouth.
		PhysicsBall->SetCollisionProfileName(UCollisionProfile::PhysicsActor_ProfileName);
		PhysicsBall->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
		PhysicsBall->SetCollisionResponseToChannel(ECC_Visibility, ECR_Ignore);
		PhysicsBall->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
		PhysicsBall->SetPhysMaterialOverride(BouncyMaterial);
		PhysicsBall->SetUseCCD(true);
		PhysicsBall->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}
	PhysicsBallColor = Color;
	if (PhysicsBallMaterial)
	{
		PhysicsBallMaterial->SetVectorParameterValue(MuseumAssets::Params::Color, Color);
	}
	PhysicsBall->SetWorldTransform(FTransform(FQuat::Identity, From, FVector(Diameter / 100.f)));
	PhysicsBall->SetVisibility(true);
	PhysicsBall->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	PhysicsBall->SetMassOverrideInKg(NAME_None, 0.08f, true);
	PhysicsBall->SetSimulatePhysics(true);
	PhysicsBall->SetPhysicsLinearVelocity(Velocity);
	bPhysicsBallActive = true;
	PhysicsBallLife = Life;
	PhysicsBallPopRadius = PopRadius;
	PhysicsBallPopSpeed = PopSpeed;
	PhysicsBallTrail = 0.f;
}

void UAlienActionComponent::UpdatePhysicsBall(float Dt)
{
	if (!bPhysicsBallActive || !PhysicsBall)
	{
		return;
	}
	PhysicsBallLife -= Dt;
	PhysicsBallTrail += Dt;
	const FVector At = PhysicsBall->GetComponentLocation();
	if (PhysicsBallTrail >= 0.07f)
	{
		PhysicsBallTrail = 0.f;
		const float Size = static_cast<float>(PhysicsBall->GetComponentScale().X) * 100.f;
		SpawnFx(EFx::Sphere, At, FQuat::Identity, FVector(Size * 0.6f), FVector(Size * 0.1f), PhysicsBallColor, 0.3f, 0.6f, 2.5f);
	}
	// Pops when its time is up, or if it somehow left the case.
	const AAlienChamber* Case = GetChamber();
	if (PhysicsBallLife <= 0.f || (Case && !Case->IsInsideMovementBounds(At, -20.f)))
	{
		PopPhysicsBall();
	}
}

void UAlienActionComponent::PopPhysicsBall()
{
	if (!bPhysicsBallActive || !PhysicsBall)
	{
		return;
	}
	bPhysicsBallActive = false;
	const FVector At = PhysicsBall->GetComponentLocation();
	const float Size = static_cast<float>(PhysicsBall->GetComponentScale().X) * 100.f;
	const float S = Alien ? Alien->GetScaleFactor() : 1.f;
	if (!(Alien && Alien->IsHeld()))
	{
		Burst(At, 8, Size * 0.3f, Size * 5.f, PhysicsBallColor, 0.55f, 500.f * S, 0.8f);
		SpawnFx(EFx::Sphere, At, FQuat::Identity, FVector(Size), FVector(Size * 3.f), PhysicsBallColor, 0.3f, 0.6f, 3.f);
		Blast(At, PhysicsBallPopRadius, PhysicsBallPopSpeed, FVector::ZeroVector, 0.6f);
		SfxAt(Current == EAlienAction::Vines ? TEXT("Move.Seed.Pop") : TEXT("Move.Energy.Pop"), At);
	}
	PhysicsBall->SetSimulatePhysics(false);
	PhysicsBall->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	PhysicsBall->SetVisibility(false);
}

// ---------------------------------------------------------------------------------------------
// Component helpers
// ---------------------------------------------------------------------------------------------

UStaticMeshComponent* UAlienActionComponent::MakeMesh(const TCHAR* Name, UStaticMesh* Mesh, UMaterialInterface* Material, USceneComponent* Parent, bool bAbsolute)
{
	AActor* Owner = GetOwner();
	if (!Owner)
	{
		return nullptr;
	}
	UStaticMeshComponent* Component = NewObject<UStaticMeshComponent>(Owner, MakeUniqueObjectName(Owner, UStaticMeshComponent::StaticClass(), Name));
	Component->SetMobility(EComponentMobility::Movable);
	Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Component->SetGenerateOverlapEvents(false);
	Component->SetCanEverAffectNavigation(false);
	Component->SetCastShadow(false);
	if (Mesh)
	{
		Component->SetStaticMesh(Mesh);
	}
	if (Material)
	{
		Component->SetMaterial(0, Material);
	}
	Component->SetupAttachment(Parent ? Parent : Owner->GetRootComponent());
	if (bAbsolute)
	{
		Component->SetAbsolute(true, true, true); // placed in world space
	}
	Component->RegisterComponent();
	return Component;
}

UMaterialInstanceDynamic* UAlienActionComponent::MakeMaterial(UMaterialInterface* Base, const FLinearColor& Color, float Intensity, float Opacity)
{
	if (!Base)
	{
		return nullptr;
	}
	UMaterialInstanceDynamic* Material = UMaterialInstanceDynamic::Create(Base, this);
	Material->SetVectorParameterValue(MuseumAssets::Params::Color, Color);
	if (Intensity >= 0.f)
	{
		Material->SetScalarParameterValue(MuseumAssets::Params::Intensity, Intensity);
	}
	if (Opacity >= 0.f)
	{
		Material->SetScalarParameterValue(MuseumAssets::Params::Opacity, Opacity);
	}
	return Material;
}

UMaterialInstanceDynamic* UAlienActionComponent::MakeLitMaterial(const FLinearColor& Color, float Roughness, float SelfIllum)
{
	UMaterialInstanceDynamic* Material = MakeMaterial(MuseumAssets::EnvLitMaterial(), Color);
	if (Material)
	{
		Material->SetScalarParameterValue(MuseumAssets::Params::Roughness, Roughness);
		Material->SetScalarParameterValue(MuseumAssets::Params::SelfIllum, SelfIllum);
	}
	return Material;
}
