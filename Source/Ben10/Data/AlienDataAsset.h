// Alien Museum - data describing one alien species (identity, look, behaviour).
//
// Create one per alien: Content Browser > Add > Miscellaneous > Data Asset > AlienDataAsset.
//
// The look is built from engine basic shapes: either one of the built-in body shapes (plus optional
// extra Parts), or BodyShape = Custom where the whole creature is described by the Parts list.
// Part positions and sizes are in "height units": 1.0 = the alien's full Height, so the same
// part list works at any size. Axes: X = forward (the way the alien faces), Y = right, Z = up.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "AlienDataAsset.generated.h"

class AAlienCharacter;

/** Silhouette of the built-in placeholder alien. */
UENUM(BlueprintType)
enum class EAlienBodyShape : uint8
{
	Blob UMETA(ToolTip = "Round body, big head"),
	Tall UMETA(ToolTip = "Thin cylindrical body"),
	Squat UMETA(ToolTip = "Wide and short"),
	Custom UMETA(ToolTip = "No built-in body, head, eyes, antennae or feet: the look comes only from Parts")
};

/** Surface style of the Skin colour. */
UENUM(BlueprintType)
enum class EAlienSkinStyle : uint8
{
	Organic UMETA(ToolTip = "Lit skin with a soft rim glow"),
	Crystal UMETA(ToolTip = "See-through glowing crystal"),
	Ghost UMETA(ToolTip = "Faint see-through ghost")
};

/** Engine basic shape used for a part (each is 100 cm across at size 1). */
UENUM(BlueprintType)
enum class EAlienPartShape : uint8
{
	Sphere,
	Cylinder,
	Cone UMETA(ToolTip = "Tip points up (+Z) before rotation"),
	Cube
};

/** What a part is attached to. Offsets are measured from that point. */
UENUM(BlueprintType)
enum class EAlienPartAttach : uint8
{
	Feet UMETA(ToolTip = "Ground point under the alien; does not bob"),
	Body UMETA(ToolTip = "Ground point, but bobs / squashes / hovers with the body"),
	Head UMETA(ToolTip = "The neck; follows the head when it looks around")
};

UENUM(BlueprintType)
enum class EAlienPartColor : uint8
{
	Skin,
	Accent,
	Dark,
	Eye UMETA(ToolTip = "Glows in Eye Color and blinks"),
	Glow UMETA(ToolTip = "Glows in Glow Color"),
	Custom UMETA(ToolTip = "Custom Color (lit, or glowing when Custom Glows is ticked)")
};

UENUM(BlueprintType)
enum class EAlienPartMotion : uint8
{
	None,
	SwayRoll UMETA(ToolTip = "Rocks around the forward axis (wings: big amount, fast)"),
	SwayPitch UMETA(ToolTip = "Nods forward and back"),
	SwayYaw UMETA(ToolTip = "Swings left and right (tails)"),
	WalkSwing UMETA(ToolTip = "Swings forward/back with the walk cycle (arms, legs)"),
	Flicker UMETA(ToolTip = "Flame-like height flicker"),
	Pulse UMETA(ToolTip = "Grows and shrinks"),
	Spin UMETA(ToolTip = "Turns continuously around the up axis")
};

/** One primitive of a data-driven alien body. */
USTRUCT(BlueprintType)
struct BEN10_API FAlienBodyPart
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Part")
	EAlienPartShape Shape = EAlienPartShape::Sphere;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Part")
	EAlienPartAttach AttachTo = EAlienPartAttach::Body;

	/** Centre of the part in height units, relative to the attach point. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Part")
	FVector Offset = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Part")
	FRotator Rotation = FRotator::ZeroRotator;

	/** Full size along X, Y, Z in height units (diameter for spheres, length for cylinders/cones). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Part")
	FVector Size = FVector(0.1f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Part")
	EAlienPartColor Color = EAlienPartColor::Skin;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Part", meta = (EditCondition = "Color == EAlienPartColor::Custom", EditConditionHides))
	FLinearColor CustomColor = FLinearColor::White;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Part", meta = (EditCondition = "Color == EAlienPartColor::Custom", EditConditionHides))
	bool bCustomGlows = false;

	/** Also create a mirror copy on the other side (Y flipped). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Part")
	bool bMirror = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Part|Motion")
	EAlienPartMotion Motion = EAlienPartMotion::None;

	/** Degrees for sways/swings, degrees per second for Spin, percent for Flicker/Pulse. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Part|Motion")
	float MotionAmount = 10.f;

	/** Cycles per second (roughly; excited aliens move faster). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Part|Motion")
	float MotionSpeed = 1.f;

	/** 0..1 offset into the cycle. A mirrored WalkSwing part swings half a cycle later. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Part|Motion")
	float MotionPhase = 0.f;

	/** Joint the motion rotates around, relative to the part centre in height units (e.g. a shoulder). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Part|Motion")
	FVector PivotOffset = FVector::ZeroVector;
};

UCLASS(BlueprintType)
class BEN10_API UAlienDataAsset : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	// ---------- Identity ----------

	/** Unique id, used for saving which alien lives in which chamber. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Identity")
	FName AlienId;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Identity")
	FText DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Identity")
	FText Species;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Identity")
	FText HomePlanet;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Identity", meta = (MultiLine = true))
	FText Description;

	// ---------- Spawning ----------

	/** Character class to spawn. Leave empty to use the built-in placeholder alien (AAlienCharacter). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spawning")
	TSoftClassPtr<AAlienCharacter> CharacterClass;

	// ---------- Placeholder appearance ----------

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Appearance")
	EAlienBodyShape BodyShape = EAlienBodyShape::Blob;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Appearance")
	EAlienSkinStyle SkinStyle = EAlienSkinStyle::Organic;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Appearance")
	FLinearColor SkinColor = FLinearColor(0.30f, 0.85f, 0.35f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Appearance")
	FLinearColor AccentColor = FLinearColor(0.08f, 0.35f, 0.18f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Appearance")
	FLinearColor EyeColor = FLinearColor(1.0f, 0.9f, 0.2f);

	/** Colour of parts set to Glow (flames, circuits, lures...). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Appearance")
	FLinearColor GlowColor = FLinearColor(1.0f, 0.6f, 0.1f);

	/** Edge glow of the skin. Alpha 0 = automatic (a lighter skin colour). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Appearance")
	FLinearColor RimColor = FLinearColor(0.f, 0.f, 0.f, 0.f);

	/** Built-in eyes (0-3). Custom bodies use Eye parts instead. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Appearance", meta = (ClampMin = 0, ClampMax = 3))
	int32 EyeCount = 2;

	/** Size multiplier for the built-in eyes. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Appearance", meta = (ClampMin = 0.3, ClampMax = 3))
	float EyeSize = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Appearance")
	bool bHasAntennae = true;

	/** Floats above the ground instead of walking on feet. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Appearance")
	bool bHovers = false;

	/** Standing height at chamber scale 1. The default chamber fits aliens up to ~45 cm. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Appearance", meta = (ClampMin = 15, ClampMax = 45, Units = "cm"))
	float Height = 32.f;

	/** Custom bodies: the neck / head joint in height units (the head turns around it). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Appearance", meta = (EditCondition = "BodyShape == EAlienBodyShape::Custom", EditConditionHides))
	FVector CustomHeadPivot = FVector(0.f, 0.f, 0.62f);

	/** Custom bodies: collision radius in height units (keeps the body away from the glass). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Appearance", meta = (EditCondition = "BodyShape == EAlienBodyShape::Custom", EditConditionHides, ClampMin = 0.1, ClampMax = 0.6))
	float CustomRadius = 0.3f;

	/** Extra primitives added to the body (or the whole body when BodyShape = Custom). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Appearance")
	TArray<FAlienBodyPart> Parts;

	// ---------- Chamber ----------

	/** Chamber light / glass tint while this alien lives in it. Alpha 0 = keep the chamber's own colour. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Chamber")
	FLinearColor ChamberLightColor = FLinearColor(0.f, 0.f, 0.f, 0.f);

	// ---------- Behaviour ----------

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Behaviour", meta = (ClampMin = 5, ClampMax = 80, Units = "cm/s"))
	float WalkSpeed = 22.f;

	/** Random idle time range in seconds (min, max). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Behaviour")
	FVector2D IdleDuration = FVector2D(1.5f, 4.0f);

	/** Chance to look around instead of walking when an idle period ends. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Behaviour", meta = (ClampMin = 0, ClampMax = 1))
	float LookAroundChance = 0.35f;

	/** Chance to come to the glass when the player walks up. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Behaviour", meta = (ClampMin = 0, ClampMax = 1))
	float Curiosity = 0.7f;

	/** Distance at which the alien notices the player's head. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Behaviour", meta = (Units = "cm"))
	float NoticePlayerDistance = 160.f;

	/** 0 = calm, 1 = hyperactive (bob speed, hops, antenna wiggle). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Behaviour", meta = (ClampMin = 0, ClampMax = 1))
	float Energy = 0.5f;

	virtual FPrimaryAssetId GetPrimaryAssetId() const override;
};
