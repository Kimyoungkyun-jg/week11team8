#include "EnginePCH.h"
#include "Animation/TestAnimInstance.h"
#include "Asset/AssetManager.h"
#include "Asset/BlendSpace2D.h"
#include "Asset/AnimSequence.h"
#include "Components/CharacterMovementComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/TestCharacter.h"
#include "Render/SkeletalMesh.h"

#include <algorithm>
#include <cmath>

namespace
{
	bool NearSequenceEnd(const FAnimNode_SequencePlayer* Player, float BlendTime)
	{
		if (!Player || !Player->Sequence || !Player->Sequence->GetDataModel()) return false;
		const float TriggerTime = std::min(BlendTime, Player->Sequence->GetDataModel()->PlayLength * 0.5f);
		return Player->IsFinished() || Player->GetRemainingTime() <= TriggerTime;
	}
}

void UTestAnimInstance::BuildGraph()
{
	if (ATestCharacter* Character = Cast<ATestCharacter>(TryGetPawnOwner()))
	{
		Character->OnLandingAnimationFinished();
		Character->OnAttackAnimationFinished();
	}
	LocomotionPlayer = nullptr;
	JumpPlayer = FallLoopPlayer = LandPlayer = AttackPlayer = nullptr;
	StateMachine = nullptr;
	bJumpEventThisTick = bAttackEventThisTick = bAttackRequested = false;

	USkeletalMeshComponent* Component = GetOwningComponent();
	USkeletalMesh* Mesh = Component ? Component->GetSkeletalMesh() : nullptr;
	if (!Mesh || !Mesh->Skeleton) return;

	const FString BasePath = "Models/UnrealModel/";
	UBlendSpace2D* SimpleBlendSpace = Cast<UBlendSpace2D>(
		UAssetManager::LoadBlendSpace("Animations/simple.blendspace"));
	if (!SimpleBlendSpace || SimpleBlendSpace->Skeleton != Mesh->Skeleton) return;

	LocomotionPlayer = CreateNode<FAnimNode_BlendSpacePlayer>();
	LocomotionPlayer->BlendSpace = SimpleBlendSpace;
	StateMachine = CreateNode<FAnimNode_StateMachine>();
	const int32 Locomotion = StateMachine->AddState("Locomotion", LocomotionPlayer);
	const int32 Jump = AddSequenceState(*StateMachine, "Jump", BasePath + "AS_MM_Jump.fbxbin", false, JumpPlayer);
	const int32 FallLoop = AddSequenceState(*StateMachine, "FallLoop", BasePath + "AS_MM_Fall_Loop.fbxbin", true, FallLoopPlayer);
	const int32 Land = AddSequenceState(*StateMachine, "Land", BasePath + "AS_MM_Land.fbxbin", false, LandPlayer);
	const int32 Attack = AddSequenceState(*StateMachine, "Attack", BasePath + "AS_MM_Attack_01.fbxbin", false, AttackPlayer);
	if (Jump >= 0 && FallLoop >= 0 && Land >= 0)
	{
		StateMachine->AddTransition(Locomotion, Jump, [this] { return bJumpEventThisTick; }, 0.08f);
		StateMachine->AddTransition(Locomotion, FallLoop, [this] { return bFalling; }, 0.10f);
		StateMachine->AddTransition(Jump, Land, [this] { return bGrounded; }, 0.08f);
		StateMachine->AddTransition(Jump, FallLoop,
			[this] { return NearSequenceEnd(JumpPlayer, 0.12f); }, 0.12f);
		StateMachine->AddTransition(FallLoop, Land, [this] { return bGrounded; }, 0.08f);
		const auto FinishLanding = [this]
		{
			if (ATestCharacter* Character = Cast<ATestCharacter>(TryGetPawnOwner()))
				Character->OnLandingAnimationFinished();
		};
		StateMachine->AddTransition(Land, Locomotion,
			[this] { return bMovingOnGround; }, 0.05f, FinishLanding);
		StateMachine->AddTransition(Land, Locomotion,
			[this] { return NearSequenceEnd(LandPlayer, 0.12f); }, 0.12f, FinishLanding);
	}
	if (Attack >= 0)
	{
		StateMachine->AddTransition(Locomotion, Attack,
			[this] { return bAttackEventThisTick && bGrounded; }, 0.08f, {},
			[this]
			{
				if (ATestCharacter* Character = Cast<ATestCharacter>(TryGetPawnOwner()))
					Character->OnAttackAnimationStarted();
			});
		StateMachine->AddTransition(Attack, Locomotion,
			[this] { return NearSequenceEnd(AttackPlayer, 0.15f); }, 0.15f,
			[this]
			{
				if (ATestCharacter* Character = Cast<ATestCharacter>(TryGetPawnOwner()))
					Character->OnAttackAnimationFinished();
			});
	}
	ACharacter* Character = Cast<ACharacter>(TryGetPawnOwner());
	if (UCharacterMovementComponent* Movement = Character ? Character->GetCharacterMovement() : nullptr)
		LastJumpCounter = Movement->GetJumpCounter();
	RootNode = StateMachine;
}

void UTestAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
	if (!LocomotionPlayer) return;
	ACharacter* Character = Cast<ACharacter>(TryGetPawnOwner());
	UCharacterMovementComponent* Movement = Character ? Character->GetCharacterMovement() : nullptr;
	bJumpEventThisTick = false;
	bGrounded = false;
	bFalling = false;
	bMovingOnGround = false;
	if (!Movement)
	{
		LocomotionPlayer->InputX = 0.0f;
		LocomotionPlayer->InputY = 0.0f;
		bAttackEventThisTick = false;
		bAttackRequested = false;
		return;
	}
	const uint32 JumpCounter = Movement->GetJumpCounter();
	bJumpEventThisTick = JumpCounter != LastJumpCounter;
	LastJumpCounter = JumpCounter;
	bGrounded = Movement->IsMovingOnGround();
	const FVector& Velocity = Movement->Velocity;
	bFalling = !bGrounded && Velocity.Z < -0.01f;
	bMovingOnGround = bGrounded && FVector(Velocity.X, Velocity.Y, 0.0f).SizeSquared() > 0.09f;
	if ((bJumpEventThisTick || bFalling) && JumpPlayer && FallLoopPlayer && LandPlayer)
		if (ATestCharacter* TestCharacter = Cast<ATestCharacter>(Character))
			TestCharacter->LockJumpUntilLandingAnimation();
	bAttackEventThisTick = bAttackRequested && bGrounded;
	bAttackRequested = false;
	const FVector HorizontalVelocity(Velocity.X, Velocity.Y, 0.0f);
	const float Speed = HorizontalVelocity.Size() * 100.0f;
	const FQuat Facing = Character->GetActorQuat();
	const float ForwardSpeed = FVector::Dot(HorizontalVelocity, Facing.GetForwardVector());
	const float RightSpeed = FVector::Dot(HorizontalVelocity, Facing.GetRightVector());
	LocomotionPlayer->InputX = Speed > 0.01f ? FMath::RadiansToDegrees(std::atan2(RightSpeed, ForwardSpeed)) : 0.0f;
	LocomotionPlayer->InputY = std::clamp(Speed, 0.0f, 600.0f);
}
