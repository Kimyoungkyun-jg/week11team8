#include "EnginePCH.h"
#include "Core/ResourcePaths.h"
#include "AssetManager.h"
#include "Render/Buffer.h"
#include "Render/Material.h"

#include "Render/Vertex.h"
#include "Render/Texture2D.h"
#include "Render/RenderCommand.h"

#include "Render/RenderResourceManager.h"

#include "Asset/ObjImporter/ObjImporter.h"
#include "Asset/FbxImporter/FbxAssetImporter.h"
#include "Asset/FbxImporter/FbxAssetLoader.h"
#include "Asset/FbxImporter/FbxAssetBake.h"
#include "Asset/FbxImporter/FbxPath.h"

#include "Render/GeometryGenerator.h"
#include "UObject/UObjectGlobals.h"

#include "Engine/Classes/Sound/SoundWave.h"

#include "Render/ImageLoader.h"
#include <cctype>
#include <type_traits>



namespace
{
	FString GetAssetExtension(const FString& Path) {
		FString Extension{ FbxPath::ToString(FbxPath::FromString(Path).extension()) };
		for (char& Character : Extension) {
			Character = static_cast<char>(std::tolower(static_cast<unsigned char>(Character)));
		}
		return Extension;
	}

	UStaticMesh* CreateStaticMesh(const FStaticMeshData* MeshData)
	{
		if (!MeshData || !MeshData->Vertices.Num() || !MeshData->Indices.Num())
			return nullptr;

		FStaticMeshData Optimized = *MeshData;

		// Mesh에 MaterialSlot 없을 경우 Default Material 할당
		if (Optimized.MaterialSlots.IsEmpty())
		{
			FStaticMaterialSlot DefaultSlot;
			DefaultSlot.Name = "Default";
			Optimized.MaterialSlots.Add(DefaultSlot);
		}

		// Mesh에 Section이 없을 경우 전체 메쉬을 섹션 하나로 세팅
		if (Optimized.Sections.IsEmpty())
		{
			FStaticMeshSection DefaultSection;
			DefaultSection.StartIndex = 0;
			DefaultSection.IndexCount = static_cast<uint32>(Optimized.Indices.Num());
			DefaultSection.MaterialSlotIndex = 0;

			Optimized.Sections.Add(DefaultSection);
		}

		Optimized.BuildTangentSpace();
		Optimized.OptimizeTriangleOrderForVertexCache();

		TUniquePtr<FVertexBuffer> VB = RenderCommand::CreateStaticVertexBuffer(Optimized.Vertices.GetData(), sizeof(FVertex) * static_cast<uint32>(Optimized.Vertices.size()), sizeof(FVertex));
		TUniquePtr<FIndexBuffer> IB = RenderCommand::CreateStaticIndexBuffer(Optimized.Indices.GetData(), static_cast<uint32>(Optimized.Indices.size()));
		if (VB == nullptr || IB == nullptr) return nullptr;

		UStaticMesh* Mesh = NewObject<UStaticMesh>();
		Mesh->MeshData = std::move(Optimized);

		// Vertex/Index GPU 업로드
		Mesh->VertexBuffer = std::move(VB);
		Mesh->IndexBuffer = std::move(IB);

		UMaterial* DefaultMaterial = UAssetManager::GetAssetByPath<UMaterial>("DefaultMaterial");
		if (!DefaultMaterial)
		{
			return nullptr;
		}

		// MaterialSlots를 실제 UMaterial로 변환
		for (const FStaticMaterialSlot& Slot : Mesh->MeshData.MaterialSlots)
		{
			UMaterial* Material = UMaterial::CreateInstance(DefaultMaterial);
			if (!Material)
			{
				return nullptr;
			}
			Material->BaseColor = Slot.BaseColor;
                Material->ApplyImportedProperties(Slot);
			if (Material->BaseColor.W < 1.0f)
			{
				Material->SetBlendState(EBlendState::AlphaBlend);
			}

			if (!Slot.DiffuseTexturePath.empty())
			{
				UTexture2D* Texture = UAssetManager::Get().LoadTexture(Slot.DiffuseTexturePath);

				if (Texture)
				{
					Material->Textures[0] = Texture;
				}
			}

			if (!Slot.NormalTexturePath.empty()) {
				if (UTexture2D* NormalTexture{ UAssetManager::Get().LoadTexture(Slot.NormalTexturePath) }) {
					Material->Textures[1] = NormalTexture;
				}
			}

			Mesh->Materials.Add(Material);
		}

		Mesh->MeshData.BuildTriangleBVH();
		Mesh->RebuildRenderData();

		return Mesh;
	}

	FString MakeAssetKey(const FString& Path)
	{
		return FbxPath::ToString(fs::relative(FbxPath::FromString(Path), "Assets"));
	}

	FString FoldCase(FString Value)
	{
		for (char& Character : Value) Character = static_cast<char>(std::tolower(static_cast<unsigned char>(Character)));
		return Value;
	}
}

UAssetManager& UAssetManager::Get()
{
	static UAssetManager* Instance = NewObject<UAssetManager>();
	return *Instance;
}

void UAssetManager::Init(const FAssetLoadProgress& OnProgress)
{
	FGeometryGenerator::CreateDefaultMeshDatas();
	// 머티리얼이 참조하므로 반드시 먼저 만든다
	Get().CreateDefaultTextures();
	Get().CreateDefaultMaterial();
	Get().ScanAssets(ContentPaths::AssetRoot, OnProgress);
	// 엔진 기본 폰트는 스캔하지 않는 Resources에 있으므로 경로를 키로 직접 등록한다.
	if (UFont* DefaultFont = Get().LoadFontAtlas(ResourcePaths::DefaultFont, ResourcePaths::DefaultFontAtlas))
	{
		Get().RegisterAsset(ResourcePaths::DefaultFont, DefaultFont);
	}
	Get().CreateDefaultMeshes();
	Get().CreateBillboardMaterial();
	Get().CreateParticleMaterial();
	Get().CreateEditorArrowMaterial();
}

void UAssetManager::ScanAssets(const fs::path& AssetRoot, const FAssetLoadProgress& OnProgress)
{
	if (!fs::exists(AssetRoot))
	{
		HTR_LOG(Error, "Asset root not found: {}", FbxPath::ToString(AssetRoot));
		return;
	}

	TArray<fs::path> Files;
	for (fs::recursive_directory_iterator It(AssetRoot), End; It != End; ++It)
	{
		const fs::directory_entry& Entry = *It;
		if (Entry.is_directory() && Entry.path().filename().native().starts_with(L"."))
		{ It.disable_recursion_pending(); continue; }
		if (Entry.is_regular_file() && Entry.path().extension() != ".bin")
		{
			Files.Add(Entry.path());
		}
	}

	TArray<FString> FbxFiles;
	const int32 Total = Files.Num();
	for (int32 i = 0; i < Total; ++i)
	{
		// 키와 경로는 UTF-8이다. generic_string()은 ANSI 코드페이지로 바꾸므로 한글·이모지 이름에서 깨지거나 예외가 난다.
		FString Key = FbxPath::ToString(fs::relative(Files[i], AssetRoot));
		FString Path = FbxPath::ToString(Files[i]);
		AssetPathMap.Add(Key, Path);
		LoadAsset(Key, Path);

		const FString Extension = GetAssetExtension(Path);
		if (Extension == ".fbx") FbxFiles.Add(Path);
		else if (Extension == ".fbxbin")
		{
			// 실제로 로드된 메시 바이너리가 있어야 임포트된 FBX로 친다. 구버전이나 손상 파일은 다시 묻는다.
			FFbxAssetData Header; FString Error;
			if (FFbxAssetBake::ReadHeader(Path, Header, Error) &&
				(Header.Type == EFbxAssetType::StaticMesh || Header.Type == EFbxAssetType::SkeletalMesh) &&
				GetAssetByPath<URenderAsset>(Path))
				MarkFbxImported(Header.SourcePath);
		}

		if (OnProgress)
		{
			OnProgress(i + 1, Total, Key);
		}
	}

	// 스캔 순서는 정해져 있지 않으므로 모든 바이너리를 본 뒤에 비교한다.
	FbxImportCandidates.Reset();
	for (const FString& Path : FbxFiles)
	{
		FString Key, Error;
		if (FbxPath::GetAssetKey(Path, Key, Error) && !IsFbxImported(Key))
		{
			FbxImportCandidates.Add(Path);
		}
	}
}

void UAssetManager::MarkFbxImported(const FString& SourceIdentity)
{
	ImportedFbxSources.insert(FoldCase(SourceIdentity));
}

bool UAssetManager::IsFbxImported(const FString& SourceIdentity) const
{
	return ImportedFbxSources.contains(FoldCase(SourceIdentity));
}

void UAssetManager::LoadAsset(const FString& Key, const FString& Path) {
	const FString Extension{ GetAssetExtension(Path) };
	if (Extension == ".fbxbin")
	{
		FString Error;
		if (!FFbxAssetLoader::LoadAsset(Path, Error)) HTR_LOG(Error, "[FBX] {}", Error);
		return;
	}
	if (Extension == ".png" || Extension == ".dds") {
		if (UTexture2D* Texture{ LoadTexture(Path) }) {
			AssetMap[Key] = Texture;
		}
	}
	if (Extension == ".json" && FbxPath::FromString(Path).parent_path().filename() == "Fonts") {
		const FString TexturePath{ FbxPath::ToString(FbxPath::FromString(Path).replace_extension(".png")) };
		if (UFont* Font{ LoadFontAtlas(Path, TexturePath) }) {
			AssetMap[Key] = Font;
		}
	}
	if (Extension == ".obj") {
		LoadObjStaticMesh(Path);
	}
}

void UAssetManager::CreateDefaultTextures() {
	// 텍스처가 지정되지 않은 머티리얼이 검게 나오지 않도록 하는 1x1 흰색 텍스처.
	// 셰이더에서 곱해도 결과가 변하지 않으므로 "텍스처 없음"의 기본값으로 쓴다.
	const uint32 WhitePixel{ 0xFFFFFFFF };

	FTextureDesc Desc{};
	Desc.Width = 1;
	Desc.Height = 1;
	Desc.Format = ETextureFormat::R8G8B8A8_UNorm;
	Desc.Usage = ETextureUsage::Immutable;
	Desc.BindFlags = ETextureBindFlags::ShaderResource;

	TUniquePtr<FTexture2D> Resource{ RenderCommand::CreateTexture2D(Desc, &WhitePixel) };
	if (!Resource) {
		return;
	}

	UTexture2D* WhiteTexture{ NewObject<UTexture2D>() };
	WhiteTexture->SetResource(std::move(Resource));
	WhiteTexture->SetPath("WhiteTexture");

	AssetMap["WhiteTexture"] = WhiteTexture;

	const FVector4 NormalPixel{ 0.5f, 0.5f, 1.0f, 1.0f };
	Desc.Format = ETextureFormat::R32G32B32A32_Float;
	TUniquePtr<FTexture2D> NormalResource{ RenderCommand::CreateTexture2D(Desc, &NormalPixel) };
	if (!NormalResource) {
		return;
	}

	UTexture2D* FlatNormalTexture{ NewObject<UTexture2D>() };
	FlatNormalTexture->SetResource(std::move(NormalResource));
	FlatNormalTexture->SetPath("FlatNormalTexture");
	AssetMap["FlatNormalTexture"] = FlatNormalTexture;
}

void UAssetManager::CreateDefaultMeshes()
{
	// 메시 데이터 업로드
	FStaticMeshData* CubeData = FGeometryGenerator::GetMeshData("Cube");
	RegisterAsset("Cube", CreateStaticMesh(CubeData));

	FStaticMeshData* ConeData = FGeometryGenerator::GetMeshData("Cone");
	RegisterAsset("Cone", CreateStaticMesh(ConeData));

	FStaticMeshData* SphereData = FGeometryGenerator::GetMeshData("Sphere");
	RegisterAsset("Sphere", CreateStaticMesh(SphereData));

	FStaticMeshData* CylinderData = FGeometryGenerator::GetMeshData("Cylinder");
	RegisterAsset("Cylinder", CreateStaticMesh(CylinderData));

	FStaticMeshData* PlaneData = FGeometryGenerator::GetMeshData("Plane");
	RegisterAsset("Plane", CreateStaticMesh(PlaneData));

	FStaticMeshData* ArrowMeshData = FGeometryGenerator::GetMeshData("Arrow");
	RegisterAsset("Arrow", CreateStaticMesh(ArrowMeshData));

	FStaticMeshData* RingMeshData = FGeometryGenerator::GetMeshData("Ring");
	RegisterAsset("Ring", CreateStaticMesh(RingMeshData));

	FStaticMeshData* ScaleBarData = FGeometryGenerator::GetMeshData("ScaleBar");
	RegisterAsset("ScaleBar", CreateStaticMesh(ScaleBarData));

	FStaticMeshData* GizmoSphereData = FGeometryGenerator::GetMeshData("GizmoSphere");
	RegisterAsset("GizmoSphere", CreateStaticMesh(GizmoSphereData));

	const FParticleVertex Vertices[] =
	{
		{ FVector(0.0f, -0.5f,  0.5f), FVector2(0.0f, 0.0f) },
		{ FVector(0.0f, 0.5f,  0.5f), FVector2(1.0f, 0.0f) },
		{ FVector(0.0f, 0.5f, -0.5f), FVector2(1.0f, 1.0f) },
		{ FVector(0.0f, -0.5f, -0.5f), FVector2(0.0f, 1.0f) }
	};

	const uint32 Indices[] =
	{
		0, 1, 2,
		0, 2, 3
	};

	UStaticMesh* Mesh = NewObject<UStaticMesh>();
	Mesh->VertexBuffer = RenderCommand::CreateStaticVertexBuffer(
		Vertices,
		sizeof(Vertices), sizeof(FParticleVertex));
	Mesh->IndexBuffer = RenderCommand::CreateStaticIndexBuffer(
		Indices,
		ARRAYSIZE(Indices));

	FVertex Vertex{};
	Vertex.Position = FVector(0.0f, -0.5f, 0.5f);
	Mesh->MeshData.Vertices.Add(Vertex);
	Vertex.Position = FVector(0.0f, 0.5f, 0.5f);
	Mesh->MeshData.Vertices.Add(Vertex);
	Vertex.Position = FVector(0.0f, 0.5f, -0.5f);
	Mesh->MeshData.Vertices.Add(Vertex);
	Vertex.Position = FVector(0.0f, -0.5f, -0.5f);
	Mesh->MeshData.Vertices.Add(Vertex);

	Mesh->MeshData.Indices = { 0, 1, 2, 0, 2, 3, 0,2,1, 0,3,2 };

	Mesh->MeshData.AABB.Min = FVector(-0.001f, -0.5, -0.5);
	Mesh->MeshData.AABB.Max = FVector(0.001f, 0.5, 0.5);
	Mesh->RebuildRenderData();

	RegisterAsset("ParticleQuad", Mesh);
}

void UAssetManager::CreateDefaultMaterial() {
	UMaterial* DefaultMat{ NewObject<UMaterial>() };
	DefaultMat->Shader = FRenderResourceManager::GetShaderProgram("Resources/Shader/UberLit.hlsl");
	DefaultMat->Textures.Add(GetAssetByPath<UTexture2D>("WhiteTexture"));
	DefaultMat->Textures.Add(GetAssetByPath<UTexture2D>("FlatNormalTexture"));
	DefaultMat->ParamLayout = EMaterialParamLayout::StaticMesh;
	DefaultMat->ParamBuffer = RenderCommand::CreateConstantBuffer(sizeof(FStaticMeshMaterialParams));
	RegisterAsset("DefaultMaterial", DefaultMat);
}

void UAssetManager::CreateBillboardMaterial()
{
	UMaterial* BillboardMat = NewObject<UMaterial>();
	BillboardMat->PSOType = EPSOType::Billboard;
	BillboardMat->Textures.Add(GetAssetByPath<UTexture2D>("WhiteTexture"));
	BillboardMat->BlendState = EBlendState::AlphaBlend;
	BillboardMat->DepthStencilState = EDepthStencilState::ReadOnly;
	BillboardMat->SamplerState = ESamplerState::LinearClamp;
	BillboardMat->ParamLayout = EMaterialParamLayout::ParticleSubUV;
	RegisterAsset("BillboardMaterial", BillboardMat);
}

void UAssetManager::CreateParticleMaterial()
{
	UMaterial* ParticleMat = NewObject<UMaterial>();
	ParticleMat->PSOType = EPSOType::Particle_AlphaBlend;
	ParticleMat->Shader = FRenderResourceManager::GetShaderProgram("Resources/Shader/ParticleSubUVShader.hlsl");
	ParticleMat->Textures.Add(LoadTexture(ResourcePaths::DefaultParticleTexture));
	ParticleMat->BlendState = EBlendState::AlphaBlend;
	ParticleMat->DepthStencilState = EDepthStencilState::ReadOnly;
	ParticleMat->ParamLayout = EMaterialParamLayout::ParticleSubUV;
	ParticleMat->ParamBuffer = RenderCommand::CreateConstantBuffer(256);
	RegisterAsset("SubUVMaterial", ParticleMat);
}

void UAssetManager::CreateEditorArrowMaterial() {
	UMaterial* BaseMaterial{ GetAssetByPath<UMaterial>("DefaultMaterial") };
	UMaterial* ArrowMaterial{ UMaterial::CreateInstance(BaseMaterial) };
	ArrowMaterial->Shader = FRenderResourceManager::GetShaderProgram("Resources/Shader/EditorArrowShader.hlsl");
	ArrowMaterial->BaseColor = FVector4{ 1.0f, 1.0f, 1.0f, 1.0f };
	RegisterAsset("EditorArrowMaterial", ArrowMaterial);
}

void UAssetManager::Shutdown()
{
	Get().AssetMap.Empty();
	Get().SoundMap.Empty();
	Get().AnimationMap.Empty();
	Get().PreviewAnimationMap.Empty();
}


void UAssetManager::RegisterAsset(const FString& Key, URenderAsset* Asset)
{
	Asset->SetPath(Key);
	AssetMap[Key] = Asset;
}

void UAssetManager::PrepareAssetRegistration(const TMap<FString, URenderAsset*>& Replacements, TMap<FString, URenderAsset*>& Prepared) const
{
	Prepared = AssetMap;
	for (const auto& [Key, Asset] : Replacements) Prepared[Key] = Asset;
}

void UAssetManager::CommitAssetRegistration(TMap<FString, URenderAsset*>& Prepared) noexcept
{
	for (const auto& [Key, Previous] : AssetMap)
	{
		URenderAsset** Next = Prepared.Find(Key);
		if (Previous && Next && *Next != Previous)
		{
			Previous->SetRetired(true);
			AnimationMap.Remove(Key);
		}
	}
	AssetMap.Swap(Prepared);
}

UTexture2D* UAssetManager::LoadTexture(const FString& InPath, bool GenerateMips) {
	if (GetAssetExtension(InPath) == ".fbxbin")
	{
		FString Error;
		auto* Asset = Cast<UTexture2D>(FFbxAssetLoader::LoadAsset(InPath, Error));
		if (!Asset) HTR_LOG(Warning, "[FBX] {}", Error);
		return Asset;
	}
	if (URenderAsset** Found{ AssetMap.Find(InPath) }) {
		return Cast<UTexture2D>(*Found);
	}

	const FImageData Data = ImageLoader::LoadAuto(InPath);
	if (!Data.IsValid())
	{
		return nullptr;
	}

	const bool bUseDDSLevels = Data.MipRowPitches.Num() > 0;
	// BC 압축 DDS는 렌더 타깃으로 바인딩할 수 없으므로 GPU mip 생성 대상에서 제외한다.
	const bool bGenerateGPUmips = GenerateMips && !bUseDDSLevels && Data.BytesPerPixel != 0;

	FTextureDesc Desc{};
	Desc.Width = Data.Width;
	Desc.Height = Data.Height;
	Desc.MipLevels = bGenerateGPUmips ? 0 : Data.MipLevels;
	Desc.Usage = bGenerateGPUmips ? ETextureUsage::Default : ETextureUsage::Immutable;
	Desc.BindFlags = ETextureBindFlags::ShaderResource |
		(bGenerateGPUmips ? ETextureBindFlags::RenderTarget : ETextureBindFlags::None);
	Desc.bGenerateMips = bGenerateGPUmips;

	TUniquePtr<FTexture2D> Resource = RenderCommand::CreateTexture2D(Desc, Data);

	if (!Resource || !Resource->GetSRV()) {
		return nullptr;
	}

	UTexture2D* Asset{ NewObject<UTexture2D>() };
	Asset->SetResource(std::move(Resource));
	RegisterAsset(InPath, Asset);

	return Asset;
}

UFont* UAssetManager::LoadFontAtlas(const FString& JsonPath, const FString& AtlasTexturePath)
{
	UFont* Font = NewObject<UFont>();

	if (!Font->LoadFontAtlasJson(JsonPath))
	{
		return nullptr;
	}

	Font->AtlasTexture = LoadTexture(AtlasTexturePath, false);
	if (!Font->AtlasTexture)
	{
		return nullptr;
	}

	RegisterAsset(JsonPath, Font);
	return Font;
}

USoundWave* UAssetManager::LoadSound(const FString& Path)
{
	if (USoundWave** Found = SoundMap.Find(Path)) return *Found;

	USoundWave* Wave = NewObject<USoundWave>();
	if (!Wave->LoadFromFile(Path))
	{
		// 실패한 객체는 텍스처 로드 실패 때와 같은 방식으로 정리
		return nullptr;
	}
	SoundMap[Path] = Wave;
	return Wave;
}

UStaticMesh* UAssetManager::LoadObjStaticMesh(const FString& Path)
{
	if (GetAssetExtension(Path) == ".fbxbin")
	{
		FString Error;
		return Cast<UStaticMesh>(FFbxAssetLoader::LoadAsset(Path, Error));
	}
	const FString Key = MakeAssetKey(Path);
	if (UStaticMesh* Cached = GetAssetByPath<UStaticMesh>(Key))
	{
		return Cached;
	}

	TUniquePtr<FStaticMeshData> Data = FObjImporter::LoadStaticMeshData(Path);
	UStaticMesh* Mesh = CreateStaticMesh(Data.get());   // Data가 nullptr이면 nullptr 반환
	if (!Mesh)
	{
		return nullptr;
	}

	Get().RegisterAsset(Key, Mesh);
	return Mesh;
}

USkeletalMesh* UAssetManager::LoadFbxSkeletalMesh(const FString& Path)
{
	if (GetAssetExtension(Path) == ".fbxbin")
	{
		FString Error;
		USkeletalMesh* Mesh = Cast<USkeletalMesh>(FFbxAssetLoader::LoadAsset(Path, Error));
		if (!Mesh && !Error.empty()) HTR_LOG(Warning, "[FBX] {}", Error);
		return Mesh;
	}
	// 메시는 .fbxbin에서만 온다. .fbx를 여기서 파싱하지 않는다.
	return GetAssetByPath<USkeletalMesh>(MakeAssetKey(Path));
}

namespace
{
template <typename T>
bool LoadFbxAnimationData(const FString& Path, TArray<T>& Out)
{
	USkeletalMesh* Mesh = UAssetManager::LoadFbxSkeletalMesh(Path);
	if (!Mesh || GetAssetExtension(Path) != ".fbxbin") return false;
	FFbxAssetData Header; FString Error;
	if (!FFbxAssetBake::ReadHeader(Path, Header, Error)) { HTR_LOG(Warning, "[FBX] {}", Error); return false; }
	const FString Source = FbxPath::ToString(FbxPath::SourceFile(Header.ActualSourcePath));
	bool bExtracted;
	if constexpr (std::is_same_v<T, UAnimSequence>)
		bExtracted = FFbxAssetImporter::ExtractAnimSequence(Source, Header.Options, Mesh->RefSkeleton, Out, Error);
	else
		bExtracted = FFbxAssetImporter::ExtractAnimationClips(Source, Header.Options, Mesh->RefSkeleton, Out, Error);
	if (!bExtracted)
	{
		HTR_LOG(Warning, "[FBX] Animation extraction failed: {}", Error);
		return false;
	}
	return true;
}
}

const TArray<FAnimationClip>* UAssetManager::LoadFbxAnimations(const FString& Path)
{
	TArray<FAnimationClip> Clips;
	if (!LoadFbxAnimationData(Path, Clips)) return nullptr;
	const FString Key = MakeAssetKey(Path);
	Get().PreviewAnimationMap.FindOrAdd(Key) = std::move(Clips);
	return Get().PreviewAnimationMap.Find(Key);
}

const TArray<FAnimationClip>* UAssetManager::GetFbxAnimations(const FString& AssetKey)
{
	return Get().PreviewAnimationMap.Find(AssetKey);
}

const TArray<UAnimSequence>* UAssetManager::LoadFbxAnimSequences(const FString& Path)
{
	TArray<UAnimSequence> Sequences;
	if (!LoadFbxAnimationData(Path, Sequences)) return nullptr;
	const FString Key = MakeAssetKey(Path);
	Get().AnimationMap.FindOrAdd(Key) = std::move(Sequences);
	return Get().AnimationMap.Find(Key);
}

const TArray<UAnimSequence>* UAssetManager::GetFbxAnimSequences(const FString& AssetKey)
{
	return Get().AnimationMap.Find(AssetKey);
}

bool UAssetManager::ReorientStaticMesh(UStaticMesh& Mesh, const std::function<FVector(const FVector&)>& Rotate)
{
	if (Mesh.GetLODCount() > 1 || Mesh.MeshData.Vertices.IsEmpty())
		return false;   // LOD가 이미 옛 방향으로 만들어졌으면 서로 어긋난다

	FStaticMeshData& Data = Mesh.MeshData;
	FBox Bounds{ FVector(FLT_MAX, FLT_MAX, FLT_MAX), FVector(-FLT_MAX, -FLT_MAX, -FLT_MAX) };
	for (FVertex& Vertex : Data.Vertices)
	{
		Vertex.Position = Rotate(Vertex.Position);
		Vertex.Normal = Rotate(Vertex.Normal);
		Bounds.Min = FVector(std::min(Bounds.Min.X, Vertex.Position.X), std::min(Bounds.Min.Y, Vertex.Position.Y), std::min(Bounds.Min.Z, Vertex.Position.Z));
		Bounds.Max = FVector(std::max(Bounds.Max.X, Vertex.Position.X), std::max(Bounds.Max.Y, Vertex.Position.Y), std::max(Bounds.Max.Z, Vertex.Position.Z));
	}
	Data.AABB = Bounds;
	Data.BuildTangentSpace();

	// 인덱스는 그대로다 (회전은 삼각형 감기 방향을 바꾸지 않는다). 정점 버퍼만 다시 올린다.
	TUniquePtr<FVertexBuffer> VB = RenderCommand::CreateStaticVertexBuffer(Data.Vertices.GetData(),
		sizeof(FVertex) * static_cast<uint32>(Data.Vertices.Num()), sizeof(FVertex));
	if (!VB)
		return false;
	Mesh.VertexBuffer = std::move(VB);

	Data.BuildTriangleBVH();   // 피킹용 삼각형 BVH도 새 좌표로
	return true;
}

void UAssetManager::ForEachObjStaticMesh(const std::function<void(const FString& Key, UStaticMesh& Mesh)>& Func)
{
	for (auto& [Key, Asset] : Get().AssetMap)
	{
		if (!Asset || !Asset->IsA<UStaticMesh>() || !Key.ends_with(".obj"))
			continue;
		Func(Key, *Cast<UStaticMesh>(Asset));
	}
}

FLODGenerateResult UAssetManager::GenerateStaticMeshLODs(UStaticMesh& Mesh, const FLODGenerateRequest& Request)
{
	TArray<FStaticMeshData> Generated;
	FLODGenerateResult Result = FStaticMeshLODGenerator::Generate(Mesh.GetMeshData(), Generated);

	if (!Result.bSuccess) return Result;

	if (!(Request.ScreenThresholds[0] >
		Request.ScreenThresholds[1] &&
		Request.ScreenThresholds[1] >
		Request.ScreenThresholds[2] &&
		Request.ScreenThresholds[2] > 0.0f))
	{
		Result.bSuccess = false;
		Result.FailureReason = "Screen thresholds must descend";
		return Result;
	}

	TArray<FStaticMeshLODResource> NewResources;

	for (FStaticMeshData& Data : Generated)
	{
		FStaticMeshLODResource Resource;
		Resource.Data = std::move(Data);

		Resource.Data.BuildTangentSpace();
		Resource.Data.OptimizeTriangleOrderForVertexCache();

		Resource.VertexBuffer = RenderCommand::CreateStaticVertexBuffer(
			Resource.Data.Vertices.GetData(),
			sizeof(FVertex) *
			static_cast<uint32>(Resource.Data.Vertices.Num()),
			sizeof(FVertex));

		Resource.IndexBuffer = RenderCommand::CreateStaticIndexBuffer(
			Resource.Data.Indices.GetData(),
			static_cast<uint32>(Resource.Data.Indices.Num()));

		if (!Resource.VertexBuffer || !Resource.IndexBuffer)
		{
			Result.bSuccess = false;
			Result.FailureReason = "LOD GPU buffer creation failed";
			return Result;
		}

		NewResources.Add(std::move(Resource));
	}

	// bSaveToAsset이면 이 지점에서 파일 저장.
	// 저장 실패 시 기존 Mesh.AdditionalLODs는 그대로 둔다.

	Mesh.AdditionalLODs = std::move(NewResources);
	Mesh.ScreenThresholds = Request.ScreenThresholds;
	Mesh.RebuildRenderData();
	return Result;
}
