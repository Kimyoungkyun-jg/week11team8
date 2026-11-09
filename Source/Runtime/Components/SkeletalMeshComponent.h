#pragma once

#include "Animation/AnimSequence.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Animation/BoneTransform.h"
#include "Components/SkinnedMeshComponent.h"
#include "Render/RenderEnums.h"
#include "SkeletalMeshComponent.generated.h"

UCLASS()
class USkeletalMeshComponent : public USkinnedMeshComponent
{
	GENERATED_BODY()

public:
	USkeletalMeshComponent();
	virtual ~USkeletalMeshComponent() override = default;

	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime) override;

	virtual void SetSkeletalMesh(USkeletalMesh* InSkeletalMesh) override;

	void InitBoneTransforms();
	void SetBoneTransform(int32 BoneIndex, const FMatrix& LocalTransform);
	void SetGlobalBoneTransforms(const TArray<FMatrix>& InGlobalTransforms);

	// 모든 본의 부모 기준 로컬 포즈(위치·회전·크기)를 한 번에 적용한다. 애니메이션이 계산한 포즈가 들어오는 입구다.
	// 본 수가 메시와 다르면 아무것도 바꾸지 않고 false를 돌려준다.
	// 스키닝은 dirty 표시만 하고, 같은 틱의 TickComponent(또는 렌더·피킹이 값을 요청할 때)가 갱신한다.
	bool SetLocalPose(const TArray<FBoneTransform>& Pose);

	// 재생할 애니메이션을 정한다. nullptr이면 애니메이션을 끄고 바인드 포즈로 돌아간다.
	// Skeleton이 메시와 맞지 않으면 다음 틱에 경고를 남기고 해제한다.
	void SetAnimToPlay(UAnimSequence* InSequence) { AnimToPlay = InSequence; }
	UAnimSequence* GetAnimToPlay() const { return AnimToPlay; }
	const TArray<FMatrix>& GetBoneTransforms() const { return LocalBoneTransforms; }

	void PerformCPUSkinning();
	void UpdateSkinning();
	const TArray<FMatrix>& GetGlobalBoneTransforms() const { return GlobalBoneTransforms; }
	virtual FBox CalcLocalBounds() const override;

	virtual void SubmitToRenderQueue(FRenderQueue& RenderQueue, const FViewContext& ViewContext) override;

	virtual FPrimitiveSceneProxy* CreateSceneProxy() override;
	FVertexBuffer* GetSkinnedVertexBuffer() const { return SkinnedVertexBuffer.get(); }
	FVertexBuffer* GetSkinnedVertexBuffer(uint32 LOD);
	const TArray<FVertex>& GetSkinnedVertices() const { return SkinnedVertices; }

	virtual bool LineTraceComponent(const FRay& WorldRay, FHitResult& OutHit) override;
	virtual bool LineTraceComponentLocal(const FRay& LocalRay, float& OutT) override;
protected:
	// 비어 있으면 애니메이션을 쓰지 않아 본 포즈는 SetBoneTransform 등으로만 바뀐다(뷰어·리타게팅과 같다).
	// 재생은 월드가 컴포넌트를 틱할 때(Play) 돈다. 에디터 월드에서는 돌지 않는다.
	UPROPERTY(EditAnywhere)
	UAnimSequence* AnimToPlay = nullptr;
	UPROPERTY(EditAnywhere)
	bool bLoopAnimation = true;
	UPROPERTY(EditAnywhere)
	float AnimPlayRate = 1.0f;

private:
	// 애니메이션 인스턴스는 프로퍼티가 아니라서 씬 저장·PIE 복제에 따라가지 않는다. 틱에서 AnimToPlay를 보고 만든다.
	void TickAnimation(float DeltaTime);
	TUniquePtr<UAnimSingleNodeInstance> AnimInstance;
	USkeletalMesh* AnimInstanceMesh = nullptr;
	TArray<FBoneTransform> AnimPose;

	struct FSkinnedLOD
	{
		TArray<FVertex> Vertices;
		TUniquePtr<FVertexBuffer> Buffer;
		uint64 Revision = 0;
	};
	TArray<FSkinnedLOD> SkinnedLODs;
	uint64 PoseRevision = 1;
	void UpdateBoneMatrices();
	// 씬 로드와 복제는 프로퍼티로 메시 포인터만 채운다. 처음 쓸 때 본 상태를 만든다.
	void EnsureBoneState();
	USkeletalMesh* BoneStateMesh = nullptr;
	// 피킹용 스킨 위치. 포즈가 바뀐 뒤 처음 피킹할 때만 다시 만든다.
	void UpdatePickPositions();
	TArray<FVector> PickPositions;
	FBox PickBounds;
	uint64 PickRevision = 0;
	TArray<FMatrix> LocalBoneTransforms;
	TArray<FMatrix> GlobalBoneTransforms;
	TArray<FMatrix> SkinningMatrices;
	TArray<FVertex> SkinnedVertices;
	TUniquePtr<FVertexBuffer> SkinnedVertexBuffer;
	FBox SkinnedBounds;
	bool bSkinningMatricesDirty = true;
	bool bGlobalBoneTransformsOverridden = false;
};
