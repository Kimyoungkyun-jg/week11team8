#include "EnginePCH.h"
#include "GameFramework/Actor/ZombieActor.h"
#include "Animation/AnimGraphInstance.h"
#include "Asset/AssetManager.h"
#include "Components/CharacterMovementComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/TestCharacter.h"
#include "Render/SkeletalMesh.h"
#include "Engine/Level.h"
#include "Engine/World.h"
#include <cmath>

AZombieActor::AZombieActor()
{
	PrimaryActorTick.bCanEverTick = true;
	GetCharacterMovement()->MaxWalkSpeed = 4.0f; // 이동은 m/s, AnimGraph Speed 입력은 cm/s(400)이다.
	USkeletalMeshComponent* MeshComponent = GetSkeletalMeshComponent();
	RightHandHitCapsule = CreateDefaultSubobject<UCapsuleComponent>("RightHandHitCapsule");
	RightHandHitCapsule->SetupAttachment(MeshComponent);
	RightHandHitCapsule->CapsuleRadius = 0.12f;
	RightHandHitCapsule->CapsuleHalfHeight = 0.20f;
	RightHandHitCapsule->bBlockComponent = false;
	RightHandHitCapsule->bGenerateOverlapEvents = false;
	USkeletalMesh* Mesh = UAssetManager::LoadFbxSkeletalMesh(
		"Assets/Models/Zombie/SK_Zombie Idle_mixamorig_Hips.fbxbin");
	if (Mesh)
	{
		MeshComponent->SetSkeletalMesh(Mesh);
		if (!MeshComponent->SetAnimGraphPath("Assets/Animations/zombieanimgraph.animgraph"))
			HTR_LOG(Error, "Failed to assign zombieanimgraph.animgraph to AZombieActor");
	}
	else HTR_LOG(Error, "Failed to load zombie idle mesh for AZombieActor");
}

void AZombieActor::BeginPlay()
{
	Super::BeginPlay();
	USkeletalMesh* Mesh = GetSkeletalMeshComponent()->GetSkeletalMesh();
	if (Mesh) RightHandBoneIndex = Mesh->GetRefSkeleton().FindBoneIndex("mixamorig:RightHand");
	RightHandHitCapsule->OnComponentBeginOverlap.AddDynamic(this, &AZombieActor::OnRightHandOverlap);
}

void AZombieActor::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	UWorld* World = GetWorld();
	if (!World || !World->IsGameWorld()) return;
	USkeletalMeshComponent* MeshComponent = GetSkeletalMeshComponent();
	const TArray<FMatrix>& Bones = MeshComponent->GetGlobalBoneTransforms();
	UAnimGraphInstance* Anim = Cast<UAnimGraphInstance>(MeshComponent->GetAnimInstance());
	RightHandHitCapsule->bGenerateOverlapEvents = Anim && Anim->IsStateActiveOrPending("Attack") &&
		Bones.IsValidIndex(RightHandBoneIndex);
	if (Bones.IsValidIndex(RightHandBoneIndex))
	{
		const FMatrix HandOffset = FTransform(FRotator(0.0f, 0.0f, 0.0f), FVector(0.0f, 0.0f, 0.10f)).GetLocalMatrix();
		RightHandHitCapsule->SetWorldTransform(HandOffset * Bones[RightHandBoneIndex] * MeshComponent->GetWorldMatrix(), false);
	}
	AttackTimer = FMath::Max(0.0f, AttackTimer - DeltaTime);
	Target = nullptr;
	float ClosestSquared = DetectionRadius * DetectionRadius;
	for (AActor* Actor : World->GetPersistentLevel()->GetActors())
	{
		ACharacter* Candidate = Cast<ACharacter>(Actor);
		if (!IsValid(Candidate) || Candidate == this || Cast<AZombieActor>(Candidate) || Candidate->IsActorBeingDestroyed()) continue;
		const FVector Difference = Candidate->GetActorLocation() - GetActorLocation();
		const float DistanceSquared = Difference.SizeSquared();
		if (DistanceSquared < ClosestSquared)
		{
			ClosestSquared = DistanceSquared;
			Target = Candidate;
		}
	}

	UCharacterMovementComponent* Movement = GetCharacterMovement();
	if (!Target || (Anim && (Anim->IsStateActiveOrPending("Attack") || Anim->IsStateActiveOrPending("Hit"))))
	{
		if (Movement->IsMovingOnGround()) Movement->StopMovementImmediately();
		return;
	}
	FVector Direction = Target->GetActorLocation() - GetActorLocation();
	Direction.Z = 0.0f;
	if (Direction.SizeSquared() <= 0.0001f) return;
	const float Yaw = FMath::RadiansToDegrees(std::atan2(Direction.Y, Direction.X));
	GetRootComponent()->SetRelativeRotation(FRotator(0.0f, Yaw, 0.0f));
	if (Direction.SizeSquared() <= AttackRadius * AttackRadius)
	{
		if (Movement->IsMovingOnGround()) Movement->StopMovementImmediately();
		if (Anim && AttackTimer <= 0.0f)
		{
			Anim->RequestAttack();
			AttackTimer = AttackCooldown;
		}
	}
	else AddMovementInput(Direction, 1.0f);
}

void AZombieActor::OnRightHandOverlap(UPrimitiveComponent* Self, AActor* OtherActor, UPrimitiveComponent* OtherComponent)
{
	if (Self != RightHandHitCapsule || !RightHandHitCapsule->bGenerateOverlapEvents) return;
	ATestCharacter* Character = Cast<ATestCharacter>(OtherActor);
	if (!Character) return;
	if (UAnimGraphInstance* Anim = Cast<UAnimGraphInstance>(Character->GetSkeletalMeshComponent()->GetAnimInstance()))
		Anim->RequestHit();
}

void AZombieActor::ReactToHit()
{
	if (UAnimGraphInstance* Anim = Cast<UAnimGraphInstance>(GetSkeletalMeshComponent()->GetAnimInstance()))
		Anim->RequestHit();
}
