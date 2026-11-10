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
	if (!Sequence || !Sequence->GetDataModel() || !std::isfinite(PlayRate) || PlayRate <= 0.0f ||
		!std::isfinite(Sequence->RateScale) || Sequence->RateScale <= 0.0f)
		return std::numeric_limits<float>::infinity();
	return std::max(0.0f, Sequence->GetDataModel()->PlayLength - CurrentTime) / (PlayRate * Sequence->RateScale);
}

void FAnimNode_SequencePlayer::Update(float DeltaSeconds)
{
	if (!bPlaying || !Sequence || !Sequence->GetDataModel() ||
		!std::isfinite(PlayRate) || PlayRate < 0.0f ||
		!std::isfinite(Sequence->RateScale) || Sequence->RateScale <= 0.0f ||
		!std::isfinite(DeltaSeconds) || DeltaSeconds <= 0.0f) return;
	const float Length = Sequence->GetDataModel()->PlayLength;
	if (Length <= 0.0f || !std::isfinite(Length)) return;
	const float NextTime = CurrentTime + DeltaSeconds * PlayRate * Sequence->RateScale;
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

float FAnimNode_SequencePlayer::GetCycleSeconds() const
{
	if (!Sequence || !Sequence->GetDataModel() || !std::isfinite(PlayRate) || PlayRate <= 0.0f ||
		!std::isfinite(Sequence->RateScale) || Sequence->RateScale <= 0.0f) return 0.0f;
	const float Length = Sequence->GetDataModel()->PlayLength;
	return Length > 0.0f && std::isfinite(Length) ? Length / (PlayRate * Sequence->RateScale) : 0.0f;
}

bool FAnimNode_SequencePlayer::GetNormalizedTime(float& OutTime) const
{
	if (!Sequence || !Sequence->GetDataModel() || Sequence->GetDataModel()->PlayLength <= 0.0f) return false;
	OutTime = std::clamp(CurrentTime / Sequence->GetDataModel()->PlayLength, 0.0f, 1.0f);
	return true;
}

void FAnimNode_SequencePlayer::SetNormalizedTime(float Time)
{
	if (!Sequence || !Sequence->GetDataModel() || !std::isfinite(Time)) return;
	CurrentTime = std::clamp(Time, 0.0f, 1.0f) * Sequence->GetDataModel()->PlayLength;
}

void FAnimNode_TwoWayBlend::Reset()
{
	if (A) A->Reset();
	if (B) B->Reset();
}

void FAnimNode_TwoWayBlend::Update(float DeltaSeconds)
{
	// 가중치가 0인 자식도 시간을 진행한다. 멈춰 두면 다시 섞일 때 멈췄던 위치에서 이어져 동작이 튄다.
	const float WeightB = std::clamp(Alpha, 0.0f, 1.0f);
	FAnimNode_Base* Leader = WeightB >= 0.5f ? B : A;
	FAnimNode_Base* Follower = Leader == A ? B : A;
	if (!Leader) std::swap(Leader, Follower);
	if (!Leader) return;
	Leader->Update(DeltaSeconds);
	if (!Follower) return;
	const float LeaderCycle = Leader->GetCycleSeconds();
	const float FollowerCycle = Follower->GetCycleSeconds();
	float LeaderTime = 0.0f;
	if (bSyncByNormalizedTime && Leader->IsLooping() && Follower->IsLooping() &&
		LeaderCycle > 0.0f && FollowerCycle > 0.0f && Leader->GetNormalizedTime(LeaderTime))
	{
		// 리더가 이번 프레임에 진행한 주기 비율만큼 팔로워도 진행해 루트 모션을 같은 비율로 얻는다.
		// 그 뒤 위상을 리더에 맞춰 누적 오차와 처음 어긋남을 없앤다.
		Follower->Update(DeltaSeconds * FollowerCycle / LeaderCycle);
		Follower->SetNormalizedTime(LeaderTime);
	}
	else
		Follower->Update(DeltaSeconds);
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

float FAnimNode_BlendSpacePlayer::GetCycleSeconds() const
{
	if (!BlendSpace || !std::isfinite(PlayRate) || PlayRate <= 0.0f) return 0.0f;
	const float Length = BlendSpace->GetEffectiveLength(InputX, InputY);
	return Length > 0.0f && std::isfinite(Length) ? Length / PlayRate : 0.0f;
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

int32 FAnimNode_StateMachine::FindState(const FString& Name) const
{
	for (int32 Index = 0; Index < States.Num(); ++Index)
		if (States[Index].Name == Name) return Index;
	return -1;
}

bool FAnimNode_StateMachine::RequestTransition(int32 ToState, float BlendTime, bool bRestart)
{
	if (!States.IsValidIndex(ToState) || !std::isfinite(BlendTime) || BlendTime < 0.0f) return false;
	if (ToState == GetTargetState() && !bRestart) return true;
	// 섞는 도중이거나 지금 상태를 처음부터 다시 틀면 From 노드가 없다(또는 To와 같은 노드다).
	const bool bNeedSnapshot = NextState >= 0 || ToState == CurrentState;
	if (NextState >= 0) FinishTransition();
	bBlendFromSnapshot = bNeedSnapshot && BlendTime > 0.0f && !LastPose.IsEmpty();
	if (bBlendFromSnapshot) SnapshotPose = LastPose;
	NextState = ToState;
	TransitionDuration = BlendTime;
	TransitionElapsed = 0.0f;
	TransitionOnCompleted = {};
	if (States[ToState].Pose) States[ToState].Pose->Reset();
	if (BlendTime <= 0.0f) FinishTransition();
	return true;
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
	bBlendFromSnapshot = false;
	SnapshotPose.Reset();
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
	LastPose.Reset();
	SnapshotPose.Reset();
	bBlendFromSnapshot = false;
	for (const FState& State : States) if (State.Pose) State.Pose->Reset();
}

void FAnimNode_StateMachine::Update(float DeltaSeconds)
{
	if (!States.IsValidIndex(CurrentState) || !std::isfinite(DeltaSeconds)) return;
	DeltaSeconds = std::max(0.0f, DeltaSeconds);
	// 스냅샷에서 섞는 중이면 현재 상태 포즈는 쓰이지 않는다. 같은 노드를 다시 트는 경우 두 번 진행하지 않도록 건너뛴다.
	const bool bCurrentUnused = NextState >= 0 && (bBlendFromSnapshot || NextState == CurrentState);
	if (!bCurrentUnused && States[CurrentState].Pose) States[CurrentState].Pose->Update(DeltaSeconds);
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
	FAnimNode_Base* Current = States[CurrentState].Pose;
	bool bEvaluated = false;
	if (NextState < 0) bEvaluated = Current && Current->Evaluate(Skeleton, OutPose);
	else
	{
		FAnimNode_Base* Next = States[NextState].Pose;
		const float Alpha = TransitionElapsed / TransitionDuration;
		TArray<FAnimBoneTransform> From, To;
		if (bBlendFromSnapshot)
			bEvaluated = Next && Next->Evaluate(Skeleton, To) && BlendAnimPoses(SnapshotPose, To, Alpha, OutPose);
		else
			bEvaluated = Current && Next && Current->Evaluate(Skeleton, From) && Next->Evaluate(Skeleton, To) &&
				BlendAnimPoses(From, To, Alpha, OutPose);
	}
	if (bEvaluated) LastPose = OutPose;
	return bEvaluated;
}

bool FAnimNode_StateMachine::ConsumeRootMotion(FMatrix& OutDelta)
{
	if (!States.IsValidIndex(CurrentState)) return false;
	FAnimNode_Base* Current = States[CurrentState].Pose;
	if (NextState < 0) return Current && Current->ConsumeRootMotion(OutDelta);
	const float Alpha = TransitionDuration > 0.0f ? TransitionElapsed / TransitionDuration : 1.0f;
	// 스냅샷에는 이동량이 없으므로 제자리(Identity)와 섞는다.
	if (bBlendFromSnapshot || NextState == CurrentState)
		return BlendRootMotion(nullptr, States[NextState].Pose, Alpha, OutDelta);
	return BlendRootMotion(Current, States[NextState].Pose, Alpha, OutDelta);
}
