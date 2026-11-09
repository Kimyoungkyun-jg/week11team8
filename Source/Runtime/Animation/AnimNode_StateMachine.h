#pragma once

#include "Animation/AnimNode.h"

// 상태 기계 노드 (UE FAnimNode_StateMachine). 아직 뼈대만 있다. Two-Way Blend를 이해한 뒤 채운다.
//
// 큰 그림:
//  - State: 이름 + 자식 노드(SequencePlayer 또는 다른 노드 트리). 지금 상태의 자식이 포즈를 낸다.
//  - Transition: 출발 상태 → 도착 상태, 조건(인스턴스 변수 기반)과 블렌드 시간.
//  - 전환 중에는 출발/도착 상태의 포즈를 Two-Way Blend와 같은 방식으로 섞고, 끝나면 도착 상태만 남는다.
struct FAnimNode_StateMachine : FAnimNode_Base
{
	bool Evaluate(TArray<FBoneTransform>& OutPose) override
	{
		(void)OutPose;
		return false; // 구현 전
	}
};
