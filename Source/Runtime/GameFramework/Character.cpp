#include "EnginePCH.h"
#include "GameFramework/Character.h"
#include "Components/CapsuleComponent.h"
#include "Components/CharacterMovementComponent.h"
#include "Camera/CameraComponent.h"

ACharacter::ACharacter()
{
	SetActorEnableCollision(true);
	CapsuleComponent = CreateDefaultSubobject<UCapsuleComponent>("CapsuleComponent");
	SetRootComponent(CapsuleComponent);
	CapsuleComponent->bBlockComponent = true;
	CameraComponent = CreateDefaultSubobject<UCameraComponent>("CameraComponent");
	CameraComponent->SetupAttachment(CapsuleComponent);
	CameraComponent->SetRelativeLocation(FVector(0.0f, 0.0f, 0.7f));
	CameraComponent->SetExternalInputManaged(true);
	CharacterMovement = CreateDefaultSubobject<UCharacterMovementComponent>("CharacterMovement");
}

void ACharacter::BeginPlay()
{
	const FRotator SpawnRotation = GetActorRotation();
	CapsuleComponent->SetRelativeRotation(FRotator(0.0f, SpawnRotation.Yaw, 0.0f));
	CameraComponent->SetRelativeRotation(FRotator(SpawnRotation.Pitch, 0.0f, 0.0f));
	Super::BeginPlay();
}

void ACharacter::AddMovementInput(const FVector& Direction, float ScaleValue)
{
	Super::AddMovementInput(FVector(Direction.X, Direction.Y, 0.0f), ScaleValue);
}

void ACharacter::AddControllerYawInput(float Value)
{
	if (std::isfinite(Value))
	{
		FRotator Rotation = GetActorRotation();
		Rotation.Yaw += Value;
		CapsuleComponent->SetRelativeRotation(Rotation);
	}
}

void ACharacter::AddControllerPitchInput(float Value)
{
	if (std::isfinite(Value))
	{
		FRotator Rotation = CameraComponent->GetRelativeRotation();
		Rotation.Pitch = FMath::Clamp(Rotation.Pitch + Value, -89.0f, 89.0f);
		CameraComponent->SetRelativeRotation(Rotation);
	}
}

void ACharacter::Jump() { if (CharacterMovement) CharacterMovement->RequestJump(); }
void ACharacter::StopJumping() { if (CharacterMovement) CharacterMovement->StopJumping(); }
