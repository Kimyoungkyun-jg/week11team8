#pragma once

#include "Asset/RenderAsset.h"
#include "AnimationAsset.generated.h"

class USkeleton;

// 모든 애니메이션 에셋의 부모 (UE UAnimationAsset). AnimSequence와 이후의 BlendSpace가 여기서 파생한다.
// 애니메이션은 메시가 아니라 Skeleton에 맞춰 만들어진다. 같은 Skeleton을 쓰는 메시는 이 에셋을 함께 쓸 수 있다.
UCLASS()
class UAnimationAsset : public URenderAsset
{
	GENERATED_BODY()
public:
	USkeleton* GetSkeleton() const { return Skeleton; }
	void SetSkeleton(USkeleton* InSkeleton) { Skeleton = InSkeleton; }

	// 재생 길이(초)
	virtual float GetPlayLength() const { return 0.0f; }

private:
	USkeleton* Skeleton = nullptr;
};
