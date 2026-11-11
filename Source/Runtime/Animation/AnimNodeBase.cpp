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
	if (!HasA && !HasB)
	{
		return false;
	}
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
	PendingNotifies.Reset();
	bNotifyIncludeStart = true;
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
	{
		return std::numeric_limits<float>::infinity();
	}
	return std::max(0.0f, Sequence->GetDataModel()->PlayLength - CurrentTime) / (PlayRate * Sequence->RateScale);
}

void FAnimNode_SequencePlayer::Update(float DeltaSeconds)
{
	// 이전 프레임에 모은 Notify는 소비되었다고 보고 매 Update 시작에 비운다.
	PendingNotifies.Reset();

	if (!bPlaying || !Sequence || !Sequence->GetDataModel() ||
		!std::isfinite(PlayRate) || PlayRate < 0.0f ||
		!std::isfinite(Sequence->RateScale) || Sequence->RateScale <= 0.0f ||
		!std::isfinite(DeltaSeconds) || DeltaSeconds <= 0.0f)
	{
		return;
	}

	const float Length = Sequence->GetDataModel()->PlayLength;
	if (Length <= 0.0f || !std::isfinite(Length))
	{
		return;
	}

	const float NextTime = CurrentTime + DeltaSeconds * PlayRate * Sequence->RateScale;
	if (!std::isfinite(NextTime))
	{
		return;
	}

	const float PreviousTime = CurrentTime;
	int32 LoopCount = 0;
	if (NextTime < Length)
	{
		CurrentTime = NextTime;
	}
	else if (bLoop)
	{
		//delta time이 더해진 NextTime이 총 길이보다 256배 클 때 종료(오랜 기간 멈춰있어서 deltatime이 클 때..?)
		if (NextTime / Length > 256.0f)
		{
			return;
		}
		LoopCount = static_cast<int32>(NextTime / Length);
		CurrentTime = std::fmod(NextTime, Length);
	}
	else
	{
		CurrentTime = Length;
		bPlaying = false;
	}
	bHasPendingRootMotion = Sequence->ExtractRootMotion(PreviousTime, CurrentTime, LoopCount, PendingRootMotion);
	Sequence->GetNotifies(PreviousTime, CurrentTime, LoopCount, bNotifyIncludeStart, PendingNotifies);
	bNotifyIncludeStart = false;
}

void FAnimNode_SequencePlayer::GatherNotifies(float ParentWeight, TArray<FAnimNotifyQueueItem>& OutQueue) const
{
	for (const FAnimNotifyEvent& Event : PendingNotifies)
	{
		OutQueue.Add({ Event, ParentWeight, Sequence });
	}
}

bool FAnimNode_SequencePlayer::Evaluate(const USkeleton* Skeleton, TArray<FAnimBoneTransform>& OutPose)
{
	if (!Sequence || !Skeleton || Sequence->Skeleton != Skeleton)
	{
		return false;
	}
	return Sequence->EvaluateLocalPoseTransforms(CurrentTime, OutPose);
}

bool FAnimNode_SequencePlayer::ConsumeRootMotion(FMatrix& OutDelta)
{
	if (!bHasPendingRootMotion)
	{
		return false;
	}
	OutDelta = PendingRootMotion;
	bHasPendingRootMotion = false;
	return true;
}

float FAnimNode_SequencePlayer::GetCycleSeconds() const
{
	if (!Sequence || !Sequence->GetDataModel() || !std::isfinite(PlayRate) || PlayRate <= 0.0f ||
		!std::isfinite(Sequence->RateScale) || Sequence->RateScale <= 0.0f)
	{
		return 0.0f;
	}
	const float Length = Sequence->GetDataModel()->PlayLength;
	return Length > 0.0f && std::isfinite(Length) ? Length / (PlayRate * Sequence->RateScale) : 0.0f;
}

bool FAnimNode_SequencePlayer::GetNormalizedTime(float& OutTime) const
{
	if (!Sequence || !Sequence->GetDataModel() || Sequence->GetDataModel()->PlayLength <= 0.0f)
	{
		return false;
	}
	OutTime = std::clamp(CurrentTime / Sequence->GetDataModel()->PlayLength, 0.0f, 1.0f);
	return true;
}

void FAnimNode_SequencePlayer::SetNormalizedTime(float Time)
{
	if (!Sequence || !Sequence->GetDataModel() || !std::isfinite(Time))
	{
		return;
	}
	CurrentTime = std::clamp(Time, 0.0f, 1.0f) * Sequence->GetDataModel()->PlayLength;
}

void FAnimNode_TwoWayBlend::Reset()
{
	if (A)
	{
		A->Reset();
	}
	if (B)
	{
		B->Reset();
	}
}

void FAnimNode_TwoWayBlend::Update(float DeltaSeconds)
{
	// 가중치가 0인 자식도 시간을 진행한다. 멈춰 두면 다시 섞일 때 멈췄던 위치에서 이어져 동작이 튄다.
	const float WeightB = std::clamp(Alpha, 0.0f, 1.0f);
	FAnimNode_Base* Leader = WeightB >= 0.5f ? B : A;
	FAnimNode_Base* Follower = Leader == A ? B : A;
	if (!Leader)
	{
		std::swap(Leader, Follower);
	}
	if (!Leader)
	{
		return;
	}
	Leader->Update(DeltaSeconds);
	if (!Follower)
	{
		return;
	}
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
	{
		Follower->Update(DeltaSeconds);
	}
}

bool FAnimNode_TwoWayBlend::Evaluate(const USkeleton* Skeleton, TArray<FAnimBoneTransform>& OutPose)
{
	if (Alpha <= 0.0f)
	{
		return A && A->Evaluate(Skeleton, OutPose);
	}
	if (Alpha >= 1.0f)
	{
		return B && B->Evaluate(Skeleton, OutPose);
	}
	TArray<FAnimBoneTransform> PoseA, PoseB;
	return A && B && A->Evaluate(Skeleton, PoseA) && B->Evaluate(Skeleton, PoseB) &&
		BlendAnimPoses(PoseA, PoseB, Alpha, OutPose);
}

bool FAnimNode_TwoWayBlend::ConsumeRootMotion(FMatrix& OutDelta)
{
	return BlendRootMotion(A, B, Alpha, OutDelta);
}

void FAnimNode_TwoWayBlend::GatherNotifies(float ParentWeight, TArray<FAnimNotifyQueueItem>& OutQueue) const
{
	// Evaluate와 같이 한쪽 가중치가 0이면 그쪽은 올리지 않는다. 가중치 0인 자식도 Update는 되므로 Notify는 모여 있다.
	const float WeightB = std::clamp(Alpha, 0.0f, 1.0f);
	if (A && WeightB < 1.0f)
	{
		A->GatherNotifies(ParentWeight * (1.0f - WeightB), OutQueue);
	}
	if (B && WeightB > 0.0f)
	{
		B->GatherNotifies(ParentWeight * WeightB, OutQueue);
	}
}

void FAnimNode_BlendSpacePlayer::Reset()
{
	NormalizedTime = 0.0f;
	bPlaying = BlendSpace != nullptr;
	bHasPendingRootMotion = false;
	PendingNotifies.Reset();
	bNotifyIncludeStart = true;
}

void FAnimNode_BlendSpacePlayer::Update(float DeltaSeconds)
{
	// 이전 프레임에 모은 Notify는 소비되었다고 보고 매 Update 시작에 비운다.
	PendingNotifies.Reset();

	if (!bPlaying || !BlendSpace || !std::isfinite(DeltaSeconds) || DeltaSeconds <= 0.0f ||
		!std::isfinite(PlayRate) || PlayRate < 0.0f)
	{
		return;
	}
	const float Length = BlendSpace->GetEffectiveLength(InputX, InputY);
	if (Length <= 0.0f || !std::isfinite(Length))
	{
		return;
	}
	const float NextTime = NormalizedTime + DeltaSeconds * PlayRate / Length;
	if (!std::isfinite(NextTime))
	{
		return;
	}
	const float PreviousTime = NormalizedTime;
	int32 LoopCount = 0;
	if (NextTime < 1.0f)
	{
		NormalizedTime = NextTime;
	}
	else if (bLoop)
	{
		if (NextTime > 256.0f)
		{
			return;
		}
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
	BlendSpace->GetNotifies(InputX, InputY, PreviousTime, NormalizedTime, LoopCount, bNotifyIncludeStart, PendingNotifies);
	bNotifyIncludeStart = false;
}

void FAnimNode_BlendSpacePlayer::GatherNotifies(float ParentWeight, TArray<FAnimNotifyQueueItem>& OutQueue) const
{
	for (const FAnimNotifyQueueItem& Item : PendingNotifies)
	{
		FAnimNotifyQueueItem Gathered = Item;
		Gathered.Weight *= ParentWeight;
		OutQueue.Add(std::move(Gathered));
	}
}

bool FAnimNode_BlendSpacePlayer::Evaluate(const USkeleton* Skeleton, TArray<FAnimBoneTransform>& OutPose)
{
	return BlendSpace && Skeleton && BlendSpace->Skeleton == Skeleton &&
		BlendSpace->Evaluate(InputX, InputY, NormalizedTime, OutPose);
}

float FAnimNode_BlendSpacePlayer::GetCycleSeconds() const
{
	if (!BlendSpace || !std::isfinite(PlayRate) || PlayRate <= 0.0f)
	{
		return 0.0f;
	}
	const float Length = BlendSpace->GetEffectiveLength(InputX, InputY);
	return Length > 0.0f && std::isfinite(Length) ? Length / PlayRate : 0.0f;
}

bool FAnimNode_BlendSpacePlayer::ConsumeRootMotion(FMatrix& OutDelta)
{
	if (!bHasPendingRootMotion)
	{
		return false;
	}
	OutDelta = PendingRootMotion;
	bHasPendingRootMotion = false;
	return true;
}

int32 FAnimNode_StateMachine::AddState(const FString& Name, FAnimNode_Base* PoseNode)
{
	if (!PoseNode)
	{
		return -1;
	}
	const int32 Index = States.Num();
	States.Add({ Name, PoseNode });
	if (CurrentState < 0)
	{
		CurrentState = Index;
	}
	return Index;
}

bool FAnimNode_StateMachine::AddTransition(int32 FromState, int32 ToState,
	std::function<bool()> Condition, float BlendTime, std::function<void()> OnCompleted,
	std::function<void()> OnStarted)
{
	if (!States.IsValidIndex(FromState) || !States.IsValidIndex(ToState) ||
		FromState == ToState || !Condition || !std::isfinite(BlendTime) || BlendTime < 0.0f)
	{
		return false;
	}
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
	if (Completed)
	{
		Completed();
	}
}

void FAnimNode_StateMachine::Reset()
{
	CurrentState = States.IsEmpty() ? -1 : 0;
	NextState = -1;
	TransitionElapsed = 0.0f;
	TransitionDuration = 0.0f;
	TransitionOnCompleted = {};
	for (const FState& State : States)
	{
		if (State.Pose)
		{
			State.Pose->Reset();
		}
	}
}

void FAnimNode_StateMachine::Update(float DeltaSeconds)
{
	if (!States.IsValidIndex(CurrentState) || !std::isfinite(DeltaSeconds))
	{
		return;
	}
	DeltaSeconds = std::max(0.0f, DeltaSeconds);
	if (States[CurrentState].Pose)
	{
		States[CurrentState].Pose->Update(DeltaSeconds);
	}
	if (NextState >= 0)
	{
		if (States[NextState].Pose)
		{
			States[NextState].Pose->Update(DeltaSeconds);
		}
		TransitionElapsed += DeltaSeconds;
		if (TransitionElapsed >= TransitionDuration)
		{
			FinishTransition();
		}
		return;
	}
	for (const FTransition& Transition : Transitions)
	{
		if (Transition.FromState != CurrentState || !Transition.Condition())
		{
			continue;
		}
		NextState = Transition.ToState;
		TransitionDuration = Transition.BlendTime;
		TransitionElapsed = DeltaSeconds;
		TransitionOnCompleted = Transition.OnCompleted;
		if (Transition.OnStarted)
		{
			Transition.OnStarted();
		}
		if (States[NextState].Pose)
		{
			States[NextState].Pose->Reset();
			States[NextState].Pose->Update(DeltaSeconds);
		}
		if (TransitionElapsed >= TransitionDuration)
		{
			FinishTransition();
		}
		break;
	}
}

bool FAnimNode_StateMachine::Evaluate(const USkeleton* Skeleton, TArray<FAnimBoneTransform>& OutPose)
{
	if (!States.IsValidIndex(CurrentState))
	{
		return false;
	}
	if (NextState < 0)
	{
		return States[CurrentState].Pose &&
			States[CurrentState].Pose->Evaluate(Skeleton, OutPose);
	}
	TArray<FAnimBoneTransform> From, To;
	return States[CurrentState].Pose && States[NextState].Pose &&
		States[CurrentState].Pose->Evaluate(Skeleton, From) && States[NextState].Pose->Evaluate(Skeleton, To) &&
		BlendAnimPoses(From, To, TransitionElapsed / TransitionDuration, OutPose);
}

bool FAnimNode_StateMachine::ConsumeRootMotion(FMatrix& OutDelta)
{
	if (!States.IsValidIndex(CurrentState))
	{
		return false;
	}
	FAnimNode_Base* Current = States[CurrentState].Pose;
	if (NextState < 0)
	{
		return Current && Current->ConsumeRootMotion(OutDelta);
	}
	return BlendRootMotion(Current, States[NextState].Pose,
		TransitionDuration > 0.0f ? TransitionElapsed / TransitionDuration : 1.0f, OutDelta);
}

void FAnimNode_StateMachine::GatherNotifies(float ParentWeight, TArray<FAnimNotifyQueueItem>& OutQueue) const
{
	if (!States.IsValidIndex(CurrentState))
	{
		return;
	}
	const FAnimNode_Base* Current = States[CurrentState].Pose;
	if (NextState < 0)
	{
		if (Current)
		{
			Current->GatherNotifies(ParentWeight, OutQueue);
		}
		return;
	}
	// 전이 중에는 Evaluate와 같은 비율로 양쪽 상태의 Notify를 올린다.
	// 전이가 이번 Update에서 끝났다면 CurrentState가 이미 새 상태이므로 이전 상태의 마지막 구간 Notify는 올라오지 않는다.
	const float Alpha = TransitionDuration > 0.0f ? std::clamp(TransitionElapsed / TransitionDuration, 0.0f, 1.0f) : 1.0f;
	const FAnimNode_Base* Next = States[NextState].Pose;
	if (Current)
	{
		Current->GatherNotifies(ParentWeight * (1.0f - Alpha), OutQueue);
	}
	if (Next)
	{
		Next->GatherNotifies(ParentWeight * Alpha, OutQueue);
	}
}
