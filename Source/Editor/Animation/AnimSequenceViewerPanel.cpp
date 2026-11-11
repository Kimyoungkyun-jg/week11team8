#include "EnginePCH.h"
#include "Editor/Animation/AnimSequenceViewerPanel.h"
#include "Editor/Animation/AnimSequenceDisplayName.h"
#include "Editor/Details/PropertyEditor.h"
#include "Animation/AnimNotify.h"
#include "UObject/UObjectHash.h"
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

	// 만들 수 있는(추상이 아닌) UAnimNotify 파생 클래스를 이름순으로 모은다.
	void GetNotifyClasses(TArray<UClass*>& OutClasses)
	{
		OutClasses.Reset();
		TArray<UClass*> Derived;
		GetDerivedClasses(UAnimNotify::StaticClass(), Derived);
		for (UClass* Class : Derived)
		{
			if (Class && Class->Constructor)
			{
				OutClasses.Add(Class);
			}
		}
		std::sort(OutClasses.begin(), OutClasses.end(), [](const UClass* A, const UClass* B) { return A->Name < B->Name; });
	}

	// "UAnimNotify_PlaySound" -> "PlaySound"
	FString GetNotifyDisplayName(const UClass* Class)
	{
		const FString Prefix = "UAnimNotify_";
		return Class->Name.starts_with(Prefix) ? Class->Name.substr(Prefix.size()) : Class->Name;
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
	SelectedNotify = -1;
	DraggingNotify = -1;
	NotifyPopupTarget = -1;
	bDirty = false;
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

void FAnimSequenceViewerPanel::Save()
{
	if (!Sequence)
	{
		return;
	}
	if (UAssetManager::SaveAnimSequence(*Sequence))
	{
		bDirty = false;
		Status = "Saved";
	}
	else
	{
		Status = "Save failed";
	}
}

void FAnimSequenceViewerPanel::Tick(float DeltaTime)
{
	if (!IsOpen())
	{
		bFocused = false;
		// 창을 완전히 닫으면 프리뷰 월드와 메시를 해제한다. 다시 열면 OnRender에서 복원한다.
		if (PreviewWorld)
		{
			Release();
			bRestorePreviewOnOpen = true;
		}
		return;
	}
	// 도킹된 탭이 가려져 있으면 재생하지 않는다. 월드는 그대로 둔다.
	if (!bVisible)
	{
		return;
	}
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
	if (ImGui::DragFloat("Rate Scale", &Sequence->RateScale, 0.01f, 0.01f, 8.0f, "%.2f")) MarkDirty();
	Sequence->RateScale = std::clamp(Sequence->RateScale, 0.01f, 8.0f);
	if (ImGui::CollapsingHeader("Root Motion", ImGuiTreeNodeFlags_DefaultOpen))
	{
		if (ImGui::Checkbox("Enable Root Motion", &Sequence->bEnableRootMotion)) MarkDirty();
		if (ImGui::Checkbox("Force Root Lock", &Sequence->bForceRootLock)) MarkDirty();
		if (ImGui::Checkbox("Normalized Root Motion Scale", &Sequence->bUseNormalizedRootMotionScale)) MarkDirty();
		int RootLock = static_cast<int>(Sequence->RootMotionRootLock);
		if (ImGui::Combo("Root Lock", &RootLock, "Ref Pose\0Anim First Frame\0Zero\0"))
		{
			Sequence->RootMotionRootLock = static_cast<ERootMotionRootLock>(RootLock);
			MarkDirty();
		}
	}
	if (ImGui::Button("Save Sequence"))
		Save();
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
	DrawNotifyTrack(RulerWidth);
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

// 눈금자 아래의 Notify 줄. 마커를 눌러 고르고 끌어서 옮기고, 우클릭으로 추가·삭제한다.
void FAnimSequenceViewerPanel::DrawNotifyTrack(float TrackWidth)
{
	if (!Sequence || !Sequence->GetDataModel() || Sequence->GetDataModel()->PlayLength <= 0.0f)
	{
		return;
	}
	const UAnimDataModel* Model = Sequence->GetDataModel();
	const float Length = Model->PlayLength;
	TArray<FAnimNotifyEvent>& Notifies = Sequence->Notifies;
	ImDrawList* DrawList = ImGui::GetWindowDrawList();

	const ImVec2 Start = ImGui::GetCursorScreenPos();
	const float Height = EditorUIScale::Px(22.0f);
	const ImVec2 End(Start.x + TrackWidth, Start.y + Height);
	ImGui::InvisibleButton("##NotifyTrack", ImVec2(TrackWidth, Height));
	const bool bHovered = ImGui::IsItemHovered();
	DrawList->AddRectFilled(Start, End, ImGui::GetColorU32(ImGuiCol_FrameBg));

	auto TimeToX = [&](float Time) { return Start.x + TrackWidth * Time / Length; };
	auto XToTime = [&](float X) { return std::clamp((X - Start.x) / TrackWidth * Length, 0.0f, Length); };

	// 마우스에 가장 가까운 마커. 같은 위치면 위에 그려진(뒤에 있는) 쪽이 잡힌다.
	const ImVec2 Mouse = ImGui::GetMousePos();
	int32 Hit = -1;
	if (bHovered)
	{
		float Best = EditorUIScale::Px(7.0f);
		for (int32 Index = 0; Index < Notifies.Num(); ++Index)
		{
			const float Distance = std::abs(TimeToX(Notifies[Index].TriggerTime) - Mouse.x);
			if (Distance <= Best)
			{
				Best = Distance;
				Hit = Index;
			}
		}
	}

	if (bHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
	{
		SelectedNotify = Hit;
		DraggingNotify = Hit;
		bFocusNotifyTab = Hit >= 0;
	}
	if (bHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
	{
		NotifyPopupTime = XToTime(Mouse.x);
		NotifyPopupTarget = Hit;
		if (Hit >= 0)
		{
			SelectedNotify = Hit;
		}
		ImGui::OpenPopup("NotifyContext");
	}

	// 끄는 동안은 마우스가 줄 밖으로 나가도 이어간다. 기본은 프레임에 맞추고 Alt를 누르면 자유롭게 옮긴다.
	if (DraggingNotify >= 0)
	{
		if (ImGui::IsMouseDown(ImGuiMouseButton_Left) && Notifies.IsValidIndex(DraggingNotify))
		{
			float Time = XToTime(Mouse.x);
			const float FramesPerSecond = Model->FrameRate.AsDecimal();
			if (!ImGui::GetIO().KeyAlt && FramesPerSecond > 0.0f)
			{
				Time = std::clamp(std::round(Time * FramesPerSecond) / FramesPerSecond, 0.0f, Length);
			}
			if (Notifies[DraggingNotify].TriggerTime != Time)
			{
				Notifies[DraggingNotify].TriggerTime = Time;
				MarkDirty();
			}
		}
		else
		{
			DraggingNotify = -1;
		}
	}

	DrawList->PushClipRect(Start, End, true);
	const float Size = EditorUIScale::Px(6.0f);
	const float Middle = Start.y + Height * 0.5f;
	for (int32 Index = 0; Index < Notifies.Num(); ++Index)
	{
		const FAnimNotifyEvent& Event = Notifies[Index];
		const float X = TimeToX(Event.TriggerTime);
		const bool bSelected = Index == SelectedNotify;
		const ImU32 Color = bSelected ? IM_COL32(255, 170, 0, 255)
			: (Index == Hit ? IM_COL32(255, 220, 120, 255) : ImGui::GetColorU32(ImGuiCol_PlotHistogram));
		DrawList->AddLine(ImVec2(X, Start.y), ImVec2(X, End.y), Color);
		DrawList->AddTriangleFilled(ImVec2(X - Size, Middle - Size), ImVec2(X + Size, Middle - Size), ImVec2(X, Middle + Size), Color);
		DrawList->AddText(ImVec2(X + Size + 2.0f, Start.y + 2.0f), ImGui::GetColorU32(ImGuiCol_Text), Event.Name.c_str());
	}
	DrawList->PopClipRect();

	if (bHovered && Hit >= 0)
	{
		ImGui::SetTooltip("%s  %.3f s\n%s", Notifies[Hit].Name.c_str(), Notifies[Hit].TriggerTime,
			Notifies[Hit].Notify ? Notifies[Hit].Notify->GetClass()->Name.c_str() : "None");
	}

	if (ImGui::BeginPopup("NotifyContext"))
	{
		if (Notifies.IsValidIndex(NotifyPopupTarget))
		{
			if (ImGui::MenuItem("Delete Notify"))
			{
				Notifies.RemoveAt(NotifyPopupTarget);
				SelectedNotify = -1;
				DraggingNotify = -1;
				MarkDirty();
			}
		}
		else
		{
			ImGui::Text("%.3f s", NotifyPopupTime);
			ImGui::Separator();
			if (ImGui::BeginMenu("Add Notify"))
			{
				TArray<UClass*> Classes;
				GetNotifyClasses(Classes);
				for (UClass* Class : Classes)
				{
					if (ImGui::MenuItem(GetNotifyDisplayName(Class).c_str()) &&
						Sequence->AddNotify(GetNotifyDisplayName(Class), Class->Name, NotifyPopupTime))
					{
						SelectedNotify = Notifies.Num() - 1;
						bFocusNotifyTab = true;
						MarkDirty();
					}
				}
				ImGui::EndMenu();
			}
		}
		ImGui::EndPopup();
	}
}

void FAnimSequenceViewerPanel::DrawNotifyDetails()
{
	if (!Sequence || !Sequence->GetDataModel())
	{
		return;
	}
	TArray<FAnimNotifyEvent>& Notifies = Sequence->Notifies;
	ImGui::Text("Notifies: %d", Notifies.Num());
	ImGui::BeginChild("NotifyList", ImVec2(0, EditorUIScale::Px(90.0f)), ImGuiChildFlags_Borders);
	for (int32 Index = 0; Index < Notifies.Num(); ++Index)
	{
		ImGui::PushID(Index);
		const FString Label = Notifies[Index].Name + "  (" + std::format("{:.3f}", Notifies[Index].TriggerTime) + " s)";
		if (ImGui::Selectable(Label.c_str(), Index == SelectedNotify))
		{
			SelectedNotify = Index;
		}
		ImGui::PopID();
	}
	ImGui::EndChild();

	if (!Notifies.IsValidIndex(SelectedNotify))
	{
		ImGui::TextWrapped("Right-click the notify track under the ruler to add a notify.");
		return;
	}
	FAnimNotifyEvent& Event = Notifies[SelectedNotify];
	ImGui::Separator();
	ImGui::TextDisabled("%s", Event.Notify ? Event.Notify->GetClass()->Name.c_str() : "None");

	char NameBuffer[128]{};
	std::snprintf(NameBuffer, sizeof(NameBuffer), "%s", Event.Name.c_str());
	if (ImGui::InputText("Name", NameBuffer, sizeof(NameBuffer)))
	{
		Event.Name = NameBuffer;
		MarkDirty();
	}
	if (ImGui::DragFloat("Time", &Event.TriggerTime, 0.001f, 0.0f, Sequence->GetDataModel()->PlayLength, "%.3f s"))
	{
		Event.TriggerTime = std::clamp(Event.TriggerTime, 0.0f, Sequence->GetDataModel()->PlayLength);
		MarkDirty();
	}
	if (ImGui::DragFloat("Threshold", &Event.TriggerWeightThreshold, 0.01f, 0.0f, 1.0f, "%.3f"))
	{
		MarkDirty();
	}
	ImGui::SetItemTooltip("Weight Threshold\nThe notify is skipped when its blend weight is below this value.\nWeight is 1 for a sequence played alone and drops while blending (transitions, BlendSpace).\nRaise it (e.g. 0.5) so only the dominant animation triggers the notify.");
	if (Event.Notify)
	{
		FPropertyEditor::DrawObjectProperties(Event.Notify);
		if (ImGui::IsAnyItemActive())
		{
			MarkDirty();
		}
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
	if (bRestorePreviewOnOpen)
	{
		bRestorePreviewOnOpen = false;
		if (!Path.empty())
		{
			OpenSequence(Path);
		}
	}
	const FString Title = (Sequence ? GetAnimSequenceDisplayName(Path, Sequence->Skeleton->GetPath()) : FString("Animation Sequence")) + "###AnimationSequenceViewer";
	ImGui::SetNextWindowSize(ImVec2(EditorUIScale::Px(1250.0f), EditorUIScale::Px(750.0f)), ImGuiCond_FirstUseEver);
	bVisible = BeginPanel(Title.c_str());
	bFocused = bVisible && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
	// Ctrl+S: 이 창에 포커스가 있을 때만 시퀀스를 저장한다. 이때는 레벨 에디터의 Ctrl+S가 동작하지 않는다 (EditorEngine::BeginFrame).
	// Ctrl+Shift+S는 전체 저장이라 여기서 받지 않는다.
	const ImGuiIO& Io = ImGui::GetIO();
	if (bFocused && Sequence && Io.KeyCtrl && !Io.KeyShift && !Io.KeyAlt && ImGui::IsKeyPressed(ImGuiKey_S, false))
	{
		Save();
	}
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
		ImGui::BeginChild("Preview", ImVec2(0, -EditorUIScale::Px(150.0f)), ImGuiChildFlags_Borders);
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
			// 마커를 새로 골랐을 때 한 번만 이 탭으로 전환한다.
			const ImGuiTabItemFlags NotifyTabFlags = bFocusNotifyTab ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None;
			bFocusNotifyTab = false;
			if (ImGui::BeginTabItem("Notify", nullptr, NotifyTabFlags)) { DrawNotifyDetails(); ImGui::EndTabItem(); }
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
				if (MeshComponent) ImGui::Checkbox("Show Bones", &MeshComponent->bDrawBones);
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
		Engine->RenderPreviewViewport(&Client, PreviewWorld, nullptr, bShowSkybox ? Skybox.get() : nullptr, bShowGrid);
}
