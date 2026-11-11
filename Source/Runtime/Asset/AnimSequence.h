#pragma once

#include "Asset/AnimNotifyEvent.h"
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
	TArray<FAnimNotifyEvent> Notifies;

	// 이번 프레임에 플레이어가 진행한 구간에 든 Notify를 OutNotifies 뒤에 추가한다 (비우지 않는다).
	// 구간 규칙: 기본은 (PreviousTime, CurrentTime]. 루프를 넘으면 (PreviousTime, Length], 전체 주기 [0, Length] x (LoopCount - 1), [0, CurrentTime].
	// bIncludeStart가 true면 첫 구간의 시작도 포함한다 (재생을 막 시작한 첫 Update에서 0초 Notify를 놓치지 않기 위해).
	void GetNotifies(float PreviousTime, float CurrentTime, int32 LoopCount, bool bIncludeStart, TArray<FAnimNotifyEvent>& OutNotifies) const;

	int32 GetFrameIndex(float TimeSeconds) const;
	int32 GetRootBoneIndex() const;
	bool SampleRawRoot(float TimeSeconds, FAnimBoneTransform& OutRoot) const;
	bool ExtractRootMotion(float PreviousTime, float CurrentTime, int32 LoopCount, FMatrix& OutDelta) const;
	bool EvaluateLocalPoseTransforms(float TimeSeconds, TArray<FAnimBoneTransform>& OutPose, bool bApplyRootLock = true) const;
	bool EvaluateLocalPose(float TimeSeconds, TArray<FMatrix>& OutPose) const;
	// 본 변환은 스켈레탈 메시 컴포넌트 공간 기준
	bool EvaluateGlobalPose(float TimeSeconds, TArray<FMatrix>& OutPose) const;
};
