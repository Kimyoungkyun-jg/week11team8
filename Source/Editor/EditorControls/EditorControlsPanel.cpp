#include "EnginePCH.h"

#include "Editor/EditorControls/EditorControlsPanel.h"

#include "Engine/World.h"
#include "Editor/Viewports/ViewportsPanel.h"
#include "Editor/Viewports/EditorViewportClient.h"

#include "Input/InputSystem.h"
#include "Editor/EditorUI/EditorUIScale.h"


// 패널 초기화 성공을 반환한다.
bool FEditorControlsPanel::Init()
{
	return true;
}

// Space 입력으로 기즈모 편집 모드를 순환한다.
void FEditorControlsPanel::Tick(float DeltaTime)
{
	if (ImGui::GetIO().WantTextInput)
		return;

	// 다른 창 (fbx 뷰어)이 Spacebar를 쓰는 중이면 Level Gizmo는 건드리지 않음
	if (SkipGizmoHotkeyQuery && SkipGizmoHotkeyQuery())
		return;
	if (!ViewportsPanel || !ViewportsPanel->IsFocused() || (IsPlayingQuery && IsPlayingQuery()))
		return;

	if (Gizmo && FInputSystem::IsKeyPressed(EKeyCode::Space))
	{
		const int NextMode = (static_cast<int>(Gizmo->GetMode()) + 1) % 3;
		Gizmo->SetMode(static_cast<EGizmoMode>(NextMode));
	}
}

// 선택한 클래스의 액터를 월드에 생성한다.
void FEditorControlsPanel::AddActor(uint32 Index)
{
	if (Index >= static_cast<uint32>(Classes.Num())) return;
	SpawnInFrontOfCamera(Classes[Index]);
}

AActor* FEditorControlsPanel::SpawnInFrontOfCamera(UClass* Class)
{
	if (!World || !Class || (IsPlayingQuery && IsPlayingQuery())) return nullptr;

	// 화면을 그리는 활성 편집 View의 시점을 쓴다. 메인 카메라 컴포넌트의 월드 회전은 View와 다를 수 있다.
	constexpr float SpawnDistance = 10.0f;
	const FEditorViewportClient* Client = ViewportsPanel ? ViewportsPanel->GetViewportClient(ViewportsPanel->GetActiveViewIndex()) : nullptr;
	FTransform SpawnTransform;
	if (Client)
		SpawnTransform.Location = Client->GetViewLocation() + Client->GetViewForward() * SpawnDistance;
	AActor* Actor = World->SpawnActor(Class, NAME_None, &SpawnTransform);
	// Blueprint/SCS가 없는 이 엔진에서는 에디터의 빈 액터 배치에서만 기본 루트를 제공한다.
	// 런타임 SpawnActor<AActor>()는 컴포넌트 없는 네이티브 액터로 남는다.
	if (Actor && Class == AActor::StaticClass())
	{
		if (USceneComponent* Root = Cast<USceneComponent>(Actor->AddComponent(USceneComponent::StaticClass(), "DefaultSceneRoot")))
		{
			Root->MarkUserCreated();
			Root->SetTransform(SpawnTransform);
		}
	}
	return Actor;
}

void FEditorControlsPanel::DrawToolbar()
{
	ImGui::BeginDisabled(IsPlayingQuery && IsPlayingQuery());
	if (ImGui::Button("Add Actor")) ImGui::OpenPopup("AddActorMenu");
	if (ImGui::BeginPopup("AddActorMenu"))
	{
		for (int32 Index = 0; Index < Classes.Num(); ++Index)
			if (ImGui::MenuItem(Items[Index])) AddActor(Index);
		ImGui::Separator();
		if (ImGui::MenuItem("Directional Light")) SpawnInFrontOfCamera(ADirectionalLightActor::StaticClass());
		if (ImGui::MenuItem("Point Light")) SpawnInFrontOfCamera(APointLightActor::StaticClass());
		if (ImGui::MenuItem("Spot Light")) SpawnInFrontOfCamera(ASpotLightActor::StaticClass());
		if (ImGui::MenuItem("Rect Light")) SpawnInFrontOfCamera(ARectLightActor::StaticClass());
		if (ImGui::MenuItem("Sky Light")) SpawnInFrontOfCamera(ASkyLightActor::StaticClass());
		ImGui::EndPopup();
	}
	ImGui::EndDisabled();
	if (Gizmo)
	{
		ImGui::SameLine();
		int Mode = static_cast<int>(Gizmo->GetMode());
		ImGui::SetNextItemWidth(EditorUIScale::Px(100.0f));
		if (ImGui::Combo("##GizmoMode", &Mode, GizmoItems, IM_ARRAYSIZE(GizmoItems)))
			Gizmo->SetMode(static_cast<EGizmoMode>(Mode));
		ImGui::SameLine();
		int Space = static_cast<int>(Gizmo->GetSpace());
		ImGui::SetNextItemWidth(EditorUIScale::Px(85.0f));
		if (ImGui::Combo("##GizmoSpace", &Space, SpaceItems, IM_ARRAYSIZE(SpaceItems)))
			Gizmo->SetSpace(static_cast<EGizmoSpace>(Space));
	}

}

// 액터 생성·카메라 속성·기즈모·경로 추적 UI를 그린다.
void FEditorControlsPanel::OnRender()
{
	ImGui::SetNextWindowSize(ImVec2(EditorUIScale::Px(400.0f), EditorUIScale::Px(300.0f)), ImGuiCond_FirstUseEver);
	if (!BeginPanel())
	{
		ImGui::End();
		return;
	}
	ImGui::Spacing();

	char FpsText[64];
	std::snprintf(FpsText, sizeof(FpsText), "FPS %.1f   %.1f ms", 1.0f / DeltaTime, DeltaTime * 1000.0f);
	float TextWidth = ImGui::CalcTextSize(FpsText).x;
	float CursorX = ImGui::GetCursorPosX();
	float AvailableWidth = ImGui::GetContentRegionAvail().x;

	ImGui::SetCursorPosX(CursorX + AvailableWidth - TextWidth);
	ImGui::TextDisabled("%s", FpsText);

	//////////////////////////////////////////////////////

	ImGui::Dummy(ImVec2(0.0f, EditorUIScale::Px(SectionGap)));
	ImGui::SeparatorText("Actor Spawn");

	const float SpawnButtonWidth = EditorUIScale::ButtonWidth("Spawn");

	char CountText[32];
	std::snprintf(
		CountText,
		sizeof(CountText),
		"%d Actors",
		World->GetActorNum()
	);

	const float CountWidth = ImGui::CalcTextSize(CountText).x;
	const float Available = ImGui::GetContentRegionAvail().x;
	const float Spacing = ImGui::GetStyle().ItemSpacing.x;

	const float ComboWidth =
		Available - SpawnButtonWidth - CountWidth - Spacing * 2.0f;

	ImGui::SetNextItemWidth(ComboWidth);
	ImGui::Combo("##ActorType", &SelectedIndex, Items, IM_ARRAYSIZE(Items));

	ImGui::SameLine();

	if (ImGui::Button("Spawn", ImVec2(SpawnButtonWidth, 0)))
	{
		AddActor(SelectedIndex);
	}

	ImGui::SameLine();
	ImGui::TextDisabled("%s", CountText);

	ImGui::Dummy(ImVec2(0.0f, EditorUIScale::Px(SubsectionGap)));
	ImGui::SeparatorText("Light Spawn");
	if (ImGui::Button("Directional Light"))
		SpawnInFrontOfCamera(ADirectionalLightActor::StaticClass());
	ImGui::SameLine();
	if (ImGui::Button("Spot Light"))
		SpawnInFrontOfCamera(ASpotLightActor::StaticClass());
	ImGui::SameLine();
	if (ImGui::Button("Point Light"))
		SpawnInFrontOfCamera(APointLightActor::StaticClass());
	ImGui::SameLine();
	if (ImGui::Button("Rect Light"))
		SpawnInFrontOfCamera(ARectLightActor::StaticClass());
	ImGui::SameLine();
	if (ImGui::Button("Sky Light"))
		SpawnInFrontOfCamera(ASkyLightActor::StaticClass());

	//////////////////////////////////////////////////////

	// Gizmo가 없으면 이 섹션을 통째로 건너뛴다.
	if (Gizmo)
	{
		ImGui::Dummy(ImVec2(0.0f, EditorUIScale::Px(SectionGap)));
		ImGui::SeparatorText("Gizmo");
	}

	if (Gizmo && ImGui::BeginTable("GizmoControls", 2))
	{
		ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed, ImGui::CalcTextSize("Space").x);
		ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);

		ImGui::TableNextRow();
		ImGui::TableSetColumnIndex(0);
		ImGui::Text("Mode");

		ImGui::TableSetColumnIndex(1);
		ImGui::SetNextItemWidth(-1.0f);

		GizmoSelectedIndex = static_cast<int32>(Gizmo->GetMode());
		if (ImGui::Combo("##GizmoMode", &GizmoSelectedIndex, GizmoItems, IM_ARRAYSIZE(GizmoItems)))
		{
			Gizmo->SetMode(static_cast<EGizmoMode>(GizmoSelectedIndex));
		}

		ImGui::TableNextRow();
		ImGui::TableSetColumnIndex(0);
		ImGui::Text("Space");

		ImGui::TableSetColumnIndex(1);
		ImGui::SetNextItemWidth(-1.0f);

		if (ImGui::Combo("##GizmoSpace", &SpaceSelectedIndex, SpaceItems, IM_ARRAYSIZE(SpaceItems)))
		{
			Gizmo->SetSpace(static_cast<EGizmoSpace>(SpaceSelectedIndex));
		}

		ImGui::EndTable();
	}

	//////////////////////////////////////////////////////

	ImGui::End();
}
