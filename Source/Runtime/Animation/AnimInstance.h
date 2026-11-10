#pragma once
#include "Animation/AnimNode_SequencePlayer.h"
#include "Animation/AnimNode_TwoWayBlend.h"
#include "UObject/Object.h"

class UAnimInstance : public UObject
{
public:
	UAnimInstance() : UObject(false) {}
	// 링크가 이 인스턴스에 속한 노드의 주소를 참조한다.
	UAnimInstance(const UAnimInstance&) = delete;
	UAnimInstance& operator=(const UAnimInstance&) = delete;

	void Play(const UAnimSequence* SequenceA, const UAnimSequence* SequenceB, bool bLoop, float InAlpha);
	void SetBlendAlpha(float InAlpha);
	virtual void NativeInitializeAnimation();
	virtual void NativeUpdateAnimation(float DeltaSeconds);
	bool EvaluateLocalPose(const USkeletalMesh& Mesh, TArray<FMatrix>& OutLocalPose);

private:
	FAnimNode_SequencePlayer SequencePlayerA;
	FAnimNode_SequencePlayer SequencePlayerB;
	FAnimNode_TwoWayBlend TwoWayBlend;
	FPoseLink Root;
};
