#pragma once

#include "Animation/AnimInstance.h"
#include "Animation/AnimNode_SequencePlayer.h"
#include "AnimSingleNodeInstance.generated.h"

// 시퀀스 하나를 재생하는 가장 단순한 인스턴스 (UE UAnimSingleNodeInstance).
// 루트 노드가 SequencePlayer 하나다. 재생 시간과 연결표는 그 노드가 갖고, 이 클래스는 노드를 조작하는 창구다.
UCLASS()
class UAnimSingleNodeInstance : public UAnimInstance
{
	GENERATED_BODY()
public:
	UAnimSingleNodeInstance();

	// 재생할 시퀀스를 정하고 처음(0초)부터 재생한다. 연결표는 소유 컴포넌트의 현재 메시 기준으로 만든다.
	// 메시가 바뀌면 같은 시퀀스로 다시 불러야 연결표가 갱신된다.
	void SetAnimationAsset(UAnimSequence* InSequence);
	UAnimSequence* GetAnimationAsset() const { return Player->GetSequence(); }

	void Play(bool bInLooping) { Player->Play(bInLooping); }
	void Stop() { Player->Stop(); }
	bool IsPlaying() const { return Player->IsPlaying(); }

	void SetLooping(bool bInLooping) { Player->SetLooping(bInLooping); }
	void SetPlayRate(float InPlayRate) { Player->SetPlayRate(InPlayRate); }

	void SetPosition(float InTime) { Player->SetPosition(InTime); }
	float GetPosition() const { return Player->GetPosition(); }

private:
	// 루트 노드이기도 하다. 소유는 UAnimInstance가 한다.
	FAnimNode_SequencePlayer* Player = nullptr;
};
