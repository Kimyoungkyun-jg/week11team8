#include "EnginePCH.h"
#include "GameFramework/TestCharacter.h"
#include "GameFramework/SpringArmComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Camera/CameraComponent.h"
#include "Asset/AssetManager.h"

ATestCharacter::ATestCharacter()
{
	StaticMeshComponent = CreateDefaultSubobject<UStaticMeshComponent>("StaticMeshComponent");
	StaticMeshComponent->SetupAttachment(GetCapsuleComponent());
	StaticMeshComponent->SetStaticMesh(UAssetManager::GetAssetByPath<UStaticMesh>("Models/Hitori/Hitori.obj"));

	SpringArmComponent = CreateDefaultSubobject<USpringArmComponent>("SpringArmComponent");
	SpringArmComponent->SetupAttachment(GetCapsuleComponent());
	SpringArmComponent->SetRelativeLocation(FVector(0.0f, 0.0f, 0.7f));
	GetCameraComponent()->SetupAttachment(SpringArmComponent);
	GetCameraComponent()->SetRelativeLocation(FVector::ZeroVector);
	GetCameraComponent()->SetRelativeRotation(FRotator(0.0f, 0.0f, 0.0f));
}

void ATestCharacter::BeginPlay()
{
	Super::BeginPlay();
	const float Pitch = GetCameraComponent()->GetRelativeRotation().Pitch;
	SpringArmComponent->SetRelativeRotation(FRotator(Pitch, 0.0f, 0.0f));
	GetCameraComponent()->SetRelativeRotation(FRotator(0.0f, 0.0f, 0.0f));
	SpringArmComponent->ResetCameraLag();
}

void ATestCharacter::AddControllerPitchInput(float Value)
{
	if (!std::isfinite(Value)) return;
	FRotator Rotation = SpringArmComponent->GetRelativeRotation();
	Rotation.Pitch = FMath::Clamp(Rotation.Pitch + Value, -89.0f, 89.0f);
	SpringArmComponent->SetRelativeRotation(Rotation);
}
