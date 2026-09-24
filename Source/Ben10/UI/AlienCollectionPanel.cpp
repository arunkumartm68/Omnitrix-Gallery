// Alien Museum - the floating holographic "Alien Collection" panel.

#include "UI/AlienCollectionPanel.h"
#include "Aliens/AlienAppearanceComponent.h"
#include "Core/MuseumAssets.h"
#include "Core/MuseumDirector.h"
#include "Data/AlienCollectionAsset.h"
#include "Data/AlienDataAsset.h"
#include "Interaction/MuseumPawn.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"

// Panel space: +X points at the viewer, +Y is the viewer's left, +Z is up. Sizes in cm.
namespace
{
	const FRotator PlaneFacingViewer(-90.f, 0.f, 0.f); // basic plane normal +Z -> +X
	const FLinearColor SelectedColor(0.3f, 1.0f, 0.5f);

	void MakePointerTarget(UBoxComponent* Box)
	{
		Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		Box->SetCollisionObjectType(ECC_WorldDynamic);
		Box->SetCollisionResponseToAllChannels(ECR_Ignore);
		Box->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
		Box->SetGenerateOverlapEvents(false);
		Box->SetCanEverAffectNavigation(false);
		Box->SetHiddenInGame(true);
	}

	void SetupText(UTextRenderComponent* Text, float Size, const FColor& Color)
	{
		Text->SetHorizontalAlignment(EHTA_Center);
		Text->SetVerticalAlignment(EVRTA_TextCenter);
		Text->SetWorldSize(Size);
		Text->SetTextRenderColor(Color);
		Text->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Text->SetCastShadow(false);
	}
}

AAlienCollectionPanel::AAlienCollectionPanel()
{
	PrimaryActorTick.bCanEverTick = true;

	static ConstructorHelpers::FObjectFinder<UStaticMesh> PlaneFinder(TEXT("/Engine/BasicShapes/Plane.Plane"));
	UStaticMesh* PlaneMesh = PlaneFinder.Object;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;

	auto MakePlate = [this, PlaneMesh](const TCHAR* Name, const FVector& Location, const FVector2D& Size, int32 SortPriority)
	{
		UStaticMeshComponent* Plate = CreateDefaultSubobject<UStaticMeshComponent>(Name);
		Plate->SetupAttachment(Root);
		Plate->SetStaticMesh(PlaneMesh);
		Plate->SetRelativeLocationAndRotation(Location, PlaneFacingViewer);
		Plate->SetRelativeScale3D(FVector(Size.Y / 100.f, Size.X / 100.f, 1.f)); // X = height, Y = width after rotation
		Plate->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Plate->SetCastShadow(false);
		Plate->SetCanEverAffectNavigation(false);
		Plate->SetTranslucentSortPriority(SortPriority);
		return Plate;
	};

	// Background.
	Background = MakePlate(TEXT("Background"), FVector(-0.5f, 0.f, 0.f), FVector2D(88.f, 64.f), 2);
	BackgroundHit = CreateDefaultSubobject<UBoxComponent>(TEXT("BackgroundHit"));
	BackgroundHit->SetupAttachment(Root);
	BackgroundHit->SetBoxExtent(FVector(0.5f, 44.f, 32.f));
	BackgroundHit->SetRelativeLocation(FVector(-1.f, 0.f, 0.f));
	MakePointerTarget(BackgroundHit);

	Title = CreateDefaultSubobject<UTextRenderComponent>(TEXT("Title"));
	Title->SetupAttachment(Root);
	Title->SetRelativeLocation(FVector(0.5f, 0.f, 26.f));
	SetupText(Title, 4.2f, FColor(150, 230, 255));
	Title->SetText(FText::FromString(TEXT("ALIEN COLLECTION")));

	Status = CreateDefaultSubobject<UTextRenderComponent>(TEXT("Status"));
	Status->SetupAttachment(Root);
	Status->SetRelativeLocation(FVector(0.5f, 0.f, 20.5f));
	SetupText(Status, 1.7f, FColor(210, 240, 255));
	Status->SetText(FText::FromString(TEXT("Choose an alien, then point at a chamber.")));

	// 3 x 2 grid of cards.
	for (int32 i = 0; i < CardsPerPage; ++i)
	{
		const int32 Column = i % Columns;
		const int32 Row = i / Columns;
		const float Y = 27.f - 27.f * Column;
		const float Z = 7.f - 21.f * Row;

		FMuseumPanelCard Card;
		Card.Plate = MakePlate(*FString::Printf(TEXT("Card%dPlate"), i), FVector(0.f, Y, Z), FVector2D(25.f, 19.f), 3);

		Card.Hit = CreateDefaultSubobject<UBoxComponent>(*FString::Printf(TEXT("Card%dHit"), i));
		Card.Hit->SetupAttachment(Root);
		Card.Hit->SetBoxExtent(FVector(1.5f, 12.5f, 9.5f));
		Card.Hit->SetRelativeLocation(FVector(0.5f, Y, Z));
		MakePointerTarget(Card.Hit);

		Card.PreviewPivot = CreateDefaultSubobject<USceneComponent>(*FString::Printf(TEXT("Card%dPreviewPivot"), i));
		Card.PreviewPivot->SetupAttachment(Root);
		Card.PreviewPivot->SetRelativeLocation(FVector(3.f, Y, Z - 6.f));

		Card.Preview = CreateDefaultSubobject<UAlienAppearanceComponent>(*FString::Printf(TEXT("Card%dPreview"), i));
		Card.Preview->SetupAttachment(Card.PreviewPivot);

		Card.Name = CreateDefaultSubobject<UTextRenderComponent>(*FString::Printf(TEXT("Card%dName"), i));
		Card.Name->SetupAttachment(Root);
		Card.Name->SetRelativeLocation(FVector(1.f, Y, Z - 7.8f));
		SetupText(Card.Name, 1.6f, FColor(235, 250, 255));

		Cards.Add(Card);
	}

	AddButton = MakeButton(TEXT("AddButton"), TEXT("+ NEW CHAMBER"), FVector(0.f, 27.f, -28.f), FVector2D(24.f, 6.f), 1.7f);
	ClearButton = MakeButton(TEXT("ClearButton"), TEXT("CLEAR MUSEUM"), FVector(0.f, 0.f, -28.f), FVector2D(24.f, 6.f), 1.7f);
	CloseButton = MakeButton(TEXT("CloseButton"), TEXT("CLOSE"), FVector(0.f, -27.f, -28.f), FVector2D(24.f, 6.f), 1.7f);
	PrevButton = MakeButton(TEXT("PrevButton"), TEXT("<"), FVector(0.f, 40.5f, -3.5f), FVector2D(5.f, 12.f), 3.f);
	NextButton = MakeButton(TEXT("NextButton"), TEXT(">"), FVector(0.f, -40.5f, -3.5f), FVector2D(5.f, 12.f), 3.f);
}

FMuseumPanelButton AAlienCollectionPanel::MakeButton(const TCHAR* Name, const FString& Text, const FVector& Location, const FVector2D& Size, float TextSize)
{
	static ConstructorHelpers::FObjectFinder<UStaticMesh> PlaneFinder(TEXT("/Engine/BasicShapes/Plane.Plane"));

	FMuseumPanelButton Button;
	Button.Plate = CreateDefaultSubobject<UStaticMeshComponent>(*FString::Printf(TEXT("%sPlate"), Name));
	Button.Plate->SetupAttachment(Root);
	Button.Plate->SetStaticMesh(PlaneFinder.Object);
	Button.Plate->SetRelativeLocationAndRotation(Location, PlaneFacingViewer);
	Button.Plate->SetRelativeScale3D(FVector(Size.Y / 100.f, Size.X / 100.f, 1.f));
	Button.Plate->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Button.Plate->SetCastShadow(false);
	Button.Plate->SetTranslucentSortPriority(3);

	Button.Hit = CreateDefaultSubobject<UBoxComponent>(*FString::Printf(TEXT("%sHit"), Name));
	Button.Hit->SetupAttachment(Root);
	Button.Hit->SetBoxExtent(FVector(1.5f, Size.X * 0.5f, Size.Y * 0.5f));
	Button.Hit->SetRelativeLocation(Location + FVector(0.5f, 0.f, 0.f));
	MakePointerTarget(Button.Hit);

	Button.Label = CreateDefaultSubobject<UTextRenderComponent>(*FString::Printf(TEXT("%sLabel"), Name));
	Button.Label->SetupAttachment(Root);
	Button.Label->SetRelativeLocation(Location + FVector(1.f, 0.f, 0.f));
	SetupText(Button.Label, TextSize, FColor(235, 250, 255));
	Button.Label->SetText(FText::FromString(Text));
	return Button;
}

void AAlienCollectionPanel::BeginPlay()
{
	Super::BeginPlay();

	UMaterialInterface* Hologram = MuseumAssets::HologramMaterial();
	if (Hologram)
	{
		BackgroundMID = UMaterialInstanceDynamic::Create(Hologram, this);
		BackgroundMID->SetVectorParameterValue(MuseumAssets::Params::Color, AccentColor * 0.35f);
		BackgroundMID->SetScalarParameterValue(MuseumAssets::Params::Opacity, 0.45f);
		Background->SetMaterial(0, BackgroundMID);

		for (FMuseumPanelCard& Card : Cards)
		{
			Card.MID = UMaterialInstanceDynamic::Create(Hologram, this);
			Card.Plate->SetMaterial(0, Card.MID);
		}
		for (FMuseumPanelButton* Button : { &AddButton, &ClearButton, &CloseButton, &PrevButton, &NextButton })
		{
			Button->MID = UMaterialInstanceDynamic::Create(Hologram, this);
			Button->Plate->SetMaterial(0, Button->MID);
		}
	}

	RefreshCards();
	SetPanelVisible(false);
}

void AAlienCollectionPanel::SetCollection(UAlienCollectionAsset* InCollection)
{
	Collection = InCollection;
	Page = 0;
	if (HasActorBegunPlay())
	{
		RefreshCards();
	}
}

int32 AAlienCollectionPanel::GetPageCount() const
{
	const int32 Num = Collection ? Collection->Aliens.Num() : 0;
	return FMath::Max(1, FMath::DivideAndRoundUp(Num, CardsPerPage));
}

void AAlienCollectionPanel::SetButtonEnabled(FMuseumPanelButton& Button, bool bEnabled)
{
	Button.Plate->SetVisibility(bEnabled);
	Button.Label->SetVisibility(bEnabled);
	Button.Hit->SetCollisionEnabled(bEnabled ? ECollisionEnabled::QueryOnly : ECollisionEnabled::NoCollision);
}

void AAlienCollectionPanel::RefreshCards()
{
	Page = FMath::Clamp(Page, 0, GetPageCount() - 1);
	for (int32 i = 0; i < Cards.Num(); ++i)
	{
		FMuseumPanelCard& Card = Cards[i];
		const int32 Index = Page * CardsPerPage + i;
		UAlienDataAsset* Data = (Collection && Collection->Aliens.IsValidIndex(Index)) ? Collection->Aliens[Index].Get() : nullptr;
		Card.AlienIndex = Data ? Index : INDEX_NONE;

		Card.Plate->SetVisibility(Data != nullptr);
		Card.Name->SetVisibility(Data != nullptr);
		Card.Hit->SetCollisionEnabled(Data ? ECollisionEnabled::QueryOnly : ECollisionEnabled::NoCollision);
		if (Data)
		{
			Card.Name->SetText(Data->DisplayName);
			Card.Preview->BuildAppearance(Data);
			Card.PreviewPivot->SetRelativeScale3D(FVector(PreviewHeight / FMath::Max(1.f, Data->Height)));
		}
		else
		{
			Card.Preview->ClearAppearance();
		}
	}

	const bool bPages = GetPageCount() > 1;
	SetButtonEnabled(PrevButton, bPages);
	SetButtonEnabled(NextButton, bPages);
	UpdateHighlights();
}

void AAlienCollectionPanel::UpdateHighlights()
{
	UPrimitiveComponent* Hovered = HoveredComponent.Get();
	for (FMuseumPanelCard& Card : Cards)
	{
		if (!Card.MID)
		{
			continue;
		}
		const UAlienDataAsset* Data = (Collection && Collection->Aliens.IsValidIndex(Card.AlienIndex)) ? Collection->Aliens[Card.AlienIndex].Get() : nullptr;
		const bool bSelected = Data && Data == SelectedAlien.Get();
		const bool bHovered = Hovered == Card.Hit;
		Card.MID->SetVectorParameterValue(MuseumAssets::Params::Color, bSelected ? SelectedColor : AccentColor);
		Card.MID->SetScalarParameterValue(MuseumAssets::Params::Opacity, bHovered ? 0.6f : (bSelected ? 0.5f : 0.28f));
	}
	for (FMuseumPanelButton* Button : { &AddButton, &ClearButton, &CloseButton, &PrevButton, &NextButton })
	{
		if (Button->MID)
		{
			const bool bHovered = Hovered == Button->Hit;
			Button->MID->SetVectorParameterValue(MuseumAssets::Params::Color, AccentColor);
			Button->MID->SetScalarParameterValue(MuseumAssets::Params::Opacity, bHovered ? 0.7f : 0.35f);
		}
	}
}

void AAlienCollectionPanel::Summon(const FVector& HeadLocation, const FRotator& HeadRotation)
{
	const FRotator Flat(0.f, HeadRotation.Yaw, 0.f);
	const FVector Location = HeadLocation + Flat.Vector() * SummonDistance - FVector(0.f, 0.f, SummonDrop);
	SetActorLocationAndRotation(Location, FRotator(0.f, HeadRotation.Yaw + 180.f, 0.f));
	SetPanelVisible(true);
}

void AAlienCollectionPanel::SetPanelVisible(bool bVisible)
{
	bPanelVisible = bVisible;
	SetActorHiddenInGame(!bVisible);
	SetActorEnableCollision(bVisible);
	SetActorTickEnabled(bVisible);
	if (!bVisible)
	{
		HoveredComponent = nullptr;
	}
}

void AAlienCollectionPanel::SetStatusText(const FString& Text)
{
	Status->SetText(FText::FromString(Text));
}

void AAlienCollectionPanel::SetSelectedAlien(UAlienDataAsset* Alien)
{
	SelectedAlien = Alien;
	UpdateHighlights();
}

void AAlienCollectionPanel::OnPointerHover(UPrimitiveComponent* HitComponent, bool bHovered)
{
	if (bHovered)
	{
		HoveredComponent = HitComponent;
	}
	else if (HoveredComponent.Get() == HitComponent)
	{
		HoveredComponent = nullptr;
	}
	UpdateHighlights();
}

bool AAlienCollectionPanel::OnPointerSelect(UPrimitiveComponent* HitComponent, AMuseumPawn* Pawn)
{
	if (!HitComponent || !Pawn)
	{
		return false;
	}

	for (const FMuseumPanelCard& Card : Cards)
	{
		if (HitComponent == Card.Hit && Collection && Collection->Aliens.IsValidIndex(Card.AlienIndex))
		{
			UAlienDataAsset* Alien = Collection->Aliens[Card.AlienIndex];
			SetSelectedAlien(Alien);
			Pawn->BeginPlaceAlien(Alien);
			return true;
		}
	}

	if (HitComponent == AddButton.Hit)
	{
		SetSelectedAlien(nullptr);
		Pawn->BeginPlaceChamber();
		return true;
	}
	if (HitComponent == ClearButton.Hit)
	{
		SetSelectedAlien(nullptr);
		Pawn->CancelPlacement();
		if (AMuseumDirector* Director = AMuseumDirector::Get(this))
		{
			Director->ClearMuseum();
		}
		SetStatusText(TEXT("Museum cleared. Add a chamber to start again."));
		return true;
	}
	if (HitComponent == CloseButton.Hit)
	{
		SetSelectedAlien(nullptr);
		Pawn->CancelPlacement();
		SetPanelVisible(false);
		return true;
	}
	if (HitComponent == PrevButton.Hit || HitComponent == NextButton.Hit)
	{
		const int32 Pages = GetPageCount();
		Page = (Page + (HitComponent == NextButton.Hit ? 1 : Pages - 1)) % Pages;
		RefreshCards();
		return true;
	}

	return HitComponent == BackgroundHit; // swallow presses on the panel background
}

void AAlienCollectionPanel::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// Mini aliens turn slowly and idle-animate while the panel is open.
	for (FMuseumPanelCard& Card : Cards)
	{
		if (Card.AlienIndex != INDEX_NONE)
		{
			Card.PreviewPivot->AddLocalRotation(FRotator(0.f, 35.f * DeltaSeconds, 0.f));
			Card.Preview->UpdateAnimation(DeltaSeconds, 0.f, nullptr, HoveredComponent.Get() == Card.Hit);
		}
	}
}
