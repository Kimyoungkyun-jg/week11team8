#include "EnginePCH.h"
#include "Components/SkeletalMeshDebugComponent.h"
#include "Asset/FbxImporter/FbxDebugScene.h"
#include "Editor/HitoriEd/EditorResourcePaths.h"
#include "SkeletalMeshViewer.h"
#include "Asset/AssetManager.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Actor/SkeletalMeshActor.h"
#include "GameFramework/Actor/SkyLightActor.h"
#include "GameFramework/Actor/DirectionalLightActor.h"
#include "Render/SkeletalMesh.h"
#include "Render/SkyboxRenderer.h"
#include "Render/RenderCommand.h"
#include "Editor/EditorUI/EditorUIScale.h"
#include "Asset/FbxImporter/SkeletalMeshBake.h"
#include "Core/EngineLog.h"
#include "Collision/Ray.h"
#include "UObject/UObjectGlobals.h"
#include <imgui_internal.h>

namespace
{
	void RefreshBoneHierarchy(FSkeletalMeshViewer& Viewer)
	{
		const auto* Mesh = Viewer.MeshComponent ? Viewer.MeshComponent->GetSkeletalMesh() : nullptr;
		if (!Mesh) return;
		const auto& Bones = Mesh->MeshData.Bones;
		Viewer.Children.Reset(); Viewer.Children.SetNum(Bones.Num());
		Viewer.WeightedVertexCounts.Reset(); Viewer.WeightedVertexCounts.SetNum(Bones.Num());
		for (int I = 0; I < Bones.Num(); ++I) if (Bones[I].ParentIndex >= 0) Viewer.Children[Bones[I].ParentIndex].Add(I);
		for (const auto& Vertex : Mesh->MeshData.SkinnedVertices)
			for (int I = 0; I < 4; ++I)
				if (Vertex.BoneWeights[I] > 0 && Vertex.BoneIndices[I] >= 0 && Vertex.BoneIndices[I] < Bones.Num())
					++Viewer.WeightedVertexCounts[Vertex.BoneIndices[I]];
	}
	FTransform DecomposeMatrix(const FMatrix& M)
	{
		FTransform Out;
		Out.Location = FVector(M[3][0], M[3][1], M[3][2]);

		const FVector X(M[0][0], M[0][1], M[0][2]);
		const FVector Y(M[1][0], M[1][1], M[1][2]);
		const FVector Z(M[2][0], M[2][1], M[2][2]);
		
		Out.Scale = FVector(X.Size(), Y.Size(), Z.Size());

		FMatrix Rotation = FMatrix::Identity;
		Rotation.SetAxis(0, X.Normalized());
		Rotation.SetAxis(1, Y.Normalized());
		Rotation.SetAxis(2, Z.Normalized());
		Out.Rotation = MatrixToRotator(Rotation);

		return Out;
	}

	bool DrawAxisControl(const FString& _label, float& _value, float _speed, float _minValue, float _maxValue, float _resetValue, FVector4 _color)
	{
		bool isValueChanged = false;

		// 배율이 적용된 프레임 높이를 써야 입력칸과 버튼 높이가 맞는다 (TEAM5).
		float lineHeight = ImGui::GetFrameHeight();
		ImVec4 color(_color.X, _color.Y, _color.Z, _color.W);
		ImVec2 buttonSize = { lineHeight + 2.0f , lineHeight };

		ImGui::PushStyleColor(ImGuiCol_Button, color);
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4{ color.x + 0.1f, color.y + 0.1f, color.z + 0.1f, 1.0f });
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4{ color.x - 0.1f, color.y - 0.1f, color.z - 0.1f, 1.0f });

		if (ImGui::Button(_label.c_str(), buttonSize))
		{
			_value = _resetValue;
			isValueChanged = true;
		}
		ImGui::PopStyleColor(3);

		ImGui::SameLine();
		std::string dragID = "##" + _label;
		isValueChanged |= ImGui::DragFloat(dragID.c_str(), &_value, _speed, _minValue, _maxValue, "%.2f");

		return isValueChanged;
	}

	bool DrawVector3Controller(const FString& _label, float* _values, float _resetValue, float _speed = 0.05f)
	{
		ImGuiIO& io = ImGui::GetIO();
		auto boldFont = !io.Fonts->Fonts.empty() ? io.Fonts->Fonts[0] : nullptr;

		// 저장된 열 폭이 있으면 초기 폭을 무시하므로 저장하지 않는다 (TEAM5).
		ImGuiTableFlags flags = ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_Resizable | ImGuiTableFlags_NoSavedSettings;
		bool isVectorChanged = false;

		ImGui::PushID(_label.c_str());
		if (ImGui::BeginTable(_label.c_str(), 2, flags))
		{
			ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed, ImGui::CalcTextSize("Location").x);
			ImGui::TableSetupColumn("Content", ImGuiTableColumnFlags_WidthStretch);

			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::AlignTextToFramePadding();
			ImGui::TextUnformatted(_label.c_str());

			ImGui::TableSetColumnIndex(1);
			// 축 버튼 3개와 그 사이 간격(5칸)을 뺀 폭을 입력칸 3개가 나눠 쓴다 (TEAM5).
			const float AxisButtonWidth = ImGui::GetFrameHeight() + 2.0f;
			const float AxisSpacing = EditorUIScale::Px(2.0f);
			ImGui::PushMultiItemsWidths(3, ImGui::GetContentRegionAvail().x - AxisButtonWidth * 3.0f - AxisSpacing * 5.0f);
			ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2{ AxisSpacing, 0 });
			if (boldFont) ImGui::PushFont(boldFont);

			isVectorChanged |= DrawAxisControl("X", _values[0], _speed, 0.0f, 0.0f, _resetValue, { 0.8f, 0.1f, 0.1f, 1.0f });

			ImGui::PopItemWidth();

			ImGui::SameLine();
			isVectorChanged |= DrawAxisControl("Y", _values[1], _speed, 0.0f, 0.0f, _resetValue, { 0.1f, 0.8f, 0.1f, 1.0f });

			ImGui::PopItemWidth();

			ImGui::SameLine();
			isVectorChanged |= DrawAxisControl("Z", _values[2], _speed, 0.0f, 0.0f, _resetValue, { 0.1f, 0.1f, 0.8f, 1.0f });

			ImGui::PopItemWidth();

			if (boldFont) ImGui::PopFont();
			ImGui::PopStyleVar();
			ImGui::EndTable();
		}

		ImGui::PopID();

		return isVectorChanged;
	}

	bool DrawRotatorAsXYZ(const FString& _label, FRotator& _rotator, float _speed = 0.5f)
	{
		float Euler[3] = { _rotator.Roll, _rotator.Pitch, _rotator.Yaw };
		const bool isChanged = DrawVector3Controller(_label, Euler, 0.0f, _speed);
		if (isChanged)
		{
			_rotator.Roll = Euler[0];
			_rotator.Pitch = Euler[1];
			_rotator.Yaw = Euler[2];
		}
		return isChanged;
	}

	void DrawBoneIcon(ImDrawList* DL, ImVec2 Center, float Size, ImU32 Color)
	{
		const float Half = Size * 0.5f;
		const ImVec2 P1(Center.x - Half * 0.7f, Center.y + Half * 0.7f);
		const ImVec2 P2(Center.x + Half * 0.7f, Center.y - Half * 0.7f);
		DL->AddLine(P1, P2, Color, EditorUIScale::Px(2.0f));
		const float R = EditorUIScale::Px(2.0f);
		const float Offset = EditorUIScale::Px(1.8f);
		DL->AddCircleFilled(ImVec2(P1.x - Offset, P1.y - Offset), R, Color);
		DL->AddCircleFilled(ImVec2(P1.x + Offset, P1.y + Offset), R, Color);
		DL->AddCircleFilled(ImVec2(P2.x - Offset, P2.y - Offset), R, Color);
		DL->AddCircleFilled(ImVec2(P2.x + Offset, P2.y + Offset), R, Color);
	}
	void DrawDotIcon(ImDrawList* DL, ImVec2 Center, float Radius, ImU32 Color)
	{
		DL->AddCircleFilled(Center, Radius, Color);
	}
}

FSkeletalMeshViewer::~FSkeletalMeshViewer()
{
	delete BoneGizmoTarget;
	Client.SetWorld(nullptr);
	if (World)
		GEngine->DestroyWorld(World);
}

void FSkeletalMeshViewerPanel::Tick(float DeltaTime)
{
	for (TUniquePtr<FSkeletalMeshViewer>& Viewer : Viewers)
	{
		auto* Current = Viewer->MeshComponent ? Viewer->MeshComponent->GetSkeletalMesh() : nullptr;
		if (Current && Current->IsRetired())
		{
			if (auto* Updated = UAssetManager::GetAssetByPath<USkeletalMesh>(Viewer->Path))
			{
				const FString SelectedName = Viewer->SelectedBone >= 0 && Viewer->SelectedBone < Current->RefSkeleton.GetNum()
					? Current->RefSkeleton.RefBoneInfo[Viewer->SelectedBone].Name : FString();
				Viewer->MeshComponent->SetSkeletalMesh(Updated); RefreshBoneHierarchy(*Viewer);
				SelectBone(*Viewer, SelectedName.empty() ? -1 : Updated->RefSkeleton.FindBoneIndex(SelectedName));
				Viewer->bScrollTreeToSelection = true;
			}
		}
		if (Viewer->SaveNotificationTimer > 0.0f)
		{
			Viewer->SaveNotificationTimer -= DeltaTime;
		}
		if (Viewer->ViewSize.x >= 1.0f && Viewer->ViewSize.y >= 1.0f)
			Viewer->Client.Resize(static_cast<uint32>(Viewer->ViewSize.x), static_cast<uint32>(Viewer->ViewSize.y));
	}
}

void FSkeletalMeshViewerPanel::OpenFBX(const FString& Path)
{
	USkeletalMesh* Mesh = UAssetManager::LoadFbxSkeletalMesh(Path);
	if (!Mesh)
	{
		HTR_LOG(Error, "Failed to open skeletal mesh viewer: {}", Path);
		return;
	}

	TUniquePtr<FSkeletalMeshViewer> Viewer = MakeUnique<FSkeletalMeshViewer>();
	Viewer->Path = Path;
	Viewer->Id = NextId++;

	// World 구성
	Viewer->World = UWorld::CreateWorld(EWorldType::Editor, false);
	AActor* PreviewActor = Viewer->World->SpawnActor<AActor>();
	Viewer->MeshComponent = Cast<USkeletalMeshDebugComponent>(PreviewActor->AddComponent(USkeletalMeshDebugComponent::StaticClass(), "SkeletalMeshDebug"));
	PreviewActor->SetRootComponent(Viewer->MeshComponent);
	Viewer->MeshComponent->SetSkeletalMesh(Mesh);

	const TArray<FBoneInfo>& Bones = Mesh->MeshData.Bones;
	Viewer->Children.SetNum(Bones.Num());
	Viewer->WeightedVertexCounts.SetNum(Bones.Num());
	for (const auto& Vertex : Mesh->MeshData.SkinnedVertices)
		for (int I = 0; I < 4; ++I)
			if (Vertex.BoneWeights[I] > 0 && Vertex.BoneIndices[I] >= 0 && Vertex.BoneIndices[I] < Bones.Num())
				++Viewer->WeightedVertexCounts[Vertex.BoneIndices[I]];
	for (int32 i = 0; i < Bones.Num(); ++i)
		if (Bones[i].ParentIndex >= 0)
			Viewer->Children[Bones[i].ParentIndex].Add(i);

	// 스카이라이트 스폰
	if (ASkyLightActor* SkyLight = Viewer->World->SpawnActor<ASkyLightActor>())
	{
		SkyLight->GetBillboardComponent()->SetVisibility(false);
		SkyLight->GetLightComponent()->SetIntensity(0.3f);
	}

	// 기본 디렉셔널라이트 스폰
	if (ADirectionalLightActor* DirLight = Viewer->World->SpawnActor<ADirectionalLightActor>())
	{
		DirLight->GetBillboardComponent()->SetVisibility(false);
		DirLight->GetArrowComponent()->SetVisibility(false);
		DirLight->GetLightComponent()->SetIntensity(1.0f);
		DirLight->GetLightComponent()->SetRelativeRotation(FRotator(40.0f, 160.0f, 0.0f));
	}

	// 뷰어 전용 스카이박스 초기화
	Viewer->Skybox = MakeUnique<FSkyboxRenderer>();
	if (!Viewer->Skybox->Init(EditorResourcePaths::AssetViewerSkyPanorama))
	{
		Viewer->Skybox.reset();
	}

	// Visible 아이콘 로드
	Viewer->ShowFlagsIcon = UAssetManager::Get().LoadTexture(EditorResourcePaths::VisibleIcon);
	if (!Viewer->ShowFlagsIcon)
	{
		Viewer->ShowFlagsIcon = UAssetManager::Get().LoadTexture(EditorResourcePaths::VisibleIcon);
	}

	Viewer->Client.SetWorld(Viewer->World);
	Viewer->Client.SetViewportType(ELevelViewportType::Perspective);
	Viewer->ViewSize = ImVec2(800.0f, 600.0f);
	Viewer->Client.Resize(800, 600);

	// Mesh 정면을 바라보도록 카메라 위치 및 회전 설정 (FOV 60 기준)
	const FBox Bounds = Viewer->MeshComponent->CalcBounds();
	const FVector Center = (Bounds.Min + Bounds.Max) * 0.5f;
	const float Radius = std::max(0.5f, (Bounds.Max - Bounds.Min).Size() * 0.5f);
	const float HalfFov = FMath::DegreesToRadians(Viewer->Client.GetViewFOV()) * 0.5f;
	const float Distance = Radius / std::max(0.2f, std::sin(HalfFov)) * 1.25f;

	Viewer->Client.SetViewRotation(FRotator(10.0f, 165.0f, 0.0f));
	Viewer->Client.SetViewLocation(Center - Viewer->Client.GetViewForward() * Distance);

	Viewer->BoneGizmoTarget = NewObject<USceneComponent>();

	Viewers.Add(std::move(Viewer));
	SetOpen(true);
}

void FSkeletalMeshViewerPanel::Release()
{
	Viewers.Reset();
}

void FSkeletalMeshViewerPanel::OnRender()
{
	bHasFocusedViewer = false;
	if (Viewers.IsEmpty())
	{
		if (BeginPanel())
			ImGui::TextWrapped("Double-click a skeletal mesh in the Content Drawer to open a viewer.");
		ImGui::End();
		return;
	}

	// x 버튼으로 닫은 뷰어는 다음 프레임 맨 앞에서 지운다
	for (int32 i = Viewers.Num() - 1; i >= 0; --i)
		if (!Viewers[i]->bOpen)
			Viewers.RemoveAt(i);
	if (Viewers.IsEmpty())
	{
		SetOpen(false);
		return;
	}

	for (TUniquePtr<FSkeletalMeshViewer>& Viewer : Viewers)
	{
		const FString Title = Viewer->Path + "###SkeletalMeshViewer" + std::to_string(Viewer->Id);
		ImGui::SetNextWindowSize(ImVec2(EditorUIScale::Px(800.0f), EditorUIScale::Px(600.0f)), ImGuiCond_FirstUseEver);
		PreparePanelWindow(Title.c_str());
		Viewer->bVisible = ImGui::Begin(Title.c_str(), &Viewer->bOpen);
		if (Viewer->bVisible)
		{
			const bool bFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
			bHasFocusedViewer |= bFocused;
			if (bFocused && !Viewer->Gizmo.IsUsing() && ImGui::IsKeyPressed(ImGuiKey_Space, false))
			{
				const int NextMode = (static_cast<int>(Viewer->Gizmo.GetMode()) + 1) % 3;
				Viewer->Gizmo.SetMode(static_cast<EGizmoMode>(NextMode));
			}

			ImGui::BeginChild("Bones", ImVec2(EditorUIScale::Px(280.0f), 0.0f), ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeX);
			const float AvailableHeight = ImGui::GetContentRegionAvail().y;
			const float SplitterHeight = EditorUIScale::Px(6.0f);
			Viewer->DetailsHeight = std::clamp(Viewer->DetailsHeight, EditorUIScale::Px(100.0f), std::max(EditorUIScale::Px(100.0f), AvailableHeight - EditorUIScale::Px(80.0f)));
			const float TreeHeight = std::max(EditorUIScale::Px(50.0f), AvailableHeight - Viewer->DetailsHeight - SplitterHeight);

			ImGui::BeginChild("Tree", ImVec2(0.0f, TreeHeight));
			DrawBoneTree(*Viewer);
			ImGui::EndChild();

			// 디테일창 높이 조절 스플리터
			ImGui::InvisibleButton("##DetailsSplitter", ImVec2(ImGui::GetContentRegionAvail().x, SplitterHeight));
			const ImVec2 SplitMin = ImGui::GetItemRectMin();
			const ImVec2 SplitMax = ImGui::GetItemRectMax();
			ImGui::GetWindowDrawList()->AddLine(
				ImVec2(SplitMin.x, (SplitMin.y + SplitMax.y) * 0.5f),
				ImVec2(SplitMax.x, (SplitMin.y + SplitMax.y) * 0.5f),
				ImGui::GetColorU32(ImGuiCol_Separator), EditorUIScale::Px(2.0f));
			if (ImGui::IsItemHovered() || ImGui::IsItemActive())
				ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
			if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
			{
				Viewer->DetailsHeight = std::clamp(Viewer->DetailsHeight - ImGui::GetIO().MouseDelta.y,
					100.0f, std::max(100.0f, AvailableHeight - 80.0f));
			}

			ImGui::BeginChild("DetailsArea", ImVec2(0.0f, 0.0f), false);
			DrawBoneDetails(*Viewer);
			ImGui::EndChild();

			ImGui::EndChild();
			ImGui::SameLine();
			ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
			const FString ViewportChildId = "ViewportArea_" + std::to_string(Viewer->Id);
			if (ImGui::BeginChild(ViewportChildId.c_str(), ImVec2(0.0f, 0.0f), false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse))
			{
				DrawViewport(*Viewer);
			}
			ImGui::EndChild();
			ImGui::PopStyleVar();
		}
		ImGui::End();
	}
}

void FSkeletalMeshViewerPanel::DrawBoneDetails(FSkeletalMeshViewer& Viewer)
{
	if (ImGui::Button("FBX Report"))
	{
		FString Error;
		if (!FFbxDebugScene::DumpFile(Viewer.Path, Error)) HTR_LOG(Warning, "[FBX report] {}", Error);
	}
	if (Viewer.SelectedBone >= 0 && Viewer.SelectedBone < Viewer.WeightedVertexCounts.Num())
		ImGui::Text("Bone %d: %d influenced vertices", Viewer.SelectedBone, Viewer.WeightedVertexCounts[Viewer.SelectedBone]);
	USkeletalMeshComponent* Mesh = Viewer.MeshComponent;
	if (!Mesh || !Mesh->GetSkeletalMesh())
		return;

	// Reset Pose 및 Reset Bone 버튼
	if (ImGui::Button("Reset Pose"))
	{
		Mesh->InitBoneTransforms();
		if (Viewer.SelectedBone >= 0)
		{
			Viewer.BoneGizmoTarget->SetWorldTransform(
				Mesh->GetGlobalBoneTransforms()[Viewer.SelectedBone] * Mesh->GetWorldMatrix(), false);
		}
	}

	if (Viewer.SelectedBone >= 0)
	{
		ImGui::SameLine();
		if (ImGui::Button("Reset Bone"))
		{
			const TArray<FBoneInfo>& Bones = Mesh->GetSkeletalMesh()->MeshData.Bones;
			if (Viewer.SelectedBone < Bones.Num())
			{
				Mesh->SetBoneTransform(Viewer.SelectedBone, Bones[Viewer.SelectedBone].LocalBindPose);
				Mesh->PerformCPUSkinning();
				Viewer.BoneGizmoTarget->SetWorldTransform(
					Mesh->GetGlobalBoneTransforms()[Viewer.SelectedBone] * Mesh->GetWorldMatrix(), false);
			}
		}
	}

	ImGui::SameLine();
	ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.45f, 0.22f, 1.0f));
	ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.24f, 0.58f, 0.28f, 1.0f));
	ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.12f, 0.35f, 0.16f, 1.0f));
	if (ImGui::Button("Save Pose"))
	{
		SaveViewerPose(Viewer);
	}
	ImGui::PopStyleColor(3);

	if (Viewer.SaveNotificationTimer > 0.0f)
	{
		ImGui::SameLine();
		ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f), "Saved!");
	}

	if (Viewer.SelectedBone < 0)
	{
		ImGui::Spacing();
		ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "No Bone selected.");
		return;
	}

	const TArray<FBoneInfo>& Bones = Mesh->GetSkeletalMesh()->MeshData.Bones;
	if (Viewer.SelectedBone >= Bones.Num())
		return;

	const FBoneInfo& Bone = Bones[Viewer.SelectedBone];

	// 본 기본 정보
	ImGui::SeparatorText("Bone Info");
	ImGui::Text("Bone Name: %s", Bone.Name.c_str());
	ImGui::Text("Parent Bone: %s", (Bone.ParentIndex >= 0) ? Bones[Bone.ParentIndex].Name.c_str() : "None (Root)");

	// 로컬 트랜스폼 조작 (종곤님 브랜치 방식)
	ImGui::SeparatorText("Transform (Local)");
	FMatrix LocalMat = Mesh->GetBoneTransforms()[Viewer.SelectedBone];
	FVector Loc = LocalMat.TransformPosition(FVector::ZeroVector);
	FRotator Rot = MatrixToRotator(LocalMat);
	FVector Scale(
		FVector(LocalMat.M[0][0], LocalMat.M[0][1], LocalMat.M[0][2]).Size(),
		FVector(LocalMat.M[1][0], LocalMat.M[1][1], LocalMat.M[1][2]).Size(),
		FVector(LocalMat.M[2][0], LocalMat.M[2][1], LocalMat.M[2][2]).Size()
	);
	if (Scale.X < 0.0001f) Scale.X = 1.0f;
	if (Scale.Y < 0.0001f) Scale.Y = 1.0f;
	if (Scale.Z < 0.0001f) Scale.Z = 1.0f;

	float LocValues[3] = { Loc.X, Loc.Y, Loc.Z };
	float ScaleValues[3] = { Scale.X, Scale.Y, Scale.Z };

	bool bModified = false;
	bModified |= DrawVector3Controller("Location", LocValues, 0.0f, 0.05f);
	bModified |= DrawRotatorAsXYZ("Rotation", Rot, 0.5f);
	bModified |= DrawVector3Controller("Scale", ScaleValues, 1.0f, 0.01f);

	if (bModified)
	{
		FTransform NewLocal(Rot, FVector(LocValues[0], LocValues[1], LocValues[2]), FVector(ScaleValues[0], ScaleValues[1], ScaleValues[2]));
		Mesh->SetBoneTransform(Viewer.SelectedBone, NewLocal.GetLocalMatrix());
		Mesh->PerformCPUSkinning();
		Viewer.BoneGizmoTarget->SetWorldTransform(
			Mesh->GetGlobalBoneTransforms()[Viewer.SelectedBone] * Mesh->GetWorldMatrix(), false);
	}

	// 컴포넌트 기준 글로벌 트랜스폼 (읽기 전용)
	ImGui::SeparatorText("Transform (Component)");
	const TArray<FMatrix>& GlobalTransforms = Mesh->GetGlobalBoneTransforms();
	if (Viewer.SelectedBone < GlobalTransforms.Num())
	{
		FMatrix GlobalMat = GlobalTransforms[Viewer.SelectedBone];
		FVector GLoc = GlobalMat.TransformPosition(FVector::ZeroVector);
		FRotator GRot = MatrixToRotator(GlobalMat);
		ImGui::TextDisabled("Location: (%.2f, %.2f, %.2f)", GLoc.X, GLoc.Y, GLoc.Z);
		ImGui::TextDisabled("Rotation: (%.1f, %.1f, %.1f)", GRot.Roll, GRot.Pitch, GRot.Yaw);
	}
}

void FSkeletalMeshViewerPanel::SaveViewerPose(FSkeletalMeshViewer& Viewer)
{
	if (!Viewer.MeshComponent || !Viewer.MeshComponent->GetSkeletalMesh())
	{
		return;
	}

	USkeletalMesh* Mesh = Viewer.MeshComponent->GetSkeletalMesh();
	FSkeletalMeshData& MeshData = Mesh->MeshData;

	const TArray<FMatrix>& LocalTransforms = Viewer.MeshComponent->GetBoneTransforms();
	const TArray<FMatrix>& GlobalTransforms = Viewer.MeshComponent->GetGlobalBoneTransforms();

	const int32 BoneCount = MeshData.Bones.Num();
	if (LocalTransforms.Num() != BoneCount || GlobalTransforms.Num() != BoneCount)
	{
		return;
	}

	// 1. 본의 바인드 포즈를 현재 편집된 트랜스폼으로 갱신
	for (int32 i = 0; i < BoneCount; ++i)
	{
		MeshData.Bones[i].LocalBindPose = LocalTransforms[i];
		MeshData.Bones[i].GlobalBindPose = GlobalTransforms[i];
		MeshData.Bones[i].InverseBindPose = GlobalTransforms[i].Inverse();
	}

	// 2. 현재 스키닝된 정점 데이터를 기본 정점에 베이킹
	Viewer.MeshComponent->PerformCPUSkinning();
	const TArray<FVertex>& CurrentSkinned = Viewer.MeshComponent->GetSkinnedVertices();

	if (CurrentSkinned.Num() == MeshData.SkinnedVertices.Num() && !CurrentSkinned.IsEmpty())
	{
		FVector BoundsMin(FLT_MAX, FLT_MAX, FLT_MAX);
		FVector BoundsMax(-FLT_MAX, -FLT_MAX, -FLT_MAX);

		const int32 VertexCount = CurrentSkinned.Num();
		for (int32 v = 0; v < VertexCount; ++v)
		{
			const FVertex& SkinnedV = CurrentSkinned[v];
			FSkeletalVertex& MeshV = MeshData.SkinnedVertices[v];

			MeshV.Position = SkinnedV.Position;
			MeshV.Normal = SkinnedV.Normal;
			MeshV.Tangent = SkinnedV.Tangent;

			BoundsMin.X = std::min(BoundsMin.X, SkinnedV.Position.X);
			BoundsMin.Y = std::min(BoundsMin.Y, SkinnedV.Position.Y);
			BoundsMin.Z = std::min(BoundsMin.Z, SkinnedV.Position.Z);

			BoundsMax.X = std::max(BoundsMax.X, SkinnedV.Position.X);
			BoundsMax.Y = std::max(BoundsMax.Y, SkinnedV.Position.Y);
			BoundsMax.Z = std::max(BoundsMax.Z, SkinnedV.Position.Z);
		}

		MeshData.AABB = FBox{ BoundsMin, BoundsMax };

		// GPU 렌더링용 Static VertexBuffer 갱신
		uint32 VbSize = static_cast<uint32>(sizeof(FSkeletalVertex) * VertexCount);
		Mesh->VertexBuffer = RenderCommand::CreateStaticVertexBuffer(
			MeshData.SkinnedVertices.GetData(), VbSize, sizeof(FSkeletalVertex));
		Mesh->RebuildRenderData();
	}

	// 3. .fbx.bin 바이너리 캐시 파일로 영구 저장
	const FString BinPath = Viewer.Path + ".bin";
	FSkeletalMeshBake::WriteBaked(BinPath, MeshData);

	// 4. 컴포넌트 내부 바인드 포즈 상태 재동기화
	Viewer.MeshComponent->InitBoneTransforms();

	// 5. 기즈모 타겟 재동기화
	if (Viewer.SelectedBone >= 0)
	{
		SelectBone(Viewer, Viewer.SelectedBone);
	}

	Viewer.SaveNotificationTimer = 3.0f;
	HTR_LOG(Info, "[Viewer] Successfully saved new skeletal mesh bind pose: {}", BinPath);
}

void FSkeletalMeshViewerPanel::DrawViewportToolbar(FSkeletalMeshViewer& Viewer)
{
	if (!Viewer.ShowFlagsIcon)
	{
		Viewer.ShowFlagsIcon = UAssetManager::Get().LoadTexture(EditorResourcePaths::VisibleIcon);
		if (!Viewer.ShowFlagsIcon)
		{
			Viewer.ShowFlagsIcon = UAssetManager::Get().LoadTexture(EditorResourcePaths::VisibleIcon);
		}
	}

	bool bOpenPopup = false;

	// 100% 배율 기준 수치를 Px로 키워 글자와 같은 비율로 커지게 한다 (TEAM5 EditorUIScale).
	const float IconWidth = EditorUIScale::Px(28.0f);
	const float IconHeight = EditorUIScale::Px(25.0f);
	const float ChevronWidth = EditorUIScale::Px(7.0f);
	const float ChevronHeight = EditorUIScale::Px(4.0f);
	const float Spacing = EditorUIScale::Px(6.0f);
	const ImVec2 Padding(EditorUIScale::Px(8.0f), EditorUIScale::Px(4.0f));
	const ImVec2 TotalSize(Padding.x * 2.0f + IconWidth + Spacing + ChevronWidth, IconHeight + Padding.y * 2.0f);

	const ImVec2 CursorScreenPos = ImGui::GetCursorScreenPos();
	const FString BtnId = "##ShowFlagsUnifiedBtn_" + std::to_string(Viewer.Id);
	if (ImGui::InvisibleButton(BtnId.c_str(), TotalSize))
	{
		bOpenPopup = true;
	}

	const bool bHovered = ImGui::IsItemHovered();
	const bool bActive = ImGui::IsItemActive();

	if (bHovered)
	{
		ImGui::SetTooltip("Show Flags (Mesh, Bones)");
	}

	const ImU32 BgColor = bActive ? IM_COL32(35, 35, 35, 255) :
	                      bHovered ? IM_COL32(70, 70, 70, 255) :
	                      IM_COL32(50, 50, 50, 220);
	const ImU32 BorderColor = bHovered ? IM_COL32(110, 110, 110, 220) : IM_COL32(65, 65, 65, 180);
	const float Rounding = EditorUIScale::Px(5.0f);

	ImDrawList* DrawList = ImGui::GetWindowDrawList();
	const ImVec2 RectMax(CursorScreenPos.x + TotalSize.x, CursorScreenPos.y + TotalSize.y);
	DrawList->AddRectFilled(CursorScreenPos, RectMax, BgColor, Rounding);
	DrawList->AddRect(CursorScreenPos, RectMax, BorderColor, Rounding, 0, 1.0f);

	// 아이콘 렌더링
	if (Viewer.ShowFlagsIcon && Viewer.ShowFlagsIcon->GetResource() && Viewer.ShowFlagsIcon->GetResource()->GetSRV())
	{
		const ImVec2 IconMin(CursorScreenPos.x + Padding.x, CursorScreenPos.y + (TotalSize.y - IconHeight) * 0.5f);
		const ImVec2 IconMax(IconMin.x + IconWidth, IconMin.y + IconHeight);
		DrawList->AddImage((ImTextureID)Viewer.ShowFlagsIcon->GetResource()->GetSRV(), IconMin, IconMax);
	}
	else
	{
		const ImVec2 TextPos(CursorScreenPos.x + Padding.x, CursorScreenPos.y + (TotalSize.y - ImGui::GetTextLineHeight()) * 0.5f);
		DrawList->AddText(TextPos, IM_COL32(220, 220, 220, 255), "Show");
	}

	const float ChevronX = CursorScreenPos.x + Padding.x + IconWidth + Spacing;
	const float ChevronY = CursorScreenPos.y + (TotalSize.y - ChevronHeight) * 0.5f;
	const ImU32 ChevronColor = bActive ? IM_COL32(255, 255, 255, 255) :
	                           bHovered ? IM_COL32(230, 230, 230, 255) :
	                           IM_COL32(190, 190, 190, 255);
	const ImVec2 ChevronPts[3] = {
		ImVec2(ChevronX, ChevronY),
		ImVec2(ChevronX + ChevronWidth * 0.5f, ChevronY + ChevronHeight),
		ImVec2(ChevronX + ChevronWidth, ChevronY)
	};
	DrawList->AddPolyline(ChevronPts, 3, ChevronColor, 0, EditorUIScale::Px(1.6f));

	const FString PopupId = "ShowFlagsPopup_" + std::to_string(Viewer.Id);
	if (bOpenPopup)
	{
		ImGui::OpenPopup(PopupId.c_str());
	}

	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(EditorUIScale::Px(12.0f), EditorUIScale::Px(10.0f)));
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(EditorUIScale::Px(8.0f), EditorUIScale::Px(8.0f)));
	ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(EditorUIScale::Px(8.0f), EditorUIScale::Px(5.0f)));
	ImGui::SetNextWindowSizeConstraints(ImVec2(EditorUIScale::Px(160.0f), 0.0f), ImVec2(EditorUIScale::Px(350.0f), EditorUIScale::Px(400.0f)));

	if (ImGui::BeginPopup(PopupId.c_str()))
	{
		// 1. Mesh 硫붾돱
		ImGui::SetNextWindowSizeConstraints(ImVec2(EditorUIScale::Px(140.0f), 0.0f), ImVec2(EditorUIScale::Px(300.0f), EditorUIScale::Px(400.0f)));
		if (ImGui::BeginMenu("Mesh"))
		{
			if (ImGui::MenuItem("Mesh", nullptr, &Viewer.bShowMesh))
			{
				if (Viewer.MeshComponent)
				{
					Viewer.MeshComponent->SetVisibility(Viewer.bShowMesh);
				}
			}
			ImGui::EndMenu();
		}

		// 2. Bones 硫붾돱
		ImGui::SetNextWindowSizeConstraints(ImVec2(EditorUIScale::Px(200.0f), 0.0f), ImVec2(EditorUIScale::Px(350.0f), EditorUIScale::Px(400.0f)));
		if (ImGui::BeginMenu("Bones"))
		{
			if (auto* Debug = Cast<USkeletalMeshDebugComponent>(Viewer.MeshComponent))
			{
				ImGui::Checkbox("Bone Volumes", &Debug->bDrawBones);
				ImGui::Checkbox("Bone Axes", &Debug->bDrawBoneAxes);
				ImGui::DragFloat("Axis Length", &Debug->BoneAxisLength, .01f, .001f, 100.0f);
				ImGui::DragFloat("Joint Radius", &Debug->BoneRadiusScale, .0001f, .0001f, .1f, "%.4f");
				ImGui::Separator();
			}
			if (ImGui::MenuItem("All Hierarchy", nullptr, Viewer.BoneDrawMode == EBoneDrawMode::AllHierarchy))
			{
				Viewer.BoneDrawMode = EBoneDrawMode::AllHierarchy;
			}
			if (ImGui::MenuItem("Selected Only", nullptr, Viewer.BoneDrawMode == EBoneDrawMode::SelectedOnly))
			{
				Viewer.BoneDrawMode = EBoneDrawMode::SelectedOnly;
			}
			if (ImGui::MenuItem("Selected and Parents", nullptr, Viewer.BoneDrawMode == EBoneDrawMode::SelectedAndParents))
			{
				Viewer.BoneDrawMode = EBoneDrawMode::SelectedAndParents;
			}
			if (ImGui::MenuItem("None", nullptr, Viewer.BoneDrawMode == EBoneDrawMode::None))
			{
				Viewer.BoneDrawMode = EBoneDrawMode::None;
			}
			ImGui::EndMenu();
		}

		// 3. Grid 硫붾돱
		ImGui::SetNextWindowSizeConstraints(ImVec2(EditorUIScale::Px(140.0f), 0.0f), ImVec2(EditorUIScale::Px(300.0f), EditorUIScale::Px(400.0f)));
		if (ImGui::BeginMenu("Grid"))
		{
			ImGui::MenuItem("Grid & Axes", nullptr, &Viewer.bShowGrid);
			ImGui::EndMenu();
		}

		ImGui::EndPopup();
	}
	ImGui::PopStyleVar(3);

	// ---- Camera Speed UI (Visible 버튼 바로 우측에 배치) ----
	ImGui::SameLine(0.0f, EditorUIScale::Px(6.0f));

	char SpeedStr[16];
	snprintf(SpeedStr, sizeof(SpeedStr), "%.1f", Viewer.CameraSpeed);
	const float CamTextWidth = ImGui::CalcTextSize(SpeedStr).x;
	const float CamIconWidth = EditorUIScale::Px(18.0f);
	const float CamSpacing = EditorUIScale::Px(6.0f);
	const ImVec2 CamTotalSize(Padding.x * 2.0f + CamIconWidth + CamSpacing + CamTextWidth, TotalSize.y);

	if (Viewer.bEditingCameraSpeed)
	{
		ImGui::SetNextItemWidth(std::max(CamTotalSize.x + EditorUIScale::Px(12.0f), EditorUIScale::Px(54.0f)));
		if (Viewer.bFocusCameraSpeedInput)
		{
			ImGui::SetKeyboardFocusHere();
			Viewer.bFocusCameraSpeedInput = false;
		}
		const FString InputId = "##CameraSpeedDirectInput_" + std::to_string(Viewer.Id);
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(EditorUIScale::Px(4.0f), (TotalSize.y - ImGui::GetFontSize()) * 0.5f - 1.0f));
		if (ImGui::InputFloat(InputId.c_str(), &Viewer.CameraSpeed, 0.0f, 0.0f, "%.1f", ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll))
		{
			Viewer.bEditingCameraSpeed = false;
		}
		ImGui::PopStyleVar();

		if (ImGui::IsItemDeactivated())
		{
			Viewer.bEditingCameraSpeed = false;
		}
		Viewer.CameraSpeed = std::clamp(Viewer.CameraSpeed, 0.1f, 50.0f);
	}
	else
	{
		const ImVec2 CamCursorScreenPos = ImGui::GetCursorScreenPos();
		const FString CamBtnId = "##CameraSpeedBtn_" + std::to_string(Viewer.Id);
		ImGui::InvisibleButton(CamBtnId.c_str(), CamTotalSize);

		const bool bCamHovered = ImGui::IsItemHovered();
		const bool bCamActive = ImGui::IsItemActive();

		// 더블 클릭 시 직접 텍스트 입력 모드로 전환
		if (bCamHovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
		{
			Viewer.bEditingCameraSpeed = true;
			Viewer.bFocusCameraSpeedInput = true;
		}
		// 클릭 & 좌우 드래그로 속도 실시간 조절
		else if (bCamActive && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 1.0f))
		{
			const float DragDelta = ImGui::GetIO().MouseDelta.x;
			Viewer.CameraSpeed = std::clamp(Viewer.CameraSpeed + DragDelta * 0.05f, 0.1f, 50.0f);
		}

		if (bCamHovered || bCamActive)
		{
			ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
			ImGui::BeginTooltip();
			ImGui::Text("Camera Speed: %.1f", Viewer.CameraSpeed);
			ImGui::TextDisabled("Drag left/right to adjust");
			ImGui::TextDisabled("Double-click to type value");
			ImGui::EndTooltip();
		}

		const ImU32 CamBgColor = bCamActive ? IM_COL32(35, 35, 35, 255) :
		                         bCamHovered ? IM_COL32(70, 70, 70, 255) :
		                         IM_COL32(50, 50, 50, 220);
		const ImU32 CamBorderColor = bCamHovered ? IM_COL32(110, 110, 110, 220) : IM_COL32(65, 65, 65, 180);

		const ImVec2 CamRectMax(CamCursorScreenPos.x + CamTotalSize.x, CamCursorScreenPos.y + CamTotalSize.y);
		DrawList->AddRectFilled(CamCursorScreenPos, CamRectMax, CamBgColor, Rounding);
		DrawList->AddRect(CamCursorScreenPos, CamRectMax, CamBorderColor, Rounding, 0, 1.0f);

		// 카메라 아이콘 그리기
		const ImVec2 CamCenter(CamCursorScreenPos.x + Padding.x + CamIconWidth * 0.5f, CamCursorScreenPos.y + TotalSize.y * 0.5f);
		const ImU32 CamIconColor = bCamActive ? IM_COL32(255, 255, 255, 255) :
		                           bCamHovered ? IM_COL32(240, 240, 240, 255) :
		                           IM_COL32(200, 200, 200, 255);
		const ImU32 CamLensColor = bCamActive ? IM_COL32(35, 35, 35, 255) :
		                           bCamHovered ? IM_COL32(65, 65, 65, 255) :
		                           IM_COL32(50, 50, 50, 255);

		// 아이콘 좌표는 100% 배율 기준이라 S를 곱해 키운다.
		const float S = EditorUIScale::Px(1.0f);
		// 카메라 바디
		DrawList->AddRectFilled(ImVec2(CamCenter.x - S * 7.5f, CamCenter.y - S * 4.5f), ImVec2(CamCenter.x + S * 7.5f, CamCenter.y + S * 5.5f), CamIconColor, S * 2.0f);
		// 상단 뷰파인더 턱
		DrawList->AddRectFilled(ImVec2(CamCenter.x - S * 4.0f, CamCenter.y - S * 7.0f), ImVec2(CamCenter.x + S * 0.5f, CamCenter.y - S * 4.5f), CamIconColor, S * 1.0f);
		// 렌즈 원
		DrawList->AddCircleFilled(ImVec2(CamCenter.x, CamCenter.y + S * 0.5f), S * 3.2f, CamLensColor);
		DrawList->AddCircle(ImVec2(CamCenter.x, CamCenter.y + S * 0.5f), S * 3.2f, CamIconColor, 12, S * 1.0f);
		DrawList->AddCircleFilled(ImVec2(CamCenter.x, CamCenter.y + S * 0.5f), S * 1.2f, CamIconColor);

		// 속도 숫자 텍스트
		const float TextStartX = CamCursorScreenPos.x + Padding.x + CamIconWidth + CamSpacing;
		const float TextStartY = CamCursorScreenPos.y + (TotalSize.y - ImGui::GetTextLineHeight()) * 0.5f;
		DrawList->AddText(ImVec2(TextStartX, TextStartY), IM_COL32(220, 220, 220, 255), SpeedStr);
	}

	// ---- Toolbar Save Button ----
	ImGui::SameLine(0.0f, EditorUIScale::Px(6.0f));
	ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.45f, 0.22f, 0.85f));
	ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.24f, 0.58f, 0.28f, 1.0f));
	ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.12f, 0.35f, 0.16f, 1.0f));
	if (ImGui::Button("Save##ToolbarSave", ImVec2(0.0f, TotalSize.y)))
	{
		SaveViewerPose(Viewer);
	}
	ImGui::PopStyleColor(3);

	if (Viewer.SaveNotificationTimer > 0.0f)
	{
		ImGui::SameLine(0.0f, EditorUIScale::Px(6.0f));
		ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f), "Saved!");
	}
}

void FSkeletalMeshViewerPanel::DrawViewport(FSkeletalMeshViewer& Viewer)
{
	DrawViewportToolbar(Viewer);

	const ImVec2 Size = ImGui::GetContentRegionAvail();
	if (Size.x < 1.0f || Size.y < 1.0f)
		return;

	// 이전 SRV가 들어가지 않도록 AddImage 전에 조정
	// 실제 Resize는 다음 프레임 Tick에서.
	Viewer.ViewSize = Size;

	const ImVec2 Min = ImGui::GetCursorScreenPos();
	const bool bClicked = ImGui::InvisibleButton("##Viewport", Size, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);

	if (FTexture2D* Color = Viewer.Client.GetColorTarget())
		ImGui::GetWindowDrawList()->AddImage(Color->GetSRV(), Min, ImVec2(Min.x + Size.x, Min.y + Size.y));

	const int32 HoveredJoint = DrawSkeleton(Viewer, Min, Size);

	if (bClicked && ImGui::IsMouseReleased(ImGuiMouseButton_Left) && !Viewer.Gizmo.IsUsing())
	{
		SelectBone(Viewer, HoveredJoint);
		Viewer.bScrollTreeToSelection = HoveredJoint >= 0;
	}
	
	const bool bLeftDown = ImGui::IsItemActive() && ImGui::IsMouseDown(ImGuiMouseButton_Left);
	UpdateBoneGizmo(Viewer, Min, bLeftDown);

	// 오른쪽 버튼 + 마우스 회전
	// WASD/QE 이동
	if (ImGui::IsItemHovered() || ImGui::IsItemActive())
	{
		if (ImGui::IsMouseDown(ImGuiMouseButton_Right) && ImGui::GetIO().MouseWheel != 0.0f)
		{
			Viewer.CameraSpeed = std::clamp(Viewer.CameraSpeed + ImGui::GetIO().MouseWheel * 0.5f, 0.1f, 20.0f);
		}
		const float MoveSpeed = 2.5f * Viewer.CameraSpeed;
		Viewer.Client.TickInput(ImGui::GetIO().DeltaTime, ImGui::IsMouseDown(ImGuiMouseButton_Right), true, 0, MoveSpeed, 0.2f);
	}
}

void FSkeletalMeshViewerPanel::DrawBoneTree(FSkeletalMeshViewer& Viewer)
{
	const TArray<FBoneInfo>& Bones = Viewer.MeshComponent->GetSkeletalMesh()->MeshData.Bones;

	ImGui::PushStyleColor(ImGuiCol_TableRowBg, ImVec4(0.11f, 0.11f, 0.11f, 1.0f));
	ImGui::PushStyleColor(ImGuiCol_TableRowBgAlt, ImVec4(0.15f, 0.15f, 0.15f, 1.0f));
	ImGuiTableFlags TableFlags = ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp;

	if (ImGui::BeginTable("##BoneTreeTable", 1, TableFlags))
	{
		for (int32 i = 0; i < Bones.Num(); ++i)
		{
			if (Bones[i].ParentIndex < 0)
				DrawBoneNode(Viewer, i);
		}
		ImGui::EndTable();
	}
	ImGui::PopStyleColor(2);
}

void FSkeletalMeshViewerPanel::DrawBoneNode(FSkeletalMeshViewer& Viewer, int32 BoneIndex)
{
	ImGui::TableNextRow();
	ImGui::TableNextColumn();
	if (BoneIndex == Viewer.SelectedBone)
	{
		ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, ImColor(0, 122, 204, 255));
	}

	const FBoneInfo& Bone = Viewer.MeshComponent->GetSkeletalMesh()->MeshData.Bones[BoneIndex];
	const bool bLeaf = Viewer.Children[BoneIndex].IsEmpty();

	ImGuiTreeNodeFlags Flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth |
		ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_DrawLinesToNodes;

	if (BoneIndex == Viewer.SelectedBone)
		Flags |= ImGuiTreeNodeFlags_Selected;

	if (bLeaf)
		Flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;

	bool bIsSocketOrLeaf = false;
	if (bLeaf) // 자식이 없는 말단 본
	{
		bIsSocketOrLeaf = true;
	}
	const std::string& BoneName = Bone.Name;
	if (BoneName.find("_end") != std::string::npos ||
		BoneName.find("Socket") != std::string::npos ||
		BoneName.find("Cor_") != std::string::npos)
	{
		bIsSocketOrLeaf = true;
	}

	const bool bOpen = ImGui::TreeNodeEx(
		reinterpret_cast<void*>(static_cast<intptr_t>(BoneIndex)),
		Flags,
		"  %s", Bone.Name.c_str()
	);

	ImDrawList* DL = ImGui::GetWindowDrawList();
	ImVec2 ItemMin = ImGui::GetItemRectMin();
	float FontSize = ImGui::GetFontSize();
	float RowHeight = ImGui::GetFrameHeight();

	const float IconOffsetX = EditorUIScale::Px(4.0f);

	ImVec2 IconCenter = ImVec2(
		ItemMin.x + ImGui::GetTreeNodeToLabelSpacing() + IconOffsetX,
		ItemMin.y + RowHeight * 0.35f
	);

	ImU32 IconColor = (BoneIndex == Viewer.SelectedBone)
		? IM_COL32(255, 255, 255, 255)
		: IM_COL32(180, 180, 180, 220);

	if (bIsSocketOrLeaf)
	{
		DrawDotIcon(DL, IconCenter, EditorUIScale::Px(3.0f), IconColor);
	}
	else
	{
		DrawBoneIcon(DL, IconCenter, EditorUIScale::Px(11.0f), IconColor);
	}

	if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
		SelectBone(Viewer, BoneIndex);

	if (BoneIndex == Viewer.SelectedBone && Viewer.bScrollTreeToSelection)
	{
		ImGui::SetScrollHereY(0.5f);
		Viewer.bScrollTreeToSelection = false;
	}

	if (bOpen && !bLeaf)
	{
		for (int32 Child : Viewer.Children[BoneIndex])
			DrawBoneNode(Viewer, Child);
		ImGui::TreePop();
	}
}

int32 FSkeletalMeshViewerPanel::DrawSkeleton(FSkeletalMeshViewer& Viewer, const ImVec2& ViewMin, const ImVec2& ViewSize)
{
	if (Viewer.BoneDrawMode == EBoneDrawMode::None)
		return -1;

	const TArray<FBoneInfo>& Bones = Viewer.MeshComponent->GetSkeletalMesh()->MeshData.Bones;
	const TArray<FMatrix>& Globals = Viewer.MeshComponent->GetGlobalBoneTransforms();
	if (Globals.Num() != Bones.Num())
		return -1;

	auto ShouldDrawBone = [&](int32 Bone) -> bool
	{
		switch (Viewer.BoneDrawMode)
		{
		case EBoneDrawMode::AllHierarchy:
			return true;
		case EBoneDrawMode::SelectedOnly:
			return Viewer.SelectedBone >= 0 && Bone == Viewer.SelectedBone;
		case EBoneDrawMode::SelectedAndParents:
			if (Viewer.SelectedBone < 0) return false;
			for (int32 P = Viewer.SelectedBone; P >= 0 && P < Bones.Num(); P = Bones[P].ParentIndex)
			{
				if (P == Bone) return true;
			}
			return false;
		case EBoneDrawMode::None:
		default:
			return false;
		}
	};

	const FMatrix ComponentWorld = Viewer.MeshComponent->GetWorldMatrix();
	const FMatrix ViewProj = Viewer.Client.GetViewProjectionMatrix();

	TArray<FVector2> Joints;
	Joints.SetNum(Bones.Num());
	for (int32 i = 0; i < Bones.Num(); ++i)
	{
		const FVector Location = (Globals[i] * ComponentWorld).TransformPosition(FVector::ZeroVector);
		Joints[i] = WorldToScreen(Location, ViewProj, static_cast<int>(ViewSize.x), static_cast<int>(ViewSize.y));
	}

	const auto IsBehind = [&](int32 i) { return Joints[i].X == -FLT_MAX; };
	const auto ToScreen = [&](int32 i) { return ImVec2(ViewMin.x + Joints[i].X, ViewMin.y + Joints[i].Y); };

	const ImVec2 Mouse = ImGui::GetMousePos();
	int32 HoveredJoint = -1;
	float BestDistSq = 8.0f * 8.0f;
	for (int32 i = 0; i < Bones.Num(); ++i)
	{
		if (IsBehind(i) || !ShouldDrawBone(i))
			continue;
		const ImVec2 P = ToScreen(i);
		const float DistSq = (P.x - Mouse.x) * (P.x - Mouse.x) + (P.y - Mouse.y) * (P.y - Mouse.y);
		if (DistSq < BestDistSq)
		{
			BestDistSq = DistSq;
			HoveredJoint = i;
		}
	}
	
	ImDrawList* DL = ImGui::GetWindowDrawList();
	DL->PushClipRect(ViewMin, ImVec2(ViewMin.x + ViewSize.x, ViewMin.y + ViewSize.y), true);
	for (int32 i = 0; i < Bones.Num(); ++i)
	{
		if (IsBehind(i) || !ShouldDrawBone(i))
			continue;

		const int32 Parent = Bones[i].ParentIndex;
		if (Parent >= 0 && !IsBehind(Parent) && ShouldDrawBone(Parent))
			DL->AddLine(ToScreen(Parent), ToScreen(i), IM_COL32(220, 220, 220, 180), 1.5f);

		const bool bSelected = (i == Viewer.SelectedBone);
		const bool bHovered = (i == HoveredJoint);
		const ImU32 Color = bSelected 
			? IM_COL32(255, 170, 40, 255)
			: (bHovered ? IM_COL32(255, 255, 255, 255) : IM_COL32(100, 190, 255, 255));
		DL->AddCircleFilled(ToScreen(i), (bSelected || bHovered) ? 5.0f : 3.0f, Color);
	}
	DL->PopClipRect();
	return HoveredJoint;
}

void FSkeletalMeshViewerPanel::SelectBone(FSkeletalMeshViewer& Viewer, int32 BoneIndex)
{
	Viewer.SelectedBone = BoneIndex;
	if (auto* Debug = Cast<USkeletalMeshDebugComponent>(Viewer.MeshComponent)) Debug->SetSelectedBone(BoneIndex);
	Viewer.Gizmo.SetTarget(BoneIndex >= 0 ? Viewer.BoneGizmoTarget : nullptr);
}

void FSkeletalMeshViewerPanel::UpdateBoneGizmo(FSkeletalMeshViewer& Viewer, const ImVec2& ViewMin, bool bLeftDown)
{
	if (Viewer.SelectedBone < 0 || Viewer.BoneDrawMode == EBoneDrawMode::None)
		return;

	USkeletalMeshComponent* Mesh = Viewer.MeshComponent;
	const TArray<FMatrix>& Globals = Mesh->GetGlobalBoneTransforms(); // 모델 공간
	const FMatrix ComponentWorld = Mesh->GetWorldMatrix();

	if (!Viewer.Gizmo.IsUsing())
		Viewer.BoneGizmoTarget->SetWorldTransform(Globals[Viewer.SelectedBone] * ComponentWorld, false);

	const ImVec2 Mouse = ImGui::GetMousePos();
	const FVector2 MousePos(Mouse.x - ViewMin.x, Mouse.y - ViewMin.y);
	FEditorViewportClient& Client = Viewer.Client;
	Viewer.Gizmo.Update(
		Client.DeprojectScreenToWorld(MousePos), MousePos,
		Client.GetViewProjectionMatrix(),
		static_cast<int>(Client.GetWidth()), static_cast<int>(Client.GetHeight()),
		bLeftDown, Client.GetViewLocation(), !Client.IsPerspective()
	);

	// Gizmo가 움직인 타겟을 Bone 로컬 트랜스폼으로 되돌린다
	if (Viewer.Gizmo.IsUsing())
	{
		const int32 Parent = Mesh->GetSkeletalMesh()->MeshData.Bones[Viewer.SelectedBone].ParentIndex;
		const FMatrix ParentWorld = (Parent >= 0 ? Globals[Parent] : FMatrix::Identity) * ComponentWorld;
		Mesh->SetBoneTransform(Viewer.SelectedBone, Viewer.BoneGizmoTarget->GetWorldMatrix() * ParentWorld.Inverse());
		Mesh->PerformCPUSkinning();
	}
}
