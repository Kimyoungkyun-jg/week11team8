#pragma once

#include "Containers/Array.h"
#include "Core/EngineString.h"
#include "Core/Types.h"
#include "Math/Vector.h"
#include "Math/Vector2.h"
#include "Math/Vector4.h"
#include "Render/SkeletalMeshData.h"

struct FFbxBoneWeight
{
	int32 BoneIndices[4] = { 0, 0, 0, 0 };
	float BoneWeights[4] = { 0.0f, 0.0f, 0.0f, 0.0f };

	void AddBoneWeight(int32 BoneIndex, float Weight)
	{
		if (Weight <= 0.0f) return;
		for (int32 i = 0; i < 4; ++i)
		{
			if (BoneWeights[i] < Weight)
			{
				for (int32 j = 3; j > i; --j)
				{
					BoneWeights[j] = BoneWeights[j - 1];
					BoneIndices[j] = BoneIndices[j - 1];
				}
				BoneIndices[i] = BoneIndex;
				BoneWeights[i] = Weight;
				break;
			}
		}
	}

	void NormalizeWeights()
	{
		float TotalWeight = BoneWeights[0] + BoneWeights[1] + BoneWeights[2] + BoneWeights[3];
		float InvTotalWeight = (TotalWeight > 0.0f) ? (1.0f / TotalWeight) : 0.0f;
		if (TotalWeight > 0.0f)
		{
			for (int32 i = 0; i < 4; ++i)
			{
				BoneWeights[i] *= InvTotalWeight;
			}
		}
	}
};

struct FFbxCorner
{
	int32 ControlPointIndex = -1;
	FVector2 UVs = FVector2();
	FVector Normal = FVector();
	FVector4 Tangent = FVector4(0.0f, 0.0f, 0.0f, 0.0f);
	int32 PolygonVertexIndex = -1;
};

struct FFbxPolygon
{
	FFbxCorner Corners[3];
	int32 MaterialIndex = 0;
};

struct FFbxMaterialInfo : FImportedMaterialProperties
{
	FString Name;
	FVector4 BaseColor = FVector4(1.0f, 1.0f, 1.0f, 1.0f);
	FString DiffuseTexturePath;
	FString NormalTexturePath;
};

struct FFbxInfo
{
	FString Path;
	TArray<FBoneInfo> Bones;					// 본 계층 정보 및 바인드 포즈 행렬
	TArray<FVector> ControlPoints;				// Contorl Point 좌표
	TArray<FFbxBoneWeight> ControlPointWeights;	// Contorl Point별 본 가중치
	TArray<FFbxPolygon> Polygons;				// 삼각형 목록
	TArray<FFbxMaterialInfo> MaterialInfos;		// Material 정보
	bool bHasValidTangents = false;				// Tangent 정보가 없는 경우, Cook에서 생성
};
