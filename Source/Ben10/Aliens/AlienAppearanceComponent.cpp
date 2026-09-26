// Alien Museum - builds and animates the placeholder alien body from an AlienDataAsset.

#include "Aliens/AlienAppearanceComponent.h"
#include "Core/MuseumAssets.h"
#include "Ben10.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "GameFramework/Actor.h"

namespace
{
	/** Basic shapes are 100 cm across, so a diameter in cm maps to scale Diameter/100. */
	FVector SizeToScale(float X, float Y, float Z)
	{
		return FVector(X, Y, Z) / 100.f;
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
	CustomMIDs.Reset();
	SkinMID = AccentMID = DarkMID = EyeMID = GlowMID = nullptr;
	BodyPivot = nullptr;
	HeadPivot = nullptr;
	FootL = nullptr;
	FootR = nullptr;
	bIsModel = false;
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
	const FVector Center = ModelBounds.GetCenter();
	const FVector Offset(-Center.X * Scale, -Center.Y * Scale, -ModelBounds.Min.Z * Scale);
	AddPart(TEXT("AlienModel"), Mesh, BodyPivot, FTransform(Fix, Offset, FVector(Scale)), nullptr);

	const FVector Half = ModelBounds.GetExtent() * Scale;
	ModelRadius = FMath::Max(Half.X, Half.Y);
	CollisionRadius = ModelRadius * 0.9f;       // keeps outstretched limbs inside the glass
	ShadowRadius = FMath::Min(ModelRadius, 0.3f * H);

	// A model has no separate head; the pivot only keeps the animation code uniform.
	HeadBaseXY = FVector2D::ZeroVector;
	HeadBaseZ = 0.85f * H;
	HeadPivot = AddPivot(TEXT("AlienHeadPivot"), this, FTransform(FVector(0.f, 0.f, HeadBaseZ + HoverHeight)));
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
	SetRelativeScale3D(FVector(0.05f)); // tiny but still rendered
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
	SetRelativeScale3D(FVector(FMath::Max(0.05f, Scale)));
	if (T >= 1.f)
	{
		MaterializeTime = -1.f;
		SetRelativeScale3D(FVector::OneVector);
		return false;
	}
	return true;
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

	// Body bob / squash & stretch.
	const float Wave = FMath::Sin(AnimTime * 4.f);
	float BodyLift = HoverHeight;
	float Squash = 0.f;
	if (bHovers)
	{
		BodyLift += Wave * ModelHeight * 0.03f * (1.f + Excite);
		BodyPivot->SetRelativeLocation(FVector(0.f, 0.f, BodyLift));
	}
	else
	{
		// Models are rigid figures: half the squash, and a waddle instead of swinging legs.
		Squash = Wave * (0.025f + 0.04f * SpeedAlpha + 0.03f * Excite) * (bIsModel ? 0.5f : 1.f);
		BodyPivot->SetRelativeScale3D(FVector(1.f - Squash * 0.5f, 1.f - Squash * 0.5f, 1.f + Squash));
		if (bIsModel)
		{
			const float Step = AnimTime * 6.f;
			BodyPivot->SetRelativeRotation(FRotator(-3.f * SpeedAlpha, 0.f, FMath::Sin(Step) * 4.f * SpeedAlpha));
		}

		if (FootL && FootR)
		{
			const float Step = AnimTime * 6.f;
			const float Lift = ModelHeight * 0.06f * SpeedAlpha;
			FootL->SetRelativeLocation(FootLBase + FVector(FMath::Cos(Step) * Lift, 0.f, FMath::Max(0.f, FMath::Sin(Step)) * Lift));
			FootR->SetRelativeLocation(FootRBase + FVector(FMath::Cos(Step + PI) * Lift, 0.f, FMath::Max(0.f, FMath::Sin(Step + PI)) * Lift));
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
