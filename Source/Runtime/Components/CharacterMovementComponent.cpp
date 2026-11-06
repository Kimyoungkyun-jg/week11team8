#include "EnginePCH.h"
#include "Components/CharacterMovementComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Collision/Ray.h"
#include <algorithm>
#include <cmath>

UCharacterMovementComponent::UCharacterMovementComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = ETickingGroup::DuringPhysics;
}

bool UCharacterMovementComponent::FindFloor(float Distance, FHitResult& OutHit) const
{
	auto* Capsule = Cast<UCapsuleComponent>(UpdatedComponent);
	if (!Capsule || !GetWorld()) return false;
	const float Radius = Capsule->GetScaledCapsuleRadius();
	const float Offset = Capsule->GetScaledCapsuleHalfHeight() - Radius;
	const FVector Start = Capsule->GetWorldLocation() - FVector(0, 0, Offset);
	if (!GetWorld()->SweepSingle(FRay(Start, FVector(0, 0, -1)), Distance, Radius,
		OutHit, GetOwner(), true)) return false;
	const float Radians = FMath::Clamp(WalkableFloorAngle, 0.0f, 89.0f) * PI / 180.0f;
	return OutHit.Normal.Z >= std::cos(Radians);
}

bool UCharacterMovementComponent::SweepCapsule(const FVector& Delta, FHitResult& OutHit) const
{
	auto* Capsule = Cast<UCapsuleComponent>(UpdatedComponent);
	if (!Capsule || !GetWorld() || Delta.SizeSquared() <= SMALL_NUMBER) return false;
	const float Radius = Capsule->GetScaledCapsuleRadius();
	const float Offset = Capsule->GetScaledCapsuleHalfHeight() - Radius;
	const float Distance = Delta.Size();
	bool bHit = false;
	for (float Height : { -Offset, 0.0f, Offset })
	{
		FHitResult Candidate;
		const FVector Start = Capsule->GetWorldLocation() + FVector(0, 0, Height);
		if (GetWorld()->SweepSingle(FRay(Start, Delta / Distance), Distance, Radius,
			Candidate, GetOwner(), true) && (!bHit || Candidate.Time < OutHit.Time))
		{
			OutHit = Candidate;
			bHit = true;
		}
	}
	return bHit;
}

void UCharacterMovementComponent::TickComponent(float DeltaTime)
{
	Super::TickComponent(DeltaTime);
	auto* Pawn = Cast<APawn>(GetOwner());
	if (!Pawn || !UpdatedComponent || !GetWorld() || !std::isfinite(DeltaTime) || DeltaTime <= 0.0f) return;
	FVector Input = Pawn->ConsumeMovementInputVector();
	Input.Z = 0.0f;
	Input = Input.GetClampedToMaxSize(1.0f);
	float Remaining = std::min(DeltaTime, 0.25f);
	while (Remaining > 0.00001f)
	{
		const float Step = std::min(Remaining, 1.0f / 60.0f);
		Remaining -= Step;
		FHitResult Floor;
		if (Velocity.Z <= 0.0f && FindFloor(0.12f, Floor))
		{
			MovementMode = ECharacterMovementMode::Walking;
			Velocity.Z = 0.0f;
		}
		else MovementMode = ECharacterMovementMode::Falling;
		if (bJumpRequested && IsMovingOnGround())
		{
			Velocity.Z = JumpZVelocity;
			MovementMode = ECharacterMovementMode::Falling;
		}
		bJumpRequested = false;
		const FVector Horizontal(Velocity.X, Velocity.Y, 0.0f);
		const FVector Target = Input * MaxWalkSpeed;
		const FVector Difference = Target - Horizontal;
		const float Acceleration = Input.SizeSquared() > SMALL_NUMBER ? MaxAcceleration *
			(IsMovingOnGround() ? 1.0f : AirControl) : BrakingDecelerationWalking;
		const float MaxChange = Acceleration * Step;
		const FVector NewHorizontal = Horizontal + Difference.GetClampedToMaxSize(MaxChange);
		Velocity.X = NewHorizontal.X;
		Velocity.Y = NewHorizontal.Y;
		if (!IsMovingOnGround()) Velocity.Z -= Gravity * GravityScale * Step;
		const FVector Delta = Velocity * Step;
		FHitResult Hit;
		const bool bHit = SweepCapsule(Delta, Hit);
		const FVector Move = bHit ? Delta * std::max(0.0f, Hit.Time - 0.001f) : Delta;
		UpdatedComponent->MoveComponent(Move, UpdatedComponent->GetWorldRotation(), false, nullptr);
		if (bHit)
		{
			const FVector Normal = Hit.Normal.GetSafeNormal();
			const float Inward = Velocity.Dot(Normal);
			if (Inward < 0.0f) Velocity -= Normal * Inward;
			if (Normal.Z > 0.7f && Delta.Z < 0.0f) MovementMode = ECharacterMovementMode::Walking;
		}
	}
	UpdateComponentVelocity();
}

void UCharacterMovementComponent::StopMovementImmediately()
{
	Super::StopMovementImmediately();
	bJumpRequested = false;
	if (auto* Pawn = Cast<APawn>(GetOwner())) Pawn->ConsumeMovementInputVector();
}
