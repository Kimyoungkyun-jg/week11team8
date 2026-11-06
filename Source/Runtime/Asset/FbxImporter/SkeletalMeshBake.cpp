#include "EnginePCH.h"
#include "SkeletalMeshBake.h"

#include <cstring>
#include <cmath>
#include <fstream>
#include <type_traits>

#include "Render/SkeletalMeshData.h"
#include "Core/EngineLog.h"

static_assert(sizeof(FSkeletalVertex) == 96);
static_assert(sizeof(FMatrix) == 64);

static_assert(std::is_trivially_copyable_v<FSkeletalVertex>);
static_assert(std::is_standard_layout_v<FMatrix>);
static_assert(std::is_trivially_copyable_v<FSkeletalMeshSection>);
static_assert(std::is_trivially_copyable_v<FVector4>);
static_assert(std::is_trivially_copyable_v<FBox>);

namespace
{
	// FSkeletalMeshData, FVertex, FSkeletalMeshSection, FStaticMaterialSlot 구조가 바뀌면 올린다
	constexpr uint32 BakeVersion{ 2 };

	struct FBakeHeader
	{
		// 'S''K'eletal Mesh 'B'a'K'e
		char Magic[4] = { 'S', 'K', 'B', 'K' };
		uint32 Version = BakeVersion;
	};

	struct FAnimationBakeHeader
	{
		char Magic[4] = { 'A', 'N', 'B', 'K' };
		uint32 Version = 3;
	};

	// 숫자 || 숫자만 있는 구조체
	template<typename T>
	void WritePod(std::ofstream& Out, const T& Value)
	{
		static_assert(std::is_trivially_copyable_v<T> || std::is_standard_layout_v<T>, "invalid plain old data");
		Out.write(reinterpret_cast<const char*>(&Value), sizeof(T));
	}
	template<typename T>
	bool ReadPod(std::ifstream& In, T& Value)
	{
		static_assert(std::is_trivially_copyable_v<T> || std::is_standard_layout_v<T>);
		In.read(reinterpret_cast<char*>(&Value), sizeof(T));
		return static_cast<bool>(In);
	}

	// 배열: 개수 + 원소 전체를 한 번에
	template<typename T>
	void WriteArray(std::ofstream& Out, const TArray<T>& Array)
	{
		const uint32 Count = static_cast<uint32>(Array.Num());
		WritePod(Out, Count);
		if (Count > 0)
		{
			Out.write(reinterpret_cast<const char*>(Array.GetData()), sizeof(T) * Count);
		}
	}

	template<typename T>
	bool ReadArray(std::ifstream& In, TArray<T>& Array, uint32 MaxCount)
	{
		uint32 Count = 0;
		if (!ReadPod(In, Count) || Count > MaxCount)
		{
			return false;
		}

		Array.SetNum(static_cast<int32>(Count));
		if (Count > 0)
		{
			In.read(reinterpret_cast<char*>(Array.GetData()), sizeof(T) * Count);
		}
		return static_cast<bool>(In);
	}

	// 길이 + 문자열
	void WriteString(std::ofstream& Out, const FString& Str)
	{
		const uint32 Length = static_cast<uint32>(Str.size());
		WritePod(Out, Length);
		Out.write(Str.data(), Length);
	}

	bool ReadString(std::ifstream& In, FString& Str)
	{
		uint32 Length = 0;
		if (!ReadPod(In, Length) || Length > 4096)
		{
			return false;
		}
		Str.resize(Length);
		In.read(Str.data(), Length);
		return static_cast<bool>(In);
	}
}

void FSkeletalMeshBake::WriteBaked(const FString& BinPath, const FSkeletalMeshData& Data)
{
	std::ofstream Out(BinPath, std::ios::binary);
	if (!Out)
	{
		HTR_LOG(Warning, "[BAKE] {}: cannot open for writing", BinPath);
		return;
	}
	
	WritePod<FBakeHeader>(Out, FBakeHeader());
	WriteArray<FSkeletalVertex>(Out, Data.SkinnedVertices);
	WriteArray<uint32>(Out, Data.Indices);
	WriteArray<FSkeletalMeshSection>(Out, Data.Sections);
	// MaterialSlots
	WritePod<uint32>(Out, Data.MaterialSlots.Num());
	for (const auto& Slot : Data.MaterialSlots)
	{
		WriteString(Out, Slot.Name);
		WritePod<FVector4>(Out, Slot.BaseColor);
		WritePod<FImportedMaterialProperties>(Out, Slot);
		WriteString(Out, Slot.DiffuseTexturePath);
		WriteString(Out, Slot.NormalTexturePath);
	}
	// Bones
	WritePod<uint32>(Out, Data.Bones.Num());
	for (const auto& Bone : Data.Bones)
	{
		WriteString(Out, Bone.Name);
		WritePod<int32>(Out, Bone.ParentIndex);
		WritePod<FMatrix>(Out, Bone.LocalBindPose);
		WritePod<FMatrix>(Out, Bone.GlobalBindPose);
		WritePod<FMatrix>(Out, Bone.InverseBindPose);
	}
	// AABB
	WritePod<FBox>(Out, Data.AABB);

	// 스트림은 한 번 실패하면 상태가 유지되므로 마지막에 한 번만 확인
	if (!Out)
	{
		HTR_LOG(Warning, "[BAKE] {}: write failed", BinPath);
	}
	else
	{
		HTR_LOG(Info, "[BAKE] {}: saved", BinPath);
	}
}

TUniquePtr<FSkeletalMeshData> FSkeletalMeshBake::ReadBaked(const FString& BinPath)
{
	std::ifstream In(BinPath, std::ios::binary);
	if (!In)
	{
		return nullptr;
	}

	TUniquePtr<FSkeletalMeshData> Data = MakeUnique<FSkeletalMeshData>();
	uint32 MaxCount = 1000000;

	FBakeHeader Header;
	const FBakeHeader ExpectedHeader;
	if(!ReadPod<FBakeHeader>(In, Header)||
		std::memcmp(Header.Magic, ExpectedHeader.Magic, sizeof(Header.Magic)) != 0 ||
		Header.Version != ExpectedHeader.Version)
	{
		HTR_LOG(Warning, "[BAKE] {}: invalid header", BinPath);
		return nullptr;
	}

	if (!ReadArray<FSkeletalVertex>(In, Data->SkinnedVertices, MaxCount) ||
		!ReadArray<uint32>(In, Data->Indices, MaxCount) ||
		!ReadArray<FSkeletalMeshSection>(In, Data->Sections, MaxCount))
	{
		return nullptr;
	}

	uint32 MaterialSlotsNum = 0;
	if (!ReadPod<uint32>(In, MaterialSlotsNum))
	{
		return nullptr;
	}

	for (int32 i = 0; i < MaterialSlotsNum; ++i)
	{
		FSkeletalMaterialSlot Slot{};
		if (!ReadString(In, Slot.Name) || !ReadPod<FVector4>(In, Slot.BaseColor) || !ReadPod(In, static_cast<FImportedMaterialProperties&>(Slot)) || !ReadString(In, Slot.DiffuseTexturePath) || !ReadString(In, Slot.NormalTexturePath)) {
			return nullptr;
		}
		Data->MaterialSlots.Add(std::move(Slot));
	}

	uint32 BonesNum = 0;
	if (!ReadPod<uint32>(In, BonesNum))
	{
		return nullptr;
	}

	for (int32 i = 0; i < BonesNum; ++i)
	{
		FBoneInfo Bone{};
		if (!ReadString(In, Bone.Name) || !ReadPod<int32>(In, Bone.ParentIndex) || !ReadPod<FMatrix>(In, Bone.LocalBindPose) || !ReadPod<FMatrix>(In, Bone.GlobalBindPose) || !ReadPod<FMatrix>(In, Bone.InverseBindPose)) {
			return nullptr;
		}
		Data->Bones.Add(std::move(Bone));
	}

	if(!ReadPod<FBox>(In, Data->AABB))
	{
		return nullptr;
	}

	FString Error;
	if (!Data->Validate(Error))
	{
		HTR_LOG(Warning, "[BAKE] {}: {}, re-cooking", BinPath, Error);
		return nullptr;
	}

	HTR_LOG(Info, "[BAKE] {}: loaded", BinPath);
	return Data;
}

bool FSkeletalMeshBake::WriteAnimations(const FString& BinPath, const TArray<FAnimationClip>& Clips)
{
	std::ofstream Out(BinPath, std::ios::binary | std::ios::trunc);
	if (!Out) return false;
	WritePod(Out, FAnimationBakeHeader());
	WritePod(Out, static_cast<uint32>(Clips.Num()));
	for (const FAnimationClip& Clip : Clips)
	{
		WriteString(Out, Clip.Name);
		WriteString(Out, Clip.SkeletonName);
		WritePod(Out, Clip.FramesPerSecond);
		WritePod(Out, Clip.DurationSeconds);
		WritePod(Out, static_cast<uint32>(Clip.BoneNames.Num()));
		for (const FString& BoneName : Clip.BoneNames) WriteString(Out, BoneName);
		WriteArray(Out, Clip.ParentIndices);
		WritePod(Out, static_cast<uint32>(Clip.Frames.Num()));
		for (const FAnimationFrame& Frame : Clip.Frames) WriteArray(Out, Frame.Bones);
	}
	return static_cast<bool>(Out);
}

bool FSkeletalMeshBake::ReadAnimations(const FString& BinPath, TArray<FAnimationClip>& OutClips)
{
	std::ifstream In(BinPath, std::ios::binary);
	if (!In) return false;
	FAnimationBakeHeader Header;
	const FAnimationBakeHeader Expected;
	if (!ReadPod(In, Header) || std::memcmp(Header.Magic, Expected.Magic, sizeof(Header.Magic)) != 0 ||
		Header.Version != Expected.Version) return false;
	uint32 ClipCount = 0;
	if (!ReadPod(In, ClipCount) || ClipCount > 256) return false;
	TArray<FAnimationClip> Loaded;
	for (uint32 ClipIndex = 0; ClipIndex < ClipCount; ++ClipIndex)
	{
		FAnimationClip Clip;
		uint32 BoneCount = 0;
		uint32 FrameCount = 0;
		if (!ReadString(In, Clip.Name) || !ReadString(In, Clip.SkeletonName) ||
			!ReadPod(In, Clip.FramesPerSecond) || !ReadPod(In, Clip.DurationSeconds) ||
			!std::isfinite(Clip.FramesPerSecond) || Clip.FramesPerSecond <= 0.0f ||
			!std::isfinite(Clip.DurationSeconds) || Clip.DurationSeconds < 0.0f ||
			!ReadPod(In, BoneCount) || BoneCount > 4096) return false;
		for (uint32 BoneIndex = 0; BoneIndex < BoneCount; ++BoneIndex)
		{
			FString BoneName;
			if (!ReadString(In, BoneName)) return false;
			Clip.BoneNames.Add(std::move(BoneName));
		}
		if (!ReadArray(In, Clip.ParentIndices, 4096) || Clip.ParentIndices.Num() != BoneCount ||
			!ReadPod(In, FrameCount) || FrameCount > 100000 ||
			static_cast<uint64>(FrameCount) * BoneCount > 50000000) return false;
		for (uint32 BoneIndex = 0; BoneIndex < BoneCount; ++BoneIndex)
			if (Clip.ParentIndices[BoneIndex] < -1 || Clip.ParentIndices[BoneIndex] >= static_cast<int32>(BoneIndex)) return false;
		for (uint32 FrameIndex = 0; FrameIndex < FrameCount; ++FrameIndex)
		{
			FAnimationFrame Frame;
			if (!ReadArray(In, Frame.Bones, BoneCount) || Frame.Bones.Num() != BoneCount) return false;
			Clip.Frames.Add(std::move(Frame));
		}
		Loaded.Add(std::move(Clip));
	}
	OutClips = std::move(Loaded);
	return true;
}
