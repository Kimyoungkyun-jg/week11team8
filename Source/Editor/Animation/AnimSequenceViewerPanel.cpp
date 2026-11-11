#include "EnginePCH.h"
#include "Editor/Animation/AnimSequenceViewerPanel.h"
#include "Editor/Animation/AnimSequenceDisplayName.h"
#include "Editor/HitoriEd/EditorEngine.h"
#include "Editor/HitoriEd/EditorResourcePaths.h"
#include "Editor/EditorUI/EditorUIScale.h"
#include "Serialization/StructuredArchive.h"
#include "Components/SkeletalMeshDebugComponent.h"
#include "Asset/AssetManager.h"
#include "Asset/FbxImporter/FbxPath.h"
#include "Asset/AnimSequence.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Animation/Skeleton.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Actor/SkyLightActor.h"
#include "GameFramework/Actor/DirectionalLightActor.h"
#include "Components/SkyLightComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/BillboardComponent.h"
#include "Render/SkeletalMesh.h"
#include "Render/SkyboxRenderer.h"
#include "Math/Matrix.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace
{
	void ShowTransform(const char* Label, const FBoneTransform& Transform)
	{
		const FRotator Rotation = MatrixToRotator(Transform.Rotation.ToFMatrix());
		ImGui::TextUnformatted(Label);
		ImGui::Text("Position  %.3f  %.3f  %.3f", Transform.Translation.X, Transform.Translation.Y, Transform.Translation.Z);
		ImGui::Text("Rotation  %.1f  %.1f  %.1f", Rotation.Roll, Rotation.Pitch, Rotation.Yaw);
		ImGui::Text("Scale     %.3f  %.3f  %.3f", Transform.Scale.X, Transform.Scale.Y, Transform.Scale.Z);
	}
}

FAnimSequenceViewerPanel::FAnimSequenceViewerPanel() = default;
FAnimSequenceViewerPanel::~FAnimSequenceViewerPanel() { Release(); }

void FAnimSequenceViewerPanel::Release()
{
	Client.SetWorld(nullptr);
	Skybox.reset();
	MeshComponent = nullptr;
	SkyLight = nullptr;
	DirectionalLight = nullptr;
	if (PreviewWorld)
	{
		PreviewWorld->ClearWorld();
		PreviewWorld->FlushPendingDestruction();
		if (GEngine) GEngine->DestroyWorld(PreviewWorld);
		PreviewWorld = nullptr;
	}
	Sequence = nullptr;
	BoneChildren.Reset();
	SelectedBone = -1;
}

void FAnimSequenceViewerPanel::OpenSequence(const FString& InPath)
{
	FString Key = InPath;
	FString PathError;
	FString AssetKey;
	if (FbxPath::GetAssetKey(InPath, AssetKey, PathError, false)) Key = AssetKey;
	if (Key.ends_with(".bin")) Key.resize(Key.size() - 4);
	UAnimSequence* Loaded = UAssetManager::LoadAnimSequence(Key);
	if (!Loaded || !Loaded->Skeleton || !Loaded->GetDataModel())
	{
		HTR_LOG(Warning, "Cannot open animation sequence: {}", InPath);
		Release();
		Path = Key;
		Status = "Could not load sequence. Check Output Log for the file or skeleton error.";
		SetOpen(true);
		return;
	}
	Release();
	Path = Key;
	Sequence = Loaded;
	Status.clear();
	const auto& Skeleton = Sequence->Skeleton->RefSkeleton;
	BoneChildren.SetNum(Skeleton.GetNum());
	for (int32 Index = 0; Index < Skeleton.GetNum(); ++Index)
	{
		const int32 Parent = Skeleton.RefBoneInfo[Index].ParentIndex;
		if (Parent >= 0 && Parent < Skeleton.GetNum()) BoneChildren[Parent].Add(Index);
	}
	bPlaying = true;
	PreviewRate = 1.0f;
	PreviewWorld = UWorld::CreateWorld(EWorldType::Editor, false);
	if (!PreviewWorld) { Release(); return; }
	AActor* Actor = PreviewWorld->SpawnActor<AActor>();
	if (!Actor) { Release(); return; }
	MeshComponent = Cast<USkeletalMeshDebugComponent>(Actor->AddComponent(USkeletalMeshDebugComponent::StaticClass(), "SequencePreview"));
	if (!MeshComponent) { Release(); return; }
	Actor->SetRootComponent(MeshComponent);
	MeshComponent->bDrawBones = true;
	MeshComponent->SetAnimInstanceClass(UAnimSingleNodeInstance::StaticClass());
	SkyLight = PreviewWorld->SpawnActor<ASkyLightActor>();
	if (SkyLight)
	{
		SkyLight->GetBillboardComponent()->SetVisibility(false);
		SkyLight->GetLightComponent()->SetIntensity(SkyLightIntensity);
	}
	DirectionalLight = PreviewWorld->SpawnActor<ADirectionalLightActor>();
	if (DirectionalLight)
	{
		DirectionalLight->GetBillboardComponent()->SetVisibility(false);
		DirectionalLight->GetArrowComponent()->SetVisibility(false);
		DirectionalLight->GetLightComponent()->SetIntensity(DirectionalLightIntensity);
		DirectionalLight->GetLightComponent()->SetRelativeRotation(FRotator(40.0f, 160.0f, 0.0f));
	}
	Client.SetWorld(PreviewWorld);
	Client.SetViewportType(ELevelViewportType::Perspective);
	Client.Resize(800, 600);
	Skybox = MakeUnique<FSkyboxRenderer>();
	if (!Skybox->Init(EditorResourcePaths::AssetViewerSkyPanorama)) Skybox.reset();
	TArray<USkeletalMesh*> Meshes;
	UAssetManager::GetSkeletalMeshesForSkeleton(Sequence->Skeleton, Meshes);
	if (!Meshes.IsEmpty()) SetPreviewMesh(Meshes[0]);
	SetOpen(true);
}

void FAnimSequenceViewerPanel::SetPreviewMesh(USkeletalMesh* Mesh)
{
	if (!MeshComponent || !Sequence || (Mesh && Mesh->Skeleton != Sequence->Skeleton)) return;
	MeshComponent->SetSkeletalMesh(Mesh);
	SelectedBone = -1;
	if (!Mesh) return;
	MeshComponent->SetAnimationSequence(Sequence);
	if (auto* Player = Cast<UAnimSingleNodeInstance>(MeshComponent->GetAnimInstance()))
	{
		Player->Play(bLoop);
		Player->SetPlayRate(PreviewRate);
	}
	const FBox Bounds = MeshComponent->CalcBounds();
	const FVector Center = (Bounds.Min + Bounds.Max) * 0.5f;
	const float Radius = std::max(0.5f, (Bounds.Max - Bounds.Min).Size() * 0.5f);
	Client.SetViewRotation(FRotator(10.0f, 165.0f, 0.0f));
	Client.SetViewLocation(Center - Client.GetViewForward() * (Radius * 2.5f));
}

void FAnimSequenceViewerPanel::Tick(float DeltaTime)
{
	if (!Sequence || !PreviewWorld) return;
	if (Sequence->IsRetired())
	{
		OpenSequence(Path);
		return;
	}
	if (MeshComponent && MeshComponent->GetSkeletalMesh() && MeshComponent->GetSkeletalMesh()->IsRetired())
	{
		TArray<USkeletalMesh*> Meshes;
		UAssetManager::GetSkeletalMeshesForSkeleton(Sequence->Skeleton, Meshes);
		SetPreviewMesh(Meshes.IsEmpty() ? nullptr : Meshes[0]);
	}
	PreviewWorld->Tick(bPlaying ? DeltaTime : 0.0f);
	if (MeshComponent) MeshComponent->TickComponent(bPlaying ? DeltaTime : 0.0f);
	if (auto* Player = MeshComponent ? Cast<UAnimSingleNodeInstance>(MeshComponent->GetAnimInstance()) : nullptr)
		if (bPlaying && !Player->IsPlaying()) bPlaying = false;
}

void FAnimSequenceViewerPanel::DrawBoneTree(int32 Parent)
{
	if (!Sequence) return;
	const auto& Skeleton = Sequence->Skeleton->RefSkeleton;
	for (int32 Index = 0; Index < Skeleton.GetNum(); ++Index)
	{
		if (Skeleton.RefBoneInfo[Index].ParentIndex != Parent) continue;
		const bool HasChildren = BoneChildren.IsValidIndex(Index) && !BoneChildren[Index].IsEmpty();
		ImGuiTreeNodeFlags Flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
		if (!HasChildren) Flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
		if (SelectedBone == Index) Flags |= ImGuiTreeNodeFlags_Selected;
		const bool Open = ImGui::TreeNodeEx((void*)(intptr_t)(Index + 1), Flags, "%s", Skeleton.RefBoneInfo[Index].Name.c_str());
		if (ImGui::IsItemClicked())
		{
			SelectedBone = Index;
			if (MeshComponent) MeshComponent->SetSelectedBone(Index);
		}
		if (Open && HasChildren)
		{
			DrawBoneTree(Index);
			ImGui::TreePop();
		}
	}
}

void FAnimSequenceViewerPanel::DrawBoneDetails()
{
	if (!Sequence || !Sequence->Skeleton) return;
	const auto& Skeleton = Sequence->Skeleton->RefSkeleton;
	if (!Skeleton.RefBoneInfo.IsValidIndex(SelectedBone))
	{
		ImGui::TextUnformatted("Select a bone in the Skeleton Tree.");
		return;
	}
	ImGui::Text("Bone: %s", Skeleton.RefBoneInfo[SelectedBone].Name.c_str());
	const int32 Parent = Skeleton.RefBoneInfo[SelectedBone].ParentIndex;
	ImGui::Text("Parent: %s", Parent >= 0 ? Skeleton.RefBoneInfo[Parent].Name.c_str() : "None");
	if (MeshComponent && MeshComponent->GetBoneTransforms().IsValidIndex(SelectedBone))
		ShowTransform("Current local", FBoneTransform::FromMatrix(MeshComponent->GetBoneTransforms()[SelectedBone]));
	if (Skeleton.RefBonePose.IsValidIndex(SelectedBone))
		ShowTransform("Reference local", Skeleton.RefBonePose[SelectedBone]);
	if (MeshComponent && MeshComponent->GetGlobalBoneTransforms().IsValidIndex(SelectedBone))
		ShowTransform("Mesh relative", FBoneTransform::FromMatrix(MeshComponent->GetGlobalBoneTransforms()[SelectedBone]));
}

void FAnimSequenceViewerPanel::DrawAssetDetails()
{
	if (!Sequence || !Sequence->GetDataModel()) return;
	const UAnimDataModel* Model = Sequence->GetDataModel();
	ImGui::Text("%s", GetAnimSequenceDisplayName(Path, Sequence->Skeleton->GetPath()).c_str());
	ImGui::TextWrapped("Source FBX: %s", Sequence->SourceFilePath.c_str());
	ImGui::Text("Take: %s (%d)", Sequence->Name.c_str(), Sequence->SourceTakeIndex);
	ImGui::Text("Skeleton: %s", Sequence->Skeleton->GetPath().c_str());
	ImGui::Text("Length: %.3f s", Model->PlayLength);
	ImGui::Text("Frames: %d  /  %.2f fps", Model->NumberOfFrames, Model->FrameRate.AsDecimal());
	ImGui::Separator();
	ImGui::DragFloat("Rate Scale", &Sequence->RateScale, 0.01f, 0.01f, 8.0f, "%.2f");
	Sequence->RateScale = std::clamp(Sequence->RateScale, 0.01f, 8.0f);
	if (ImGui::CollapsingHeader("Root Motion", ImGuiTreeNodeFlags_DefaultOpen))
	{
		ImGui::Checkbox("Enable Root Motion", &Sequence->bEnableRootMotion);
		ImGui::Checkbox("Force Root Lock", &Sequence->bForceRootLock);
		ImGui::Checkbox("Normalized Root Motion Scale", &Sequence->bUseNormalizedRootMotionScale);
		int RootLock = static_cast<int>(Sequence->RootMotionRootLock);
		if (ImGui::Combo("Root Lock", &RootLock, "Ref Pose\0Anim First Frame\0Zero\0"))
			Sequence->RootMotionRootLock = static_cast<ERootMotionRootLock>(RootLock);
	}
	if (ImGui::Button("Save Sequence"))
		Status = UAssetManager::SaveAnimSequence(*Sequence) ? "Saved" : "Save failed";
	ImGui::SameLine();
	ImGui::TextUnformatted(Status.c_str());
}

void FAnimSequenceViewerPanel::DrawViewport()
{
	ImVec2 Size = ImGui::GetContentRegionAvail();
	Size.x = std::max(1.0f, Size.x);
	Size.y = std::max(EditorUIScale::Px(100.0f), Size.y);
	Client.Resize(static_cast<uint32>(Size.x), static_cast<uint32>(Size.y));
	if (Client.GetColorTarget()) ImGui::Image(Client.GetColorTarget()->GetSRV(), Size);
	const bool Hovered = ImGui::IsItemHovered();
	const bool bCaptured = Hovered && ImGui::IsMouseDown(ImGuiMouseButton_Right);
	float Wheel = ImGui::GetIO().MouseWheel;
	// 우클릭으로 카메라를 잡은 동안 휠은 이동 속도를 바꾼다.
	if (bCaptured && Wheel != 0.0f)
	{
		CameraSpeed = std::clamp(CameraSpeed + Wheel * 0.5f, 0.1f, 20.0f);
		Wheel = 0.0f;
	}
	Client.TickInputFromImGui(ImGui::GetIO().DeltaTime, bCaptured, Hovered,
		static_cast<int32>(Wheel * 120), 4.0f * CameraSpeed, 0.2f);
	if (!MeshComponent || !MeshComponent->GetSkeletalMesh())
	{
		ImGui::SetCursorPos(ImVec2(ImGui::GetCursorPosX() + 10, ImGui::GetCursorPosY() - Size.y + 10));
		ImGui::TextUnformatted("Choose a preview mesh in Preview Scene Settings.");
	}
}

void FAnimSequenceViewerPanel::DrawTimeline()
{
	if (!Sequence || !Sequence->GetDataModel()) return;
	const UAnimDataModel* Model = Sequence->GetDataModel();
	const float FramesPerSecond = Model->FrameRate.AsDecimal();
	auto* Player = MeshComponent ? Cast<UAnimSingleNodeInstance>(MeshComponent->GetAnimInstance()) : nullptr;
	int Frame = Player ? std::clamp(static_cast<int>(std::round(Player->GetPosition() * FramesPerSecond)), 0, Model->NumberOfFrames) : 0;
	ImGui::Text("Frame %d / %d     %.3f / %.3f s", Frame, Model->NumberOfFrames,
		Player ? Player->GetPosition() : 0.0f, Model->PlayLength);
	const ImVec2 RulerStart = ImGui::GetCursorScreenPos();
	const float RulerWidth = std::max(1.0f, ImGui::GetContentRegionAvail().x);
	ImGui::InvisibleButton("##FrameRuler", ImVec2(RulerWidth, EditorUIScale::Px(25.0f)));
	const int32 TickStep = std::max(1, Model->NumberOfFrames / 12);
	for (int32 Tick = 0; Tick <= Model->NumberOfFrames; Tick += TickStep)
	{
		const float X = RulerStart.x + RulerWidth * Tick / Model->NumberOfFrames;
		ImGui::GetWindowDrawList()->AddLine(ImVec2(X, RulerStart.y),
			ImVec2(X, RulerStart.y + EditorUIScale::Px(8.0f)), ImGui::GetColorU32(ImGuiCol_TextDisabled));
		ImGui::GetWindowDrawList()->AddText(ImVec2(X + 2, RulerStart.y + EditorUIScale::Px(8.0f)),
			ImGui::GetColorU32(ImGuiCol_TextDisabled), std::to_string(Tick).c_str());
	}
	const float HeadX = RulerStart.x + RulerWidth * Frame / Model->NumberOfFrames;
	ImGui::GetWindowDrawList()->AddLine(ImVec2(HeadX, RulerStart.y),
		ImVec2(HeadX, RulerStart.y + EditorUIScale::Px(25.0f)), ImGui::GetColorU32(ImGuiCol_PlotLines), 2.0f);
	if (Player && ImGui::IsItemActive() && ImGui::IsMouseDown(ImGuiMouseButton_Left))
	{
		Frame = std::clamp(static_cast<int>(std::round((ImGui::GetMousePos().x - RulerStart.x) /
			RulerWidth * Model->NumberOfFrames)), 0, Model->NumberOfFrames);
		Player->SetPosition(Frame / FramesPerSecond);
		MeshComponent->TickComponent(0.0f);
	}
	ImGui::SetNextItemWidth(-1);
	if (ImGui::SliderInt("##Frame", &Frame, 0, Model->NumberOfFrames) && Player)
	{
		Player->SetPosition(Frame / FramesPerSecond);
		if (MeshComponent) MeshComponent->TickComponent(0.0f);
	}
	if (ImGui::Button("|<") && Player) { Player->SetPosition(0.0f); MeshComponent->TickComponent(0.0f); }
	ImGui::SameLine();
	if (ImGui::Button(bPlaying ? "Pause" : "Play") && Player)
	{
		bPlaying = !bPlaying;
		if (bPlaying)
		{
			if (Player->GetPosition() >= Model->PlayLength) Player->SetPosition(0.0f);
			Player->Play(bLoop);
		}
		else Player->Stop();
	}
	ImGui::SameLine();
	if (ImGui::Button(">|") && Player) { Player->SetPosition(Model->PlayLength); MeshComponent->TickComponent(0.0f); }
	ImGui::SameLine();
	if (ImGui::Checkbox("Loop", &bLoop) && Player && bPlaying) Player->Play(bLoop);
	ImGui::SameLine();
	ImGui::SetNextItemWidth(EditorUIScale::Px(100.0f));
	if (ImGui::DragFloat("Preview x", &PreviewRate, 0.05f, 0.05f, 4.0f, "%.2f") && Player)
	{
		PreviewRate = std::clamp(PreviewRate, 0.05f, 4.0f);
		Player->SetPlayRate(PreviewRate);
	}
}

void FAnimSequenceViewerPanel::DrawAssetBrowser()
{
	ImGui::InputText("Search", Search, sizeof(Search));
	if (!Sequence) return;
	TArray<UAnimSequence*> Sequences;
	UAssetManager::GetAllAnimSequences(Sequences);
	FString RequestedPath;
	for (UAnimSequence* Item : Sequences)
	{
		const FString DisplayName = GetAnimSequenceDisplayName(Item->GetPath(), Item->Skeleton->GetPath());
		if (Search[0] && DisplayName.find(Search) == FString::npos && Item->GetPath().find(Search) == FString::npos) continue;
		ImGui::PushID(Item->GetPath().c_str());
		if (ImGui::Selectable(DisplayName.c_str(), Item == Sequence)) RequestedPath = Item->GetPath();
		if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", Item->GetPath().c_str());
		ImGui::PopID();
	}
	if (!RequestedPath.empty() && RequestedPath != Path) OpenSequence(RequestedPath);
}

void FAnimSequenceViewerPanel::OnRender()
{
	if (!IsOpen()) return;
	const FString Title = (Sequence ? GetAnimSequenceDisplayName(Path, Sequence->Skeleton->GetPath()) : FString("Animation Sequence")) + "###AnimationSequenceViewer";
	ImGui::SetNextWindowSize(ImVec2(EditorUIScale::Px(1250.0f), EditorUIScale::Px(750.0f)), ImGuiCond_FirstUseEver);
	bVisible = BeginPanel(Title.c_str());
	if (bVisible && !Sequence)
	{
		ImGui::TextUnformatted(Path.c_str());
		ImGui::TextWrapped("%s", Status.c_str());
		ImGui::End();
		return;
	}
	if (bVisible)
	{
		const float Width = ImGui::GetContentRegionAvail().x;
		const float LeftWidth = std::min(EditorUIScale::Px(290.0f), Width * 0.27f);
		const float RightWidth = std::min(EditorUIScale::Px(310.0f), Width * 0.28f);
		ImGui::BeginChild("Left", ImVec2(LeftWidth, 0), ImGuiChildFlags_Borders);
		if (ImGui::BeginTabBar("LeftTabs"))
		{
			if (ImGui::BeginTabItem("Asset Details")) { DrawAssetDetails(); ImGui::EndTabItem(); }
			if (ImGui::BeginTabItem("Skeleton Tree"))
			{
				if (Sequence->Skeleton) DrawBoneTree(-1);
				ImGui::EndTabItem();
			}
			ImGui::EndTabBar();
		}
		ImGui::EndChild();
		ImGui::SameLine();
		ImGui::BeginChild("Center", ImVec2(std::max(1.0f, Width - LeftWidth - RightWidth - EditorUIScale::Px(20.0f)), 0));
		ImGui::BeginChild("Preview", ImVec2(0, -EditorUIScale::Px(115.0f)), ImGuiChildFlags_Borders);
		DrawViewport();
		ImGui::EndChild();
		ImGui::BeginChild("Timeline", ImVec2(0, 0), ImGuiChildFlags_Borders);
		DrawTimeline();
		ImGui::EndChild();
		ImGui::EndChild();
		ImGui::SameLine();
		ImGui::BeginChild("Right", ImVec2(0, 0));
		ImGui::BeginChild("Details", ImVec2(0, -EditorUIScale::Px(235.0f)), ImGuiChildFlags_Borders);
		if (ImGui::BeginTabBar("RightTabs"))
		{
			if (ImGui::BeginTabItem("Details")) { DrawBoneDetails(); ImGui::EndTabItem(); }
			if (ImGui::BeginTabItem("Preview Scene Settings"))
			{
				TArray<USkeletalMesh*> Meshes;
				UAssetManager::GetSkeletalMeshesForSkeleton(Sequence->Skeleton, Meshes);
				USkeletalMesh* Current = MeshComponent ? MeshComponent->GetSkeletalMesh() : nullptr;
				if (ImGui::BeginCombo("Preview Mesh", Current ? Current->GetPath().c_str() : "None"))
				{
					for (USkeletalMesh* Mesh : Meshes)
						if (ImGui::Selectable(Mesh->GetPath().c_str(), Mesh == Current)) SetPreviewMesh(Mesh);
					ImGui::EndCombo();
				}
				if (MeshComponent)
				{
					ImGui::Checkbox("Show Bones", &MeshComponent->bDrawBones);
					ImGui::Checkbox("Show Bone AABBs", &MeshComponent->bDrawBoneBounds);
				}
				ImGui::Checkbox("Show AABB", &bShowBounds);
				ImGui::Checkbox("Show Grid", &bShowGrid);
				ImGui::Checkbox("Show Skybox", &bShowSkybox);
				if (ImGui::DragFloat("Sky Light", &SkyLightIntensity, 0.01f, 0.0f, 5.0f) && SkyLight)
					SkyLight->GetLightComponent()->SetIntensity(std::clamp(SkyLightIntensity, 0.0f, 5.0f));
				if (ImGui::DragFloat("Directional Light", &DirectionalLightIntensity, 0.01f, 0.0f, 5.0f) && DirectionalLight)
					DirectionalLight->GetLightComponent()->SetIntensity(std::clamp(DirectionalLightIntensity, 0.0f, 5.0f));
				ImGui::EndTabItem();
			}
			ImGui::EndTabBar();
		}
		ImGui::EndChild();
		ImGui::BeginChild("Asset Browser", ImVec2(0, 0), ImGuiChildFlags_Borders);
		ImGui::TextUnformatted("Asset Browser");
		DrawAssetBrowser();
		ImGui::EndChild();
		ImGui::EndChild();
	}
	ImGui::End();
}

void FAnimSequenceViewerPanel::RenderPreview(UEditorEngine* Engine)
{
	if (Engine && IsOpen() && bVisible && PreviewWorld)
		Engine->RenderPreviewViewport(&Client, PreviewWorld, nullptr, bShowSkybox ? Skybox.get() : nullptr,
			bShowGrid, bShowBounds && MeshComponent && MeshComponent->GetSkeletalMesh() ? MeshComponent : nullptr);
}
