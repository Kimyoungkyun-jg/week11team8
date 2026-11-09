#include "EnginePCH.h"
#include "Animation/TestAnimInstance.h"
#include "Asset/AssetManager.h"
#include "Asset/SimpleBlendSpace.h"
#include "Asset/AnimSequence.h"
#include "Components/CharacterMovementComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/TestCharacter.h"
#include "Render/SkeletalMesh.h"

#include <algorithm>

namespace
{
	bool NearSequenceEnd(const FAnimNode_SequencePlayer* Player, float BlendTime)
	{
		if (!Player || !Player->Sequence || !Player->Sequence->GetDataModel()) return false;
		const float TriggerTime = std::min(BlendTime, Player->Sequence->GetDataModel()->PlayLength * 0.5f);
		return Player->IsFinished() || Player->GetRemainingTime() <= TriggerTime;
	}
}

UTestAnimInstance::~UTestAnimInstance()
{
	delete SimpleBlendSpace;
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
	delete SimpleBlendSpace;
	SimpleBlendSpace = nullptr;

	USkeletalMeshComponent* Component = GetOwningComponent();
	USkeletalMesh* Mesh = Component ? Component->GetSkeletalMesh() : nullptr;
	if (!Mesh || !Mesh->Skeleton) return;

	const FString BasePath = "Assets/Models/UnrealModel/";
	const auto LoadSequence = [&](const char* FileName) -> UAnimSequence*
	{
		const TArray<UAnimSequence*>* Sequences = UAssetManager::LoadFbxAnimSequences(
			BasePath + FileName, *Mesh->Skeleton);
		return Sequences && !Sequences->IsEmpty() ? (*Sequences)[0] : nullptr;
	};
	UAnimSequence* Idle = LoadSequence("Idle.FBX");
	UAnimSequence* Walk = LoadSequence("Walk.FBX");
	UAnimSequence* Jog = LoadSequence("Jog.FBX");
	if (!Idle || !Walk || !Jog) return;

	SimpleBlendSpace = NewObject<USimpleBlendSpace>(this);
	if (!SimpleBlendSpace || !SimpleBlendSpace->Configure(Idle, Walk, Jog))
	{
		delete SimpleBlendSpace;
		SimpleBlendSpace = nullptr;
		return;
	}
	for (UAnimSequence* Sequence : { Idle, Walk, Jog })
	{
		Sequence->bEnableRootMotion = false;
		Sequence->bForceRootLock = true;
	}

	LocomotionPlayer = CreateNode<FAnimNode_BlendSpacePlayer>();
	LocomotionPlayer->BlendSpace = SimpleBlendSpace;
	StateMachine = CreateNode<FAnimNode_StateMachine>();
	const int32 Locomotion = StateMachine->AddState("Locomotion", LocomotionPlayer);
	const auto AddClip = [&](const char* FileName, const char* StateName, bool bLoop,
		FAnimNode_SequencePlayer*& OutPlayer)
	{
		UAnimSequence* Sequence = LoadSequence(FileName);
		if (!Sequence || Sequence->Skeleton != Mesh->Skeleton || !Sequence->GetDataModel()) return -1;
		Sequence->bEnableRootMotion = false;
		Sequence->bForceRootLock = true;
		OutPlayer = CreateNode<FAnimNode_SequencePlayer>();
		OutPlayer->Sequence = Sequence;
		OutPlayer->bLoop = bLoop;
		return StateMachine->AddState(StateName, OutPlayer);
	};
	const int32 Jump = AddClip("MM_Jump.FBX", "Jump", false, JumpPlayer);
	const int32 FallLoop = AddClip("MM_Fall_Loop.FBX", "FallLoop", true, FallLoopPlayer);
	const int32 Land = AddClip("MM_Land.FBX", "Land", false, LandPlayer);
	const int32 Attack = AddClip("MM_Attack_01.FBX", "Attack", false, AttackPlayer);
	if (AttackPlayer)
	{
		AttackPlayer->Sequence->bEnableRootMotion = true;
		AttackPlayer->Sequence->bForceRootLock = true;
	}
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
	const float Speed = FVector(Velocity.X, Velocity.Y, 0.0f).Size() * 100.0f;
	LocomotionPlayer->InputX = std::clamp(Speed, 0.0f, 600.0f);
}
