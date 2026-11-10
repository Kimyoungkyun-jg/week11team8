#include "EnginePCH.h"
#include "Asset/AnimSequence.h"
#include "Asset/AnimTransform.h"
#include "Math/EngineMath.h"
#include "Animation/Skeleton.h"
#include "Animation/BoneTransform.h"

#include <algorithm>
#include <cmath>

int32 UAnimSequence::GetFrameIndex(float TimeSeconds) const
{
	if (!DataModel || DataModel->NumberOfKeys <= 0 || DataModel->FrameRate.AsDecimal() <= 0.0f ||
		!std::isfinite(TimeSeconds)) return -1;
	const double Sample = static_cast<double>(std::max(0.0f, TimeSeconds)) * DataModel->FrameRate.AsDecimal();
	if (Sample >= DataModel->NumberOfKeys - 1) return DataModel->NumberOfKeys - 1;
	return static_cast<int32>(Sample);
}

int32 UAnimSequence::GetRootBoneIndex() const
{
	if (!Skeleton || Skeleton->RefSkeleton.GetNum() == 0) return -1;
	return Skeleton->RefSkeleton.RefBoneInfo[0].ParentIndex < 0 ? 0 : -1;
}

bool UAnimSequence::SampleRawRoot(float TimeSeconds, FAnimBoneTransform& OutRoot) const
{
	TArray<FAnimBoneTransform> Pose;
	const int32 Root = GetRootBoneIndex();
	if (Root < 0 || !EvaluateLocalPoseTransforms(TimeSeconds, Pose, false) || !Pose.IsValidIndex(Root)) return false;
	OutRoot = Pose[Root];
	return true;
}

bool UAnimSequence::ExtractRootMotion(float PreviousTime, float CurrentTime, int32 LoopCount, FMatrix& OutDelta) const
{
	OutDelta = FMatrix::Identity;
	if (!bEnableRootMotion || !DataModel || GetRootBoneIndex() < 0 ||
		!Skeleton->RefSkeleton.RefBonePose.IsValidIndex(0) || LoopCount < 0 || LoopCount > 256) return false;
	const float Length = DataModel->PlayLength;
	if (Length <= 0.0f || !std::isfinite(PreviousTime) || !std::isfinite(CurrentTime)) return false;
	FAnimBoneTransform Lock;
	switch (RootMotionRootLock)
	{
	case ERootMotionRootLock::RefPose:
	{
		const FBoneTransform& Ref = Skeleton->RefSkeleton.RefBonePose[0];
		Lock = { Ref.Translation, Ref.Rotation, Ref.Scale };
		break;
	}
	case ERootMotionRootLock::AnimFirstFrame:
		if (!SampleRawRoot(0.0f, Lock)) return false;
		break;
	case ERootMotionRootLock::Zero: break;
	}
	const FMatrix LockMatrix = MakeAnimMatrix(Lock.Position, Lock.Rotation, Lock.Scale);
	const auto Segment = [&](float Start, float End, FMatrix& Delta)
	{
		FAnimBoneTransform A, B;
		if (!SampleRawRoot(Start, A) || !SampleRawRoot(End, B)) return false;
		const FMatrix RawA = MakeAnimMatrix(A.Position, A.Rotation, A.Scale);
		const FMatrix RawB = MakeAnimMatrix(B.Position, B.Rotation, B.Scale);
		Delta = LockMatrix.Inverse() * RawB * RawA.Inverse() * LockMatrix;
		return true;
	};
	if (LoopCount == 0)
	{
		if (!Segment(PreviousTime, CurrentTime, OutDelta)) return false;
	}
	else
	{
		FMatrix First, Cycle, Last;
		if (!Segment(PreviousTime, Length, First) || !Segment(0.0f, Length, Cycle) ||
			!Segment(0.0f, CurrentTime, Last)) return false;
		OutDelta = First;
		for (int32 Index = 1; Index < LoopCount; ++Index) OutDelta = Cycle * OutDelta;
		OutDelta = Last * OutDelta;
	}
	if (bUseNormalizedRootMotionScale)
	{
		FBoneTransform Normalized = FBoneTransform::FromMatrix(OutDelta);
		Normalized.Scale = FVector::OneVector;
		OutDelta = Normalized.ToMatrix();
	}
	return true;
}

bool UAnimSequence::EvaluateLocalPoseTransforms(float TimeSeconds, TArray<FAnimBoneTransform>& OutPose, bool bApplyRootLock) const
{
	if (!Skeleton || !DataModel || DataModel->NumberOfKeys <= 0 || DataModel->FrameRate.AsDecimal() <= 0.0f ||
		DataModel->BoneAnimationTracks.Num() != Skeleton->RefSkeleton.GetNum() || !std::isfinite(TimeSeconds)) return false;
	const float Sample = std::clamp(TimeSeconds * DataModel->FrameRate.AsDecimal(),
		0.0f, static_cast<float>(DataModel->NumberOfKeys - 1));
	const int32 First = static_cast<int32>(Sample);
	const int32 Second = std::min(First + 1, DataModel->NumberOfKeys - 1);
	const float Alpha = Sample - First;
	const int32 NumBones = Skeleton->RefSkeleton.GetNum();
	OutPose.SetNum(NumBones);
	for (int32 BoneIndex = 0; BoneIndex < NumBones; ++BoneIndex)
	{
		const FBoneAnimationTrack& BoneTrack = DataModel->BoneAnimationTracks[BoneIndex];
		if (BoneTrack.Name != FName(Skeleton->RefSkeleton.RefBoneInfo[BoneIndex].Name)) return false;
		const FRawAnimSequenceTrack& Track = BoneTrack.InternalTrack;
		if ((Track.PosKeys.Num() != 1 && Track.PosKeys.Num() != DataModel->NumberOfKeys) ||
			(Track.RotKeys.Num() != 1 && Track.RotKeys.Num() != DataModel->NumberOfKeys) ||
			(Track.ScaleKeys.Num() != 1 && Track.ScaleKeys.Num() != DataModel->NumberOfKeys)) return false;
		const FVector3f Position = FMath::Lerp(Track.PosKeys[Track.PosKeys.Num() == 1 ? 0 : First],
			Track.PosKeys[Track.PosKeys.Num() == 1 ? 0 : Second], Alpha);
		const FQuat4f Rotation = FMath::Slerp(Track.RotKeys[Track.RotKeys.Num() == 1 ? 0 : First],
			Track.RotKeys[Track.RotKeys.Num() == 1 ? 0 : Second], Alpha);
		const FVector3f Scale = FMath::Lerp(Track.ScaleKeys[Track.ScaleKeys.Num() == 1 ? 0 : First],
			Track.ScaleKeys[Track.ScaleKeys.Num() == 1 ? 0 : Second], Alpha);
		OutPose[BoneIndex] = { Position, Rotation, Scale };
	}
	const int32 Root = GetRootBoneIndex();
	if (bApplyRootLock && Root >= 0 && (bEnableRootMotion || bForceRootLock))
	{
		switch (RootMotionRootLock)
		{
		case ERootMotionRootLock::RefPose:
			if (Skeleton->RefSkeleton.RefBonePose.IsValidIndex(Root))
			{
				const FBoneTransform& Ref = Skeleton->RefSkeleton.RefBonePose[Root];
				OutPose[Root] = { Ref.Translation, Ref.Rotation, Ref.Scale };
			}
			break;
		case ERootMotionRootLock::AnimFirstFrame:
		{
			const FRawAnimSequenceTrack& Track = DataModel->BoneAnimationTracks[Root].InternalTrack;
			OutPose[Root] = { Track.PosKeys[0], Track.RotKeys[0], Track.ScaleKeys[0] };
			break;
		}
		case ERootMotionRootLock::Zero: OutPose[Root] = FAnimBoneTransform{}; break;
		}
	}
	return true;
}

bool UAnimSequence::EvaluateLocalPose(float TimeSeconds, TArray<FMatrix>& OutPose) const
{
	TArray<FAnimBoneTransform> Transforms;
	if (!EvaluateLocalPoseTransforms(TimeSeconds, Transforms)) return false;
	OutPose.SetNum(Transforms.Num());
	for (int32 BoneIndex = 0; BoneIndex < Transforms.Num(); ++BoneIndex)
	{
		const FAnimBoneTransform& Bone = Transforms[BoneIndex];
		OutPose[BoneIndex] = MakeAnimMatrix(Bone.Position, Bone.Rotation, Bone.Scale);
	}
	return true;
}

bool UAnimSequence::EvaluateGlobalPose(float TimeSeconds, TArray<FMatrix>& OutPose) const
{
	if (!EvaluateLocalPose(TimeSeconds, OutPose)) return false;
	for (int32 BoneIndex = 0; BoneIndex < OutPose.Num(); ++BoneIndex)
	{
		const int32 Parent = Skeleton->RefSkeleton.RefBoneInfo[BoneIndex].ParentIndex;
		if (Parent >= 0) OutPose[BoneIndex] = OutPose[BoneIndex] * OutPose[Parent];
	}
	return true;
}
