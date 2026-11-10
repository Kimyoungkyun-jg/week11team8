#pragma once
#include "FbxAssetData.h"

class UAnimSequence;
class USkeleton;

class FFbxAssetBake
{
public:
	static bool ReadBaked(const FString& Path, FFbxAssetData& Out, FString& Error);
	static bool WriteBaked(const FString& Path, const FFbxAssetData& Data, FString& Error);
	// 재임포트 이름 판정은 페이로드 버전과 독립적인 공통 출처 필드만 읽는다
	static bool ReadSourceIdentity(const FString& Path, FString& Source, FString& Object);
	// 페이로드를 읽지 않고 타입, 출처, 해시, 옵션만 채운다
	static bool ReadHeader(const FString& Path, FFbxAssetData& Out, FString& Error);
	static bool ReadAnimationSequence(const FString& Path, USkeleton& Skeleton, UAnimSequence& OutSequence);
	static bool ReadAnimationSequenceSkeletonPath(const FString& Path, FString& OutSkeletonPath);
	static bool WriteAnimationSequence(const FString& Path, const UAnimSequence& Sequence);
};
