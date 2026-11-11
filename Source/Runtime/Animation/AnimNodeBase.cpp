#include "EnginePCH.h"
#include "Animation/AnimNodeBase.h"
#include "Animation/AnimGraphInstance.h"
#include "Animation/BoneTransform.h"
#include "Animation/Skeleton.h"
#include "Asset/AnimSequence.h"
#include "Asset/BlendSpace.h"
#include "Math/EngineMath.h"

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

	bool EvaluateSequenceFrame(UAnimSequence* Sequence, const USkeleton* Skeleton, int32 FrameIndex, TArray<FAnimBoneTransform>& OutPose)
	{
		if (!Sequence || !Skeleton || Sequence->Skeleton != Skeleton) return false;
		const UAnimDataModel* Model = Sequence->GetDataModel();
		if (!Model) return false;
		float EvalTime = 0.0f;
		const float Fps = Model->FrameRate.AsDecimal();
		if (FrameIndex > 0 && Fps > 0.0f)
		{
			EvalTime = std::clamp(static_cast<float>(FrameIndex) / Fps, 0.0f, Model->PlayLength);
		}
		return Sequence->EvaluateLocalPoseTransforms(EvalTime, OutPose);
	}

	void BuildBranchMask(const FReferenceSkeleton& RefSkeleton, int32 StartBoneIndex, TArray<uint8>& OutMask)
	{
		const int32 NumBones = RefSkeleton.GetNum();
		OutMask.Init(0, NumBones);
		if (StartBoneIndex < 0 || StartBoneIndex >= NumBones) return;
		for (int32 BoneIdx = 0; BoneIdx < NumBones; ++BoneIdx)
		{
			if (BoneIdx == StartBoneIndex)
			{
				OutMask[BoneIdx] = 1;
				continue;
			}
			const int32 ParentIdx = RefSkeleton.RefBoneInfo[BoneIdx].ParentIndex;
			if (ParentIdx >= 0 && ParentIdx < BoneIdx && OutMask[ParentIdx] != 0)
			{
				OutMask[BoneIdx] = 1;
			}
		}
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

bool FAnimNode_StateMachine::AddTransition(int32 FromState, int32 ToState,
	std::function<bool()> Condition, float BlendTime, std::function<void()> OnCompleted,
	std::function<void()> OnStarted)
{
	if ((FromState != -1 && !States.IsValidIndex(FromState)) || !States.IsValidIndex(ToState) ||
		FromState == ToState || !Condition || !std::isfinite(BlendTime) || BlendTime < 0.0f) return false;
	Transitions.Add({ FromState, ToState, std::move(Condition), BlendTime,
		std::move(OnCompleted), std::move(OnStarted) });
	return true;
}

void FAnimNode_StateMachine::FinishTransition()
{
	CurrentState = NextState;
	NextState = -1;
	InterruptedPose.Reset();
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
	InterruptedPose.Reset();
	LastEvaluatedPose.Reset();
	for (const FState& State : States) if (State.Pose) State.Pose->Reset();
}

void FAnimNode_StateMachine::Update(float DeltaSeconds)
{
	if (!States.IsValidIndex(CurrentState) || !std::isfinite(DeltaSeconds)) return;
	DeltaSeconds = std::max(0.0f, DeltaSeconds);
	if (States[CurrentState].Pose) States[CurrentState].Pose->Update(DeltaSeconds);
	const bool bWasTransitioning = NextState >= 0;
	if (bWasTransitioning)
	{
		if (States[NextState].Pose) States[NextState].Pose->Update(DeltaSeconds);
		TransitionElapsed += DeltaSeconds;
		if (TransitionElapsed >= TransitionDuration)
			FinishTransition();
	}
	for (const FTransition& Transition : Transitions) // anystate 검사
	{
		if (Transition.FromState != -1 || Transition.ToState == CurrentState ||
			Transition.ToState == NextState || !Transition.Condition()) continue;
		InterruptedPose = NextState >= 0 ? LastEvaluatedPose : TArray<FAnimBoneTransform>{};
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
		return;
	}
	if (bWasTransitioning) return;
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
	if (NextState < 0)
	{
		if (!States[CurrentState].Pose || !States[CurrentState].Pose->Evaluate(Skeleton, OutPose)) return false;
		LastEvaluatedPose = OutPose;
		return true;
	}
	TArray<FAnimBoneTransform> From, To;
	if (InterruptedPose.IsEmpty() && (!States[CurrentState].Pose ||
		!States[CurrentState].Pose->Evaluate(Skeleton, From))) return false;
	if (!InterruptedPose.IsEmpty()) From = InterruptedPose;
	if (!States[NextState].Pose || !States[NextState].Pose->Evaluate(Skeleton, To) ||
		!BlendAnimPoses(From, To, TransitionElapsed / TransitionDuration, OutPose)) return false;
	LastEvaluatedPose = OutPose;
	return true;
}

bool FAnimNode_StateMachine::ConsumeRootMotion(FMatrix& OutDelta)
{
	if (!States.IsValidIndex(CurrentState)) return false;
	FAnimNode_Base* Current = States[CurrentState].Pose;
	if (NextState < 0) return Current && Current->ConsumeRootMotion(OutDelta);
	return BlendRootMotion(InterruptedPose.IsEmpty() ? Current : nullptr, States[NextState].Pose,
		TransitionDuration > 0.0f ? TransitionElapsed / TransitionDuration : 1.0f, OutDelta);
}

void FAnimNode_LayeredBoneBlend::Reset()
{
	if (!bGraphManaged)
	{
		if (BasePose) BasePose->Reset();
		if (BlendPose && BlendPose != BasePose) BlendPose->Reset();
	}
}

void FAnimNode_LayeredBoneBlend::Update(float DeltaSeconds)
{
	if (!bGraphManaged)
	{
		if (BasePose) BasePose->Update(DeltaSeconds);
		if (BlendPose && BlendPose != BasePose) BlendPose->Update(DeltaSeconds);
	}
}

bool FAnimNode_LayeredBoneBlend::Evaluate(const USkeleton* Skeleton, TArray<FAnimBoneTransform>& OutPose)
{
	if (!BasePose || !Skeleton || !BasePose->Evaluate(Skeleton, OutPose)) return false;
	const float Weight = std::clamp(Alpha, 0.0f, 1.0f);
	if (Weight <= 0.0f || !BlendPose || StartBoneName.empty()) return true;
	const int32 StartBoneIndex = Skeleton->RefSkeleton.FindBoneIndex(StartBoneName);
	if (StartBoneIndex == INDEX_NONE) return true;
	TArray<FAnimBoneTransform> BlendPoseOut;
	if (!BlendPose->Evaluate(Skeleton, BlendPoseOut) || BlendPoseOut.Num() != OutPose.Num()) return true;
	TArray<uint8> BranchMask;
	BuildBranchMask(Skeleton->RefSkeleton, StartBoneIndex, BranchMask);
	const int32 NumBones = OutPose.Num();
	TArray<FQuat> BaseCSRot, BlendCSRot, ResultCSRot;
	if (bMeshSpaceRotationBlend)
	{
		BaseCSRot.SetNum(NumBones);
		BlendCSRot.SetNum(NumBones);
		ResultCSRot.SetNum(NumBones);
		for (int32 BoneIdx = 0; BoneIdx < NumBones; ++BoneIdx)
		{
			const int32 ParentIdx = Skeleton->RefSkeleton.RefBoneInfo[BoneIdx].ParentIndex;
			const FQuat BaseLocal = OutPose[BoneIdx].Rotation.Normalized();
			const FQuat BlendLocal = BlendPoseOut[BoneIdx].Rotation.Normalized();
			BaseCSRot[BoneIdx] = ParentIdx >= 0 && ParentIdx < BoneIdx
				? (BaseCSRot[ParentIdx] * BaseLocal).Normalized() : BaseLocal;
			BlendCSRot[BoneIdx] = ParentIdx >= 0 && ParentIdx < BoneIdx
				? (BlendCSRot[ParentIdx] * BlendLocal).Normalized() : BlendLocal;
		}
	}
	for (int32 BoneIdx = 0; BoneIdx < NumBones; ++BoneIdx)
	{
		if (BranchMask[BoneIdx] == 0)
		{
			if (bMeshSpaceRotationBlend)
				ResultCSRot[BoneIdx] = BaseCSRot[BoneIdx];
			continue;
		}

		OutPose[BoneIdx].Position = FMath::Lerp(OutPose[BoneIdx].Position, BlendPoseOut[BoneIdx].Position, Weight);
		OutPose[BoneIdx].Scale = FMath::Lerp(OutPose[BoneIdx].Scale, BlendPoseOut[BoneIdx].Scale, Weight);
		if (!bMeshSpaceRotationBlend)
		{
			OutPose[BoneIdx].Rotation = FMath::Slerp(OutPose[BoneIdx].Rotation, BlendPoseOut[BoneIdx].Rotation, Weight);
			continue;
		}

		const int32 ParentIdx = Skeleton->RefSkeleton.RefBoneInfo[BoneIdx].ParentIndex;
		const FQuat BlendedCS = FQuat::Slerp(BaseCSRot[BoneIdx], BlendCSRot[BoneIdx], Weight).Normalized();
		OutPose[BoneIdx].Rotation = ParentIdx >= 0 && ParentIdx < BoneIdx
			? (ResultCSRot[ParentIdx].UnitInverse() * BlendedCS).Normalized() : BlendedCS;
		ResultCSRot[BoneIdx] = BlendedCS;
	}
	return true;
}

bool FAnimNode_LayeredBoneBlend::ConsumeRootMotion(FMatrix& OutDelta)
{
	return BasePose && BasePose->ConsumeRootMotion(OutDelta);
}

void FAnimNode_Slot::Reset()
{
	if (!bGraphManaged && Source) Source->Reset();
}

void FAnimNode_Slot::Update(float DeltaSeconds)
{
	if (!bGraphManaged && Source) Source->Update(DeltaSeconds);
}

bool FAnimNode_Slot::Evaluate(const USkeleton* Skeleton, TArray<FAnimBoneTransform>& OutPose)
{
	if (!Skeleton) return false;
	bool bHasSource = false;
	if (Source)
	{
		bHasSource = Source->Evaluate(Skeleton, OutPose);
	}
	if (!bHasSource)
	{
		const int32 NumRefBones = Skeleton->RefSkeleton.RefBonePose.Num();
		OutPose.SetNum(NumRefBones);
		for (int32 BoneIdx = 0; BoneIdx < NumRefBones; ++BoneIdx)
		{
			const FBoneTransform& Ref = Skeleton->RefSkeleton.RefBonePose[BoneIdx];
			OutPose[BoneIdx].Position = Ref.Translation;
			OutPose[BoneIdx].Rotation = Ref.Rotation;
			OutPose[BoneIdx].Scale = Ref.Scale;
		}
	}
	if (GraphInstance && !SlotName.empty())
	{
		GraphInstance->EvaluateSlotPose(SlotName, Skeleton, OutPose);
	}
	return !OutPose.IsEmpty();
}

bool FAnimNode_Slot::ConsumeRootMotion(FMatrix& OutDelta)
{
	return Source && Source->ConsumeRootMotion(OutDelta);
}

bool FAnimNode_AimOffset1D::SamplePoseAtPitch(const USkeleton* Skeleton, float SamplePitch, TArray<FAnimBoneTransform>& OutPose) const
{
	if (!Skeleton || Samples.IsEmpty()) return false;
	const float Clamped = std::clamp(SamplePitch, Asset.MinPitch, Asset.MaxPitch);
	if (Samples.Num() == 1 || Clamped <= Samples[0].Pitch)
	{
		return EvaluateSequenceFrame(Samples[0].Sequence, Skeleton, Samples[0].FrameIndex, OutPose);
	}
	const int32 LastIdx = Samples.Num() - 1;
	if (Clamped >= Samples[LastIdx].Pitch)
	{
		return EvaluateSequenceFrame(Samples[LastIdx].Sequence, Skeleton, Samples[LastIdx].FrameIndex, OutPose);
	}
	for (int32 Idx = 0; Idx < LastIdx; ++Idx)
	{
		const FSampleEntry& Low = Samples[Idx];
		const FSampleEntry& High = Samples[Idx + 1];
		if (Clamped >= Low.Pitch && Clamped <= High.Pitch)
		{
			const float Span = High.Pitch - Low.Pitch;
			if (Span <= 1.e-5f)
			{
				return EvaluateSequenceFrame(Low.Sequence, Skeleton, Low.FrameIndex, OutPose);
			}
			TArray<FAnimBoneTransform> PoseLow, PoseHigh;
			if (!EvaluateSequenceFrame(Low.Sequence, Skeleton, Low.FrameIndex, PoseLow) ||
				!EvaluateSequenceFrame(High.Sequence, Skeleton, High.FrameIndex, PoseHigh))
			{
				return false;
			}
			const float BlendWeight = std::clamp((Clamped - Low.Pitch) / Span, 0.0f, 1.0f);
			return BlendAnimPoses(PoseLow, PoseHigh, BlendWeight, OutPose);
		}
	}
	return EvaluateSequenceFrame(Samples[LastIdx].Sequence, Skeleton, Samples[LastIdx].FrameIndex, OutPose);
}

bool FAnimNode_AimOffset1D::EvaluateReferencePose(const USkeleton* Skeleton, TArray<FAnimBoneTransform>& OutPose) const
{
	if (BaseSequence && EvaluateSequenceFrame(BaseSequence, Skeleton, Asset.BaseFrameIndex, OutPose))
	{
		return true;
	}
	return SamplePoseAtPitch(Skeleton, 0.0f, OutPose);
}

void FAnimNode_AimOffset1D::Reset()
{
	if (!bGraphManaged && BasePose) BasePose->Reset();
}

void FAnimNode_AimOffset1D::Update(float DeltaSeconds)
{
	if (!bGraphManaged && BasePose) BasePose->Update(DeltaSeconds);
}

bool FAnimNode_AimOffset1D::Evaluate(const USkeleton* Skeleton, TArray<FAnimBoneTransform>& OutPose)
{
	if (!BasePose || !Skeleton || !BasePose->Evaluate(Skeleton, OutPose)) return false;
	const float Weight = std::clamp(Alpha, 0.0f, 1.0f);
	if (Weight <= 0.0f || Samples.IsEmpty()) return true;

	TArray<FAnimBoneTransform> RefPose, AimPose;
	if (!EvaluateReferencePose(Skeleton, RefPose) || !SamplePoseAtPitch(Skeleton, Pitch, AimPose))
	{
		return true;
	}
	const int32 NumBones = Skeleton->RefSkeleton.GetNum();
	if (OutPose.Num() != NumBones || RefPose.Num() != NumBones || AimPose.Num() != NumBones)
	{
		return true;
	}

	// 메시 스페이스 기준 회전 차이를 구해 입력 포즈 전체에 가산한다. 본 범위는 Layered Blend 노드가 정한다.
	TArray<FQuat> BaseCSRot;
	TArray<FQuat> RefCSRot;
	TArray<FQuat> AimCSRot;
	TArray<FQuat> NewCSRot;
	BaseCSRot.SetNum(NumBones);
	RefCSRot.SetNum(NumBones);
	AimCSRot.SetNum(NumBones);
	NewCSRot.SetNum(NumBones);

	for (int32 BoneIdx = 0; BoneIdx < NumBones; ++BoneIdx)
	{
		const int32 ParentIdx = Skeleton->RefSkeleton.RefBoneInfo[BoneIdx].ParentIndex;
		const FQuat BaseLocal = OutPose[BoneIdx].Rotation.Normalized();
		const FQuat RefLocal = RefPose[BoneIdx].Rotation.Normalized();
		const FQuat AimLocal = AimPose[BoneIdx].Rotation.Normalized();
		if (ParentIdx >= 0 && ParentIdx < BoneIdx)
		{
			BaseCSRot[BoneIdx] = (BaseCSRot[ParentIdx] * BaseLocal).Normalized();
			RefCSRot[BoneIdx] = (RefCSRot[ParentIdx] * RefLocal).Normalized();
			AimCSRot[BoneIdx] = (AimCSRot[ParentIdx] * AimLocal).Normalized();
		}
		else
		{
			BaseCSRot[BoneIdx] = BaseLocal;
			RefCSRot[BoneIdx] = RefLocal;
			AimCSRot[BoneIdx] = AimLocal;
		}

		const FQuat FullDeltaCS = (AimCSRot[BoneIdx] * RefCSRot[BoneIdx].UnitInverse()).Normalized();
		const FQuat WeightedDeltaCS = FQuat::Slerp(FQuat::Identity(), FullDeltaCS, Weight);
		NewCSRot[BoneIdx] = (WeightedDeltaCS * BaseCSRot[BoneIdx]).Normalized();
	}

	for (int32 BoneIdx = 0; BoneIdx < NumBones; ++BoneIdx)
	{
		const int32 ParentIdx = Skeleton->RefSkeleton.RefBoneInfo[BoneIdx].ParentIndex;
		if (ParentIdx >= 0 && ParentIdx < BoneIdx)
		{
			OutPose[BoneIdx].Rotation = (NewCSRot[ParentIdx].UnitInverse() * NewCSRot[BoneIdx]).Normalized();
		}
		else
		{
			OutPose[BoneIdx].Rotation = NewCSRot[BoneIdx];
		}
	}
	return true;
}

bool FAnimNode_AimOffset1D::ConsumeRootMotion(FMatrix& OutDelta)
{
	return BasePose && BasePose->ConsumeRootMotion(OutDelta);
}
