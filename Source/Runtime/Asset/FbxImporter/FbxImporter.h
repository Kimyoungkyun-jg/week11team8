#pragma once

#include "FbxInfo.h"
#include "AnimationData.h"
#include "Containers/Array.h"
#include "Core/Types.h"
#include "Core/EngineString.h"

struct FSkeletalMeshData;

class FFbxImporter
{
public:
	static TUniquePtr<FSkeletalMeshData> LoadSkeletalMeshData(const FString& Path);
	static bool LoadAnimationClips(const FString& Path, TArray<FAnimationClip>& OutClips);

	// 디버그용 출력
	/*static void PrintObjInfo(const FFbxInfo& ObjInfo);
	static void PrintSMD(const FSkeletalMeshData& SMD);*/

private:
	// .fbx 파일 경로 -> FFbxInfo 변환
	static bool ParseFbx(const FString& Path, FFbxInfo& Out);
	// FFbxInfo -> FSkeletalMeshData 변환
	static bool Cook(const FFbxInfo& Raw, FSkeletalMeshData& Out);
};
