#pragma once

#include "Containers/Array.h"
#include "Core/EngineString.h"
#include "Core/NameTypes.h"
#include "Core/Types.h"
#include "Math/Matrix.h"
#include "Math/Quat.h"
#include "Math/Vector.h"

struct FAnimationFrame
{
	// Each bone transform is in skeletal mesh component space.
	TArray<FMatrix> Bones;
};

struct FAnimationClip
{
	FString Name;
	FString SkeletonName;
	float FramesPerSecond = 30.0f;
	float DurationSeconds = 0.0f;
	TArray<FString> BoneNames;
	TArray<int32> ParentIndices;
	TArray<FAnimationFrame> Frames;
};

struct FRawAnimSequenceTrack
{
	TArray<FVector> PosKeys;
	TArray<FQuat> RotKeys;
	TArray<FVector> ScaleKeys;
};

struct FBoneAnimationTrack
{
	FName Name;                          // Bone 이름
	FRawAnimSequenceTrack InternalTrack; // 실제 애니메이션 데이터
};

enum class EAnimationCurveInterpolation : uint8
{
	Constant,
	Linear,
	Cubic
};

struct FAnimationCurveKey
{
	float TimeSeconds = 0.0f;
	float Value = 0.0f;

	// 이 키에서 다음 키까지의 보간 방식
	EAnimationCurveInterpolation Interpolation = EAnimationCurveInterpolation::Linear;
	bool bConstantNext = false;

	// Cubic 보간용 기울기: 값의 변화량 / 초
	float ArriveTangent = 0.0f;
	float LeaveTangent = 0.0f;
};

enum class EAnimationCurveProperty : uint8
{
	Translation,
	Rotation,
	Scale
};

struct FAnimationCurveData
{
	int32 BoneIndex = -1; // BoneAnimationTracks의 인덱스
	int32 LayerIndex = -1;
	EAnimationCurveProperty Property = EAnimationCurveProperty::Translation;
	uint8 Axis = 0; // X, Y, Z

	// FBX 채널 키를 시간 오름차순으로 저장
	TArray<FAnimationCurveKey> Keys;
	float DefaultValue = 0.0f;

	float Evaluate(float TimeSeconds) const;
};
