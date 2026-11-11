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

	// Notify 클래스 이름(예: "UAnimNotify_Log")으로 객체를 만들어 TriggerTime에 추가하고 그 객체를 돌려준다.
	// 클래스를 찾을 수 없거나 추상 클래스이거나 UAnimNotify가 아니면 로그를 남기고 nullptr을 돌려준다.
	// 속성은 돌려받은 객체에 설정한다. 저장·로드와 에디터 편집도 이 함수로 Notify를 만든다.
	UAnimNotify* AddNotify(const FString& EventName, const FString& NotifyClassName, float TriggerTime);

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
