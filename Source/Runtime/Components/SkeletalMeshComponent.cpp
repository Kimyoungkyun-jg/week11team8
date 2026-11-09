#include "EnginePCH.h"
#include "Components/SkeletalMeshComponent.h"
#include "Render/RenderCommand.h"
#include "Render/RenderPacket.h"
#include "Collision/Ray.h"
#include "Engine/PrimitiveSceneProxy.h"
#include "Asset/LOD/StaticMeshLODSelector.h"

namespace
{
	// 섞인 Bone Matrix의 0~2열. Affine이므로 3열은 (0,0,0,1)이므로 계산하지 않음
	using FBlendMatrix = float[4][3];

	// 점 변환 (행 벡터): pxM = x*Row0 + y*Row1 + z*Row2 + Row3
	inline void TransformPoint(const FBlendMatrix& B, const FVector& P, FVector& Out)
	{
		Out.X = P.X * B[0][0] + P.Y * B[1][0] + P.Z * B[2][0] + B[3][0];
		Out.Y = P.X * B[0][1] + P.Y * B[1][1] + P.Z * B[2][1] + B[3][1];
		Out.Z = P.X * B[0][2] + P.Y * B[1][2] + P.Z * B[2][2] + B[3][2];
	}

	// 방향 변환: 이동(Row3) 없이 변환한 뒤 정규화. 너무 짧으면 0.
	inline void TransformDirection(const FBlendMatrix& B, float X, float Y, float Z, float* Out)
	{
		const float RX = X * B[0][0] + Y * B[1][0] + Z * B[2][0];
		const float RY = X * B[0][1] + Y * B[1][1] + Z * B[2][1];
		const float RZ = X * B[0][2] + Y * B[1][2] + Z * B[2][2];
		const float LengthSq = RX * RX + RY * RY + RZ * RZ;
		const float InvLength = LengthSq > SMALL_NUMBER ? 1.0f / std::sqrt(LengthSq) : 0.0f;
		Out[0] = RX * InvLength;
		Out[1] = RY * InvLength;
		Out[2] = RZ * InvLength;
	}
}

USkeletalMeshComponent::USkeletalMeshComponent()
{
	PrimaryComponentTick.bCanEverTick = true;   // 애니메이션 재생. AnimToPlay가 비어 있으면 TickAnimation은 바로 끝난다.
}

void USkeletalMeshComponent::BeginPlay()
{
    Super::BeginPlay();
    InitBoneTransforms();
}

void USkeletalMeshComponent::TickComponent(float DeltaTime)
{
	Super::TickComponent(DeltaTime);

	// 순서: 시간 진행 → 포즈 계산 → SetLocalPose(dirty) → 아래에서 스키닝. 같은 틱 안에서 끝나므로 한 프레임 늦지 않는다.
	TickAnimation(DeltaTime);

	if (!SkinningMatrices.IsEmpty() && bSkinningMatricesDirty)
	{
		UpdateSkinning();
	}
}

void USkeletalMeshComponent::SetSkeletalMesh(USkeletalMesh* InSkeletalMesh)
{
	Super::SetSkeletalMesh(InSkeletalMesh);
	InitBoneTransforms();
}

void USkeletalMeshComponent::EnsureBoneState()
{
	if (BoneStateMesh != SkeletalMesh) InitBoneTransforms();
}

void USkeletalMeshComponent::InitBoneTransforms()
{
	BoneStateMesh = SkeletalMesh;
	SkinnedVertexBuffer.reset();
	SkinnedLODs.Reset();
	++PoseRevision;
	LocalBoneTransforms.Reset();
	GlobalBoneTransforms.Reset();
	SkinningMatrices.Reset();
	SkinnedVertices.Reset();
	bSkinningMatricesDirty = false;
	bGlobalBoneTransformsOverridden = false;
	if (!SkeletalMesh) return;
	SkinnedLODs.SetNum(SkeletalMesh->AdditionalLODs.Num());

	const TArray<FBoneInfo>& Bones = SkeletalMesh->MeshData.Bones;
	int32 BoneCount = Bones.Num();

	LocalBoneTransforms.SetNum(BoneCount);
	GlobalBoneTransforms.SetNum(BoneCount);
	SkinningMatrices.SetNum(BoneCount);
	SkinnedBounds = SkeletalMesh->MeshData.AABB;
	if (BoneCount == 0)
	{
		const TArray<FSkeletalVertex>& Vertices = SkeletalMesh->MeshData.SkinnedVertices;
		SkinnedVertices.SetNum(Vertices.Num());
		for (int32 Index = 0; Index < Vertices.Num(); ++Index)
		{
			SkinnedVertices[Index].Position = Vertices[Index].Position;
			SkinnedVertices[Index].Normal = Vertices[Index].Normal;
			SkinnedVertices[Index].Tangent = Vertices[Index].Tangent;
			SkinnedVertices[Index].UV = Vertices[Index].UV;
			SkinnedVertices[Index].Color = Vertices[Index].Color;
		}
		SkinnedVertexBuffer = RenderCommand::CreateDynamicVertexBuffer(
			static_cast<uint32>(Vertices.Num() * sizeof(FVertex)), sizeof(FVertex));
		if (SkinnedVertexBuffer)
			RenderCommand::UpdateBufferData(SkinnedVertexBuffer.get(), SkinnedVertices.GetData(),
				static_cast<uint32>(Vertices.Num() * sizeof(FVertex)));
		return;
	}
	for (int32 i = 0; i < BoneCount; ++i)
	{
		LocalBoneTransforms[i] = Bones[i].LocalBindPose;
		GlobalBoneTransforms[i] = Bones[i].GlobalBindPose;
		SkinningMatrices[i] = FMatrix::Identity; // 초기 상태는 T-Pose
	}

	bSkinningMatricesDirty = true;
	UpdateSkinning();
}

void USkeletalMeshComponent::ResetAnimInstance()
{
	AnimInstance.reset();
	SingleInstance = nullptr;
	BlendInstance = nullptr;
	AnimInstanceMesh = nullptr;
	PushedBlendAlpha = std::numeric_limits<float>::quiet_NaN();
}

void USkeletalMeshComponent::TickAnimation(float DeltaTime)
{
	if (!AnimToPlay)
	{
		// 애니메이션을 껐으면 마지막 자세에 멈추지 않고 바인드 포즈로 돌린다. 켠 적이 없으면 아무것도 하지 않는다.
		if (AnimInstance)
		{
			ResetAnimInstance();
			if (SkeletalMesh)
			{
				SetLocalPose(SkeletalMesh->RefSkeleton.RefBonePose);
			}
		}
		return;
	}

	EnsureBoneState();
	if (!SkeletalMesh)
	{
		return;
	}

	// 애니메이션은 Skeleton에 맞춰 만들어진다. 지금은 포인터로 비교한다(부분 재임포트 뒤에는 옛 포인터가 남을 수 있다).
	if (AnimToPlay->GetSkeleton() != SkeletalMesh->Skeleton)
	{
		HTR_LOG(Warning, "[Anim] {}: Skeleton does not match mesh {}; animation cleared",
			AnimToPlay->GetPath(), SkeletalMesh->GetPath());
		AnimToPlay = nullptr;
		return;
	}
	if (AnimToBlend && AnimToBlend->GetSkeleton() != SkeletalMesh->Skeleton)
	{
		HTR_LOG(Warning, "[Anim] {}: Skeleton does not match mesh {}; blend animation cleared",
			AnimToBlend->GetPath(), SkeletalMesh->GetPath());
		AnimToBlend = nullptr;
	}

	// 인스턴스 종류가 달라졌으면(AnimToBlend를 넣거나 뺌) 새로 만든다. 변수는 새 인스턴스에서 처음부터 시작한다.
	const bool bWantBlend = AnimToBlend != nullptr;
	if (AnimInstance && (bWantBlend ? BlendInstance == nullptr : SingleInstance == nullptr))
	{
		ResetAnimInstance();
	}
	if (!AnimInstance)
	{
		if (bWantBlend)
		{
			BlendInstance = NewObject<UAnimTwoWayBlendInstance>(this, FName("AnimInstance"));
			AnimInstance.reset(BlendInstance);
		}
		else
		{
			SingleInstance = NewObject<UAnimSingleNodeInstance>(this, FName("AnimInstance"));
			AnimInstance.reset(SingleInstance);
		}
		AnimInstance->Initialize(this);
	}

	// 시퀀스나 메시가 바뀐 쪽만 다시 맞춘다. 트랙 연결표는 이때만 만든다.
	const bool bMeshChanged = AnimInstanceMesh != SkeletalMesh;
	AnimInstanceMesh = SkeletalMesh;
	if (SingleInstance)
	{
		if (bMeshChanged || SingleInstance->GetAnimationAsset() != AnimToPlay)
		{
			SingleInstance->SetAnimationAsset(AnimToPlay);
		}
		// 재생 설정은 프로퍼티가 진실이다. 매 틱 넘겨 두면 Details에서 바꾼 값이 바로 반영된다.
		SingleInstance->SetLooping(bLoopAnimation);
		SingleInstance->SetPlayRate(AnimPlayRate);
	}
	else
	{
		if (bMeshChanged || BlendInstance->GetAnimationA() != AnimToPlay)
		{
			BlendInstance->SetAnimationA(AnimToPlay);
		}
		if (bMeshChanged || BlendInstance->GetAnimationB() != AnimToBlend)
		{
			BlendInstance->SetAnimationB(AnimToBlend);
		}
		BlendInstance->SetLooping(bLoopAnimation);
		BlendInstance->SetPlayRate(AnimPlayRate);

		// 알파는 프로퍼티 값이 바뀐 틱에만 인스턴스 변수로 넘긴다. 매 틱 넘기면 게임플레이가 쓴 값을 덮어쓴다.
		if (BlendAlpha != PushedBlendAlpha)
		{
			BlendInstance->SetFloat(UAnimTwoWayBlendInstance::BlendAlphaName(), BlendAlpha);
			PushedBlendAlpha = BlendAlpha;
		}
	}

	AnimInstance->UpdateAnimation(DeltaTime);
	if (AnimInstance->EvaluateAnimation(AnimPose))
	{
		SetLocalPose(AnimPose);
	}
}

void USkeletalMeshComponent::SetBoneTransform(int32 BoneIndex, const FMatrix& LocalTransform)
{
	if (BoneIndex < 0 || BoneIndex >= LocalBoneTransforms.Num())
	{
		return;
	}

	if (bGlobalBoneTransformsOverridden)
	{
		const TArray<FBoneInfo>& Bones = SkeletalMesh->MeshData.Bones;
		for (int32 Index = 0; Index < Bones.Num(); ++Index)
		{
			const int32 Parent = Bones[Index].ParentIndex;
			LocalBoneTransforms[Index] = Parent < 0 ? GlobalBoneTransforms[Index] :
				GlobalBoneTransforms[Index] * GlobalBoneTransforms[Parent].Inverse();
		}
		bGlobalBoneTransformsOverridden = false;
	}
	LocalBoneTransforms[BoneIndex] = LocalTransform;
	bSkinningMatricesDirty = true;
}

void USkeletalMeshComponent::SetGlobalBoneTransforms(const TArray<FMatrix>& InGlobalTransforms)
{
	if (!SkeletalMesh || InGlobalTransforms.Num() != GlobalBoneTransforms.Num()) return;
	GlobalBoneTransforms = InGlobalTransforms;
	bGlobalBoneTransformsOverridden = true;
	bSkinningMatricesDirty = true;
}

bool USkeletalMeshComponent::SetLocalPose(const TArray<FBoneTransform>& Pose)
{
	EnsureBoneState();
	if (!SkeletalMesh || Pose.IsEmpty() || Pose.Num() != LocalBoneTransforms.Num())
	{
		return false;
	}
	// 메시의 LocalBindPose도 FBoneTransform::ToMatrix로 만든 값이라 같은 규약이다.
	for (int32 Index = 0; Index < Pose.Num(); ++Index)
	{
		LocalBoneTransforms[Index] = Pose[Index].ToMatrix();
	}
	// 로컬을 통째로 바꿨으므로 글로벌 직접 지정(리타게팅)은 해제하고, 글로벌은 UpdateBoneMatrices가 다시 계산한다.
	bGlobalBoneTransformsOverridden = false;
	bSkinningMatricesDirty = true;
	return true;
}

void USkeletalMeshComponent::UpdateBoneMatrices()
{
	if (!SkeletalMesh) return;
	const auto& Bones = SkeletalMesh->MeshData.Bones;
	int32 BoneCount = Bones.Num();
	if (BoneCount == 0) return;

	for (int32 i = 0; i < BoneCount; ++i)
	{
		if (!bGlobalBoneTransformsOverridden)
		{	
			int32 ParentIndex = Bones[i].ParentIndex;
			if (ParentIndex == -1)
			{
				// 루트 본의 경우, 로컬 변환을 그대로 글로벌 변환으로 사용
				GlobalBoneTransforms[i] = LocalBoneTransforms[i];
			}
			else
			{
				// 부모 본의 글로벌 변환과 로컬 변환을 곱하여 글로벌 변환 계산
				GlobalBoneTransforms[i] = LocalBoneTransforms[i] * GlobalBoneTransforms[ParentIndex];
			}
		}
		// 스키닝 매트릭스 계산
		SkinningMatrices[i] = Bones[i].InverseBindPose * GlobalBoneTransforms[i];
	}
	bSkinningMatricesDirty = false;
}

void USkeletalMeshComponent::UpdateSkinning()
{
	if (!SkeletalMesh || SkinningMatrices.IsEmpty()) return;
	++PoseRevision;
	if (bSkinningMatricesDirty) UpdateBoneMatrices();
	PerformCPUSkinning();
}

void USkeletalMeshComponent::PerformCPUSkinning()
{
	if (!SkeletalMesh) return;
	const FSkeletalMeshData& MeshData = SkeletalMesh->MeshData;
	const int32 BoneCount = MeshData.Bones.Num();
	if (BoneCount == 0) return;
	if (bSkinningMatricesDirty) UpdateBoneMatrices();
	if (!SkinnedVertexBuffer)
		SkinnedVertexBuffer = RenderCommand::CreateDynamicVertexBuffer(
			static_cast<uint32>(MeshData.SkinnedVertices.Num() * sizeof(FVertex)), sizeof(FVertex));

	int32 VertexCount = static_cast<int32>(MeshData.SkinnedVertices.Num());
	SkinnedVertices.SetNum(VertexCount);

	FVector BoundsMin(FLT_MAX, FLT_MAX, FLT_MAX);
	FVector BoundsMax(-FLT_MAX, -FLT_MAX, -FLT_MAX);

	for (int32 v = 0; v < VertexCount; ++v)
	{
		const FSkeletalVertex& Src = MeshData.SkinnedVertices[v];
		FVertex& Dst = SkinnedVertices[v];

		// Bone Influence(최대 4개)의 스키닝 행렬을 가중치로 섞어 행렬 하나로 만든다
		float B[4][3] = {};
		float TotalWeight = 0.0f;
		for (int32 i = 0; i < 4; ++i)
		{
			int32 BoneIndex = Src.BoneIndices[i];
			float Weight = Src.BoneWeights[i];
			
			if (BoneIndex < 0 || BoneIndex >= BoneCount || Weight <= 0.0f)
				continue;

			const FMatrix& S = SkinningMatrices[BoneIndex];
			for (int32 Row = 0; Row < 4; ++Row)
			{
				B[Row][0] += S.M[Row][0] * Weight;
				B[Row][1] += S.M[Row][1] * Weight;
				B[Row][2] += S.M[Row][2] * Weight;
			}
			TotalWeight += Weight;
		}

		// Bone Influence가 없으면 단위 행렬 = 바인드 포즈 그대로
		if (TotalWeight <= 0.0f)
			B[0][0] = B[1][1] = B[2][2] = 1.0f;

		// 섞은 행렬 하나로 위치, 노멀, 탄젠트를 변환
		TransformPoint(B, Src.Position, Dst.Position);
		TransformDirection(B, Src.Normal.X, Src.Normal.Y, Src.Normal.Z, Dst.Normal.V);
		TransformDirection(B, Src.Tangent.X, Src.Tangent.Y, Src.Tangent.Z, Dst.Tangent.V);
		Dst.Tangent.W = Src.Tangent.W;
		Dst.UV = Src.UV;
		Dst.Color = Src.Color;

		BoundsMin.X = std::min(BoundsMin.X, Dst.Position.X);
		BoundsMin.Y = std::min(BoundsMin.Y, Dst.Position.Y);
		BoundsMin.Z = std::min(BoundsMin.Z, Dst.Position.Z);
		BoundsMax.X = std::max(BoundsMax.X, Dst.Position.X);
		BoundsMax.Y = std::max(BoundsMax.Y, Dst.Position.Y);
		BoundsMax.Z = std::max(BoundsMax.Z, Dst.Position.Z);
	}

	SkinnedBounds.Min = BoundsMin;
	SkinnedBounds.Max = BoundsMax;
	MarkTransformDirty();

	if (SkinnedVertexBuffer)
	{
		RenderCommand::UpdateBufferData(
			SkinnedVertexBuffer.get(),
			SkinnedVertices.GetData(),
			VertexCount * sizeof(FVertex)
		);
	}
}

FBox USkeletalMeshComponent::CalcLocalBounds() const
{
	const_cast<USkeletalMeshComponent*>(this)->EnsureBoneState();
	if (SkeletalMesh && !SkinningMatrices.IsEmpty() && bSkinningMatricesDirty)
		const_cast<USkeletalMeshComponent*>(this)->UpdateSkinning();
	return SkeletalMesh ? SkinnedBounds : Super::CalcLocalBounds();
}

FPrimitiveSceneProxy* USkeletalMeshComponent::CreateSceneProxy()
{
	return new FSkeletalMeshSceneProxy(this);
}

FVertexBuffer* USkeletalMeshComponent::GetSkinnedVertexBuffer(uint32 LOD)
{
	if (!SkeletalMesh || LOD >= SkeletalMesh->GetLODCount()) return nullptr;
	if (LOD == 0) return SkinnedVertexBuffer.get();
	FSkinnedLOD& Cached = SkinnedLODs[LOD - 1];
	if (Cached.Buffer && Cached.Revision == PoseRevision) return Cached.Buffer.get();
	const auto& Data = SkeletalMesh->GetMeshData(LOD);
	Cached.Vertices.SetNum(Data.SkinnedVertices.Num());
	for (int32 V = 0; V < Data.SkinnedVertices.Num(); ++V)
	{
		const auto& Src = Data.SkinnedVertices[V]; auto& Dst = Cached.Vertices[V];
		float Blend[4][3]{}; float Total = 0;
		for (int I = 0; I < 4; ++I)
		{
			const int Bone = Src.BoneIndices[I]; const float Weight = Src.BoneWeights[I];
			if (Bone < 0 || Bone >= SkinningMatrices.Num() || Weight <= 0) continue;
			for (int R = 0; R < 4; ++R) for (int C = 0; C < 3; ++C) Blend[R][C] += SkinningMatrices[Bone].M[R][C] * Weight;
			Total += Weight;
		}
		if (Total <= 0) Blend[0][0] = Blend[1][1] = Blend[2][2] = 1;
		TransformPoint(Blend, Src.Position, Dst.Position);
		TransformDirection(Blend, Src.Normal.X, Src.Normal.Y, Src.Normal.Z, Dst.Normal.V);
		TransformDirection(Blend, Src.Tangent.X, Src.Tangent.Y, Src.Tangent.Z, Dst.Tangent.V);
		Dst.Tangent.W = Src.Tangent.W; Dst.Color = Src.Color; Dst.UV = Src.UV;
	}
	if (!Cached.Buffer) Cached.Buffer = RenderCommand::CreateDynamicVertexBuffer(static_cast<uint32>(Cached.Vertices.Num() * sizeof(FVertex)), sizeof(FVertex));
	if (Cached.Buffer) RenderCommand::UpdateBufferData(Cached.Buffer.get(), Cached.Vertices.GetData(), static_cast<uint32>(Cached.Vertices.Num() * sizeof(FVertex)));
	Cached.Revision = PoseRevision;
	return Cached.Buffer.get();
}

void USkeletalMeshComponent::SubmitToRenderQueue(FRenderQueue& RenderQueue, const FViewContext& ViewContext)
{
	EnsureBoneState();
	if (!SkeletalMesh) return;
	if (!SkinningMatrices.IsEmpty() && bSkinningMatricesDirty) UpdateSkinning();
	if (!SkinnedVertexBuffer) return;
	FMeshRenderState State;
	State.LODCount = static_cast<uint8>(SkeletalMesh->GetLODCount());
	for (int I = 0; I < 3; ++I) State.LODThresholdSq[I] = SkeletalMesh->ScreenThresholds[I] * SkeletalMesh->ScreenThresholds[I];
	const FLODSelectionInput Input{ SkeletalMesh->GetLocalLODSphere().Transform(GetWorldMatrix()), &State };
	const uint32 LOD = ViewContext.bOrthographic ? SelectSphereLOD<true>(Input, ViewContext) : SelectSphereLOD<false>(Input, ViewContext);
	const FSkeletalMeshData& MeshData = SkeletalMesh->GetMeshData(LOD);

	for (const FSkeletalMeshSection& Section : MeshData.Sections)
	{
		UMaterial* SectionMaterial = GetMaterial(static_cast<int32>(Section.MaterialSlotIndex));
		if (!SectionMaterial) continue;

		FRenderPacket Packet;
		Packet.SkeletalMesh = SkeletalMesh;
		Packet.SkeletalVertexBuffer = GetSkinnedVertexBuffer(LOD);
		Packet.LODIndex = static_cast<uint8>(LOD);
		Packet.Model = RenderQueue.StoreWorldMatrix(GetWorldMatrix());
		Packet.StartIndex = Section.StartIndex;
		Packet.IndexCount = Section.IndexCount;
		Packet.Material = SectionMaterial;
		RenderQueue.Add(Packet);
	}
}

void USkeletalMeshComponent::UpdatePickPositions()
{
	EnsureBoneState();
	if (!SkeletalMesh) return;
	if (!SkinningMatrices.IsEmpty() && bSkinningMatricesDirty) UpdateSkinning();
	if (PickRevision == PoseRevision && !PickPositions.IsEmpty()) return;

	const TArray<FSkeletalVertex>& Vertices = SkeletalMesh->MeshData.SkinnedVertices;
	PickPositions.SetNum(Vertices.Num());
	FVector Min(FLT_MAX, FLT_MAX, FLT_MAX), Max(-FLT_MAX, -FLT_MAX, -FLT_MAX);
	for (int32 V = 0; V < Vertices.Num(); ++V)
	{
		const FSkeletalVertex& Src = Vertices[V];
		FBlendMatrix Blend{};
		float Total = 0.0f;
		for (int32 I = 0; I < 4; ++I)
		{
			const int32 Bone = Src.BoneIndices[I];
			const float Weight = Src.BoneWeights[I];
			if (Bone < 0 || Bone >= SkinningMatrices.Num() || Weight <= 0.0f) continue;
			for (int32 R = 0; R < 4; ++R) for (int32 C = 0; C < 3; ++C) Blend[R][C] += SkinningMatrices[Bone].M[R][C] * Weight;
			Total += Weight;
		}
		if (Total <= 0.0f) Blend[0][0] = Blend[1][1] = Blend[2][2] = 1.0f;
		FVector& P = PickPositions[V];
		TransformPoint(Blend, Src.Position, P);
		Min = FVector(std::min(Min.X, P.X), std::min(Min.Y, P.Y), std::min(Min.Z, P.Z));
		Max = FVector(std::max(Max.X, P.X), std::max(Max.Y, P.Y), std::max(Max.Z, P.Z));
	}
	PickBounds = FBox{ Min, Max };
	PickRevision = PoseRevision;
}

bool USkeletalMeshComponent::LineTraceComponent(const FRay& WorldRay, FHitResult& OutHit)
{
	const FMatrix WorldToLocal = GetWorldMatrix().Inverse();
	const FRay LocalRay{ .Origin = WorldToLocal.TransformPosition(WorldRay.Origin), .Direction = WorldToLocal.TransformVector(WorldRay.Direction) };
	float T = FLT_MAX;
	if (!LineTraceComponentLocal(LocalRay, T)) return false;
	OutHit.bBlockingHit = true;
	OutHit.Distance = T;
	OutHit.ImpactPoint = WorldRay.Origin + WorldRay.Direction * T;
	OutHit.HitComponent = this;
	return true;
}

bool USkeletalMeshComponent::LineTraceComponentLocal(const FRay& LocalRay, float& OutT)
{
	UpdatePickPositions();
	if (!SkeletalMesh || PickPositions.IsEmpty()) return false;

	// 포즈를 반영한 박스로 먼저 거른다. 빈 공간을 클릭해도 메시가 선택되지 않는다.
	float BoxT = 0.0f;
	if (!RayIntersectsAABB(LocalRay, PickBounds.Min, PickBounds.Max, BoxT) || BoxT >= OutT) return false;

	const TArray<uint32>& Indices = SkeletalMesh->MeshData.Indices;
	bool bHit = false;
	for (int32 I = 0; I + 2 < Indices.Num(); I += 3)
	{
		float T = 0.0f;
		if (RayIntersectsTriangle(LocalRay, PickPositions[Indices[I]], PickPositions[Indices[I + 1]], PickPositions[Indices[I + 2]], T) && T < OutT)
		{
			OutT = T;
			bHit = true;
		}
	}
	return bHit;
}
