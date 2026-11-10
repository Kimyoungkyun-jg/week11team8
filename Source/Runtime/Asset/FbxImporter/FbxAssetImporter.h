#pragma once
#include "FbxAssetData.h"

struct FAnimationClip;
class UAnimSequence;

// 원본 파싱/Cook만 담당한다. 런타임 객체 생성·파일 반영은 FBX 애셋 로더가 담당한다.
class FFbxAssetImporter
{
public:
	static bool IsAvailable();
	// OwnerSource를 주면 Source를 읽되 파일 이름과 소유 표시는 OwnerSource 기준으로 만든다 (Reimport From File).
	static bool Cook(const FString& Source, const FFbxImportOptions& Options, const FString& ExtractionDirectory,
		TArray<FFbxAssetData>& Out, TArray<FString>& Warnings, FString& Error, const FString& OutputDirectory = {}, const FString& OwnerSource = {});
	// AnimStack을 스켈레톤의 본 순서대로 샘플링한다. 각 프레임은 컴포넌트 공간 행렬이다.
	static bool ExtractAnimationClips(const FString& Source, const FFbxImportOptions& Options, const FReferenceSkeleton& Skeleton,
		TArray<FAnimationClip>& Out, FString& Error);

	// 유효한 AnimStack마다 시퀀스를 하나 만든다. 실패 시 Out은 유지한다.
	static bool ExtractAnimSequence(const FString& Source, const FFbxImportOptions& Options, const FReferenceSkeleton& Skeleton,
		TArray<UAnimSequence>& Out, FString& Error, float SampleRate = 30.0f);
};
