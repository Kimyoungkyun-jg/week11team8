#pragma once

#include "Containers/Array.h"
#include "Core/EngineString.h"
#include "Core/Types.h"
#include "Math/Matrix.h"

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
