#pragma once

#include "Core/Types.h"
#include "Core/EngineString.h"
#include "AnimationData.h"

struct FSkeletalMeshData;

// Cook이 끝난 FSkeletalMeshData 를 .bin으로 저장하고 읽는다.
class FSkeletalMeshBake
{
public:
	static TUniquePtr<FSkeletalMeshData> ReadBaked(const FString& BinPath);
	static void WriteBaked(const FString& BinPath, const FSkeletalMeshData& Data);
	static bool ReadAnimations(const FString& BinPath, TArray<FAnimationClip>& OutClips);
	static bool WriteAnimations(const FString& BinPath, const TArray<FAnimationClip>& Clips);
};
