#include "EnginePCH.h"
#include "Animation/AnimNodeBase.h"
#include "Asset/AnimSequence.h"
#include "Asset/BlendSpace.h"
#include "Math/EngineMath.h"
#include "Animation/BoneTransform.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace
{
	bool BlendRootMotion(FAnimNode_Base* A, FAnimNode_Base* B, float Alpha, FMatrix& OutDelta)
	{
		FMatrix DeltaA, DeltaB;
		const bool HasA = A && A->ConsumeRootMotion(DeltaA);
		const bool HasB = B && B->ConsumeRootMotion(DeltaB);
		if (!HasA && !HasB) return false;
		const FBoneTransform TransformA = HasA ? FBoneTransform::FromMatrix(DeltaA) : FBoneTransform::Identity;
		const FBoneTransform TransformB = HasB ? FBoneTransform::FromMatrix(DeltaB) : FBoneTransform::Identity;
		OutDelta = FBoneTransform::Blend(TransformA, TransformB, std::clamp(Alpha, 0.0f, 1.0f)).ToMatrix();
		return true;
	}
}

void FAnimNode_SequencePlayer::Reset()
{
	CurrentTime = 0.0f;
	bPlaying = Sequence != nullptr;
	bHasPendingRootMotion = false;
}

bool FAnimNode_SequencePlayer::IsFinished() const
{
	return Sequence && Sequence->GetDataModel() && !bLoop && !bPlaying &&
		CurrentTime >= Sequence->GetDataModel()->PlayLength;
}

float FAnimNode_SequencePlayer::GetRemainingTime() const
{
	if (!Sequence || !Sequence->GetDataModel() || !std::isfinite(PlayRate) || PlayRate <= 0.0f)
		return std::numeric_limits<float>::infinity();
	return std::max(0.0f, Sequence->GetDataModel()->PlayLength - CurrentTime) / PlayRate;
}

void FAnimNode_SequencePlayer::Update(float DeltaSeconds)
{
	if (!bPlaying || !Sequence || !Sequence->GetDataModel() ||
		!std::isfinite(PlayRate) || PlayRate < 0.0f ||
		!std::isfinite(DeltaSeconds) || DeltaSeconds <= 0.0f) return;
	const float Length = Sequence->GetDataModel()->PlayLength;
	if (Length <= 0.0f || !std::isfinite(Length)) return;
	const float NextTime = CurrentTime + DeltaSeconds * PlayRate;
	if (!std::isfinite(NextTime)) return;
	const float PreviousTime = CurrentTime;
	int32 LoopCount = 0;
	if (NextTime < Length) CurrentTime = NextTime;
	else if (bLoop)
	{
		if (NextTime / Length > 256.0f) return;
		LoopCount = static_cast<int32>(NextTime / Length);
		CurrentTime = std::fmod(NextTime, Length);
	}
	else
	{
		CurrentTime = Length;
		bPlaying = false;
	}
	bHasPendingRootMotion = Sequence->ExtractRootMotion(PreviousTime, CurrentTime, LoopCount, PendingRootMotion);
}

bool FAnimNode_SequencePlayer::Evaluate(const USkeleton* Skeleton, TArray<FAnimBoneTransform>& OutPose)
{
	if (!Sequence || !Skeleton || Sequence->Skeleton != Skeleton) return false;
	return Sequence->EvaluateLocalPoseTransforms(CurrentTime, OutPose);
}

bool FAnimNode_SequencePlayer::ConsumeRootMotion(FMatrix& OutDelta)
{
	if (!bHasPendingRootMotion) return false;
	OutDelta = PendingRootMotion;
	bHasPendingRootMotion = false;
	return true;
}

void FAnimNode_TwoWayBlend::Reset()
{
	if (A) A->Reset();
	if (B) B->Reset();
}

void FAnimNode_TwoWayBlend::Update(float DeltaSeconds)
{
	const float WeightB = std::clamp(Alpha, 0.0f, 1.0f);
	if (WeightB < 1.0f && A) A->Update(DeltaSeconds);
	if (WeightB > 0.0f && B) B->Update(DeltaSeconds);
}

bool FAnimNode_TwoWayBlend::Evaluate(const USkeleton* Skeleton, TArray<FAnimBoneTransform>& OutPose)
{
	if (Alpha <= 0.0f) return A && A->Evaluate(Skeleton, OutPose);
	if (Alpha >= 1.0f) return B && B->Evaluate(Skeleton, OutPose);
	TArray<FAnimBoneTransform> PoseA, PoseB;
	return A && B && A->Evaluate(Skeleton, PoseA) && B->Evaluate(Skeleton, PoseB) &&
		BlendAnimPoses(PoseA, PoseB, Alpha, OutPose);
}

bool FAnimNode_TwoWayBlend::ConsumeRootMotion(FMatrix& OutDelta)
{
	return BlendRootMotion(A, B, Alpha, OutDelta);
}

void FAnimNode_BlendSpacePlayer::Reset()
{
	NormalizedTime = 0.0f;
	bPlaying = BlendSpace != nullptr;
	bHasPendingRootMotion = false;
}

void FAnimNode_BlendSpacePlayer::Update(float DeltaSeconds)
{
	if (!bPlaying || !BlendSpace || !std::isfinite(DeltaSeconds) || DeltaSeconds <= 0.0f ||
		!std::isfinite(PlayRate) || PlayRate < 0.0f) return;
	const float Length = BlendSpace->GetEffectiveLength(InputX, InputY);
	if (Length <= 0.0f || !std::isfinite(Length)) return;
	const float NextTime = NormalizedTime + DeltaSeconds * PlayRate / Length;
	if (!std::isfinite(NextTime)) return;
	const float PreviousTime = NormalizedTime;
	int32 LoopCount = 0;
	if (NextTime < 1.0f) NormalizedTime = NextTime;
	else if (bLoop)
	{
		if (NextTime > 256.0f) return;
		LoopCount = static_cast<int32>(NextTime);
		NormalizedTime = std::fmod(NextTime, 1.0f);
	}
	else
	{
		NormalizedTime = 1.0f;
		bPlaying = false;
	}
	bHasPendingRootMotion = BlendSpace->ExtractRootMotion(InputX, InputY, PreviousTime,
		NormalizedTime, LoopCount, PendingRootMotion);
}

bool FAnimNode_BlendSpacePlayer::Evaluate(const USkeleton* Skeleton, TArray<FAnimBoneTransform>& OutPose)
{
	return BlendSpace && Skeleton && BlendSpace->Skeleton == Skeleton &&
		BlendSpace->Evaluate(InputX, InputY, NormalizedTime, OutPose);
}

bool FAnimNode_BlendSpacePlayer::ConsumeRootMotion(FMatrix& OutDelta)
{
	if (!bHasPendingRootMotion) return false;
	OutDelta = PendingRootMotion;
	bHasPendingRootMotion = false;
	return true;
}

int32 FAnimNode_StateMachine::AddState(const FString& Name, FAnimNode_Base* PoseNode)
{
	if (!PoseNode) return -1;
	const int32 Index = States.Num();
	States.Add({ Name, PoseNode });
	if (CurrentState < 0) CurrentState = Index;
	return Index;
}

bool FAnimNode_StateMachine::AddTransition(int32 FromState, int32 ToState,
	std::function<bool()> Condition, float BlendTime, std::function<void()> OnCompleted,
	std::function<void()> OnStarted)
{
	if (!States.IsValidIndex(FromState) || !States.IsValidIndex(ToState) ||
		FromState == ToState || !Condition || !std::isfinite(BlendTime) || BlendTime < 0.0f) return false;
	Transitions.Add({ FromState, ToState, std::move(Condition), BlendTime,
		std::move(OnCompleted), std::move(OnStarted) });
	return true;
}

void FAnimNode_StateMachine::FinishTransition()
{
	CurrentState = NextState;
	NextState = -1;
	std::function<void()> Completed = std::move(TransitionOnCompleted);
	TransitionOnCompleted = {};
	if (Completed) Completed();
}

void FAnimNode_StateMachine::Reset()
{
	CurrentState = States.IsEmpty() ? -1 : 0;
	NextState = -1;
	TransitionElapsed = 0.0f;
	TransitionDuration = 0.0f;
	TransitionOnCompleted = {};
	for (const FState& State : States) if (State.Pose) State.Pose->Reset();
}

void FAnimNode_StateMachine::Update(float DeltaSeconds)
{
	if (!States.IsValidIndex(CurrentState) || !std::isfinite(DeltaSeconds)) return;
	DeltaSeconds = std::max(0.0f, DeltaSeconds);
	if (States[CurrentState].Pose) States[CurrentState].Pose->Update(DeltaSeconds);
	if (NextState >= 0)
	{
		if (States[NextState].Pose) States[NextState].Pose->Update(DeltaSeconds);
		TransitionElapsed += DeltaSeconds;
		if (TransitionElapsed >= TransitionDuration)
			FinishTransition();
		return;
	}
	for (const FTransition& Transition : Transitions)
	{
		if (Transition.FromState != CurrentState || !Transition.Condition()) continue;
		NextState = Transition.ToState;
		TransitionDuration = Transition.BlendTime;
		TransitionElapsed = DeltaSeconds;
		TransitionOnCompleted = Transition.OnCompleted;
		if (Transition.OnStarted) Transition.OnStarted();
		if (States[NextState].Pose)
		{
			States[NextState].Pose->Reset();
			States[NextState].Pose->Update(DeltaSeconds);
		}
		if (TransitionElapsed >= TransitionDuration)
			FinishTransition();
		break;
	}
}

bool FAnimNode_StateMachine::Evaluate(const USkeleton* Skeleton, TArray<FAnimBoneTransform>& OutPose)
{
	if (!States.IsValidIndex(CurrentState)) return false;
	if (NextState < 0) return States[CurrentState].Pose &&
		States[CurrentState].Pose->Evaluate(Skeleton, OutPose);
	TArray<FAnimBoneTransform> From, To;
	return States[CurrentState].Pose && States[NextState].Pose &&
		States[CurrentState].Pose->Evaluate(Skeleton, From) && States[NextState].Pose->Evaluate(Skeleton, To) &&
		BlendAnimPoses(From, To, TransitionElapsed / TransitionDuration, OutPose);
}

bool FAnimNode_StateMachine::ConsumeRootMotion(FMatrix& OutDelta)
{
	if (!States.IsValidIndex(CurrentState)) return false;
	FAnimNode_Base* Current = States[CurrentState].Pose;
	if (NextState < 0) return Current && Current->ConsumeRootMotion(OutDelta);
	return BlendRootMotion(Current, States[NextState].Pose,
		TransitionDuration > 0.0f ? TransitionElapsed / TransitionDuration : 1.0f, OutDelta);
}
