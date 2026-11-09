#pragma once

#include "Animation/AnimNode.h"
#include "Core/Types.h"

// 자식 포즈 둘을 Alpha로 섞는 노드 (UE FAnimNode_TwoWayBlend). Alpha 0이면 A, 1이면 B다.
// 본마다 FBoneTransform::Blend(위치·크기 Lerp, 회전 Slerp)를 쓴다.
//
// 시간 정책: 가중치와 상관없이 두 자식을 항상 Update한다. Alpha가 0이어도 B의 시간은 흐르므로, Alpha를 올리면 B는 지금 위상에서 보인다.
// (UE는 가중치가 0인 자식을 건너뛰지만 여기서는 단순하게 간다. 길이가 다른 두 동작의 위상 맞춤은 Sync Group의 몫이다.)
struct FAnimNode_TwoWayBlend : FAnimNode_Base
{
	TUniquePtr<FAnimNode_Base> A;
	TUniquePtr<FAnimNode_Base> B;
	float Alpha = 0.0f;
	// 비어 있지 않으면 매 Update에 인스턴스 변수 이 이름의 값을 Alpha로 읽는다. 변수가 아직 없으면 Alpha를 그대로 둔다.
	// 게임플레이와 Details가 같은 변수 하나로 알파를 정하게 하는 연결이다.
	FString AlphaVariable;

	void Initialize(const FAnimInitContext& Context) override;
	void Update(const FAnimUpdateContext& Context) override;
	bool Evaluate(TArray<FBoneTransform>& OutPose) override;

private:
	const FReferenceSkeleton* RefSkeleton = nullptr;
	// 프레임마다 할당하지 않도록 자식 포즈 버퍼를 들고 있는다.
	TArray<FBoneTransform> PoseA;
	TArray<FBoneTransform> PoseB;
};
