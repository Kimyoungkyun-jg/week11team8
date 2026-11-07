#pragma once

#include "Asset/RenderAsset.h"
#include "Render/Buffer.h"
#include "Render/SkeletalMeshData.h"
#include "Render/MeshRenderData.h"
#include "Animation/ReferenceSkeleton.h"
#include <unordered_map>
#include <vector>
#include <array>
#include "SkeletalMesh.generated.h"

class FPrimitiveSceneProxy;
class USkeleton;

struct FSkeletalMeshLODResource
{
	FSkeletalMeshData Data;
	TUniquePtr<FVertexBuffer> VertexBuffer;
	TUniquePtr<FIndexBuffer> IndexBuffer;
};

UCLASS()
class USkeletalMesh : public URenderAsset
{
    GENERATED_BODY()

public:
    USkeletalMesh() : SortID(NextSortID++) {}
    virtual ~USkeletalMesh() override;

	uint32 GetLODCount() const { return 1u + static_cast<uint32>(AdditionalLODs.Num()); }
	const FSkeletalMeshData& GetMeshData(uint32 LOD) const { return LOD == 0 ? MeshData : AdditionalLODs[LOD - 1].Data; }
	FVertexBuffer* GetVertexBuffer(uint32 LOD) const { return LOD == 0 ? VertexBuffer.get() : AdditionalLODs[LOD - 1].VertexBuffer.get(); }
	FIndexBuffer* GetIndexBuffer(uint32 LOD) const { return LOD == 0 ? IndexBuffer.get() : AdditionalLODs[LOD - 1].IndexBuffer.get(); }

    const FReferenceSkeleton& GetRefSkeleton() const { return RefSkeleton; }

    const FSkeletalMeshData& GetMeshData() const { return MeshData; }
    UMaterial* GetMaterial(uint32 SlotIndex) const;

    void RebuildRenderData();
    // 본 테이블, USkeleton, 바인드 행렬, 스킨 웨이트가 서로 맞는지 확인하고 로그로 남긴다.
    void ValidateBoneData() const;
    const FLODSphere& GetLocalLODSphere() const { return LocalLODSphere; }
    const FBox& GetRenderBounds() const { return RenderBounds; }
    void RegisterProxy(FPrimitiveSceneProxy* Proxy);
    void UnregisterProxy(FPrimitiveSceneProxy* Proxy);


	USkeleton* Skeleton = nullptr;

    // Bones에 이름, 부모, local/Global/Inverse 바인드 행렬 있음.
    FSkeletalMeshData MeshData;
	TArray<FSkeletalMeshLODResource> AdditionalLODs;

    const uint16 SortID;

	std::array<float, 3> ScreenThresholds{ .30f, .10f, .03f };

    // 이 메시의 본 테이블과 바인드 포즈(부모 기준 로컬 위치, 회전, 크기) : Global 행렬 아님.
    // 로더가 임포트 데이터로 채운다.
    // 애니메이션 포즈 계산은 이것을 쓴다. MeshData.Bones는 같은 순서의 행렬 형태다.
    FReferenceSkeleton RefSkeleton;

    // Renderer가 바인드할 런타임 머티리얼 목록
    TArray<UMaterial*> Materials;

    TUniquePtr<FVertexBuffer> VertexBuffer;
    TUniquePtr<FIndexBuffer> IndexBuffer;

private:
    static inline uint16 NextSortID = 0;

    FLODSphere LocalLODSphere;
    FBox RenderBounds;
    TArray<FPrimitiveSceneProxy*> RenderProxies;
};
