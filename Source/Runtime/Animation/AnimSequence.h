#pragma once

#include "Animation/AnimationAsset.h"
#include "Animation/AnimTypes.h"
#include "Animation/BoneTransform.h"
#include "AnimSequence.generated.h"

struct FReferenceSkeleton;

// 키 프레임 애니메이션 한 개 (UE UAnimSequence). FBX의 AnimStack 하나가 시퀀스 하나가 된다.
// 시간은 0초에서 시작한다. 임포트할 때 원본의 시작 시간을 빼서 맞춘다.
// 키 i의 시간은 i / FrameRate이고, 마지막 키의 시간이 재생 길이다.
// 에셋은 여러 컴포넌트가 함께 쓰므로 재생 시간이나 계산한 포즈는 여기에 두지 않는다.
UCLASS()
class UAnimSequence : public UAnimationAsset
{
	GENERATED_BODY()
public:
	float FrameRate = 30.0f;

	//프레임마다 계산된 키의 수(양 끝 프레임을 포함한다)
	//NumberOfKeys = (끝시간 − 시작시간) × FrameRate + 1
	//30프레임이라면 양 끝을 포함하여 키가 31개가 생긴다
	//(1.0초 - 0초) * 30 + 1 = 31개
	int32 NumberOfKeys = 0;
	TArray<FBoneAnimationTrack> Tracks;

	// (NumberOfKeys - 1) / FrameRate. 키가 1개 이하면 0이다.
	float GetPlayLength() const override;

	// 본 이름에 해당하는 트랙 번호. 없으면 -1
	int32 FindTrackIndex(const FName& BoneName) const;

	// 프레임 레이트, 키 수, 트랙 이름과 키 개수, 값이 유효한지 확인한다.
	bool Validate(FString& OutError) const;

	// 트랙 번호 → 대상 본 번호 표를 만든다. 대상에 없는 본의 트랙은 -1이다.
	// 대상 본 이름으로 FName 해시맵을 만들어 트랙마다 한 번에 찾는다(본 수 + 트랙 수 만큼).
	// 매 프레임 할 일은 아니므로 시퀀스와 대상 메시가 정해질 때 한 번 만들고 재사용한다.
	void BuildTrackToBoneMap(const FReferenceSkeleton& Target, TArray<int32>& OutTrackToBone) const;

	// 트랙 하나의 Time초 자세(부모 기준 로컬). Time은 [0, PlayLength]로 자른다.
	// 키 사이는 위치·크기 Lerp, 회전 Slerp로 보간하고, 키가 1개인 성분(상수 트랙)은 그 값을 그대로 쓴다.
	// 키가 없는 성분은 Fallback의 값을 쓴다.
	FBoneTransform GetBoneTransform(int32 TrackIndex, float Time, const FBoneTransform& Fallback = FBoneTransform::Identity) const;

	// 대상 스켈레톤 전체의 Time초 로컬 포즈. 대상의 RefBonePose에서 시작해 트랙이 있는 본만 덮어쓴다.
	// TrackToBone은 BuildTrackToBoneMap으로 같은 대상에 대해 만든 표여야 한다.
	void GetBonePose(float Time, const FReferenceSkeleton& Target, const TArray<int32>& TrackToBone, TArray<FBoneTransform>& OutPose) const;
};
