#include "EnginePCH.h"
#include "FbxAssetBake.h"
#include "FbxPath.h"
#include "Asset/AnimSequence.h"
#include "Animation/Skeleton.h"
#include "Render/Texture.h"
#include "UObject/UObjectGlobals.h"
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <type_traits>

namespace
{
// 헤더나 직렬화 필드를 변경하면 올린다. 구버전은 에디터에서 명시적으로 재임포트한다.
constexpr uint32 FbxBakeVersion = 5;
constexpr uint32 MaxElements = 1000000;
constexpr uint32 MaxImageBytes = 512u * 1024u * 1024u;
struct FFbxBakeHeader
{
	char Magic[4]{ 'F', 'B', 'X', 'B' };
	uint32 Version = FbxBakeVersion;
	uint32 Type = 0;
};

struct FAnimationBakeHeader
{
	char Magic[4]{ 'A', 'N', 'T', 'R' };
	uint32 Version = 5;
};

// 고정 크기 필드·배열·문자열을 순서대로 직렬화한다.
class FBinary
{
public:
	std::istream* In = nullptr;
	std::ostream* Out = nullptr;
	bool bGood = true;

	template<class T> void Pod(T& Value)
	{
		static_assert(std::is_trivially_copyable_v<T>);
		if (In) bGood = bGood && static_cast<bool>(In->read(reinterpret_cast<char*>(&Value), sizeof(T)));
		else bGood = bGood && static_cast<bool>(Out->write(reinterpret_cast<const char*>(&Value), sizeof(T)));
	}
	void String(FString& Value)
	{
		uint32 Size = static_cast<uint32>(Value.size());
		Pod(Size);
		if (!bGood || Size > 32768) { bGood = false; return; }
		if (In) { Value.resize(Size); bGood = static_cast<bool>(In->read(Value.data(), Size)); }
		else bGood = static_cast<bool>(Out->write(Value.data(), Size));
	}
	template<class T> void Array(TArray<T>& Values, uint32 Limit = MaxElements)
	{
		static_assert(std::is_trivially_copyable_v<T>);
		uint32 Size = static_cast<uint32>(Values.Num());
		Pod(Size);
		if (!bGood || Size > Limit) { bGood = false; return; }
		const uint64 Bytes = static_cast<uint64>(Size) * sizeof(T);
		if (In)
		{
			// 잘린 파일의 거짓 개수로 큰 메모리를 할당하지 않는다.
			const auto Pos = In->tellg();
			In->seekg(0, std::ios::end);
			const auto End = In->tellg();
			In->seekg(Pos);
			if (Pos < 0 || End < Pos || Bytes > static_cast<uint64>(End - Pos)) { bGood = false; return; }
			Values.SetNum(static_cast<int32>(Size));
			bGood = static_cast<bool>(In->read(reinterpret_cast<char*>(Values.GetData()), Bytes));
		}
		else bGood = static_cast<bool>(Out->write(reinterpret_cast<const char*>(Values.GetData()), Bytes));
	}
};

bool ValidAnimTrack(const FRawAnimSequenceTrack& Track, int32 KeyCount)
{
	if ((Track.PosKeys.Num() != 1 && Track.PosKeys.Num() != KeyCount) ||
		(Track.RotKeys.Num() != 1 && Track.RotKeys.Num() != KeyCount) ||
		(Track.ScaleKeys.Num() != 1 && Track.ScaleKeys.Num() != KeyCount)) return false;
	for (const FVector3f& Value : Track.PosKeys)
		if (!std::isfinite(Value.X) || !std::isfinite(Value.Y) || !std::isfinite(Value.Z)) return false;
	for (const FVector3f& Value : Track.ScaleKeys)
		if (!std::isfinite(Value.X) || !std::isfinite(Value.Y) || !std::isfinite(Value.Z)) return false;
	for (const FQuat4f& Value : Track.RotKeys)
	{
		const float LengthSquared = Value.X * Value.X + Value.Y * Value.Y +
			Value.Z * Value.Z + Value.W * Value.W;
		if (!std::isfinite(LengthSquared) || std::abs(LengthSquared - 1.0f) > 0.01f) return false;
	}
	return true;
}

void Skeleton(FBinary& Ar, FReferenceSkeleton& Ref)
{
	uint32 Count = static_cast<uint32>(Ref.GetNum());
	Ar.Pod(Count);
	if (!Ar.bGood || Count > 65536) { Ar.bGood = false; return; }
	if (Ar.In) { Ref.RefBoneInfo.SetNum(Count); Ref.RefBonePose.SetNum(Count); }
	for (uint32 I = 0; I < Count && Ar.bGood; ++I)
	{
		Ar.String(Ref.RefBoneInfo[I].Name);
		Ar.Pod(Ref.RefBoneInfo[I].ParentIndex);
		auto& T = Ref.RefBonePose[I];
		// 오일러로 바꾸면 값이 달라지므로 런타임 형식인 쿼터니언 그대로 저장한다.
		Ar.Pod(T.Translation); Ar.Pod(T.Rotation); Ar.Pod(T.Scale);
	}
}

// 모든 애셋 종류의 앞부분. ReadHeader는 여기까지만 읽는다.
void SerializeCommon(FBinary& Ar, FFbxAssetData& Data)
{
	Ar.String(Data.SourcePath); Ar.String(Data.SourceObject);
	auto& O = Data.Options;
	uint8 Flags = (O.bBakeStaticMeshes ? 1 : 0) | (O.bCombineStaticMeshes ? 2 : 0) | (O.bBakeSkeletalMeshes ? 4 : 0) |
		(O.bCombineSkeletalMeshes ? 8 : 0) | (O.bImportStaticMeshes ? 16 : 0) | (O.bImportSkeletalMeshes ? 32 : 0) | (O.bForceFrontX ? 64 : 0);
	Ar.Pod(Flags);
	if (Ar.In)
	{
		O.bBakeStaticMeshes = Flags & 1; O.bCombineStaticMeshes = Flags & 2; O.bBakeSkeletalMeshes = Flags & 4;
		O.bCombineSkeletalMeshes = Flags & 8; O.bImportStaticMeshes = Flags & 16; O.bImportSkeletalMeshes = Flags & 32; O.bForceFrontX = Flags & 64;
	}
	Ar.Pod(O.UniformScale);
	Ar.String(Data.ActualSourcePath); Ar.Pod(Data.SourceHash);
}

void Serialize(FBinary& Ar, FFbxAssetData& Data)
{
	SerializeCommon(Ar, Data);
	if (Data.Type == EFbxAssetType::StaticMesh || Data.Type == EFbxAssetType::SkeletalMesh)
	{
		Ar.Pod(Data.ScreenThresholds);
		uint32 Slots = Data.MaterialKeys.Num(); Ar.Pod(Slots);
		if (!Ar.bGood || Slots > 65536) { Ar.bGood = false; return; }
		if (Ar.In) Data.MaterialKeys.SetNum(Slots);
		for (auto& Key : Data.MaterialKeys) Ar.String(Key);
		uint32 LODCount = Data.LODs.Num(); Ar.Pod(LODCount);
		if (!Ar.bGood || LODCount == 0 || LODCount > 4) { Ar.bGood = false; return; }
		if (Ar.In) Data.LODs.SetNum(LODCount);
		for (auto& LOD : Data.LODs)
		{
			Ar.Array(LOD.MeshData.Vertices); Ar.Array(LOD.MeshData.Indices, MaxElements * 6);
			Ar.Array(LOD.MeshData.LODSourceVertices);
			Ar.Array(LOD.MeshData.Sections, 65536); Ar.Pod(LOD.MeshData.AABB);
			for (uint32 I = 0; I < Slots; ++I)
			{
				if (Ar.In) LOD.MeshData.MaterialSlots.Add(FStaticMaterialSlot{});
				Ar.String(LOD.MeshData.MaterialSlots[I].Name);
			}
			if (Data.Type == EFbxAssetType::SkeletalMesh)
			{
				Ar.Array(LOD.SkinWeights.InfluenceBones); Ar.Array(LOD.SkinWeights.InfluenceWeights);
			}
		}
		if (Data.Type == EFbxAssetType::SkeletalMesh) { Ar.String(Data.SkeletonKey); Skeleton(Ar, Data.RefSkeleton); }
	}
	else if (Data.Type == EFbxAssetType::Skeleton) Skeleton(Ar, Data.RefSkeleton);
	else if (Data.Type == EFbxAssetType::Material)
	{
		Ar.Pod(Data.Material.DiffuseOrFactor); Ar.Pod(Data.Material.Shininess);
		Ar.Pod(Data.Material.SpecularColor); Ar.Pod(Data.Material.SpecularFactor);
		Ar.Pod(Data.Material.EmissiveColor); Ar.Pod(Data.Material.EmissiveFactor);
		uint32 Model = static_cast<uint32>(Data.Material.ShadingModel); Ar.Pod(Model);
		if (Model > static_cast<uint32>(ELightingModel::Unlit)) { Ar.bGood = false; return; }
		Data.Material.ShadingModel = static_cast<ELightingModel>(Model);
		uint32 Sampler = static_cast<uint32>(Data.Material.SamplerState); Ar.Pod(Sampler);
		if (Sampler > static_cast<uint32>(ESamplerState::LinearWrap)) { Ar.bGood = false; return; }
		Data.Material.SamplerState = static_cast<ESamplerState>(Sampler);
		uint32 Blend = static_cast<uint32>(Data.Material.BlendState); Ar.Pod(Blend);
		if (Blend > static_cast<uint32>(EBlendState::AlphaBlend)) { Ar.bGood = false; return; }
		Data.Material.BlendState = static_cast<EBlendState>(Blend);
		Ar.String(Data.Material.DiffuseTexture); Ar.String(Data.Material.NormalTexture);
	}
	else if (Data.Type == EFbxAssetType::Texture2D)
	{
		Ar.Pod(Data.Texture.Width); Ar.Pod(Data.Texture.Height); Ar.Pod(Data.Texture.BytesPerPixel);
		Ar.Pod(Data.Texture.RowPitch); Ar.Pod(Data.Texture.Format); Ar.Array(Data.Texture.Pixels, MaxImageBytes);
	}
}

bool Finite(const FVector& V) { return std::isfinite(V.X) && std::isfinite(V.Y) && std::isfinite(V.Z); }
bool Finite(const FVector4& V) { return std::isfinite(V.X) && std::isfinite(V.Y) && std::isfinite(V.Z) && std::isfinite(V.W); }
}

bool FSkinWeightData::Validate(int32 VertexCount, int32 BoneCount, FString& Error) const
{
	if (InfluenceBones.Num() != VertexCount || InfluenceWeights.Num() != VertexCount)
	{ Error = "Skin weights must match final mesh vertices"; return false; }
	for (int32 Vertex = 0; Vertex < VertexCount; ++Vertex)
	{
		float Total = 0;
		for (int I = 0; I < 4; ++I)
		{
			const float Weight = InfluenceWeights[Vertex][I];
			if (!std::isfinite(Weight) || Weight < 0 || InfluenceBones[Vertex][I] >= BoneCount)
			{ Error = "Invalid bone index or weight"; return false; }
			Total += Weight;
		}
		if (std::abs(Total - 1.f) > 1.e-4f) { Error = "Skin weights are not normalized"; return false; }
	}
	return true;
}

bool FFbxAssetData::Validate(FString& Error) const
{
	if (Type == EFbxAssetType::StaticMesh || Type == EFbxAssetType::SkeletalMesh)
	{
		if (LODs.IsEmpty() || LODs.Num() > 4 || LODs[0].MeshData.Vertices.IsEmpty() || MaterialKeys.IsEmpty())
		{ Error = "Missing base geometry or material slots"; return false; }
		if (!std::isfinite(ScreenThresholds[0]) || !std::isfinite(ScreenThresholds[1]) || !std::isfinite(ScreenThresholds[2]) ||
			!(ScreenThresholds[0] > ScreenThresholds[1] && ScreenThresholds[1] > ScreenThresholds[2] && ScreenThresholds[2] > 0))
		{ Error = "LOD thresholds must be positive and descending"; return false; }
		if (Type == EFbxAssetType::SkeletalMesh)
		{
			if (SkeletonKey.empty()) { Error = "Missing Skeleton asset reference"; return false; }
			if (!RefSkeleton.Validate(Error)) return false;
		}
		for (const auto& LOD : LODs)
		{
			const auto& Mesh = LOD.MeshData;
			if (!Finite(Mesh.AABB.Min) || !Finite(Mesh.AABB.Max) || Mesh.AABB.Min.X > Mesh.AABB.Max.X ||
				Mesh.AABB.Min.Y > Mesh.AABB.Max.Y || Mesh.AABB.Min.Z > Mesh.AABB.Max.Z) { Error = "Invalid mesh bounds"; return false; }
			if (Mesh.Vertices.Num() > MaxElements || Mesh.Indices.Num() > MaxElements * 6 || Mesh.Indices.Num() % 3 ||
				(!Mesh.LODSourceVertices.IsEmpty() && Mesh.LODSourceVertices.Num() != Mesh.Vertices.Num()) ||
				Mesh.MaterialSlots.Num() != MaterialKeys.Num()) { Error = "Invalid mesh array size"; return false; }
			for (const auto& V : Mesh.Vertices)
				if (!Finite(V.Position) || !Finite(V.Normal) || !Finite(V.Tangent) || !Finite(V.Color) || !std::isfinite(V.UV.X) || !std::isfinite(V.UV.Y))
				{ Error = "Non-finite vertex"; return false; }
			for (uint32 I : Mesh.Indices) if (I >= static_cast<uint32>(Mesh.Vertices.Num())) { Error = "Invalid triangle index"; return false; }
			uint64 End = 0;
			for (const auto& S : Mesh.Sections)
			{
				if (S.StartIndex != End || S.IndexCount % 3 || S.MaterialSlotIndex >= static_cast<uint32>(MaterialKeys.Num()))
				{ Error = "Invalid section or material index"; return false; }
				End += S.IndexCount;
			}
			if (End != static_cast<uint64>(Mesh.Indices.Num())) { Error = "Sections must cover all triangles"; return false; }
			if (Type == EFbxAssetType::SkeletalMesh && !LOD.SkinWeights.Validate(Mesh.Vertices.Num(), RefSkeleton.GetNum(), Error)) return false;
		}
	}
	else if (Type == EFbxAssetType::Skeleton) return RefSkeleton.Validate(Error);
	else if (Type == EFbxAssetType::Material)
	{
		const auto& C = Material.DiffuseOrFactor;
		if (!std::isfinite(C.X) || !std::isfinite(C.Y) || !std::isfinite(C.Z) || !std::isfinite(C.W) || !std::isfinite(Material.Shininess))
		{ Error = "Non-finite material"; return false; }
		for (const FVector Color : { Material.SpecularColor, Material.EmissiveColor })
		{
			if (!std::isfinite(Color.X) || !std::isfinite(Color.Y) || !std::isfinite(Color.Z))
			{ Error = "Non-finite material color"; return false; }
		}
		if (!std::isfinite(Material.SpecularFactor) || !std::isfinite(Material.EmissiveFactor) ||
			Material.SpecularFactor < 0.f || Material.EmissiveFactor < 0.f)
		{ Error = "Invalid material factor"; return false; }
		if (Material.SamplerState != ESamplerState::LinearClamp && Material.SamplerState != ESamplerState::LinearWrap)
		{ Error = "Unsupported FBX material sampler"; return false; }
		if (Material.BlendState != EBlendState::Opaque && Material.BlendState != EBlendState::AlphaBlend)
		{ Error = "Unsupported FBX material blend mode"; return false; }
	}
	else if (Type == EFbxAssetType::Texture2D)
	{
		uint32 BlockBytes = 0;
		switch (Texture.Format)
		{
		case DXGI_FORMAT_BC1_UNORM: case DXGI_FORMAT_BC1_UNORM_SRGB: case DXGI_FORMAT_BC4_UNORM: case DXGI_FORMAT_BC4_SNORM: BlockBytes = 8; break;
		case DXGI_FORMAT_BC2_UNORM: case DXGI_FORMAT_BC2_UNORM_SRGB: case DXGI_FORMAT_BC3_UNORM: case DXGI_FORMAT_BC3_UNORM_SRGB:
		case DXGI_FORMAT_BC5_UNORM: case DXGI_FORMAT_BC5_SNORM: case DXGI_FORMAT_BC6H_UF16: case DXGI_FORMAT_BC6H_SF16:
		case DXGI_FORMAT_BC7_UNORM: case DXGI_FORMAT_BC7_UNORM_SRGB: BlockBytes = 16; break;
		default: break;
		}
		const uint64 PixelBytes = FormatToBytes(Texture.Format);
		const uint64 Rows = BlockBytes ? (Texture.Height + 3u) / 4u : Texture.Height;
		const uint64 MinPitch = BlockBytes ? static_cast<uint64>((Texture.Width + 3u) / 4u) * BlockBytes : Texture.Width * PixelBytes;
		if (!Texture.IsValid() || !Texture.Width || !Texture.Height || Texture.Width > 16384 || Texture.Height > 16384 ||
			(!BlockBytes && !PixelBytes) || Texture.BytesPerPixel != (BlockBytes ? 0 : PixelBytes) || Texture.GetRowPitch() < MinPitch ||
			static_cast<uint64>(Texture.GetRowPitch()) * Rows != static_cast<uint64>(Texture.Pixels.Num()))
		{ Error = "Invalid texture dimensions, format or pixel layout"; return false; }
	}
	else { Error = "Unknown FBX asset type"; return false; }
	return true;
}

bool FFbxAssetBake::ReadBaked(const FString& Path, FFbxAssetData& Out, FString& Error)
{
	std::ifstream In(FbxPath::FromString(Path), std::ios::binary);
	if (!In) { Error = "Cannot open FBX binary: " + Path; return false; }
	FFbxBakeHeader Header, Expected;
	if (!In.read(reinterpret_cast<char*>(&Header), sizeof(Header)) || std::memcmp(Header.Magic, Expected.Magic, 4) ||
		Header.Version != FbxBakeVersion || Header.Type < 1 || Header.Type > 5)
	{ Error = "Invalid .fbxbin header/version; import the FBX again: " + Path; return false; }
	FFbxAssetData Data; Data.Type = static_cast<EFbxAssetType>(Header.Type);
	FBinary Ar; Ar.In = &In; Serialize(Ar, Data);
	if (!Ar.bGood || In.peek() != std::char_traits<char>::eof()) { Error = "Truncated or trailing FBX binary data: " + Path; return false; }
	if (!Data.Validate(Error)) return false;
	Out = std::move(Data);
	return true;
}

bool FFbxAssetBake::ReadSourceIdentity(const FString& Path, FString& Source, FString& Object)
{
	std::ifstream In(FbxPath::FromString(Path), std::ios::binary);
	FFbxBakeHeader Header, Expected;
	if (!In.read(reinterpret_cast<char*>(&Header), sizeof(Header)) || std::memcmp(Header.Magic, Expected.Magic, 4) ||
		Header.Type < 1 || Header.Type > 5) return false;
	FBinary Ar; Ar.In = &In; Ar.String(Source); Ar.String(Object);
	return Ar.bGood;
}

bool FFbxAssetBake::ReadHeader(const FString& Path, FFbxAssetData& Out, FString& Error)
{
	std::ifstream In(FbxPath::FromString(Path), std::ios::binary);
	if (!In) { Error = "Cannot open FBX binary: " + Path; return false; }
	FFbxBakeHeader Header, Expected;
	if (!In.read(reinterpret_cast<char*>(&Header), sizeof(Header)) || std::memcmp(Header.Magic, Expected.Magic, 4) ||
		Header.Version != FbxBakeVersion || Header.Type < 1 || Header.Type > 5)
	{ Error = "Invalid .fbxbin header/version; import the FBX again: " + Path; return false; }
	FFbxAssetData Data; Data.Type = static_cast<EFbxAssetType>(Header.Type);
	FBinary Ar; Ar.In = &In; SerializeCommon(Ar, Data);
	if (!Ar.bGood) { Error = "Truncated FBX binary data: " + Path; return false; }
	Out = std::move(Data);
	return true;
}

bool FFbxAssetBake::WriteBaked(const FString& Path, const FFbxAssetData& Input, FString& Error)
{
	if (!Input.Validate(Error)) return false;
	std::ofstream Out(FbxPath::FromString(Path), std::ios::binary | std::ios::trunc);
	if (!Out) { Error = "Cannot create FBX binary: " + Path; return false; }
	FFbxBakeHeader Header; Header.Type = static_cast<uint32>(Input.Type);
	Out.write(reinterpret_cast<const char*>(&Header), sizeof(Header));
	// 직렬화 코드는 읽기/쓰기 필드 순서를 함께 유지한다.
	FFbxAssetData Data = Input;
	FBinary Ar; Ar.Out = &Out; Serialize(Ar, Data); Out.flush();
	if (!Ar.bGood || !Out) { Error = "Cannot write FBX binary: " + Path; return false; }
	return true;
}

bool FFbxAssetBake::WriteAnimationSequence(const FString& Path, const UAnimSequence& Sequence)
{
	const UAnimDataModel* Model = Sequence.GetDataModel();
	if (!Sequence.Skeleton || !Model || Model->FrameRate.Numerator <= 0 ||
		Model->FrameRate.Denominator <= 0 || Model->NumberOfKeys <= 0 ||
		Model->NumberOfKeys > 100000 || Model->NumberOfFrames != Model->NumberOfKeys - 1 ||
		!std::isfinite(Model->PlayLength) || Model->PlayLength <= 0.0f ||
		std::abs(Model->PlayLength - static_cast<float>(Model->NumberOfFrames) /
			Model->FrameRate.AsDecimal()) > 0.001f ||
		Model->BoneAnimationTracks.Num() != Sequence.Skeleton->RefSkeleton.GetNum()) return false;
	for (int32 BoneIndex = 0; BoneIndex < Model->BoneAnimationTracks.Num(); ++BoneIndex)
	{
		const FBoneAnimationTrack& Bone = Model->BoneAnimationTracks[BoneIndex];
		const FRawAnimSequenceTrack& Track = Bone.InternalTrack;
		if (Bone.Name != FName(Sequence.Skeleton->RefSkeleton.RefBoneInfo[BoneIndex].Name) ||
			!ValidAnimTrack(Track, Model->NumberOfKeys)) return false;
	}
	std::ofstream Out(FbxPath::FromString(Path), std::ios::binary | std::ios::trunc);
	if (!Out) return false;
	FAnimationBakeHeader Header;
	FBinary Ar; Ar.Out = &Out;
	Ar.Pod(Header);
	FString Name = Sequence.Name;
	FString SkeletonPath = Sequence.Skeleton->GetPath();
	int32 Numerator = Model->FrameRate.Numerator;
	int32 Denominator = Model->FrameRate.Denominator;
	float PlayLength = Model->PlayLength;
	int32 NumberOfFrames = Model->NumberOfFrames;
	int32 NumberOfKeys = Model->NumberOfKeys;
	uint32 TrackCount = static_cast<uint32>(Model->BoneAnimationTracks.Num());
	Ar.String(Name);
	Ar.String(SkeletonPath);
	Ar.Pod(Numerator);
	Ar.Pod(Denominator);
	Ar.Pod(PlayLength);
	Ar.Pod(NumberOfFrames);
	Ar.Pod(NumberOfKeys);
	Ar.Pod(TrackCount);
	for (const FBoneAnimationTrack& Bone : Model->BoneAnimationTracks)
	{
		FString BoneName = Bone.Name.ToString();
		TArray<FVector3f> PosKeys = Bone.InternalTrack.PosKeys;
		TArray<FQuat4f> RotKeys = Bone.InternalTrack.RotKeys;
		TArray<FVector3f> ScaleKeys = Bone.InternalTrack.ScaleKeys;
		Ar.String(BoneName);
		Ar.Array(PosKeys);
		Ar.Array(RotKeys);
		Ar.Array(ScaleKeys);
	}
	uint32 CurveCount = 0;
	Ar.Pod(CurveCount);
	uint8 EnableRootMotion = Sequence.bEnableRootMotion ? 1 : 0;
	uint8 ForceRootLock = Sequence.bForceRootLock ? 1 : 0;
	uint8 NormalizedScale = Sequence.bUseNormalizedRootMotionScale ? 1 : 0;
	uint8 RootLock = static_cast<uint8>(Sequence.RootMotionRootLock);
	Ar.Pod(EnableRootMotion);
	Ar.Pod(ForceRootLock);
	Ar.Pod(NormalizedScale);
	Ar.Pod(RootLock);
	Out.flush();
	return Ar.bGood && static_cast<bool>(Out);
}

bool FFbxAssetBake::ReadAnimationSequence(const FString& Path, USkeleton& Skeleton, UAnimSequence& OutSequence)
{
	std::ifstream In(FbxPath::FromString(Path), std::ios::binary);
	if (!In) return false;
	FAnimationBakeHeader Header, Expected;
	FBinary Ar; Ar.In = &In;
	Ar.Pod(Header);
	if (!Ar.bGood || std::memcmp(Header.Magic, Expected.Magic, sizeof(Header.Magic)) != 0 ||
		(Header.Version != 4 && Header.Version != Expected.Version)) return false;
	FString Name;
	FString SkeletonPath;
	FAnimFrameRate FrameRate;
	float PlayLength = 0.0f;
	int32 NumberOfFrames = 0;
	int32 NumberOfKeys = 0;
	uint32 BoneCount = 0;
	Ar.String(Name);
	Ar.String(SkeletonPath);
	Ar.Pod(FrameRate.Numerator);
	Ar.Pod(FrameRate.Denominator);
	Ar.Pod(PlayLength);
	Ar.Pod(NumberOfFrames);
	Ar.Pod(NumberOfKeys);
	Ar.Pod(BoneCount);
	if (!Ar.bGood || SkeletonPath != Skeleton.GetPath() ||
		FrameRate.Numerator <= 0 || FrameRate.Denominator <= 0 ||
		!std::isfinite(PlayLength) || PlayLength <= 0.0f || NumberOfKeys <= 0 ||
		NumberOfKeys > 100000 || NumberOfFrames != NumberOfKeys - 1 ||
		std::abs(PlayLength - static_cast<float>(NumberOfFrames) / FrameRate.AsDecimal()) > 0.001f ||
		BoneCount != static_cast<uint32>(Skeleton.RefSkeleton.GetNum()) ||
		static_cast<uint64>(NumberOfKeys) * BoneCount > 50000000) return false;
	TArray<FBoneAnimationTrack> Tracks;
	Tracks.Reserve(BoneCount);
	for (uint32 BoneIndex = 0; BoneIndex < BoneCount; ++BoneIndex)
	{
		FString BoneName;
		FBoneAnimationTrack Bone;
		Ar.String(BoneName);
		Ar.Array(Bone.InternalTrack.PosKeys, static_cast<uint32>(NumberOfKeys));
		Ar.Array(Bone.InternalTrack.RotKeys, static_cast<uint32>(NumberOfKeys));
		Ar.Array(Bone.InternalTrack.ScaleKeys, static_cast<uint32>(NumberOfKeys));
		if (!Ar.bGood || BoneName != Skeleton.RefSkeleton.RefBoneInfo[BoneIndex].Name ||
			!ValidAnimTrack(Bone.InternalTrack, NumberOfKeys)) return false;
		Bone.Name = FName(BoneName);
		Tracks.Add(std::move(Bone));
	}
	uint32 CurveCount = 0;
	Ar.Pod(CurveCount);
	if (!Ar.bGood || CurveCount != 0) return false;
	uint8 EnableRootMotion = 0, ForceRootLock = 0, NormalizedScale = 0, RootLock = 0;
	if (Header.Version >= 5)
	{
		Ar.Pod(EnableRootMotion);
		Ar.Pod(ForceRootLock);
		Ar.Pod(NormalizedScale);
		Ar.Pod(RootLock);
		if (!Ar.bGood || RootLock > static_cast<uint8>(ERootMotionRootLock::Zero)) return false;
	}
	UAnimDataModel* Model = OutSequence.DataModel ? OutSequence.DataModel : NewObject<UAnimDataModel>(&OutSequence);
	Model->BoneAnimationTracks = std::move(Tracks);
	Model->FrameRate = FrameRate;
	Model->PlayLength = PlayLength;
	Model->NumberOfFrames = NumberOfFrames;
	Model->NumberOfKeys = NumberOfKeys;
	OutSequence.Name = std::move(Name);
	OutSequence.Skeleton = &Skeleton;
	OutSequence.DataModel = Model;
	OutSequence.bEnableRootMotion = EnableRootMotion != 0;
	OutSequence.bForceRootLock = ForceRootLock != 0;
	OutSequence.bUseNormalizedRootMotionScale = NormalizedScale != 0;
	OutSequence.RootMotionRootLock = static_cast<ERootMotionRootLock>(RootLock);
	return true;
}

bool FFbxAssetBake::ReadAnimationSequenceSkeletonPath(const FString& Path, FString& OutSkeletonPath)
{
	std::ifstream In(FbxPath::FromString(Path), std::ios::binary);
	if (!In) return false;
	FAnimationBakeHeader Header, Expected;
	FString Name;
	FBinary Ar; Ar.In = &In;
	Ar.Pod(Header);
	Ar.String(Name);
	Ar.String(OutSkeletonPath);
	if (!Ar.bGood || std::memcmp(Header.Magic, Expected.Magic, sizeof(Header.Magic)) != 0 ||
		(Header.Version != 4 && Header.Version != Expected.Version)) return false;
	return !OutSkeletonPath.empty();
}
