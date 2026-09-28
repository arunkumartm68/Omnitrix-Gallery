// Alien Museum - data describing one alien species (identity, look, behaviour).
//
// Create one per alien: Content Browser > Add > Miscellaneous > Data Asset > AlienDataAsset.
//
// The look is either an imported 3D model (ModelMesh), or built from engine basic shapes: one of the
// built-in body shapes (plus optional extra Parts), or BodyShape = Custom where the whole creature is
// described by the Parts list.
// Part positions and sizes are in "height units": 1.0 = the alien's full Height, so the same
// part list works at any size. Axes: X = forward (the way the alien faces), Y = right, Z = up.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "AlienDataAsset.generated.h"

class AAlienCharacter;
class UStaticMesh;
class USkeletalMesh;
class UAnimSequence;
class UChamberHabitatAsset;

/** Signature move an alien performs now and then, and to show off to a visitor (UAlienActionComponent). */
UENUM(BlueprintType)
enum class EAlienAction : uint8
{
	Roll UMETA(ToolTip = "Curls into an armoured ball, rolls and bounces off the glass (Cannonbolt)"),
	Dash UMETA(ToolTip = "Super-speed zig-zag with after-images (XLR8)"),
	Flare UMETA(ToolTip = "Head flames surge and a fireball flies (Heatblast)"),
	Flex UMETA(ToolTip = "Faces the visitor, strikes a strength pose and stomps a shockwave (Four Arms)"),
	CrystalBurst UMETA(ToolTip = "Crystal spikes burst out of the ground around it (Diamondhead)"),
	Phase UMETA(ToolTip = "Fades into mist and reappears somewhere else (Ghostfreak)"),
	Scream UMETA(ToolTip = "Sonic rings blast out towards the visitor (Echo Echo)"),
	Clone UMETA(ToolTip = "Splits into copies that step out and merge back (Ditto, Echo Echo)"),
	Spit UMETA(ToolTip = "Eats a loose object and spits a bouncing energy ball (Upchuck)"),
	Pounce UMETA(ToolTip = "Sniffs the air, then leaps (Wildmutt, Ripjaws)"),
	Vines UMETA(ToolTip = "Vines lash out to the glass and pull back (Wildvine)"),
	Melt UMETA(ToolTip = "Melts into the floor and re-forms somewhere else (Upgrade)"),
	Scurry UMETA(ToolTip = "Quick tiny zig-zag scurry with hops (Grey Matter)"),
	Fly UMETA(ToolTip = "Takes off and circles around the case (Stinkfly)"),
	Howl UMETA(ToolTip = "Winds up, opens its jaw wide and blasts sonic rings at the visitor (Benwolf; plays the model's Special clips when it has them)")
};

/** One of an animated model's own animations (UAlienDataAsset::Clips). */
UENUM(BlueprintType)
enum class EAlienClip : uint8
{
	Idle,
	Move,
	Jump,
	SpecialStart,
	SpecialLoop,
	Special,
	Hit,
	Attack
};

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

/**
 * A piece of an imported model that moves on its own (e.g. a wing split off the body in Blender). It
 * was exported in the same frame as ModelMesh, so it sits exactly where it belongs; it swings around
 * its hinge - slowly at rest, fast while the alien flies.
 */
USTRUCT(BlueprintType)
struct BEN10_API FAlienModelPart
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Part")
	TSoftObjectPtr<UStaticMesh> Mesh;

	/** Joint it swings around, in the model mesh's own space (cm, as imported). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Part")
	FVector Hinge = FVector::ZeroVector;

	/** Swing axis in the model mesh's own space (flapping wings: forward; the other wing: backward). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Part")
	FVector Axis = FVector::ForwardVector;

	/**
	 * At rest it swings Amount degrees either side of Offset, Speed times a second. Wings that meet
	 * over the back beat on one side only: Offset = Amount keeps them between 0 and twice the amount.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Part")
	float Amount = 15.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Part")
	float Offset = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Part")
	float Speed = 6.f;

	/** The same while flying (the Fly move lifts the body), moving or held; blends from the rest values. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Part")
	float FlyingAmount = 32.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Part")
	float FlyingOffset = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Part")
	float FlyingSpeed = 12.f;

	/** 0..1 offset into the cycle. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Part")
	float Phase = 0.f;
};

/** One leg of a rigged model: three bones from the body down to the paw that is planted on the ground. */
USTRUCT(BlueprintType)
struct BEN10_API FAlienRigLeg
{
	GENERATED_BODY()

	/** Shoulder / hip. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rig")
	FName Upper;

	/** Elbow / knee: bends the way it bends in the model's rest pose. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rig")
	FName Lower;

	/** Wrist / ankle: stays where it is put down while the leg carries the body (IK). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rig")
	FName End;

	/** When this leg lifts, 0..1 of the step cycle (a walk: left hind 0, left fore 0.25, right hind 0.5, right fore 0.75). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rig")
	float Phase = 0.f;

	/** Front legs fold the paw back while it swings forward. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rig")
	bool bFront = false;
};

/**
 * How a rigged model moves bone by bone (UAlienAppearanceComponent): which bones walk, breathe, look
 * around and wag. Every part is optional; bone names come from the model's skeleton.
 */
USTRUCT(BlueprintType)
struct BEN10_API FAlienRig
{
	GENERATED_BODY()

	/** Legs that walk: the paws are planted on the ground and step in turn (a four-legged walk). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rig")
	TArray<FAlienRigLeg> Legs;

	/** Pelvis to chest. The first bone carries the body: it bobs with the steps and lowers to crouch. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rig")
	TArray<FName> Spine;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rig")
	FName Neck;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rig")
	FName Head;

	/** Opens a little to pant, wide to snarl when excited. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rig")
	FName Jaw;

	/** Tail bones, root first: a wave runs down them to the tip. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rig")
	TArray<FName> Tail;

	/** Bones that drift slowly with their children (a ghost's arms). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rig")
	TArray<FName> Floating;

	/** Stride at full walking speed and how high a paw lifts, in heights of the model. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rig")
	float StrideLength = 0.3f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rig")
	float StepHeight = 0.07f;

	/** Degrees the tail tip swings (the root less), and waves per second. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rig")
	float TailAmount = 14.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rig")
	float TailSpeed = 0.6f;

	/** Lifts its nose and sniffs the air now and then (Wildmutt has no eyes). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rig")
	bool bSniffs = false;
};

/**
 * An animated model's own hand-made animations (blender_convert_models.py `animated`: Benwolf), played on
 * its rigged model in place of the rest pose. The Rig's head look and sniffing still work on top.
 * Idle loops while it stands, Move while it walks (faster or slower with the ground speed), Jump gives
 * its mid-air pose (leaping, held in a hand); the others play when a move or a reaction asks for them.
 */
USTRUCT(BlueprintType)
struct BEN10_API FAlienClips
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Clips")
	TSoftObjectPtr<UAnimSequence> Idle;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Clips")
	TSoftObjectPtr<UAnimSequence> Move;

	/** Ground speed (cm/s at the data asset's Height) of Move played at its own pace and in full, its paws keeping pace with the ground. Slower walks take shorter strides. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Clips", meta = (Units = "cm/s", ClampMin = 1))
	float MoveSpeed = 60.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Clips")
	TSoftObjectPtr<UAnimSequence> Jump;

	/** The signature move's wind-up (plays once). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Clips")
	TSoftObjectPtr<UAnimSequence> SpecialStart;

	/** The part of the signature move that repeats while it lasts. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Clips")
	TSoftObjectPtr<UAnimSequence> SpecialLoop;

	/** The whole signature move in one go. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Clips")
	TSoftObjectPtr<UAnimSequence> Special;

	/** A flinch (startled, hurt). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Clips")
	TSoftObjectPtr<UAnimSequence> Hit;

	/** A strike (swipe, punch). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Clips")
	TSoftObjectPtr<UAnimSequence> Attack;

	const TSoftObjectPtr<UAnimSequence>& Get(EAlienClip Clip) const
	{
		switch (Clip)
		{
		case EAlienClip::Move: return Move;
		case EAlienClip::Jump: return Jump;
		case EAlienClip::SpecialStart: return SpecialStart;
		case EAlienClip::SpecialLoop: return SpecialLoop;
		case EAlienClip::Special: return Special;
		case EAlienClip::Hit: return Hit;
		case EAlienClip::Attack: return Attack;
		default: return Idle;
		}
	}
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

	// ---------- Imported model ----------

	/**
	 * Imported 3D model. When set it is shown instead of the shape-built body: it is scaled to Height,
	 * stood on its lowest point and centred, so models of any size and pivot work. It should face +X.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Model")
	TSoftObjectPtr<UStaticMesh> ModelMesh;

	/** Extra rotation for a model that does not face forward (+X) or stand upright. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Model")
	FRotator ModelRotation = FRotator::ZeroRotator;

	/** Where the model came from (download / author), shown on the chamber's info panel. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Model")
	FText ModelCredit;

	/** Pieces of the model that move on their own (Stinkfly's wings). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Model")
	TArray<FAlienModelPart> ModelParts;

	/**
	 * Optional skinned version of ModelMesh - the same shape in the same frame (blender_convert_models.py
	 * `rigged`). When set it is shown instead, and Rig animates its bones: walking legs with planted paws,
	 * breathing, looking around, a wagging tail. ModelMesh still sets the size and makes the after-images.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Model")
	TSoftObjectPtr<USkeletalMesh> RiggedMesh;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Model")
	FAlienRig Rig;

	/** The rigged model's own animations (Benwolf). When Idle is set they replace the procedural walk. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Model")
	FAlienClips Clips;

	bool HasModel() const { return !ModelMesh.IsNull(); }

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

	/** Standing height at chamber scale 1. The default chamber (105 cm glass) fits aliens up to ~90 cm. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Appearance", meta = (ClampMin = 10, ClampMax = 150, Units = "cm"))
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

	/** Home-world diorama built inside the case (ground, props with physics, ambient effect). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Chamber")
	TObjectPtr<UChamberHabitatAsset> Habitat;

	// ---------- Signature moves ----------

	/** Moves this alien performs; the first one is its show-off move for visitors. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Actions")
	TArray<EAlienAction> SignatureActions;

	/** Chance to perform a move (instead of wandering or looking around) when an idle period ends. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Actions", meta = (ClampMin = 0, ClampMax = 1))
	float ActionChance = 0.4f;

	/** Colour of the moves' effects (flames, trails, rings, crystals...). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Actions")
	FLinearColor ActionColor = FLinearColor(0.3f, 1.f, 0.4f);

	/** Flames burn on top of the head all the time (Heatblast). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Actions")
	bool bHeadFlames = false;

	/** Leaves glowing after-images whenever it runs fast (XLR8). Needs an imported model. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Actions")
	bool bSpeedTrail = false;

	/** Optional second pose of ModelMesh (same size and pivot), shown during Flex, e.g. a double-biceps pose. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Actions")
	TSoftObjectPtr<UStaticMesh> PoseMesh;

	/** Rolled-up form shown during Roll (e.g. Cannonbolt's ball model). Empty = a built-in armoured ball in the action colour. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Actions")
	TSoftObjectPtr<UStaticMesh> BallMesh;

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
