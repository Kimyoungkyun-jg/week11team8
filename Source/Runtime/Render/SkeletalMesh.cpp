#include "EnginePCH.h"
#include "Render/SkeletalMesh.h"
#include "Render/Renderer.h"
#include "Asset/AssetManager.h"
#include "Engine/PrimitiveSceneProxy.h"
#include <algorithm>
#include <cmath>
#include <limits>

USkeletalMesh::~USkeletalMesh()
{
    for (FPrimitiveSceneProxy* Proxy : RenderProxies)
    {
        Proxy->OnMeshDestroyed();
    }
    VertexBuffer = nullptr;
    IndexBuffer = nullptr;
}

UMaterial* USkeletalMesh::GetMaterial(uint32 SlotIndex) const
{
    if (SlotIndex < Materials.size() && Materials[SlotIndex])
    {
        return Materials[SlotIndex];
    }
    return UAssetManager::GetAssetByPath<UMaterial>("DefaultMaterial");
}

void USkeletalMesh::RebuildRenderData()
{
    RefSkeleton.BuildFromBones(MeshData.Bones);
	RefBasesInvMatrix.Reset();
	for (const auto& Bone : MeshData.Bones) RefBasesInvMatrix.Add(Bone.InverseBindPose);
    {
        // 행렬 -> TRS -> 행렬 왕복 오차. 크면 전단이 있거나 분해 규약이 어긋난 것이다.
        float MaxError = 0.0f;
        for (int32 Index = 0; Index < RefSkeleton.GetNum(); ++Index)
        {
            const FMatrix Rebuilt = RefSkeleton.RefBonePose[Index].ToMatrix();
            for (int32 Row = 0; Row < 4; ++Row)
                for (int32 Col = 0; Col < 4; ++Col)
                    MaxError = std::max(MaxError, std::abs(Rebuilt.M[Row][Col] - MeshData.Bones[Index].LocalBindPose.M[Row][Col]));
        }
        FString Error;
        if (RefSkeleton.GetNum() > 0 && !RefSkeleton.Validate(Error))
            HTR_LOG(Warning, "[Skeleton] {}: {}", GetPath(), Error);
        else if (MaxError > 1.0e-3f)
            HTR_LOG(Warning, "[Skeleton] {}: bind pose TRS round-trip error {:.6f}", GetPath(), MaxError);
#ifdef ENGINE_DEBUG
        else
            HTR_LOG(Info, "[Skeleton] {}: {} bones, TRS round-trip error {:.6f}", GetPath(), RefSkeleton.GetNum(), MaxError);
#endif
    }

    RenderBounds = MeshData.AABB;
    LocalLODSphere.Center = (RenderBounds.Min + RenderBounds.Max) * 0.5f;

    double RadiusSquared = 0.0;
    for (const FSkeletalVertex& Vertex : MeshData.SkinnedVertices)
    {
        const double X = static_cast<double>(Vertex.Position.X) - LocalLODSphere.Center.X;
        const double Y = static_cast<double>(Vertex.Position.Y) - LocalLODSphere.Center.Y;
        const double Z = static_cast<double>(Vertex.Position.Z) - LocalLODSphere.Center.Z;
        RadiusSquared = std::max(RadiusSquared, X * X + Y * Y + Z * Z);
    }

    LocalLODSphere.RadiusSquared = std::nextafter(
        static_cast<float>(RadiusSquared * (1.0 + 2e-5)), std::numeric_limits<float>::infinity());

    for (FPrimitiveSceneProxy* Proxy : RenderProxies)
    {
        Proxy->OnMeshRenderDataChanged();
    }
}

void USkeletalMesh::RegisterProxy(FPrimitiveSceneProxy* Proxy)
{
    RenderProxies.Add(Proxy);
}

void USkeletalMesh::UnregisterProxy(FPrimitiveSceneProxy* Proxy)
{
    const auto It = std::find(RenderProxies.begin(), RenderProxies.end(), Proxy);
    if (It != RenderProxies.end())
    {
        RenderProxies.RemoveAtSwap(static_cast<uint32>(It - RenderProxies.begin()));
    }
}
