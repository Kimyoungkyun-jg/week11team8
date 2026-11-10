#pragma once

#include "Asset/FbxImporter/FbxPath.h"

#include <algorithm>
#include <cctype>

inline FString GetAnimSequenceDisplayName(const FString& AssetPath, const FString& SkeletonPath)
{
	FString FileName = FbxPath::ToString(FbxPath::FromString(AssetPath).filename());
	if (FileName.ends_with(".bin")) FileName.resize(FileName.size() - 4);
	if (FileName.ends_with(".animseq")) FileName.resize(FileName.size() - 8);
	FString Lower = FileName;
	std::transform(Lower.begin(), Lower.end(), Lower.begin(),
		[](unsigned char C) { return static_cast<char>(std::tolower(C)); });
	const size_t Fbx = Lower.find(".fbx.");
	FString Original = Fbx == FString::npos ? FileName : FileName.substr(0, Fbx);
	if (Fbx != FString::npos)
	{
		const FString Take = FileName.substr(Fbx + 5);
		const size_t Separator = Take.rfind('_');
		if (Separator != FString::npos && Separator + 1 < Take.size() &&
			std::all_of(Take.begin() + Separator + 1, Take.end(),
				[](unsigned char C) { return std::isdigit(C) != 0; }))
		{
			const FString Number = Take.substr(Separator + 1);
			if (Number.find_first_not_of('0') != FString::npos)
				Original += "_" + Number;
		}
	}
	const FString Skeleton = FbxPath::ToString(FbxPath::FromString(SkeletonPath).stem());
	return Skeleton.empty() ? Original : Original + "_" + Skeleton;
}
