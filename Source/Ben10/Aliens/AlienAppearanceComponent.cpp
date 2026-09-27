// Alien Museum - builds and animates the placeholder alien body from an AlienDataAsset.

#include "Aliens/AlienAppearanceComponent.h"
#include "Aliens/AlienCharacter.h"
#include "Core/MuseumAssets.h"
#include "Ben10.h"
#include "Components/PoseableMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "GameFramework/Actor.h"

namespace
{
	/** Basic shapes are 100 cm across, so a diameter in cm maps to scale Diameter/100. */
	FVector SizeToScale(float X, float Y, float Z)
	{
		return FVector(X, Y, Z) / 100.f;
	}

	/** Two-bone IK in one plane: where the middle joint and the tip go so the tip reaches Target. */
	void SolveTwoBone(const FVector& Root, const FVector& Target, const FVector& BendDirection, float UpperLength, float LowerLength,
		FVector& OutJoint, FVector& OutTip)
	{
		const FVector ToTarget = Target - Root;
		const float Distance = static_cast<float>(ToTarget.Size());
		const FVector Along = Distance > KINDA_SMALL_NUMBER ? ToTarget / Distance : FVector::DownVector;
		const float Reach = FMath::Clamp(Distance, FMath::Abs(UpperLength - LowerLength) + 0.01f, (UpperLength + LowerLength) * 0.999f);
		FVector Bend = BendDirection - Along * FVector::DotProduct(BendDirection, Along);
		if (!Bend.Normalize())
		{
			Bend = FVector::ForwardVector;
		}
		const float CosA = FMath::Clamp((UpperLength * UpperLength + Reach * Reach - LowerLength * LowerLength) / (2.f * UpperLength * Reach), -1.f, 1.f);
		OutJoint = Root + (Along * CosA + Bend * FMath::Sqrt(1.f - CosA * CosA)) * UpperLength;
		OutTip = Root + Along * Reach;
	}

	TAutoConsoleVariable<float> CVarRigTestSpeed(TEXT("Museum.RigTestSpeed"), 0.f,
		TEXT("Debug: rigged aliens step on the spot as if walking at this speed (cm/s). 0 = off."));

	/** The turn that points direction From along direction To. */
	FQuat TurnBetween(const FVector& From, const FVector& To)
	{
		return FQuat::FindBetweenNormals(From.GetSafeNormal(), To.GetSafeNormal());
	}

	FQuat TurnAround(const FVector& Axis, float Degrees)
	{
		return FQuat(Axis, FMath::DegreesToRadians(Degrees));
	}
}

UAlienAppearanceComponent::UAlienAppearanceComponent()
{
	PrimaryComponentTick.bCanEverTick = false; // the owning actor drives UpdateAnimation()
}

// ---------------------------------------------------------------------------------------------
// Component helpers
// ---------------------------------------------------------------------------------------------

USceneComponent* UAlienAppearanceComponent::AddPivot(const TCHAR* BaseName, USceneComponent* Parent, const FTransform& Relative)
{
	AActor* Owner = GetOwner();
	USceneComponent* Pivot = NewObject<USceneComponent>(Owner, MakeUniqueObjectName(Owner, USceneComponent::StaticClass(), BaseName));
	Pivot->SetupAttachment(Parent);
	Pivot->SetRelativeTransform(Relative);
	Pivot->RegisterComponent();
	Parts.Add(Pivot);
	return Pivot;
}

UStaticMeshComponent* UAlienAppearanceComponent::AddPart(const TCHAR* BaseName, UStaticMesh* Mesh, USceneComponent* Parent, const FTransform& Relative, UMaterialInterface* Material)
{
	AActor* Owner = GetOwner();
	UStaticMeshComponent* Part = NewObject<UStaticMeshComponent>(Owner, MakeUniqueObjectName(Owner, UStaticMeshComponent::StaticClass(), BaseName));
	Part->SetStaticMesh(Mesh);
	Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Part->SetGenerateOverlapEvents(false);
	Part->SetCanEverAffectNavigation(false);
	Part->SetCastShadow(bCastShadows);
	Part->SetupAttachment(Parent);
	Part->SetRelativeTransform(Relative);
	if (Material)
	{
		Part->SetMaterial(0, Material);
	}
	Part->RegisterComponent();
	Parts.Add(Part);
	return Part;
}

UStaticMesh* UAlienAppearanceComponent::GetShapeMesh(EAlienPartShape Shape) const
{
	switch (Shape)
	{
	case EAlienPartShape::Cylinder: return MuseumAssets::CylinderMesh();
	case EAlienPartShape::Cone: return MuseumAssets::ConeMesh();
	case EAlienPartShape::Cube: return MuseumAssets::CubeMesh();
	case EAlienPartShape::Sphere:
	default: return MuseumAssets::SphereMesh();
	}
}

void UAlienAppearanceComponent::ClearAppearance()
{
	// Destroy children first (reverse creation order).
	for (int32 i = Parts.Num() - 1; i >= 0; --i)
	{
		if (Parts[i])
		{
			Parts[i]->DestroyComponent();
		}
	}
	Parts.Reset();
	AntennaPivots.Reset();
	Eyes.Reset();
	EyeBaseScales.Reset();
	AntennaBaseRoll.Reset();
	AnimatedParts.Reset();
	ModelPartMotions.Reset();
	CustomMIDs.Reset();
	SkinMID = AccentMID = DarkMID = EyeMID = GlowMID = nullptr;
	BodyPivot = nullptr;
	HeadPivot = nullptr;
	FootL = nullptr;
	FootR = nullptr;
	ModelComponent = nullptr;
	RigComponent = nullptr;
	RigLegs.Reset();
	RigSpine.Reset();
	RigTail.Reset();
	RigFloating.Reset();
	RigRestLocal.Reset();
	RigNeck = RigHead = RigJaw = INDEX_NONE;
	RestMesh = nullptr;
	bIsModel = false;
	bBodyVisible = true;
}

// ---------------------------------------------------------------------------------------------
// Materials
// ---------------------------------------------------------------------------------------------

void UAlienAppearanceComponent::CreateMaterials(const UAlienDataAsset* Data)
{
	UMaterialInterface* OrganicBase = SkinMaterialOverride ? SkinMaterialOverride.Get() : MuseumAssets::AlienSkinMaterial();
	UMaterialInterface* SkinBase = (Data->SkinStyle == EAlienSkinStyle::Organic || SkinMaterialOverride)
		? OrganicBase
		: MuseumAssets::AlienTranslucentMaterial();
	UMaterialInterface* EmissiveBase = GlowMaterialOverride ? GlowMaterialOverride.Get() : MuseumAssets::EmissiveMaterial();

	const FLinearColor Rim = Data->RimColor.A > 0.f
		? FLinearColor(Data->RimColor.R, Data->RimColor.G, Data->RimColor.B, 1.f)
		: FMath::Lerp(Data->SkinColor, FLinearColor::White, 0.5f);

	auto MakeLit = [this](UMaterialInterface* Base, const FLinearColor& Color, const FLinearColor& RimColor) -> UMaterialInstanceDynamic*
	{
		if (!Base)
		{
			return nullptr;
		}
		UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(Base, this);
		MID->SetVectorParameterValue(MuseumAssets::Params::Color, Color);
		MID->SetVectorParameterValue(MuseumAssets::Params::RimColor, RimColor);
		return MID;
	};
	auto MakeGlow = [this, EmissiveBase](const FLinearColor& Color) -> UMaterialInstanceDynamic*
	{
		if (!EmissiveBase)
		{
			return nullptr;
		}
		UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(EmissiveBase, this);
		MID->SetVectorParameterValue(MuseumAssets::Params::Color, Color);
		MID->SetScalarParameterValue(MuseumAssets::Params::Intensity, 1.6f);
		return MID;
	};

	SkinMID = MakeLit(SkinBase, Data->SkinColor, Rim);
	if (SkinMID && SkinBase != OrganicBase)
	{
		// See-through styles.
		const bool bCrystal = Data->SkinStyle == EAlienSkinStyle::Crystal;
		SkinMID->SetScalarParameterValue(TEXT("BaseOpacity"), bCrystal ? 0.6f : 0.35f);
		SkinMID->SetScalarParameterValue(TEXT("EdgeOpacity"), bCrystal ? 0.95f : 0.8f);
		SkinMID->SetScalarParameterValue(TEXT("Brightness"), bCrystal ? 0.8f : 0.9f);
	}
	AccentMID = MakeLit(OrganicBase, Data->AccentColor, Data->SkinColor);
	DarkMID = MakeLit(OrganicBase, FLinearColor(0.02f, 0.02f, 0.025f), FLinearColor(0.15f, 0.15f, 0.17f));
	EyeMID = MakeGlow(Data->EyeColor);
	GlowMID = MakeGlow(Data->GlowColor);
}

UMaterialInterface* UAlienAppearanceComponent::GetPartMaterial(const FAlienBodyPart& Part)
{
	switch (Part.Color)
	{
	case EAlienPartColor::Accent: return AccentMID;
	case EAlienPartColor::Dark: return DarkMID;
	case EAlienPartColor::Eye: return EyeMID;
	case EAlienPartColor::Glow: return GlowMID;
	case EAlienPartColor::Custom:
	{
		const FLinearColor& C = Part.CustomColor;
		const FString Key = FString::Printf(TEXT("%.3f_%.3f_%.3f_%d"), C.R, C.G, C.B, Part.bCustomGlows ? 1 : 0);
		if (TObjectPtr<UMaterialInstanceDynamic>* Found = CustomMIDs.Find(Key))
		{
			return *Found;
		}
		UMaterialInterface* Base = Part.bCustomGlows
			? (GlowMaterialOverride ? GlowMaterialOverride.Get() : MuseumAssets::EmissiveMaterial())
			: (SkinMaterialOverride ? SkinMaterialOverride.Get() : MuseumAssets::AlienSkinMaterial());
		if (!Base)
		{
			return nullptr;
		}
		UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(Base, this);
		MID->SetVectorParameterValue(MuseumAssets::Params::Color, C);
		if (Part.bCustomGlows)
		{
			MID->SetScalarParameterValue(MuseumAssets::Params::Intensity, 1.6f);
		}
		else
		{
			MID->SetVectorParameterValue(MuseumAssets::Params::RimColor, FMath::Lerp(C, FLinearColor::White, 0.5f));
		}
		CustomMIDs.Add(Key, MID);
		return MID;
	}
	case EAlienPartColor::Skin:
	default:
		return SkinMID;
	}
}

// ---------------------------------------------------------------------------------------------
// Building
// ---------------------------------------------------------------------------------------------

void UAlienAppearanceComponent::BuildAppearance(const UAlienDataAsset* Data)
{
	ClearAppearance();
	if (!Data || !GetOwner())
	{
		return;
	}

	const float H = Data->Height;
	ModelHeight = H;
	Energy = Data->Energy;
	bHovers = Data->bHovers;
	HoverHeight = bHovers ? H * 0.10f : 0.f;

	// An imported model replaces the shape-built body.
	UStaticMesh* Model = Data->HasModel() ? Data->ModelMesh.LoadSynchronous() : nullptr;
	if (Data->HasModel() && !Model)
	{
		UE_LOG(LogAlienMuseum, Warning, TEXT("%s: model %s could not be loaded, using the shape body"),
			*Data->GetName(), *Data->ModelMesh.ToString());
	}
	bIsModel = Model != nullptr;
	if (!bIsModel)
	{
		CreateMaterials(Data);
	}

	// Everything that bobs / squashes / hovers hangs off the body pivot (origin = feet).
	BodyPivot = AddPivot(TEXT("AlienBodyPivot"), this, FTransform(FVector(0.f, 0.f, HoverHeight)));

	if (bIsModel)
	{
		BuildModelBody(Data, Model);
		if (!Data->RiggedMesh.IsNull())
		{
			if (USkeletalMesh* Rigged = Data->RiggedMesh.LoadSynchronous())
			{
				BuildRig(Data, Rigged);
			}
			else
			{
				UE_LOG(LogAlienMuseum, Warning, TEXT("%s: rigged model %s could not be loaded, showing the static model"),
					*Data->GetName(), *Data->RiggedMesh.ToString());
			}
		}
		AnimTime = FMath::FRand() * 10.f;
		HeadRotation = FRotator::ZeroRotator;
		return;
	}

	if (Data->BodyShape == EAlienBodyShape::Custom)
	{
		// Whole creature from parts; the head joint comes from the data.
		HeadBaseXY = FVector2D(Data->CustomHeadPivot.X * H, Data->CustomHeadPivot.Y * H);
		HeadBaseZ = Data->CustomHeadPivot.Z * H;
		HeadPivot = AddPivot(TEXT("AlienHeadPivot"), this, FTransform(FVector(HeadBaseXY.X, HeadBaseXY.Y, HeadBaseZ + HoverHeight)));
		ModelRadius = Data->CustomRadius * H;
		CollisionRadius = ModelRadius;
		HeadTopOffset = 0.1f * H; // raised by the head parts below
	}
	else
	{
		HeadBaseXY = FVector2D::ZeroVector;
		BuildBuiltInBody(Data);
		CollisionRadius = ModelRadius * 0.8f;
	}

	for (const FAlienBodyPart& Part : Data->Parts)
	{
		AddDataPart(Part, false);
		if (Part.bMirror)
		{
			AddDataPart(Part, true);
		}
	}

	ShadowRadius = ModelRadius;
	AnimTime = FMath::FRand() * 10.f;
	BlinkTimer = FMath::FRandRange(1.f, 4.f);
	HeadRotation = FRotator::ZeroRotator;
}

void UAlienAppearanceComponent::BuildModelBody(const UAlienDataAsset* Data, UStaticMesh* Mesh)
{
	const float H = Data->Height;
	const FQuat Fix = Data->ModelRotation.Quaternion();

	// The bounds after the fix-up rotation decide scale and placement: Height tall, lowest point on
	// the ground, centred on the capsule. So any download works, whatever its units and pivot.
	const FBox ModelBounds = Mesh->GetBoundingBox().TransformBy(FTransform(Fix));
	const float MeshHeight = FMath::Max(static_cast<float>(ModelBounds.Max.Z - ModelBounds.Min.Z), 0.01f);
	const float Scale = H / MeshHeight;
	ModelMeshHeight = MeshHeight;
	const FVector Center = ModelBounds.GetCenter();
	const FVector Offset(-Center.X * Scale, -Center.Y * Scale, -ModelBounds.Min.Z * Scale);
	ModelComponent = AddPart(TEXT("AlienModel"), Mesh, BodyPivot, FTransform(Fix, Offset, FVector(Scale)), nullptr);
	RestMesh = Mesh;
	ModelFix = Fix;
	ModelOffset = Offset;
	ModelScale = Scale;

	// Pieces exported in the same frame as the model (wings): each hangs off a pivot at its hinge, so
	// it lines up with the body and can swing.
	for (const FAlienModelPart& Part : Data->ModelParts)
	{
		UStaticMesh* PartMesh = Part.Mesh.LoadSynchronous();
		if (!PartMesh)
		{
			continue;
		}
		const FVector HingeLocation = Fix.RotateVector(Part.Hinge * Scale) + Offset;
		USceneComponent* Hinge = AddPivot(TEXT("AlienModelHinge"), BodyPivot, FTransform(HingeLocation));
		AddPart(TEXT("AlienModelPart"), PartMesh, Hinge, FTransform(Fix, Offset - HingeLocation, FVector(Scale)), nullptr);
		FModelPartMotion Motion;
		Motion.Pivot = Hinge;
		Motion.Axis = Fix.RotateVector(Part.Axis).GetSafeNormal();
		Motion.Amount = Part.Amount;
		Motion.Offset = Part.Offset;
		Motion.Speed = Part.Speed;
		Motion.FlyingAmount = Part.FlyingAmount;
		Motion.FlyingOffset = Part.FlyingOffset;
		Motion.FlyingSpeed = Part.FlyingSpeed;
		Motion.Phase = Part.Phase;
		ModelPartMotions.Add(Motion);
	}

	const FVector Half = ModelBounds.GetExtent() * Scale;
	ModelRadius = FMath::Max(Half.X, Half.Y);
	CollisionRadius = ModelRadius * 0.9f;       // keeps outstretched limbs inside the glass
	ShadowRadius = FMath::Min(ModelRadius, 0.3f * H);

	// A model has no separate head; the pivot only keeps the animation code uniform.
	HeadBaseXY = FVector2D::ZeroVector;
	HeadBaseZ = 0.85f * H;
	HeadTopOffset = 0.11f * H;
	HeadPivot = AddPivot(TEXT("AlienHeadPivot"), this, FTransform(FVector(0.f, 0.f, HeadBaseZ + HoverHeight)));
}

void UAlienAppearanceComponent::BuildRig(const UAlienDataAsset* Data, USkeletalMesh* Mesh)
{
	// The skinned model sits exactly where the static one does (same shape and frame); the static one
	// stays, hidden, for the after-images.
	AActor* Owner = GetOwner();
	UPoseableMeshComponent* Rig = NewObject<UPoseableMeshComponent>(Owner, MakeUniqueObjectName(Owner, UPoseableMeshComponent::StaticClass(), TEXT("AlienRig")));
	Rig->SetSkinnedAssetAndUpdate(Mesh);
	Rig->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Rig->SetGenerateOverlapEvents(false);
	Rig->SetCanEverAffectNavigation(false);
	Rig->SetCastShadow(bCastShadows);
	Rig->SetupAttachment(BodyPivot);
	Rig->SetRelativeTransform(FTransform(ModelFix, ModelOffset, FVector(ModelScale)));
	Rig->RegisterComponent();
	Parts.Add(Rig);
	RigComponent = Rig;
	if (ModelComponent)
	{
		ModelComponent->SetVisibility(false);
	}

	// Rest pose in bone and component space.
	const FReferenceSkeleton& Skeleton = Mesh->GetRefSkeleton();
	RigRestLocal = Skeleton.GetRefBonePose();
	const int32 Num = RigRestLocal.Num();
	RigParents.SetNum(Num);
	RigRestSpace.SetNum(Num);
	for (int32 i = 0; i < Num; ++i)
	{
		RigParents[i] = Skeleton.GetParentIndex(i);
		RigRestSpace[i] = RigParents[i] == INDEX_NONE ? RigRestLocal[i] : RigRestLocal[i] * RigRestSpace[RigParents[i]];
	}
	RigPose = RigRestLocal;
	RigSpace = RigRestSpace;
	RigTurns.Init(FQuat::Identity, Num);
	RigSettings = Data->Rig;
	RigHeight = ModelMeshHeight;

	auto Find = [&Skeleton](FName Name) { return Name.IsNone() ? INDEX_NONE : Skeleton.FindBoneIndex(Name); };
	auto FindAll = [&Find](const TArray<FName>& Names, TArray<int32>& Out)
	{
		Out.Reset();
		for (const FName& Name : Names)
		{
			const int32 Index = Find(Name);
			if (Index != INDEX_NONE)
			{
				Out.Add(Index);
			}
		}
	};
	for (const FAlienRigLeg& Leg : RigSettings.Legs)
	{
		FRigLeg State;
		State.Upper = Find(Leg.Upper);
		State.Lower = Find(Leg.Lower);
		State.End = Find(Leg.End);
		if (State.Upper == INDEX_NONE || State.Lower == INDEX_NONE || State.End == INDEX_NONE
			|| RigParents[State.Lower] != State.Upper || RigParents[State.End] != State.Lower)
		{
			UE_LOG(LogAlienMuseum, Warning, TEXT("%s: rig leg %s / %s / %s is not a chain of the skeleton"),
				*Data->GetName(), *Leg.Upper.ToString(), *Leg.Lower.ToString(), *Leg.End.ToString());
			continue;
		}
		const FVector Root = RigRestSpace[State.Upper].GetLocation();
		const FVector Joint = RigRestSpace[State.Lower].GetLocation();
		const FVector Tip = RigRestSpace[State.End].GetLocation();
		State.Home = Tip;
		State.UpperLength = FMath::Max(0.01f, static_cast<float>(FVector::Dist(Root, Joint)));
		State.LowerLength = FMath::Max(0.01f, static_cast<float>(FVector::Dist(Joint, Tip)));
		State.Bend = (Joint - (Root + Tip) * 0.5f).GetSafeNormal();
		State.Phase = Leg.Phase;
		State.bFront = Leg.bFront;
		RigLegs.Add(State);
	}
	FindAll(RigSettings.Spine, RigSpine);
	FindAll(RigSettings.Tail, RigTail);
	FindAll(RigSettings.Floating, RigFloating);
	RigNeck = Find(RigSettings.Neck);
	RigHead = Find(RigSettings.Head);
	RigJaw = Find(RigSettings.Jaw);
	if (RigTail.Num() > 1)
	{
		RigTailDirection = (RigRestSpace[RigTail.Last()].GetLocation() - RigRestSpace[RigTail[0]].GetLocation()).GetSafeNormal();
	}
	GaitCycle = FMath::FRand();
	SniffTimer = FMath::FRandRange(2.f, 5.f);
	UE_LOG(LogAlienMuseum, Log, TEXT("%s: rigged model with %d bones, %d legs, %d spine, %d tail, %d floating bones"),
		*Data->GetName(), Num, RigLegs.Num(), RigSpine.Num(), RigTail.Num(), RigFloating.Num());
}

void UAlienAppearanceComponent::AddRigTurn(int32 Bone, const FQuat& Turn)
{
	if (RigTurns.IsValidIndex(Bone))
	{
		RigTurns[Bone] = Turn * RigTurns[Bone];
	}
}

void UAlienAppearanceComponent::UpdateRig(float DeltaSeconds, float TurnAlpha, float Excite)
{
	UPoseableMeshComponent* Rig = RigComponent;
	const int32 Num = RigRestLocal.Num();
	if (!Rig || Num == 0 || Rig->BoneSpaceTransforms.Num() != Num)
	{
		return;
	}
	// Component space: the model faces +X, up is +Z.
	const FVector Forward = FVector::ForwardVector;
	const FVector Up = FVector::UpVector;
	const FVector Right = FVector::RightVector;
	const FVector NoseUp = FVector::CrossProduct(Forward, Up); // turning around it lifts what points forward
	const float H = FMath::Max(1.f, RigHeight);

	// How the body moves, in the rig's own units.
	const AAlienCharacter* Alien = Cast<AAlienCharacter>(GetOwner());
	const UCharacterMovementComponent* Move = Alien ? Alien->GetCharacterMovement() : nullptr;
	const float UnitsPerCm = 1.f / FMath::Max(0.01f, static_cast<float>(Rig->GetComponentScale().Z));
	const float TestSpeed = CVarRigTestSpeed.GetValueOnGameThread();
	const float Speed = (TestSpeed > 0.f ? TestSpeed : (Alien ? static_cast<float>(Alien->GetVelocity().Size2D()) : 0.f)) * UnitsPerCm;
	const bool bInAir = Alien && (Alien->IsOutOfCase() || (Move && Move->IsFalling()));
	Airborne = FMath::FInterpTo(Airborne, bInAir ? 1.f : 0.f, DeltaSeconds, bInAir ? 8.f : 12.f);
	GaitMoving = FMath::FInterpTo(GaitMoving, FMath::Clamp(FMath::Max(Speed / (0.12f * H), TurnAlpha), 0.f, 1.f), DeltaSeconds, 6.f);
	const float Run = FMath::Clamp(Speed / (1.2f * H), 0.f, 1.f);
	const float Frequency = 0.9f + 1.6f * FMath::Min(Speed / H, 1.5f) + 0.7f * TurnAlpha; // steps per second
	GaitCycle = FMath::Frac(GaitCycle + DeltaSeconds * Frequency);
	RigBreathCycle += DeltaSeconds * FMath::Lerp(0.28f, 1.8f, Excite); // slow breaths, quick panting when excited
	const float Breath = FMath::Sin(RigBreathCycle * 2.f * PI);
	const float Time = AnimTime;

	for (FQuat& Turn : RigTurns)
	{
		Turn = FQuat::Identity;
	}

	// ---- Body: dips on every step, sways from side to side, crouches, breathes.
	FVector RootOffset = Up * (-0.012f * H * GaitMoving * (0.5f + 0.5f * FMath::Cos(4.f * PI * GaitCycle)) - RigCrouch * 0.14f * H);
	const float Sway = FMath::Sin(2.f * PI * GaitCycle) * 3.f * GaitMoving;
	for (int32 i = 0; i < RigSpine.Num(); ++i)
	{
		// The hips swing one way, the chest the other: the walking body snakes a little.
		AddRigTurn(RigSpine[i], TurnAround(Up, Sway * (i == 0 ? 0.8f : -0.5f) / FMath::Max(1, RigSpine.Num() - 1)));
	}
	if (RigSpine.Num() > 0)
	{
		AddRigTurn(RigSpine.Last(), TurnAround(NoseUp, Breath * FMath::Lerp(1.2f, 2.5f, Excite)));
	}

	// ---- Head: looks around (the body's sway taken out), bobs with the steps, sniffs the air.
	float LookYaw = HeadRotation.Yaw - Sway;
	float LookPitch = HeadRotation.Pitch + RigNod - 2.5f * GaitMoving * FMath::Sin(4.f * PI * GaitCycle + 0.5f);
	bool bSniffing = false;
	if (RigSettings.bSniffs)
	{
		if (SniffTime < 0.f)
		{
			SniffTimer -= DeltaSeconds;
			if (SniffTimer <= 0.f && GaitMoving < 0.5f && Airborne < 0.5f)
			{
				SniffTime = 0.f;
			}
		}
		if (SniffTime >= 0.f)
		{
			// Nose up, then short quick sniffs.
			SniffTime += DeltaSeconds;
			const float Length = 1.5f;
			const float Envelope = FMath::Sin(PI * FMath::Clamp(SniffTime / Length, 0.f, 1.f));
			LookPitch += Envelope * (12.f + 4.f * FMath::Sin(SniffTime * 2.f * PI * 6.f));
			bSniffing = true;
			if (SniffTime >= Length)
			{
				SniffTime = -1.f;
				SniffTimer = FMath::FRandRange(2.5f, 6.f);
			}
		}
	}
	AddRigTurn(RigNeck, TurnAround(Up, LookYaw * 0.4f) * TurnAround(NoseUp, LookPitch * 0.4f));
	AddRigTurn(RigHead, TurnAround(Up, LookYaw * (RigNeck == INDEX_NONE ? 1.f : 0.6f)) * TurnAround(NoseUp, LookPitch * (RigNeck == INDEX_NONE ? 1.f : 0.6f)));
	// Mouth: a little open, panting when excited, wide open in a snarl now and then.
	const float Snarl = Excite * FMath::Max(0.f, FMath::Sin(Time * 0.7f)) * 12.f;
	const float Open = 2.f + (bSniffing ? 3.f : 0.f) + Excite * (4.f + 4.f * FMath::Max(0.f, Breath)) + Snarl;
	AddRigTurn(RigJaw, TurnAround(NoseUp, -Open));

	// ---- Tail: a wave runs down it (faster when excited), and it streams behind when moving.
	if (RigTail.Num() > 0)
	{
		RigTailCycle += DeltaSeconds * RigSettings.TailSpeed * (1.f + 0.8f * Excite + 0.6f * GaitMoving);
		const FVector SideAxis = FVector::CrossProduct(RigTailDirection, Right).GetSafeNormal(); // + swings the tip right
		const FVector BackAxis = FVector::CrossProduct(RigTailDirection, -Forward).GetSafeNormal(); // + swings it back
		const float Trail = 10.f * FMath::Clamp(Speed / (0.3f * H), 0.f, 1.f);
		const int32 Count = RigTail.Num();
		for (int32 i = 0; i < Count; ++i)
		{
			const float Weight = 0.35f + 0.65f * (i + 1.f) / Count; // the tip swings most
			const float Side = RigSettings.TailAmount * Weight * FMath::Sin(2.f * PI * RigTailCycle - 0.9f * i);
			const float Back = RigSettings.TailAmount * 0.4f * Weight * FMath::Sin(2.f * PI * 0.7f * RigTailCycle - 0.6f * i + 1.3f)
				+ Trail * Weight / Count * 2.f;
			AddRigTurn(RigTail[i], TurnAround(SideAxis, Side) * TurnAround(BackAxis, Back));
		}
	}

	// ---- Floating limbs (a ghost's arms): a slow drift, each on its own.
	for (int32 i = 0; i < RigFloating.Num(); ++i)
	{
		const float Drift = BreathTime + i * 1.7f;
		AddRigTurn(RigFloating[i], TurnAround(Forward, 5.f * FMath::Sin(Drift * 2.f * PI * 0.21f)) * TurnAround(Right, 6.f * FMath::Sin(Drift * 2.f * PI * 0.16f + 1.f)));
	}

	// ---- Pass 1: the turns into bone space, down the hierarchy (a parent is always before its children).
	const int32 RootBone = RigSpine.Num() > 0 ? RigSpine[0] : INDEX_NONE;
	for (int32 i = 0; i < Num; ++i)
	{
		const int32 Parent = RigParents[i];
		FTransform Local = RigRestLocal[i];
		const FQuat ParentRotation = Parent == INDEX_NONE ? FQuat::Identity : RigSpace[Parent].GetRotation();
		if (!RigTurns[i].Equals(FQuat::Identity, 1.e-6f))
		{
			Local.SetRotation(ParentRotation.Inverse() * RigTurns[i] * ParentRotation * Local.GetRotation());
		}
		if (i == RootBone)
		{
			Local.AddToTranslation(Parent == INDEX_NONE ? RootOffset : RigSpace[Parent].InverseTransformVectorNoScale(RootOffset));
		}
		RigPose[i] = Local;
		RigSpace[i] = Parent == INDEX_NONE ? Local : Local * RigSpace[Parent];
	}

	// ---- Pass 2: the legs. Each paw is planted while its leg carries the body, then lifts and swings
	// forward; two-bone IK bends the elbow / knee to keep it there whatever the body does.
	const float Duty = FMath::Lerp(0.66f, 0.45f, Run); // share of the cycle a paw is on the ground
	for (const FRigLeg& Leg : RigLegs)
	{
		const float LegReach = Leg.UpperLength + Leg.LowerLength;
		const float Stride = FMath::Min(FMath::Max(Speed / Frequency, 0.08f * H * TurnAlpha), 0.6f * LegReach);
		const float P = FMath::Frac(GaitCycle + Leg.Phase);
		float Along = 0.f;
		float Lift = 0.f;
		float Swing = 0.f;
		if (P < Duty)
		{
			Along = Stride * (0.5f - P / Duty); // planted: the body moves over it
		}
		else
		{
			const float T = (P - Duty) / (1.f - Duty);
			Along = Stride * (-0.5f + T * T * (3.f - 2.f * T));
			Swing = FMath::Sin(PI * T);
			Lift = RigSettings.StepHeight * H * Swing;
		}
		FVector Target = Leg.Home + (Forward * Along + Up * Lift) * GaitMoving;
		float Fold = Swing * GaitMoving;
		if (Airborne > 0.f)
		{
			// Leaping or held: front paws reach forward, hind legs stretch back - paddling when excited.
			const float Paddle = FMath::Sin(2.f * PI * (Time * 0.8f + Leg.Phase)) * Excite;
			const FVector Reach = Leg.bFront
				? Forward * (0.16f + 0.05f * Paddle) + Up * (0.13f + 0.04f * Paddle)
				: Forward * (-0.13f + 0.05f * Paddle) + Up * (0.07f + 0.03f * Paddle);
			Target = FMath::Lerp(Target, Leg.Home + Reach * H, Airborne);
			Fold = FMath::Lerp(Fold, Leg.bFront ? 0.6f : 0.25f, Airborne);
		}

		const int32 Parent = RigParents[Leg.Upper];
		const FTransform& ParentSpace = Parent == INDEX_NONE ? FTransform::Identity : RigSpace[Parent];
		FTransform UpperSpace = RigSpace[Leg.Upper];
		FTransform LowerSpace = RigPose[Leg.Lower] * UpperSpace;
		const FVector Root = UpperSpace.GetLocation();
		FVector Joint, Tip;
		SolveTwoBone(Root, Target, Leg.Bend, Leg.UpperLength, Leg.LowerLength, Joint, Tip);
		UpperSpace.SetRotation(TurnBetween(LowerSpace.GetLocation() - Root, Joint - Root) * UpperSpace.GetRotation());
		LowerSpace = RigPose[Leg.Lower] * UpperSpace;
		const FVector EndNow = (RigPose[Leg.End] * LowerSpace).GetLocation();
		LowerSpace.SetRotation(TurnBetween(EndNow - LowerSpace.GetLocation(), Tip - LowerSpace.GetLocation()) * LowerSpace.GetRotation());
		FTransform EndSpace = RigPose[Leg.End] * LowerSpace;
		// The paw stays flat as it stood; swinging, a front paw folds back and a hind foot lifts its toes.
		EndSpace.SetRotation(TurnAround(Right, (Leg.bFront ? 65.f : -15.f) * Fold) * RigRestSpace[Leg.End].GetRotation());
		RigPose[Leg.Upper] = UpperSpace.GetRelativeTransform(ParentSpace);
		RigPose[Leg.Lower] = LowerSpace.GetRelativeTransform(UpperSpace);
		RigPose[Leg.End] = EndSpace.GetRelativeTransform(LowerSpace);
	}

	Rig->BoneSpaceTransforms = RigPose;
	Rig->RefreshBoneTransforms();
}

void UAlienAppearanceComponent::BuildBuiltInBody(const UAlienDataAsset* Data)
{
	const float H = Data->Height;
	UStaticMesh* Sphere = MuseumAssets::SphereMesh();
	UStaticMesh* Cylinder = MuseumAssets::CylinderMesh();

	// ---- Body ----
	float HeadDiameter = 0.46f * H;
	float HeadOffset = 0.20f * H; // head centre above the neck pivot

	switch (Data->BodyShape)
	{
	case EAlienBodyShape::Tall:
		AddPart(TEXT("AlienBody"), Cylinder, BodyPivot, FTransform(FQuat::Identity, FVector(0, 0, 0.30f * H), SizeToScale(0.30f * H, 0.30f * H, 0.52f * H)), SkinMID);
		HeadBaseZ = 0.56f * H;
		HeadDiameter = 0.40f * H;
		HeadOffset = 0.19f * H;
		ModelRadius = 0.20f * H;
		break;

	case EAlienBodyShape::Squat:
		AddPart(TEXT("AlienBody"), Sphere, BodyPivot, FTransform(FQuat::Identity, FVector(0, 0, 0.26f * H), SizeToScale(0.78f * H, 0.78f * H, 0.50f * H)), SkinMID);
		HeadBaseZ = 0.44f * H;
		HeadDiameter = 0.40f * H;
		HeadOffset = 0.17f * H;
		ModelRadius = 0.39f * H;
		break;

	case EAlienBodyShape::Blob:
	default:
		AddPart(TEXT("AlienBody"), Sphere, BodyPivot, FTransform(FQuat::Identity, FVector(0, 0, 0.30f * H), SizeToScale(0.60f * H, 0.60f * H, 0.58f * H)), SkinMID);
		HeadBaseZ = 0.55f * H;
		ModelRadius = 0.30f * H;
		break;
	}

	// Belly patch in the accent colour.
	AddPart(TEXT("AlienBelly"), Sphere, BodyPivot, FTransform(FQuat::Identity, FVector(ModelRadius * 0.62f, 0, 0.26f * H), SizeToScale(0.10f * H, 0.28f * H, 0.26f * H)), AccentMID);

	// ---- Head (not parented to the body pivot so squash does not distort it) ----
	HeadPivot = AddPivot(TEXT("AlienHeadPivot"), this, FTransform(FVector(0.f, 0.f, HeadBaseZ + HoverHeight)));
	AddPart(TEXT("AlienHead"), Sphere, HeadPivot, FTransform(FQuat::Identity, FVector(0, 0, HeadOffset), SizeToScale(HeadDiameter, HeadDiameter, HeadDiameter * 0.95f)), SkinMID);

	const float HeadR = HeadDiameter * 0.5f;
	HeadTopOffset = HeadOffset + HeadR * 0.95f;

	// ---- Eyes ----
	const int32 EyeCount = FMath::Clamp(Data->EyeCount, 0, 3);
	const float EyeD = (EyeCount == 1 ? 0.16f : 0.10f) * H * Data->EyeSize;
	TArray<FVector> EyePositions;
	if (EyeCount == 1)
	{
		EyePositions.Add(FVector(HeadR * 0.80f, 0.f, HeadOffset + HeadR * 0.10f));
	}
	else if (EyeCount == 2)
	{
		EyePositions.Add(FVector(HeadR * 0.78f, -HeadR * 0.38f, HeadOffset + HeadR * 0.12f));
		EyePositions.Add(FVector(HeadR * 0.78f, HeadR * 0.38f, HeadOffset + HeadR * 0.12f));
	}
	else if (EyeCount == 3)
	{
		EyePositions.Add(FVector(HeadR * 0.76f, -HeadR * 0.46f, HeadOffset + HeadR * 0.05f));
		EyePositions.Add(FVector(HeadR * 0.84f, 0.f, HeadOffset + HeadR * 0.30f));
		EyePositions.Add(FVector(HeadR * 0.76f, HeadR * 0.46f, HeadOffset + HeadR * 0.05f));
	}
	for (const FVector& EyePos : EyePositions)
	{
		const FVector EyeScale = SizeToScale(EyeD * 0.7f, EyeD, EyeD);
		Eyes.Add(AddPart(TEXT("AlienEye"), Sphere, HeadPivot, FTransform(FQuat::Identity, EyePos, EyeScale), EyeMID));
		EyeBaseScales.Add(EyeScale);
	}

	// ---- Antennae ----
	if (Data->bHasAntennae)
	{
		for (int32 Side = -1; Side <= 1; Side += 2)
		{
			const float BaseRoll = Side * 22.f;
			USceneComponent* Pivot = AddPivot(TEXT("AlienAntennaPivot"), HeadPivot,
				FTransform(FRotator(0.f, 0.f, BaseRoll), FVector(0.f, Side * HeadR * 0.35f, HeadOffset + HeadR * 0.80f)));
			AddPart(TEXT("AlienAntenna"), Cylinder, Pivot, FTransform(FQuat::Identity, FVector(0, 0, 0.11f * H), SizeToScale(0.03f * H, 0.03f * H, 0.22f * H)), AccentMID);
			AddPart(TEXT("AlienAntennaTip"), Sphere, Pivot, FTransform(FQuat::Identity, FVector(0, 0, 0.23f * H), SizeToScale(0.07f * H, 0.07f * H, 0.07f * H)), EyeMID);
			AntennaPivots.Add(Pivot);
			AntennaBaseRoll.Add(BaseRoll);
		}
	}

	// ---- Feet (walkers only) ----
	if (!bHovers)
	{
		const float FootD = 0.16f * H;
		FootLBase = FVector(0.05f * H, -0.14f * H, FootD * 0.28f);
		FootRBase = FVector(0.05f * H, 0.14f * H, FootD * 0.28f);
		FootL = AddPart(TEXT("AlienFootL"), Sphere, this, FTransform(FQuat::Identity, FootLBase, SizeToScale(FootD * 1.3f, FootD, FootD * 0.55f)), AccentMID);
		FootR = AddPart(TEXT("AlienFootR"), Sphere, this, FTransform(FQuat::Identity, FootRBase, SizeToScale(FootD * 1.3f, FootD, FootD * 0.55f)), AccentMID);
	}
}

void UAlienAppearanceComponent::AddDataPart(const FAlienBodyPart& Part, bool bMirrored)
{
	const float H = ModelHeight;

	FVector Offset = Part.Offset * H;
	FRotator Rotation = Part.Rotation;
	FVector PivotOffset = Part.PivotOffset * H;
	float Amount = Part.MotionAmount;
	float Phase = Part.MotionPhase;

	if (bMirrored)
	{
		// Mirror across the alien's centre plane (Y -> -Y).
		Offset.Y = -Offset.Y;
		PivotOffset.Y = -PivotOffset.Y;
		Rotation.Yaw = -Rotation.Yaw;
		Rotation.Roll = -Rotation.Roll;
		if (Part.Motion == EAlienPartMotion::WalkSwing)
		{
			Phase += 0.5f; // left and right limbs alternate
		}
		else if (Part.Motion == EAlienPartMotion::SwayRoll || Part.Motion == EAlienPartMotion::SwayYaw)
		{
			Amount = -Amount; // wings flap symmetrically
		}
	}

	USceneComponent* Parent = BodyPivot;
	if (Part.AttachTo == EAlienPartAttach::Head && HeadPivot)
	{
		Parent = HeadPivot;
	}
	else if (Part.AttachTo == EAlienPartAttach::Feet)
	{
		Parent = this;
	}

	const FVector Scale = Part.Size * H / 100.f;
	UStaticMeshComponent* Component = AddPart(TEXT("AlienPart"), GetShapeMesh(Part.Shape), Parent, FTransform(Rotation, Offset, Scale), GetPartMaterial(Part));

	const bool bGlows = Part.Color == EAlienPartColor::Glow || Part.Color == EAlienPartColor::Eye
		|| (Part.Color == EAlienPartColor::Custom && Part.bCustomGlows);
	if (Parent == HeadPivot && !bGlows)
	{
		HeadTopOffset = FMath::Max(HeadTopOffset, static_cast<float>(Offset.Z) + 0.5f * static_cast<float>(Part.Size.Z) * H);
	}

	if (Part.Color == EAlienPartColor::Eye)
	{
		Eyes.Add(Component);
		EyeBaseScales.Add(Scale);
	}

	if (Part.Motion != EAlienPartMotion::None)
	{
		FAnimatedPart Animated;
		Animated.Component = Component;
		Animated.BaseLocation = Offset;
		Animated.BaseRotation = Rotation.Quaternion();
		Animated.BaseScale = Scale;
		Animated.Pivot = Offset + PivotOffset;
		Animated.Motion = Part.Motion;
		Animated.Amount = Amount;
		Animated.Speed = Part.MotionSpeed;
		Animated.Phase = Phase;
		AnimatedParts.Add(Animated);
	}
}

// ---------------------------------------------------------------------------------------------
// Materialize
// ---------------------------------------------------------------------------------------------

void UAlienAppearanceComponent::PlayMaterialize(float Duration)
{
	MaterializeDuration = FMath::Max(0.05f, Duration);
	MaterializeTime = 0.f;
	MaterializeScale = 0.05f; // tiny but still rendered
	ApplyRootTransform();
}

bool UAlienAppearanceComponent::UpdateMaterialize(float DeltaSeconds)
{
	if (MaterializeTime < 0.f)
	{
		return false;
	}
	MaterializeTime += DeltaSeconds;
	const float T = FMath::Clamp(MaterializeTime / MaterializeDuration, 0.f, 1.f);
	// Ease-out-back: overshoots slightly, then settles at 1.
	const float C1 = 1.70158f;
	const float C3 = C1 + 1.f;
	const float Scale = 1.f + C3 * FMath::Pow(T - 1.f, 3.f) + C1 * FMath::Pow(T - 1.f, 2.f);
	MaterializeScale = FMath::Max(0.05f, Scale);
	if (T >= 1.f)
	{
		MaterializeTime = -1.f;
		MaterializeScale = 1.f;
	}
	ApplyRootTransform();
	return MaterializeTime >= 0.f;
}

// ---------------------------------------------------------------------------------------------
// Whole-body transform and poses (signature moves)
// ---------------------------------------------------------------------------------------------

void UAlienAppearanceComponent::ApplyRootTransform()
{
	if (!bHasBaseLocation)
	{
		BaseLocation = GetRelativeLocation();
		bHasBaseLocation = true;
	}
	FVector Scale = ActionScale;
	FRotator Rotation = ActionRotation;
	if (RigComponent)
	{
		// A rigged body bends instead of squashing: a move's squash becomes a crouch on bent legs, and on
		// the ground its nod moves the head - the paws stay planted.
		RigCrouch = FMath::Clamp((1.f - static_cast<float>(ActionScale.Z)) / 0.18f, 0.f, 1.f);
		const bool bGrounded = Airborne < 0.5f;
		RigNod = bGrounded ? static_cast<float>(ActionRotation.Pitch) : 0.f;
		Scale = FVector::OneVector;
		if (bGrounded)
		{
			Rotation = FRotator(0.f, ActionRotation.Yaw, ActionRotation.Roll);
		}
	}
	SetRelativeLocationAndRotation(BaseLocation + ActionOffset + FVector(0.f, 0.f, ExtraLift), Rotation);
	SetRelativeScale3D(Scale * MaterializeScale);
}

void UAlienAppearanceComponent::SetBaseLocation(const FVector& Location)
{
	BaseLocation = Location;
	bHasBaseLocation = true;
	ApplyRootTransform();
}

void UAlienAppearanceComponent::SetActionTransform(const FVector& Scale, const FRotator& Rotation, const FVector& Offset)
{
	ActionScale = Scale.ComponentMax(FVector(0.01f));
	ActionRotation = Rotation;
	ActionOffset = Offset;
	ApplyRootTransform();
}

void UAlienAppearanceComponent::SetExtraLift(float Lift)
{
	ExtraLift = Lift;
	ApplyRootTransform();
}

void UAlienAppearanceComponent::SetBodyVisible(bool bShow)
{
	bBodyVisible = bShow;
	for (USceneComponent* Part : Parts)
	{
		if (Part)
		{
			Part->SetVisibility(bShow && !(RigComponent && Part == ModelComponent)); // a rigged model's static twin stays hidden
		}
	}
}

bool UAlienAppearanceComponent::ShowPose(UStaticMesh* PoseMesh)
{
	if (!ModelComponent || !PoseMesh || !RestMesh)
	{
		return false;
	}
	// Same scale and centre as the normal pose; the feet stay on the ground even if the pose moves them.
	const FBox PoseBounds = PoseMesh->GetBoundingBox().TransformBy(FTransform(ModelFix));
	ModelComponent->SetStaticMesh(PoseMesh);
	ModelComponent->SetRelativeLocation(FVector(ModelOffset.X, ModelOffset.Y, -PoseBounds.Min.Z * ModelScale));
	return true;
}

void UAlienAppearanceComponent::EndPose()
{
	if (ModelComponent && RestMesh && ModelComponent->GetStaticMesh() != RestMesh)
	{
		ModelComponent->SetStaticMesh(RestMesh);
		ModelComponent->SetRelativeLocation(ModelOffset);
	}
}

USceneComponent* UAlienAppearanceComponent::GetHeadTop(FVector& OutOffset) const
{
	if (bIsModel)
	{
		// One rigid mesh: the top of the figure, moving with the body.
		OutOffset = FVector(0.f, 0.f, 0.96f * ModelHeight);
		return BodyPivot;
	}
	OutOffset = FVector(0.f, 0.f, HeadTopOffset);
	return HeadPivot;
}

// ---------------------------------------------------------------------------------------------
// Animation
// ---------------------------------------------------------------------------------------------

void UAlienAppearanceComponent::AnimateDataParts(float SpeedAlpha)
{
	const float Tau = 2.f * PI;
	const float Step = AnimTime * 6.f; // same walk clock as the built-in feet

	for (const FAnimatedPart& Part : AnimatedParts)
	{
		UStaticMeshComponent* Component = Part.Component.Get();
		if (!Component)
		{
			continue;
		}
		const float Cycle = (AnimTime * Part.Speed + Part.Phase) * Tau;

		FRotator Delta = FRotator::ZeroRotator;
		switch (Part.Motion)
		{
		case EAlienPartMotion::SwayRoll:
			Delta.Roll = FMath::Sin(Cycle) * Part.Amount;
			break;
		case EAlienPartMotion::SwayPitch:
			Delta.Pitch = FMath::Sin(Cycle) * Part.Amount;
			break;
		case EAlienPartMotion::SwayYaw:
			Delta.Yaw = FMath::Sin(Cycle) * Part.Amount;
			break;
		case EAlienPartMotion::WalkSwing:
			// Big swing while walking, a tiny idle sway while standing.
			Delta.Pitch = FMath::Sin(Step + Part.Phase * Tau) * Part.Amount * SpeedAlpha
				+ FMath::Sin(AnimTime * 1.3f + Part.Phase * Tau) * 2.f;
			break;
		case EAlienPartMotion::Spin:
			Delta.Yaw = FMath::Fmod(AnimTime * Part.Amount * Part.Speed, 360.f);
			break;
		case EAlienPartMotion::Flicker:
		{
			const float Noise = 0.6f * FMath::Sin(Cycle) + 0.4f * FMath::Sin(Cycle * 2.3f + 1.7f);
			const float A = Part.Amount / 100.f;
			Component->SetRelativeScale3D(Part.BaseScale * FVector(1.f - Noise * A * 0.3f, 1.f - Noise * A * 0.3f, 1.f + Noise * A));
			continue;
		}
		case EAlienPartMotion::Pulse:
			Component->SetRelativeScale3D(Part.BaseScale * (1.f + FMath::Sin(Cycle) * Part.Amount / 100.f));
			continue;
		default:
			continue;
		}

		// Rotate around the joint (pivot) in the parent's space.
		const FQuat DeltaQ = Delta.Quaternion();
		const FVector Location = Part.Pivot + DeltaQ.RotateVector(Part.BaseLocation - Part.Pivot);
		Component->SetRelativeLocationAndRotation(Location, DeltaQ * Part.BaseRotation);
	}
}

void UAlienAppearanceComponent::UpdateAnimation(float DeltaSeconds, float SpeedAlpha, const FVector* LookTarget, bool bExcited)
{
	if (!BodyPivot || !HeadPivot)
	{
		return;
	}

	const float Excite = bExcited ? 1.f : 0.f;
	SpeedAlpha = FMath::Clamp(SpeedAlpha, 0.f, 1.f);
	AnimTime += DeltaSeconds * (0.8f + Energy) * (1.f + SpeedAlpha * 1.5f + Excite);

	// A body has weight: lean into turns (yaw rate) and pitch when speeding up or slowing down.
	float YawRate = 0.f;
	if (const USceneComponent* Parent = GetAttachParent())
	{
		const float ParentYaw = static_cast<float>(Parent->GetComponentRotation().Yaw);
		if (bHasLastYaw && DeltaSeconds > KINDA_SMALL_NUMBER)
		{
			YawRate = FRotator::NormalizeAxis(ParentYaw - LastParentYaw) / DeltaSeconds;
		}
		LastParentYaw = ParentYaw;
		bHasLastYaw = true;
	}
	const float TargetRoll = FMath::Clamp(YawRate * 0.06f * SpeedAlpha, -10.f, 10.f);
	const float TargetPitch = DeltaSeconds > KINDA_SMALL_NUMBER ? FMath::Clamp(-(SpeedAlpha - LastSpeedAlpha) / DeltaSeconds * 5.f, -8.f, 8.f) : 0.f;
	LastSpeedAlpha = SpeedAlpha;
	LeanRoll = FMath::FInterpTo(LeanRoll, TargetRoll, DeltaSeconds, 5.f);
	LeanPitch = FMath::FInterpTo(LeanPitch, TargetPitch, DeltaSeconds, 3.f);

	// Turning on the spot is stepping too.
	const float TurnAlpha = FMath::Clamp(FMath::Abs(YawRate) / 150.f, 0.f, 1.f) * (1.f - SpeedAlpha);
	const float StepAlpha = FMath::Max(SpeedAlpha, 0.6f * TurnAlpha);
	BreathTime += DeltaSeconds;

	// Body bob / squash & stretch.
	const float Wave = FMath::Sin(AnimTime * 4.f);
	float BodyLift = HoverHeight;
	float Squash = 0.f;
	if (bHovers)
	{
		BodyLift += Wave * ModelHeight * 0.03f * (1.f + Excite);
		BodyPivot->SetRelativeLocation(FVector(0.f, 0.f, BodyLift));
	}
	else if (RigComponent)
	{
		// Rigged: the skeleton breathes and steps (UpdateRig).
		BodyPivot->SetRelativeLocation(FVector(0.f, 0.f, BodyLift));
	}
	else if (bIsModel)
	{
		// A rigid figure: it breathes slowly when still (about 15 breaths a minute), and each step
		// lands with a little squash and pushes the body up again.
		const float Step = AnimTime * 6.f;
		const float Contact = (1.f - FMath::Abs(FMath::Sin(Step))) * StepAlpha;
		const float Breath = FMath::Sin(BreathTime * 2.f * PI * 0.25f) * (1.f - StepAlpha);
		Squash = 0.006f * Breath - 0.015f * Contact + 0.015f * Excite * Wave;
		const float Wide = 1.f + 0.012f * Breath + 0.0075f * Contact;
		BodyPivot->SetRelativeScale3D(FVector(Wide, Wide, 1.f + Squash));
		BodyLift += ModelHeight * 0.015f * (StepAlpha - Contact);
		BodyPivot->SetRelativeLocation(FVector(0.f, 0.f, BodyLift));
	}
	else
	{
		Squash = Wave * (0.025f + 0.04f * SpeedAlpha + 0.03f * Excite);
		BodyPivot->SetRelativeScale3D(FVector(1.f - Squash * 0.5f, 1.f - Squash * 0.5f, 1.f + Squash));

		if (FootL && FootR)
		{
			const float Step = AnimTime * 6.f;
			const float Lift = ModelHeight * 0.06f * SpeedAlpha;
			FootL->SetRelativeLocation(FootLBase + FVector(FMath::Cos(Step) * Lift, 0.f, FMath::Max(0.f, FMath::Sin(Step)) * Lift));
			FootR->SetRelativeLocation(FootRBase + FVector(FMath::Cos(Step + PI) * Lift, 0.f, FMath::Max(0.f, FMath::Sin(Step + PI)) * Lift));
		}
	}

	// Models are one rigid figure: they waddle, lean, shift their weight while standing, and turn a
	// little towards what they look at.
	if (RigComponent)
	{
		// Only a little lean into turns: the legs and the spine do the rest.
		BodyPivot->SetRelativeRotation(FRotator(0.f, 0.f, LeanRoll * 0.5f));
	}
	else if (bIsModel)
	{
		const float Waddle = bHovers ? 0.f : FMath::Sin(AnimTime * 6.f) * 4.f * StepAlpha;
		const float WeightShift = FMath::Sin(BreathTime * 2.f * PI * 0.12f) * 1.2f * (1.f - StepAlpha);
		const float BodyYaw = HeadRotation.Yaw * 0.35f * (1.f - SpeedAlpha);
		BodyPivot->SetRelativeRotation(FRotator(-3.f * SpeedAlpha + LeanPitch, BodyYaw, Waddle + LeanRoll + WeightShift));
	}

	// Wings and other swinging model parts: a buzz at rest, big fast strokes while flying - and a
	// hovering flyer beats harder when it moves or is held.
	if (ModelPartMotions.Num() > 0)
	{
		float Flying = FMath::Clamp(ExtraLift / FMath::Max(1.f, 0.08f * ModelHeight), 0.f, 1.f);
		if (bHovers)
		{
			Flying = FMath::Max3(Flying, 0.6f * SpeedAlpha, 0.7f * Excite);
		}
		for (FModelPartMotion& Part : ModelPartMotions)
		{
			if (USceneComponent* Pivot = Part.Pivot.Get())
			{
				Part.Cycle += DeltaSeconds * FMath::Lerp(Part.Speed, Part.FlyingSpeed, Flying);
				const float Swing = FMath::Sin((Part.Cycle + Part.Phase) * 2.f * PI) * FMath::Lerp(Part.Amount, Part.FlyingAmount, Flying);
				const float Angle = FMath::Lerp(Part.Offset, Part.FlyingOffset, Flying) + Swing;
				Pivot->SetRelativeRotation(FQuat(Part.Axis, FMath::DegreesToRadians(Angle)));
			}
		}
	}

	// Head follows the top of the body.
	HeadPivot->SetRelativeLocation(FVector(HeadBaseXY.X, HeadBaseXY.Y, HeadBaseZ * (1.f + Squash) + BodyLift));

	// Head look-at (clamped), otherwise a lazy idle sway.
	FRotator Desired(0.f, FMath::Sin(AnimTime * 0.6f) * 12.f, FMath::Sin(AnimTime * 0.9f) * 4.f);
	if (LookTarget)
	{
		const FVector Local = GetComponentTransform().InverseTransformPosition(*LookTarget) - HeadPivot->GetRelativeLocation();
		const float Yaw = FMath::RadiansToDegrees(FMath::Atan2(Local.Y, Local.X));
		const float Pitch = FMath::RadiansToDegrees(FMath::Atan2(Local.Z, FMath::Max(1.f, Local.Size2D())));
		Desired = FRotator(FMath::Clamp(Pitch, -30.f, 35.f), FMath::Clamp(Yaw, -70.f, 70.f), 0.f);
	}
	HeadRotation = FMath::RInterpTo(HeadRotation, Desired, DeltaSeconds, 5.f + 5.f * Excite);
	HeadPivot->SetRelativeRotation(HeadRotation);

	if (RigComponent)
	{
		UpdateRig(DeltaSeconds, TurnAlpha, Excite);
	}

	// Antenna sway.
	for (int32 i = 0; i < AntennaPivots.Num(); ++i)
	{
		if (AntennaPivots[i])
		{
			const float Amp = 6.f + 10.f * Excite + 6.f * Energy;
			AntennaPivots[i]->SetRelativeRotation(FRotator(
				FMath::Sin(AnimTime * 3.1f + i) * Amp,
				0.f,
				AntennaBaseRoll[i] + FMath::Sin(AnimTime * 2.3f + i * 1.7f) * Amp * 0.6f));
		}
	}

	// Data-driven parts: limbs, wings, tails, flames...
	AnimateDataParts(SpeedAlpha);

	// Blink.
	float EyeOpen = 1.f;
	if (BlinkPhase < 0.f)
	{
		BlinkTimer -= DeltaSeconds;
		if (BlinkTimer <= 0.f)
		{
			BlinkPhase = 0.f;
		}
	}
	if (BlinkPhase >= 0.f)
	{
		BlinkPhase += DeltaSeconds;
		const float Half = 0.08f;
		EyeOpen = BlinkPhase < Half ? 1.f - 0.9f * (BlinkPhase / Half) : 0.1f + 0.9f * FMath::Min(1.f, (BlinkPhase - Half) / Half);
		if (BlinkPhase > Half * 2.f)
		{
			BlinkPhase = -1.f;
			BlinkTimer = FMath::FRandRange(2.f, 5.f);
			EyeOpen = 1.f;
		}
	}
	for (int32 i = 0; i < Eyes.Num(); ++i)
	{
		if (Eyes[i])
		{
			const FVector& Base = EyeBaseScales[i];
			Eyes[i]->SetRelativeScale3D(FVector(Base.X, Base.Y, Base.Z * EyeOpen));
		}
	}
}
