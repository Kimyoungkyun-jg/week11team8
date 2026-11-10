#pragma once

#include "Components/SkinnedMeshComponent.h"
#include "Animation/AnimInstance.h"
#include "Asset/AnimSequence.h"
#include "Render/RenderEnums.h"
#include "Serialization/StructuredArchive.h"
#include "SkeletalMeshComponent.generated.h"

UCLASS()
class USkeletalMeshComponent : public USkinnedMeshComponent
{
	GENERATED_BODY()

public:
	USkeletalMeshComponent();
	virtual ~USkeletalMeshComponent() override;

	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime) override;
	virtual void Serialize(FStructuredArchive::FRecord Record) override;

	virtual void SetSkeletalMesh(USkeletalMesh* InSkeletalMesh) override;
	bool SetAnimInstanceClass(UClass* InAnimClass);
	bool SetAnimGraphPath(const FString& InPath);
	const FString& GetAnimGraphPath() const { return AnimGraphPath; }
	UClass* GetAnimInstanceClass() const { return AnimClass; }
	UAnimInstance* GetAnimInstance() const { return AnimScriptInstance; }
	bool SetAnimationSequence(UAnimSequence* InSequence);
	UAnimSequence* GetAnimationSequence() const { return AnimSequence; }

	void InitBoneTransforms();
	void SetBoneTransform(int32 BoneIndex, const FMatrix& LocalTransform);
	void SetGlobalBoneTransforms(const TArray<FMatrix>& InGlobalTransforms);
	const TArray<FMatrix>& GetBoneTransforms() const { return LocalBoneTransforms; }

	void PerformCPUSkinning();
	void UpdateSkinning();
	static void SetSkinningMode(ESkinningMode Mode) { SkinningMode = Mode; }
	static ESkinningMode GetSkinningMode() { return SkinningMode; }
	const TArray<FMatrix>& GetGlobalBoneTransforms() const { return GlobalBoneTransforms; }
	virtual FBox CalcLocalBounds() const override;

	virtual void SubmitToRenderQueue(FRenderQueue& RenderQueue, const FViewContext& ViewContext) override;

	virtual FPrimitiveSceneProxy* CreateSceneProxy() override;
	FVertexBuffer* GetSkinnedVertexBuffer() const { return SkinnedVertexBuffer.get(); }
	FVertexBuffer* GetSkinnedVertexBuffer(uint32 LOD);
	FStructuredBuffer* GetSkinningMatricesBuffer() const { return AppliedMode == ESkinningMode::GPU ? SkinningMatricesBuffer.get() : nullptr; }
	const TArray<FVertex>& GetSkinnedVertices() const { return SkinnedVertices; }

	virtual bool LineTraceComponent(const FRay& WorldRay, FHitResult& OutHit) override;
	virtual bool LineTraceComponentLocal(const FRay& LocalRay, float& OutT) override;
private:
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
	// 피킹용 스킨 위치. GPU 스키닝에서도 CPU에서 계산하며 포즈가 바뀐 뒤 처음 피킹할 때만 다시 만든다.
	void UpdatePickPositions();
	TArray<FVector> PickPositions;
	FBox PickBounds;
	uint64 PickRevision = 0;
	UPROPERTY(EditAnywhere, Transient)
	UClass* AnimClass = nullptr;
	UPROPERTY(EditAnywhere, Transient)
	FString AnimGraphPath;
	UPROPERTY(Transient)
	UAnimInstance* AnimScriptInstance = nullptr;
	UPROPERTY(EditAnywhere, Transient)
	UAnimSequence* AnimSequence = nullptr;
	inline static ESkinningMode SkinningMode = ESkinningMode::CPU;
	ESkinningMode AppliedMode = ESkinningMode::CPU;
	TArray<FMatrix> LocalBoneTransforms;
	TArray<FMatrix> GlobalBoneTransforms;
	TArray<FMatrix> SkinningMatrices;
	TArray<FVertex> SkinnedVertices;
	TUniquePtr<FVertexBuffer> SkinnedVertexBuffer;
	TUniquePtr<FStructuredBuffer> SkinningMatricesBuffer;
	FBox SkinnedBounds;
	bool bSkinningMatricesDirty = true;
	bool bGlobalBoneTransformsOverridden = false;
};
