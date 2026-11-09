#pragma once

#include "Animation/AnimInstance.h"
#include "Animation/AnimSequence.h"
#include "AnimSingleNodeInstance.generated.h"

// 시퀀스 하나를 재생하는 가장 단순한 인스턴스 (UE UAnimSingleNodeInstance).
// 현재 시간(CurrentTime)과 재생 설정을 들고 있고, 트랙 → 본 연결표를 캐시한다.
UCLASS()
class UAnimSingleNodeInstance : public UAnimInstance
{
	GENERATED_BODY()
public:
	// 재생할 시퀀스를 정하고 처음(0초)부터 재생한다. 연결표는 소유 컴포넌트의 현재 메시 기준으로 만든다.
	// 메시가 바뀌면 같은 시퀀스로 다시 불러야 연결표가 갱신된다.
	void SetAnimationAsset(UAnimSequence* InSequence);
	UAnimSequence* GetAnimationAsset() const { return Sequence; }

	void Play(bool bInLooping) { bLooping = bInLooping; bPlaying = true; }
	void Stop() { bPlaying = false; }
	bool IsPlaying() const { return bPlaying; }

	void SetLooping(bool bInLooping) { bLooping = bInLooping; }
	void SetPlayRate(float InPlayRate) { PlayRate = InPlayRate; }

	// 현재 시간(초). [0, PlayLength]로 자른다.
	void SetPosition(float InTime);
	float GetPosition() const { return CurrentTime; }

	void UpdateAnimation(float DeltaTime) override;
	bool EvaluateAnimation(TArray<FBoneTransform>& OutPose) override;

private:
	UAnimSequence* Sequence = nullptr;
	// Sequence의 트랙 번호 → 메시 본 번호. 이름 비교는 시퀀스를 정할 때 한 번만 한다.
	TArray<int32> TrackToBone;
	float CurrentTime = 0.0f;
	float PlayRate = 1.0f;
	bool bLooping = true;
	bool bPlaying = false;
};
