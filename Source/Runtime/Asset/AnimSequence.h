#pragma once

#include "Asset/AnimSequenceBase.h"
#include "Asset/AnimTransform.h"
#include "Math/Matrix.h"
#include "AnimSequence.generated.h"

enum class ERootMotionRootLock : uint8 { RefPose, AnimFirstFrame, Zero };

UCLASS()
class UAnimSequence : public UAnimSequenceBase
{
	GENERATED_BODY()
public:
	FString Name;
	FString SourceFilePath;
	int32 SourceTakeIndex = 0;
	bool bEnableRootMotion = false;
	bool bForceRootLock = false;
	bool bUseNormalizedRootMotionScale = false;
	ERootMotionRootLock RootMotionRootLock = ERootMotionRootLock::RefPose;
	float RateScale = 1.0f;

	int32 GetFrameIndex(float TimeSeconds) const;
	int32 GetRootBoneIndex() const;
	bool SampleRawRoot(float TimeSeconds, FAnimBoneTransform& OutRoot) const;
	bool ExtractRootMotion(float PreviousTime, float CurrentTime, int32 LoopCount, FMatrix& OutDelta) const;
	bool EvaluateLocalPoseTransforms(float TimeSeconds, TArray<FAnimBoneTransform>& OutPose, bool bApplyRootLock = true) const;
	bool EvaluateLocalPose(float TimeSeconds, TArray<FMatrix>& OutPose) const;
	// 본 변환은 스켈레탈 메시 컴포넌트 공간 기준
	bool EvaluateGlobalPose(float TimeSeconds, TArray<FMatrix>& OutPose) const;
};
