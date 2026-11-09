#pragma once

#include "Containers/Array.h"
#include "Animation/BoneTransform.h"
#include "Animation/ReferenceSkeleton.h"

class UAnimInstance;

// 애니메이션 그래프의 노드 (UE FAnimNode_Base). 노드는 포즈를 만드는 연산 하나이고, 자식 노드를 가질 수 있어 트리로 조합된다.
// UObject가 아니라 일반 구조체다. 소유는 부모 노드(또는 루트라면 UAnimInstance)가 TUniquePtr로 한다.
//
// 호출 순서: Initialize(메시가 정해지거나 바뀔 때) → 매 틱 Update(시간 진행) → Evaluate(로컬 포즈 계산)
// 노드는 소유 컴포넌트를 직접 찾지 않는다. 필요한 것은 컨텍스트로 받는다.

struct FAnimInitContext
{
	UAnimInstance* Instance = nullptr;
	// 대상 메시의 본 테이블. Evaluate가 채우는 포즈의 본 수와 순서의 기준이다. 메시가 없으면 nullptr
	const FReferenceSkeleton* RefSkeleton = nullptr;
};

struct FAnimUpdateContext
{
	UAnimInstance* Instance = nullptr;
	float DeltaTime = 0.0f;
};

struct FAnimNode_Base
{
	virtual ~FAnimNode_Base() = default;

	// 메시가 정해지거나 바뀔 때 부른다. 자식이 있으면 자식에게도 전달해야 한다.
	virtual void Initialize(const FAnimInitContext& Context) { (void)Context; }

	// 시간을 진행한다. 자식이 있으면 자식에게도 전달해야 한다.
	virtual void Update(const FAnimUpdateContext& Context) { (void)Context; }

	// 부모 기준 로컬 포즈를 OutPose에 채운다. 본 수와 순서는 Initialize 때 받은 RefSkeleton과 같다.
	// 계산할 수 없으면(시퀀스 없음 등) false를 돌려주고 OutPose는 건드리지 않는다.
	virtual bool Evaluate(TArray<FBoneTransform>& OutPose) = 0;
};
