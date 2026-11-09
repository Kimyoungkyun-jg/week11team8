#pragma once

#include "Animation/AnimNode.h"
#include "Animation/AnimSequence.h"

// 시퀀스 하나를 자기 시간으로 재생해 포즈를 내는 말단 노드 (UE FAnimNode_SequencePlayer).
// 재생 시간은 노드가 갖는다. 같은 시퀀스를 두 노드가 재생해도 서로의 시간은 독립이다.
struct FAnimNode_SequencePlayer : FAnimNode_Base
{
	// 재생할 시퀀스를 정하고 처음(0초)부터 재생한다. 트랙 → 본 연결표를 다시 만든다.
	void SetSequence(UAnimSequence* InSequence);
	UAnimSequence* GetSequence() const { return Sequence; }

	void Play(bool bInLooping) { bLooping = bInLooping; bPlaying = true; }
	void Stop() { bPlaying = false; }
	bool IsPlaying() const { return bPlaying; }

	void SetLooping(bool bInLooping) { bLooping = bInLooping; }
	void SetPlayRate(float InPlayRate) { PlayRate = InPlayRate; }

	// 현재 시간(초). [0, PlayLength]로 자른다.
	void SetPosition(float InTime);
	float GetPosition() const { return CurrentTime; }

	void Initialize(const FAnimInitContext& Context) override;
	void Update(const FAnimUpdateContext& Context) override;
	bool Evaluate(TArray<FBoneTransform>& OutPose) override;

private:
	void RebuildTrackMap();

	UAnimSequence* Sequence = nullptr;
	const FReferenceSkeleton* RefSkeleton = nullptr;
	// Sequence의 트랙 번호 → 메시 본 번호. 이름 비교는 시퀀스나 메시가 정해질 때 한 번만 한다.
	TArray<int32> TrackToBone;
	float CurrentTime = 0.0f;
	float PlayRate = 1.0f;
	bool bLooping = true;
	bool bPlaying = false;
};
