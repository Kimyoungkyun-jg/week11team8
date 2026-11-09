#pragma once

#include "Animation/AnimInstance.h"
#include "Animation/AnimNode_SequencePlayer.h"
#include "Animation/AnimNode_TwoWayBlend.h"
#include "AnimTwoWayBlendInstance.generated.h"

// 시퀀스 둘을 알파로 섞는 고정 그래프 인스턴스. Two-Way Blend를 Details에서 시험하기 위한 임시 구성이다.
// 루트 TwoWayBlend(A = PlayerA, B = PlayerB). 알파는 인스턴스 변수 "BlendAlpha"에서 읽으므로
// 게임플레이는 SetFloat(BlendAlphaName, v)로 바꾼다. State Machine이 생기면 크로스페이드가 같은 노드를 내부에서 쓰게 된다.
UCLASS()
class UAnimTwoWayBlendInstance : public UAnimInstance
{
	GENERATED_BODY()
public:
	UAnimTwoWayBlendInstance();

	static const char* BlendAlphaName() { return "BlendAlpha"; }

	// 재생할 시퀀스를 정하고 처음(0초)부터 재생한다. 메시가 바뀌면 같은 시퀀스로 다시 불러야 연결표가 갱신된다.
	void SetAnimationA(UAnimSequence* InSequence);
	void SetAnimationB(UAnimSequence* InSequence);
	UAnimSequence* GetAnimationA() const { return PlayerA->GetSequence(); }
	UAnimSequence* GetAnimationB() const { return PlayerB->GetSequence(); }

	// 두 플레이어에 함께 적용한다.
	void SetLooping(bool bInLooping) { PlayerA->SetLooping(bInLooping); PlayerB->SetLooping(bInLooping); }
	void SetPlayRate(float InPlayRate) { PlayerA->SetPlayRate(InPlayRate); PlayerB->SetPlayRate(InPlayRate); }

private:
	// 노드 소유는 TwoWayBlend 루트가 한다.
	FAnimNode_SequencePlayer* PlayerA = nullptr;
	FAnimNode_SequencePlayer* PlayerB = nullptr;
};
