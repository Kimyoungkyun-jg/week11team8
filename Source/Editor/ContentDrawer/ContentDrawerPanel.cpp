#include "EnginePCH.h"
#include "Editor/HitoriEd/EditorResourcePaths.h"
#include "Editor/ContentDrawer/ContentDrawerPanel.h"
#include "Editor/EditorUI/EditorUIScale.h"
#include "Asset/AssetManager.h"
#include "Asset/FbxImporter/FbxAssetLoader.h"
#include "Asset/FbxImporter/FbxPath.h"
#include "Text/Font.h"
#include "Editor/HitoriEd/EditorDragDrop.h"

#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Editor/HitoriEd/EditorEngine.h"

#include "GameFramework/Actor/SkeletalMeshActor.h"
#include "GameFramework/Actor/SkyLightActor.h"
#include "GameFramework/Actor/DirectionalLightActor.h"
#include "Components/BillboardComponent.h"
#include "Components/SkeletalMeshComponent.h"

#include "Render/SkeletalMesh.h"
#include "Render/RenderCommand.h"
#include "Math/MathUtility.h"
#include "Components/LightComponent.h"
#include "Launch/LaunchEngineLoop.h"
#include "Render/Swapchain.h"
#include "Render/SkyboxRenderer.h"

FContentDrawerPanel::FContentDrawerPanel() = default;

void FContentDrawerPanel::Release()
{
	ThumbnailClient.SetWorld(nullptr);
	if (ThumbnailWorld)
	{
		ThumbnailWorld->ClearWorld();
		ThumbnailWorld->FlushPendingDestruction();
		if (GEngine)
		{
			GEngine->DestroyWorld(ThumbnailWorld);
		}
		ThumbnailWorld = nullptr;
	}
	ThumbnailActor = nullptr;
	ThumbnailSkybox.reset();
	ThumbnailCache.clear();
	bStudioInitialized = false;
}

FContentDrawerPanel::~FContentDrawerPanel()
{
	Release();
}

bool FContentDrawerPanel::Init()
{
	return false;
}

void FContentDrawerPanel::Tick(float DeltaTime)
{
	if (bRefreshThumbnails) { ThumbnailCache.clear(); bRefreshThumbnails = false; }
}

void FContentDrawerPanel::OnRender()
{
	ImGui::Begin("Content Drawer");
	std::error_code Error;

	if (!fs::is_directory(CurrentPath, Error))
	{
		CurrentPath = AssetRootPath;
	}

	Error.clear();

	if (!fs::is_directory(AssetRootPath, Error))
	{
		ImGui::TextUnformatted("Asset folder is unavailable.");
		ImGui::End();
		return;
	}

	float treeViewWidth = EditorUIScale::Px(200.0f);
	ImGui::BeginChild("FolderTreeView", ImVec2(treeViewWidth, 0));
	{
		// 루트 노드
		if (ImGui::TreeNodeEx(AssetRootPath.string().c_str(), ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth |
			(AssetRootPath == CurrentPath ? ImGuiTreeNodeFlags_Selected : 0) | ImGuiTreeNodeFlags_DrawLinesToNodes))
		{
			if (ImGui::IsItemClicked())
			{
				CurrentPath = AssetRootPath;
			}
			RenderFolderTree(AssetRootPath, CurrentPath);
			ImGui::TreePop();
		}
	}
	ImGui::EndChild();

	ImGui::SameLine();

	ImGui::BeginChild("AssetGridView");
	{
		// 빈 곳 우클릭 (항목 위 우클릭은 제외)
		if (ImGui::BeginPopupContextWindow("##DrawerContext", ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems))
		{
			if (ImGui::MenuItem("Create Widget"))
			{
				strcpy_s(NewAssetName, "NewWidget");
				bOpenCreateWidget = true;   // 메뉴 팝업 안에서 바로 다른 팝업을 열 수 없어 다음 줄에서 연다
			}
			ImGui::EndPopup();
		}
		if (bOpenCreateWidget) { ImGui::OpenPopup("Create Widget"); bOpenCreateWidget = false; }

		if (ImGui::BeginPopupModal("Create Widget", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
		{
			ImGui::InputText("Name", NewAssetName, sizeof(NewAssetName));
			const fs::path Target = CurrentPath / (FString(NewAssetName) + ".widget");
			const bool bExists = fs::exists(Target);
			if (bExists) ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "A file with this name already exists.");

			ImGui::BeginDisabled(bExists || NewAssetName[0] == '\0');
			if (ImGui::Button("Create") && OnCreateWidget)
			{
				OnCreateWidget(FbxPath::ToString(Target));   // "Assets/UI/GameOver.widget" 형식
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndDisabled();
			ImGui::SameLine();
			if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
			ImGui::EndPopup();
		}

		// 그리드 레이아웃 계산
		float padding = EditorUIScale::Px(24.0f);
		float thumbnailSize = EditorUIScale::Px(80.0f);
		float cellSize = thumbnailSize + padding;
		float panelWidth = ImGui::GetContentRegionAvail().x;
		int ColumnCount = static_cast<int>(panelWidth / cellSize);
		if (ColumnCount < 1) ColumnCount = 1;

		// 테이블 기반 그리드 시작
		fs::path PendingOpen;
		if (ImGui::BeginTable("AssetGridTable", ColumnCount))
		{
			std::error_code IterateError;
			fs::directory_iterator It(CurrentPath, IterateError);
			const fs::directory_iterator End;

			for (; !IterateError && It != End; It.increment(IterateError))
			{
				const fs::path Path = It->path();
				if (Path.extension() == ".bin" || FbxPath::ToString(Path.filename()).starts_with('.')) continue;

				std::error_code TypeError;
				const bool bIsDirectory = fs::is_directory(Path, TypeError);

				if (TypeError)
					continue;

				ImGui::TableNextColumn();

				std::string PathString = FbxPath::ToString(Path);
				std::string FilenameString = FbxPath::ToString(Path.filename());

				ImGui::PushID(PathString.c_str()); // 각 위젯에 고유 ID 부여
				UTexture2D* Thumbnail = nullptr;

				// 실제 에셋일 때만 채워진다.
				// 폴더/파일 아이콘은 끌 수 없어야 하므로 썸네일과 구분해서 들고 있는다.
				UTexture2D* TextureAsset = nullptr;
				UFont* FontAsset = nullptr;
				// 3D 모델용 아이콘
				ID3D11ShaderResourceView* ThumbnailSRV = nullptr;

				// 폴더 또는 파일 아이콘 표시
				if (bIsDirectory)
				{
					Thumbnail = UAssetManager::Get().LoadTexture(FolderIconPath);
				}
				else // 파일인 경우
				{
					URenderAsset* Asset = UAssetManager::GetAssetByPath<URenderAsset>(PathString);
					if (Asset && Asset->IsA<UTexture2D>())
					{
						TextureAsset = Cast<UTexture2D>(Asset);
						Thumbnail = TextureAsset;
					}
					else if (Asset && Asset->IsA<UFont>())
					{
						FontAsset = Cast<UFont>(Asset);

						// 폰트는 글리프 아틀라스를 그대로 미리보기로 쓴다
						Thumbnail = FontAsset->AtlasTexture;
					}
					else if ((Asset && Asset->IsA<USkeletalMesh>()) || Path.extension() == ".fbx" || Path.extension() == ".FBX")
					{
						if (FTexture2D* FbxThumb = GetOrCreateSkeletalMeshThumbnail(PathString))
						{
							ThumbnailSRV = FbxThumb->GetSRV();
						}
					}

				}

				if (Thumbnail && Thumbnail->GetResource())
				{
					ThumbnailSRV = Thumbnail->GetResource()->GetSRV();
				}
				else if (!ThumbnailSRV)
				{
					Thumbnail = UAssetManager::Get().LoadTexture(FileIconPath);
					if (Thumbnail && Thumbnail->GetResource())
					{
						ThumbnailSRV = Thumbnail->GetResource()->GetSRV();
					}
				}

				ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(EditorUIScale::Px(4.0f), EditorUIScale::Px(4.0f)));

				const float availWidth = ImGui::GetContentRegionAvail().x;
				const float totalButtonWidth = thumbnailSize + EditorUIScale::Px(8.0f);
				if (availWidth > totalButtonWidth)
				{
					ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (availWidth - totalButtonWidth) * 0.5f);
				}

				if (ThumbnailSRV)
				{
					ImGui::ImageButton(PathString.c_str(), (ImTextureID)ThumbnailSRV, { thumbnailSize, thumbnailSize });
				}
				else
				{
					ImGui::Button(FilenameString.c_str(), { thumbnailSize, thumbnailSize });
				}

				ImGui::PopStyleVar();
				auto OpenOnClick = [&]()
				{
					if (!ImGui::IsItemHovered() || !ImGui::IsMouseClicked(ImGuiMouseButton_Left)) return false;
					const bool Open = SelectedAssetPath == Path && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
					SelectedAssetPath = Path;
					return Open;
				};
				bool bOpenRequested = OpenOnClick();
				if (ImGui::BeginPopupContextItem("AssetActions"))
				{
					FString Extension = Path.extension().string();
					std::transform(Extension.begin(), Extension.end(), Extension.begin(), [](unsigned char C) { return static_cast<char>(std::tolower(C)); });
					if ((Extension == ".fbx" || Extension == ".fbxbin") && OnImportFbx)
						if (ImGui::MenuItem(Extension == ".fbx" ? "Import FBX..." : "Reimport..."))
							OnImportFbx(PathString, Extension == ".fbxbin");
					if (ImGui::MenuItem("Open")) bOpenRequested = true;
					ImGui::EndPopup();
				}

				// 페이로드에 포인터를 그대로 실어서 받는 쪽이 AssetMap을 다시 뒤지지 않게 한다.
				if (TextureAsset && ImGui::BeginDragDropSource())
				{
					ImGui::SetDragDropPayload(EditorDragDrop::Texture, &TextureAsset, sizeof(UTexture2D*));

					// 끌고 다니는 동안 커서를 따라다니는 미리보기
					ImGui::Image((ImTextureID)TextureAsset->GetResource()->GetSRV(), { thumbnailSize, thumbnailSize });
					ImGui::TextUnformatted(FilenameString.c_str());

					ImGui::EndDragDropSource();
				}
				if (FontAsset && ImGui::BeginDragDropSource())
				{
					ImGui::SetDragDropPayload(EditorDragDrop::Font, &FontAsset, sizeof(UFont*));

					ImGui::TextUnformatted(FilenameString.c_str());

					ImGui::EndDragDropSource();
				}
				// 스크립트는 에셋 객체가 없으므로 파일 경로를 그대로 싣는다 (TEAM5).
				if (!bIsDirectory && Path.extension() == ".lua" && ImGui::BeginDragDropSource())
				{
					ImGui::SetDragDropPayload(EditorDragDrop::ScriptFile, PathString.c_str(), PathString.size() + 1);
					ImGui::TextUnformatted(FilenameString.c_str());
					ImGui::EndDragDropSource();
				}
				// 파일 이름 표시
				ImGui::TextWrapped("%s", FilenameString.c_str());
				bOpenRequested |= OpenOnClick();
				if (bOpenRequested) PendingOpen = Path;

				ImGui::PopID();
			}
			ImGui::EndTable();
		}
		if (!PendingOpen.empty()) OpenItem(PendingOpen);
	}
	ImGui::EndChild();
	ImGui::End();
}

void FContentDrawerPanel::OpenItem(const fs::path& Path)
{
	std::error_code Error;
	if (fs::is_directory(Path, Error)) { CurrentPath = Path; return; }
	FString Extension = Path.extension().string();
	std::transform(Extension.begin(), Extension.end(), Extension.begin(), [](unsigned char C) { return static_cast<char>(std::tolower(C)); });
	if (Extension == ".widget" && OnOpenWidget) OnOpenWidget(FbxPath::ToString(Path));
	else if (Extension == ".fbxbin")
	{
		FString Error;
		auto* Asset = FFbxAssetLoader::LoadAsset(FbxPath::ToString(Path), Error);
		if (!Asset) HTR_LOG(Warning, "[FBX] {}", Error);
		else if (Asset->IsA<USkeletalMesh>() && OnOpenSkeletalMesh) OnOpenSkeletalMesh(FbxPath::ToString(Path));
		else if (OnOpenAsset) OnOpenAsset(FbxPath::ToString(Path));
	}
	else if (Extension == ".fbx" && OnOpenSkeletalMesh) OnOpenSkeletalMesh(FbxPath::ToString(Path));
	else if (OnOpenAsset) OnOpenAsset(FbxPath::ToString(Path));
}

void FContentDrawerPanel::RenderFolderTree(const fs::path& Path, fs::path& SelectedPath)
{
	std::error_code IterateError;
	fs::directory_iterator It(Path, IterateError);
	const fs::directory_iterator End;

	for (; !IterateError && It != End; It.increment(IterateError))
	{
		const fs::path Entry = It->path();

		std::error_code TypeError;
		const bool bIsDirectory =
			fs::is_directory(Entry, TypeError);

		if (TypeError || !bIsDirectory || FbxPath::ToString(Entry.filename()).starts_with('.'))
			continue;

		const FString FilenameString =
			FbxPath::ToString(Entry.filename());

		ImGuiTreeNodeFlags Flags =
			ImGuiTreeNodeFlags_OpenOnArrow |
			ImGuiTreeNodeFlags_SpanAvailWidth |
			ImGuiTreeNodeFlags_DrawLinesToNodes;

		if (Entry == SelectedPath)
			Flags |= ImGuiTreeNodeFlags_Selected;

		const bool bIsOpen =
			ImGui::TreeNodeEx(FilenameString.c_str(), Flags);

		if (ImGui::IsItemClicked())
			SelectedPath = Entry;

		if (bIsOpen)
		{
			RenderFolderTree(Entry, SelectedPath);
			ImGui::TreePop();
		}
	}

	if (IterateError)
	{
		ImGui::TextUnformatted("Could not read this folder.");
	}
}

void FContentDrawerPanel::InitThumbnailStudio()
{
	if (bStudioInitialized)
		return;

	ThumbnailWorld = UWorld::CreateWorld(EWorldType::Editor, false);
	if (!ThumbnailWorld)
		return;

	ThumbnailActor = ThumbnailWorld->SpawnActor<ASkeletalMeshActor>();

	if (ASkyLightActor* SkyLight = ThumbnailWorld->SpawnActor<ASkyLightActor>())
	{
		SkyLight->GetBillboardComponent()->SetVisibility(false);
		SkyLight->GetLightComponent()->SetIntensity(0.3f);
	}
	if (ADirectionalLightActor* Sun = ThumbnailWorld->SpawnActor<ADirectionalLightActor>())
	{
		Sun->GetBillboardComponent()->SetVisibility(false);
		Sun->GetArrowComponent()->SetVisibility(false);
		Sun->GetLightComponent()->SetIntensity(1.0f);
		Sun->GetLightComponent()->SetRelativeRotation(FRotator(40.0f, 160.0f, 0.0f));
	}

	ThumbnailSkybox = MakeUnique<FSkyboxRenderer>();
	if (!ThumbnailSkybox->Init(EditorResourcePaths::AssetViewerSkyPanorama))
	{
		ThumbnailSkybox.reset();
	}

	ThumbnailClient.SetWorld(ThumbnailWorld);
	ThumbnailClient.SetViewportType(ELevelViewportType::Perspective);
	ThumbnailClient.Resize(128, 128);
	ThumbnailClient.SetRect(FRect{ 0.0f, 0.0f, 128.0f, 128.0f });

	bStudioInitialized = true;
}

FTexture2D* FContentDrawerPanel::GetOrCreateSkeletalMeshThumbnail(const FString& FbxPath)
{
	auto It = ThumbnailCache.find(FbxPath);
	if (It != ThumbnailCache.end())
	{
		return It->second.get();
	}

	InitThumbnailStudio();
	if (!ThumbnailActor || !ThumbnailWorld)
	{
		ThumbnailCache[FbxPath] = nullptr;
		return nullptr;
	}

	USkeletalMesh* Mesh = UAssetManager::LoadFbxSkeletalMesh(FbxPath);
	if (!Mesh)
	{
		ThumbnailCache[FbxPath] = nullptr;
		return nullptr;
	}
	ThumbnailActor->GetSkeletalMeshComponent()->SetSkeletalMesh(Mesh);

	const FBox Bounds = ThumbnailActor->GetSkeletalMeshComponent()->CalcBounds();
	const FVector Extent = Bounds.Max - Bounds.Min;
	const FVector Center = (Bounds.Min + Bounds.Max) * 0.5f;

	const float HalfDim = std::max(Extent.Z * 0.5f, std::max(Extent.X, Extent.Y) * 0.5f);
	const float HalfFov = FMath::DegreesToRadians(ThumbnailClient.GetViewFOV()) * 0.5f;
	const float Distance = (HalfDim / std::max(0.2f, std::sin(HalfFov))) * 0.8f;

	ThumbnailClient.SetViewRotation(FRotator(10.0f, 165.0f, 0.0f));
	const FVector TargetPos = Center + FVector(0.0f, 0.0f, Extent.Z * 0.04f);
	ThumbnailClient.SetViewLocation(TargetPos - ThumbnailClient.GetViewForward() * Distance);

	if (UEditorEngine* EdEngine = Cast<UEditorEngine>(GEngine))
	{
		EdEngine->RenderPreviewViewport(&ThumbnailClient, ThumbnailWorld, nullptr, ThumbnailSkybox.get(), false);
		FEngineLoop& Loop = GEngine->GetEngineLoop();
		if (FSwapchain* SC = Loop.GetSwapchain())
		{
			if (FWindow* Win = Loop.GetMainWindow())
			{
				RenderCommand::BindRenderPassNoClear(SC->GetBackbuffer(), nullptr,
					Win->GetWidth(), Win->GetHeight());
			}
		}
	}

	FTexture2D* RenderedTarget = ThumbnailClient.GetColorTarget();
	if (!RenderedTarget)
	{
		ThumbnailCache[FbxPath] = nullptr;
		return nullptr;
	}

	FTextureDesc Desc{};
	Desc.Width = RenderedTarget->GetWidth();
	Desc.Height = RenderedTarget->GetHeight();
	Desc.Format = RenderedTarget->GetTextureFormat();
	Desc.Usage = ETextureUsage::Default;
	Desc.BindFlags = ETextureBindFlags::ShaderResource;

	TUniquePtr<FTexture2D> CachedTex = RenderCommand::CreateTexture2D(Desc);
	if (CachedTex)
	{
		RenderCommand::CopyTexture(CachedTex.get(), RenderedTarget);
		ThumbnailCache[FbxPath] = std::move(CachedTex);
		return ThumbnailCache[FbxPath].get();
	}

	ThumbnailCache[FbxPath] = nullptr;
	return nullptr;
}
