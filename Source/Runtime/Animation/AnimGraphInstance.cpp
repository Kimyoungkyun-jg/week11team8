#include "EnginePCH.h"
#include "Animation/AnimGraphInstance.h"
#include "Asset/AnimSequence.h"
#include "Asset/AssetManager.h"
#include "Components/SkeletalMeshComponent.h"
#include "Render/SkeletalMesh.h"
#include "Asset/BlendSpace1D.h"
#include "Asset/BlendSpace2D.h"
#include "GameFramework/Character.h"
#include "Components/CharacterMovementComponent.h"
#include <algorithm>
#include <cmath>

bool UAnimGraphInstance::SetPreviewGraph(const FAnimGraphAsset& InGraph)
{
	Graph = InGraph;
	bUsePreviewGraph = true;
	InitializeAnimation();
	TArray<FMatrix> Pose;
	return RootNode && EvaluatePose(Pose);
}

int32 UAnimGraphInstance::GetActiveStateId() const
{
	const int32 Index = GraphMachine ? GraphMachine->CurrentState : -1;
	return Index >= 0 && static_cast<size_t>(Index) < StateIds.size() ? StateIds[Index] : 0;
}

int32 UAnimGraphInstance::GetTargetStateId() const
{
	const int32 Index = GraphMachine ? GraphMachine->NextState : -1;
	return Index >= 0 && static_cast<size_t>(Index) < StateIds.size() ? StateIds[Index] : 0;
}

float UAnimGraphInstance::InputValue(EAnimGraphInput Input, float Constant) const
{
	switch (Input)
	{
	case EAnimGraphInput::Speed: return Speed;
	case EAnimGraphInput::Direction: return Direction;
	case EAnimGraphInput::JumpRequested: return bJumpRequested ? 1.0f : 0.0f;
	case EAnimGraphInput::Falling: return bFalling ? 1.0f : 0.0f;
	case EAnimGraphInput::Grounded: return bGrounded ? 1.0f : 0.0f;
	case EAnimGraphInput::MovingOnGround: return bMovingOnGround ? 1.0f : 0.0f;
	default: return Constant;
	}
}

void UAnimGraphInstance::NativeUpdateAnimation(float DeltaSeconds)
{
	Speed = Direction = 0.0f;
	bJumpRequested |= bManualJumpRequested;
	bManualJumpRequested = false;
	bFalling = bGrounded = bMovingOnGround = false;
	ACharacter* Character = Cast<ACharacter>(TryGetPawnOwner());
	UCharacterMovementComponent* Movement = Character ? Character->GetCharacterMovement() : nullptr;
	if (Movement)
	{
		const uint32 JumpCounter = Movement->GetJumpCounter();
		bJumpRequested |= JumpCounter != LastJumpCounter;
		LastJumpCounter = JumpCounter;
		bGrounded = Movement->IsMovingOnGround();
		bFalling = !bGrounded && Movement->Velocity.Z < -0.01f;
		const FVector Velocity(Movement->Velocity.X, Movement->Velocity.Y, 0.0f);
		Speed = Velocity.Size() * 100.0f;
		bMovingOnGround = bGrounded && Velocity.SizeSquared() > 0.09f;
		if (Speed > 0.01f)
		{
			const FQuat Facing = Character->GetActorQuat();
			const float Forward = FVector::Dot(Velocity, Facing.GetForwardVector());
			const float Right = FVector::Dot(Velocity, Facing.GetRightVector());
			Direction = FMath::RadiansToDegrees(std::atan2(Right, Forward));
			if (Graph.bInvertDirection) Direction = -Direction;
		}
	}
	if (bPreviewMovementOverride)
	{
		bFalling = bPreviewFalling;
		bGrounded = !bPreviewFalling;
		bMovingOnGround = bGrounded && Speed > 30.0f;
	}
	for (const FInputBinding& Binding : Bindings)
	{
		Binding.Player->InputX = InputValue(Binding.X, Binding.ConstantX);
		Binding.Player->InputY = InputValue(Binding.Y, Binding.ConstantY);
	}
}

void UAnimGraphInstance::BuildGraph()
{
	Bindings.clear(); StateIds.clear(); SequencePlayers.clear(); GraphMachine = nullptr;
	bJumpRequested = bManualJumpRequested = false;
	USkeletalMeshComponent* Component = GetOwningComponent();
	USkeletalMesh* Mesh = Component ? Component->GetSkeletalMesh() : nullptr;
	if (!Component || !Mesh || !Mesh->Skeleton || (!bUsePreviewGraph && Component->GetAnimGraphPath().empty())) return;
	if (!bUsePreviewGraph)
	{
		FString Error;
		if (!FAnimGraphAsset::Load(Component->GetAnimGraphPath(), Graph, Error))
		{ HTR_LOG(Warning, "AnimGraph load failed: {}", Error); return; }
	}
	if (!Graph.SkeletonPath.empty() && Graph.SkeletonPath != Mesh->Skeleton->GetPath())
	{ HTR_LOG(Warning, "AnimGraph skeleton does not match mesh: {}", Component->GetAnimGraphPath()); return; }
	if (!Graph.bRootPoseConnected)
	{ HTR_LOG(Warning, "AnimGraph root Output Pose is disconnected: {}", Component->GetAnimGraphPath()); return; }
	if (ACharacter* Character = Cast<ACharacter>(TryGetPawnOwner()))
		if (UCharacterMovementComponent* Movement = Character->GetCharacterMovement())
			LastJumpCounter = Movement->GetJumpCounter();
	GraphMachine = CreateNode<FAnimNode_StateMachine>();
	std::vector<const FAnimGraphState*> Ordered;
	for (const auto& State : Graph.States) if (State.Id == Graph.EntryState) Ordered.push_back(&State);
	for (const auto& State : Graph.States) if (State.Id != Graph.EntryState) Ordered.push_back(&State);
	for (const FAnimGraphState* State : Ordered)
	{
		if (!State->bPoseConnected)
		{ HTR_LOG(Warning, "AnimGraph state has no Output Pose connection: {}", State->Name); GraphMachine = nullptr; return; }
		FAnimGraphPlayerNode PlayerData;
		if (State->OutputPlayerId == 0)
		{
			PlayerData.Pose = State->Pose; PlayerData.AssetPath = State->AssetPath;
			PlayerData.InputX = State->InputX; PlayerData.InputY = State->InputY;
			PlayerData.ConstantX = State->ConstantX; PlayerData.ConstantY = State->ConstantY;
			PlayerData.PlayRate = State->PlayRate; PlayerData.bLoop = State->bLoop;
		}
		else
		{
			const auto It = std::find_if(State->Players.begin(), State->Players.end(),
				[&](const auto& Player) { return Player.Id == State->OutputPlayerId; });
			if (It == State->Players.end()) { GraphMachine = nullptr; return; }
			PlayerData = *It;
		}
		FAnimNode_Base* Pose = nullptr;
		if (PlayerData.Pose == EAnimGraphPose::Sequence)
		{
			UAnimSequence* Sequence = UAssetManager::LoadAnimSequence(PlayerData.AssetPath);
			if (!Sequence) HTR_LOG(Warning, "AnimGraph sequence failed to load: {}", PlayerData.AssetPath);
			else if (Sequence->Skeleton != Mesh->Skeleton)
				HTR_LOG(Warning, "AnimGraph skeleton mismatch: sequence {} uses {}, mesh uses {}",
					PlayerData.AssetPath, Sequence->Skeleton ? Sequence->Skeleton->GetPath() : "None",
					Mesh->Skeleton->GetPath());
			if (Sequence && Sequence->Skeleton == Mesh->Skeleton && Sequence->GetDataModel())
			{
				auto* Player = CreateNode<FAnimNode_SequencePlayer>();
				Player->Sequence = Sequence; Player->bLoop = PlayerData.bLoop; Player->PlayRate = PlayerData.PlayRate;
				Pose = Player;
				SequencePlayers.push_back({ State->Id, Player });
			}
		}
		else
		{
			UBlendSpace* BlendSpace = UAssetManager::LoadBlendSpace(PlayerData.AssetPath);
			const bool CorrectType = PlayerData.Pose == EAnimGraphPose::BlendSpace1D
				? BlendSpace && BlendSpace->IsA<UBlendSpace1D>() : BlendSpace && BlendSpace->IsA<UBlendSpace2D>();
			if (CorrectType && BlendSpace->Skeleton == Mesh->Skeleton)
			{
				auto* Player = CreateNode<FAnimNode_BlendSpacePlayer>();
				Player->BlendSpace = BlendSpace; Player->bLoop = PlayerData.bLoop; Player->PlayRate = PlayerData.PlayRate;
				Bindings.push_back({ Player, PlayerData.InputX, PlayerData.InputY, PlayerData.ConstantX, PlayerData.ConstantY });
				Pose = Player;
			}
		}
		if (!Pose)
		{ HTR_LOG(Warning, "AnimGraph state asset is missing or incompatible: {}", PlayerData.AssetPath); GraphMachine = nullptr; return; }
		GraphMachine->AddState(State->Name, Pose);
		StateIds.push_back(State->Id);
	}
	for (const auto& Transition : Graph.Transitions)
	{
		auto From = std::find(StateIds.begin(), StateIds.end(), Transition.From);
		auto To = std::find(StateIds.begin(), StateIds.end(), Transition.To);
		if (From == StateIds.end() || To == StateIds.end()) continue;
		FAnimNode_SequencePlayer* FromPlayer = nullptr;
		for (const auto& Entry : SequencePlayers) if (Entry.first == Transition.From) { FromPlayer = Entry.second; break; }
		GraphMachine->AddTransition(static_cast<int32>(From - StateIds.begin()), static_cast<int32>(To - StateIds.begin()),
			[this, Transition, FromPlayer]
			{
			if (Transition.Input == EAnimGraphInput::SequenceEnding)
			{
				if (!FromPlayer || !FromPlayer->Sequence || !FromPlayer->Sequence->GetDataModel()) return false;
				const float TriggerTime = std::min(Transition.Threshold,
					FromPlayer->Sequence->GetDataModel()->PlayLength * 0.5f);
				return FromPlayer->IsFinished() || FromPlayer->GetRemainingTime() <= TriggerTime;
			}
			const float Value = InputValue(Transition.Input);
			switch (Transition.Compare)
			{
			case EAnimGraphCompare::Always: return true;
			case EAnimGraphCompare::Greater: return Value > Transition.Threshold;
			case EAnimGraphCompare::GreaterEqual: return Value >= Transition.Threshold;
			case EAnimGraphCompare::Less: return Value < Transition.Threshold;
			case EAnimGraphCompare::LessEqual: return Value <= Transition.Threshold;
			}
			return false;
			}, Transition.BlendTime, {}, Transition.Input == EAnimGraphInput::JumpRequested
				? std::function<void()>([this] { bJumpRequested = false; }) : std::function<void()>{});
	}
	RootNode = GraphMachine;
}

void UAnimGraphInstance::NativeInitializeAnimation()
{
	BuildGraph();
}

int32 UAnimGraphInstance::AddSequenceState(FAnimNode_StateMachine& Machine, const FString& Name,
	const FString& AssetPath, bool bLoop, FAnimNode_SequencePlayer*& OutPlayer)
{
	OutPlayer = nullptr;
	USkeletalMeshComponent* Component = GetOwningComponent();
	USkeletalMesh* Mesh = Component ? Component->GetSkeletalMesh() : nullptr;
	if (!Mesh || !Mesh->Skeleton) return -1;
	UAnimSequence* Sequence = UAssetManager::LoadAnimSequence(AssetPath);
	if (!Sequence || Sequence->Skeleton != Mesh->Skeleton || !Sequence->GetDataModel()) return -1;
	OutPlayer = CreateNode<FAnimNode_SequencePlayer>();
	OutPlayer->Sequence = Sequence;
	OutPlayer->bLoop = bLoop;
	return Machine.AddState(Name, OutPlayer);
}
