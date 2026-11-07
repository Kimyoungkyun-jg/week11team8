#include "EnginePCH.h"
#include "Render/SkeletalMesh.h"
#include "Render/Renderer.h"
#include "Asset/AssetManager.h"
#include "Engine/PrimitiveSceneProxy.h"
#include "Animation/Skeleton.h"
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
	// 로더가 임포트한 RefSkeleton(FBX에서 바로 만든 로컬 TRS)을 넣어 둔다. 행렬에서 다시 분해해 덮어쓰면 그 값을 잃는다.
	// 다른 생성 경로가 생겨 MeshData.Bones만 채워지는 경우를 정리할 때까지 남겨 둔다.
	// RefSkeleton.BuildFromBones(MeshData.Bones);
	ValidateBoneData();

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

void USkeletalMesh::ValidateBoneData() const
{
	const TArray<FBoneInfo>& Bones = MeshData.Bones;
	FString Error;
	if (!RefSkeleton.Validate(Error))
	{
		HTR_LOG(Warning, "[Skeleton] {}: {}", GetPath(), Error);
		return;
	}

	// 1. 메시의 두 본 테이블(행렬 MeshData.Bones, TRS RefSkeleton)이 같은 순서와 계층인지
	if (Bones.Num() != RefSkeleton.GetNum())
	{
		HTR_LOG(Warning, "[Skeleton] {}: MeshData has {} bones but RefSkeleton has {}", GetPath(), Bones.Num(), RefSkeleton.GetNum());
		return;
	}
	for (int32 Index = 0; Index < Bones.Num(); ++Index)
	{
		const FMeshBoneInfo& Ref = RefSkeleton.RefBoneInfo[Index];
		if (Bones[Index].Name != Ref.Name || Bones[Index].ParentIndex != Ref.ParentIndex)
		{
			HTR_LOG(Warning, "[Skeleton] {}: bone {} differs between MeshData ({}) and RefSkeleton ({})", GetPath(), Index, Bones[Index].Name, Ref.Name);
			return;
		}
	}

	// 2. 메시가 가리키는 USkeleton과 본 구조가 같은지. 애니메이션은 USkeleton 기준으로 만들고 이름으로 메시 본에 연결한다.
	if (!Skeleton)
	{
		HTR_LOG(Warning, "[Skeleton] {}: no USkeleton is linked", GetPath());
	}
	else
	{
		const FReferenceSkeleton& SkeletonRef = Skeleton->RefSkeleton;
		if (SkeletonRef.GetNum() != RefSkeleton.GetNum())
		{
			HTR_LOG(Warning, "[Skeleton] {}: USkeleton {} has {} bones but mesh has {}", GetPath(), Skeleton->GetPath(), SkeletonRef.GetNum(), RefSkeleton.GetNum());
		}
		else
		{
			for (int32 Index = 0; Index < RefSkeleton.GetNum(); ++Index)
			{
				if (SkeletonRef.RefBoneInfo[Index].Name != RefSkeleton.RefBoneInfo[Index].Name ||
					SkeletonRef.RefBoneInfo[Index].ParentIndex != RefSkeleton.RefBoneInfo[Index].ParentIndex)
				{
					HTR_LOG(Warning, "[Skeleton] {}: bone {} differs from USkeleton {}", GetPath(), RefSkeleton.RefBoneInfo[Index].Name, Skeleton->GetPath());
					break;
				}
			}
		}
	}

	// 3. 바인드 포즈에서 스키닝 행렬(InverseBind × GlobalBind)이 단위 행렬인지. 아니면 바인드 포즈에서도 메시가 움직인다.
	float MaxBindError = 0.0f;
	for (const FBoneInfo& Bone : Bones)
	{
		const FMatrix Skinning = Bone.InverseBindPose * Bone.GlobalBindPose;
		for (int32 Row = 0; Row < 4; ++Row)
		{
			for (int32 Col = 0; Col < 4; ++Col)
			{
				MaxBindError = std::max(MaxBindError, std::abs(Skinning.M[Row][Col] - (Row == Col ? 1.0f : 0.0f)));
			}
		}
	}

	// 4. 스킨 웨이트가 유효한 본을 가리키고 합이 1인지
	int32 InvalidInfluences = 0;
	float MaxWeightError = 0.0f;
	for (const FSkeletalVertex& Vertex : MeshData.SkinnedVertices)
	{
		float Sum = 0.0f;
		for (int32 Influence = 0; Influence < 4; ++Influence)
		{
			if (Vertex.BoneWeights[Influence] <= 0.0f) continue;
			if (Vertex.BoneIndices[Influence] < 0 || Vertex.BoneIndices[Influence] >= Bones.Num()) ++InvalidInfluences;
			Sum += Vertex.BoneWeights[Influence];
		}
		MaxWeightError = std::max(MaxWeightError, std::abs(Sum - 1.0f));
	}

	if (MaxBindError > 1.0e-3f)
	{
		HTR_LOG(Warning, "[Skeleton] {}: InverseBind x GlobalBind differs from identity by {:.6f}", GetPath(), MaxBindError);
	}
	if (InvalidInfluences > 0 || MaxWeightError > 1.0e-3f)
	{
		HTR_LOG(Warning, "[Skeleton] {}: {} invalid influences, weight sum error {:.6f}", GetPath(), InvalidInfluences, MaxWeightError);
	}
#ifdef ENGINE_DEBUG
	HTR_LOG(Info, "[Skeleton] {}: {} bones, skeleton {}, bind error {:.6f}, weight sum error {:.6f}",
		GetPath(), Bones.Num(), Skeleton ? Skeleton->GetPath() : FString("none"), MaxBindError, MaxWeightError);
#endif
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
