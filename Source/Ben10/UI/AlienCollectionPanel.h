// Alien Museum - the floating holographic "Alien Collection" panel.
//
// Built from 3D components (planes, text, box hit volumes) instead of UMG so it can be pointed at
// and pinched with the same ray as everything else, without a widget-interaction setup.
// Each card shows a spinning mini version of the alien (its imported model when it has one);
// Prev / Next page through collections larger than one page.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Core/MuseumInteractable.h"
#include "AlienCollectionPanel.generated.h"

class UStaticMeshComponent;
class UTextRenderComponent;
class UBoxComponent;
class UMaterialInstanceDynamic;
class UAlienAppearanceComponent;
class UAlienCollectionAsset;
class UAlienDataAsset;

USTRUCT()
struct FMuseumPanelButton
{
	GENERATED_BODY()

	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> Plate;

	UPROPERTY()
	TObjectPtr<UTextRenderComponent> Label;

	UPROPERTY()
	TObjectPtr<UBoxComponent> Hit;

	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> MID;
};

USTRUCT()
struct FMuseumPanelCard
{
	GENERATED_BODY()

	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> Plate;

	UPROPERTY()
	TObjectPtr<UBoxComponent> Hit;

	UPROPERTY()
	TObjectPtr<UTextRenderComponent> Name;

	UPROPERTY()
	TObjectPtr<USceneComponent> PreviewPivot;

	UPROPERTY()
	TObjectPtr<UAlienAppearanceComponent> Preview;

	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> MID;

	int32 AlienIndex = INDEX_NONE;
};

UCLASS()
class BEN10_API AAlienCollectionPanel : public AActor, public IMuseumInteractable
{
	GENERATED_BODY()

public:
	AAlienCollectionPanel();

	UFUNCTION(BlueprintCallable, Category = "Museum|Collection")
	void SetCollection(UAlienCollectionAsset* InCollection);

	/** Places the panel in front of the viewer and shows it. */
	UFUNCTION(BlueprintCallable, Category = "Museum|Collection")
	void Summon(const FVector& HeadLocation, const FRotator& HeadRotation);

	UFUNCTION(BlueprintCallable, Category = "Museum|Collection")
	void SetPanelVisible(bool bVisible);

	UFUNCTION(BlueprintPure, Category = "Museum|Collection")
	bool IsPanelVisible() const { return bPanelVisible; }

	UFUNCTION(BlueprintCallable, Category = "Museum|Collection")
	void SetStatusText(const FString& Text);

	/** Highlights the card of the alien waiting to be placed (nullptr clears). */
	UFUNCTION(BlueprintCallable, Category = "Museum|Collection")
	void SetSelectedAlien(UAlienDataAsset* Alien);

	virtual void OnPointerHover(UPrimitiveComponent* HitComponent, bool bHovered) override;
	virtual bool OnPointerSelect(UPrimitiveComponent* HitComponent, AMuseumPawn* Pawn) override;
	virtual bool IsPointedThroughCases() const override { return true; }
	virtual void Tick(float DeltaSeconds) override;

	/** Distance in front of the head when summoned (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Museum|Collection")
	float SummonDistance = 70.f;

	/** How far below eye height the panel centre sits (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Museum|Collection")
	float SummonDrop = 12.f;

	/** Height of the mini aliens on the cards (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Museum|Collection")
	float PreviewHeight = 11.f;

	/** Widest a mini alien may be on its card (cm); wide models are shrunk to fit. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Museum|Collection")
	float PreviewWidth = 20.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Museum|Collection")
	FLinearColor AccentColor = FLinearColor(0.15f, 0.7f, 1.0f);

	/** Heading shown at the top of the panel. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Museum|Collection")
	FText PanelTitle = FText::FromString(TEXT("ALIEN COLLECTION"));

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Museum|Collection")
	TObjectPtr<UAlienCollectionAsset> Collection;

protected:
	virtual void BeginPlay() override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Museum|Collection")
	TObjectPtr<USceneComponent> Root;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Museum|Collection")
	TObjectPtr<UStaticMeshComponent> Background;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Museum|Collection")
	TObjectPtr<UBoxComponent> BackgroundHit;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Museum|Collection")
	TObjectPtr<UTextRenderComponent> Title;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Museum|Collection")
	TObjectPtr<UTextRenderComponent> Status;

	UPROPERTY()
	TArray<FMuseumPanelCard> Cards;

	UPROPERTY()
	FMuseumPanelButton AddButton;

	UPROPERTY()
	FMuseumPanelButton ClearButton;

	UPROPERTY()
	FMuseumPanelButton CloseButton;

	UPROPERTY()
	FMuseumPanelButton PrevButton;

	UPROPERTY()
	FMuseumPanelButton NextButton;

private:
	FMuseumPanelButton MakeButton(const TCHAR* Name, const FString& Text, const FVector& Location, const FVector2D& Size, float TextSize);
	void RefreshCards();
	void UpdateHighlights();
	void SetButtonEnabled(FMuseumPanelButton& Button, bool bEnabled);
	int32 GetPageCount() const;

	/** A panel sound (UMuseumAudio library name) at the card or button pressed. */
	void PlayPanelSound(FName Name, const UPrimitiveComponent* At) const;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> BackgroundMID;

	UPROPERTY(Transient)
	TObjectPtr<UAlienDataAsset> SelectedAlien;

	TWeakObjectPtr<UPrimitiveComponent> HoveredComponent;
	int32 Page = 0;
	bool bPanelVisible = false;
	bool bMaterialsCreated = false;

	static constexpr int32 CardsPerPage = 6;
	static constexpr int32 Columns = 3;
};
