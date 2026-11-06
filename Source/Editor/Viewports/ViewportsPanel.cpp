#include "EnginePCH.h"
#include "Editor/Viewports/ViewportsPanel.h"
#include "Engine/GameViewportClient.h"

#include "Core/StatOverlay.h"
#include "Core/Stats/LightweightStats.h"
#include "Render/RenderCommand.h"

#include <algorithm>
#include <cstring>
#include <cmath>
#include <cassert>
#include <format>

#include "Core/Stats/EditorStats.h"
#include "Editor/EditorUI/EditorUIScale.h"

namespace {
	constexpr float SplitterThickness = 6.0f;
	constexpr ImU32 SplitterColor = IM_COL32(55, 55, 55, 230);
	constexpr ImU32 SplitterHoverColor = IM_COL32(255, 192, 0, 255);
	constexpr const char* ViewportTypeLabels[] = {
			"Perspective", "Top", "Front", "Right" };

	// Stat Overlay
	constexpr float StatOverlayMargin = 8.0f;
	constexpr float StatOverlayPadding = 6.0f;
	constexpr ImU32 StatOverlayBackgroundColor = IM_COL32(0, 0, 0, 140);
	constexpr ImU32 TitleColor = IM_COL32(255, 210, 60, 255);
	constexpr ImU32 ValueColor = IM_COL32(235, 235, 235, 255);
} // namespace

// 네 View의 렌더 타깃을 최소 크기로 초기화한다.
bool FViewportsPanel::Init() {
	for (int32 i = 0; i < 4; ++i) {
		ViewportClients[i] = MakeUnique<FEditorViewportClient>();
		ViewportClients[i]->SetViewIndex(i);
		ViewportClients[i]->Resize(1, 1);
	}

	// 초기 시점 설정
	ViewportClients[0]->SetViewportType(ELevelViewportType::Perspective);
	ViewportClients[1]->SetViewportType(ELevelViewportType::Top);
	ViewportClients[2]->SetViewportType(ELevelViewportType::Front);
	ViewportClients[3]->SetViewportType(ELevelViewportType::Right);

	return true;
}

void FViewportsPanel::SetRenderingPath(ERenderingPath Path)
{
	RenderingPath = Path;
	if (Path == ERenderingPath::Deferred)
		for (auto& Client : ViewportClients)
			if (Client && Client->GetShadingModel() == EShadingModel::Gouraud)
				Client->SetShadingModel(EShadingModel::Phong);
}

// 패널의 프레임 갱신 인터페이스이며 별도 계산은 하지 않는다.
void FViewportsPanel::Tick(float DeltaTime) { (void)DeltaTime; }

// Core Rect에 맞춰 View 활성 상태와 타깃 크기를 갱신한다.
void FViewportsPanel::SetView(const int32 ViewIndex, const FRect& Rect,
	const bool bActive) {
	assert(ViewIndex >= 0 && ViewIndex < 4);
	FEditorViewportClient* Client = ViewportClients[ViewIndex].get();
	if (!Client)
		return;

	Client->SetRect(Rect);
	const bool bSlotActive = bActive && Rect.Width > 0.0f && Rect.Height > 0.0f;
	Client->SetActive(bSlotActive);
	if (!bSlotActive)
		return;

	const uint32 Width = static_cast<uint32>(std::max(1.0f, Rect.Width));
	const uint32 Height = static_cast<uint32>(std::max(1.0f, Rect.Height));
	if (Width != Client->GetWidth() || Height != Client->GetHeight())
		Client->Resize(Width, Height);
}

// 마우스 위치에서 패널 원점을 빼 로컬 좌표로 바꾼다.
FVector2 FViewportsPanel::GetLocalMousePosition() const {
	POINT Cursor{};
	const ImVec2 Mouse = ::GetCursorPos(&Cursor)
		? ImVec2(static_cast<float>(Cursor.x), static_cast<float>(Cursor.y))
		: ImGui::GetMousePos();
	return { Mouse.x - ContentOrigin.x, Mouse.y - ContentOrigin.y };
}

// 엔진 입력 처리는 ImGui NewFrame보다 먼저 실행되므로 현재 OS 커서로 버튼
// 영역을 검사한다.
bool FViewportsPanel::IsHovered() const {
	if (!IsOpen() || bToolbarPopupOpen) return false;
	const FVector2 LocalMouse = GetLocalMousePosition();
	const ImVec2 ScreenMouse{ LocalMouse.X + ContentOrigin.x, LocalMouse.Y + ContentOrigin.y };
	for (int Index = 0; Index < 4; ++Index)
		if (bToolbarVisible[Index] && ScreenMouse.x >= ToolbarMin[Index].x && ScreenMouse.x < ToolbarMax[Index].x &&
			ScreenMouse.y >= ToolbarMin[Index].y && ScreenMouse.y < ToolbarMax[Index].y)
			return false;
	if (bStatResetButtonVisible && FStatOverlay::IsAnyEnabled()) {
		POINT Cursor{};
		if (::GetCursorPos(&Cursor) && Cursor.x >= StatResetButtonMin.x &&
			Cursor.x < StatResetButtonMax.x && Cursor.y >= StatResetButtonMin.y &&
			Cursor.y < StatResetButtonMax.y)
			return false;
	}
	const FVector2 Mouse = GetLocalMousePosition();
	return bHovered && Mouse.X >= 0.0f && Mouse.Y >= 0.0f &&
		Mouse.X < ContentSize.x && Mouse.Y < ContentSize.y;
}

// 누적 가로 Splitter 이동량을 반환하고 초기화한다.
float FViewportsPanel::ConsumeHorizontalDrag() {
	const float Result = PendingHorizontalDrag;
	PendingHorizontalDrag = 0.0f;
	return Result;
}

// 누적 세로 Splitter 이동량을 반환하고 초기화한다.
float FViewportsPanel::ConsumeVerticalDrag() {
	const float Result = PendingVerticalDrag;
	PendingVerticalDrag = 0.0f;
	return Result;
}

// 대기 Layout 요청을 한 번 반환하고 플래그를 지운다.
bool FViewportsPanel::ConsumeLayoutRequest(ELayoutMode& OutMode,
	int32& OutSingleViewIndex) {
	if (!bHasLayoutRequest)
		return false;
	OutMode = RequestedLayoutMode;
	OutSingleViewIndex = RequestedSingleViewIndex;
	CurrentLayoutMode = RequestedLayoutMode;
	CurrentSingleViewIndex = RequestedSingleViewIndex;
	bHasLayoutRequest = false;
	return true;
}



// View Texture와 Splitter·Layout·Preset UI를 그리고 요청을 기록한다.
void FViewportsPanel::OnRender() {
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{ 0.0f, 0.0f });
	if (!BeginPanel(
		ImGuiWindowFlags_NoScrollbar |
		ImGuiWindowFlags_NoScrollWithMouse))
	{
		bHovered = false;
		bFocused = false;
		bStatResetButtonVisible = false;
		ImGui::End();
		ImGui::PopStyleVar();
		return;
	}

	ContentOrigin = ImGui::GetCursorScreenPos();
	ContentSize = ImGui::GetContentRegionAvail();
	ContentSize.x = std::max(1.0f, ContentSize.x);
	ContentSize.y = std::max(1.0f, ContentSize.y);
	bHovered = ImGui::IsWindowHovered();
	bFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
	bStatResetButtonVisible = false;
	std::fill(std::begin(bToolbarVisible), std::end(bToolbarVisible), false);

	// 전체 캔버스를 한 번 확보한 뒤 각 렌더 타깃을 창 DrawList에 직접 그린다.
	// Image 항목 네 개를 따로 배치하면 ImGui 레이아웃과 클리핑 상태가 삽입 순서에
	// 영향을 받아, Core가 올바른 사각형을 줘도 아래쪽 행이 잘릴 수 있다.
	ImGui::Dummy(ContentSize);
	ImDrawList* DrawList = ImGui::GetWindowDrawList();
	DrawList->PushClipRect(ContentOrigin,
		{ ContentOrigin.x + ContentSize.x, ContentOrigin.y + ContentSize.y }, true);
	for (int32 ViewIndex = 0; ViewIndex < 4; ++ViewIndex)
	{
		FEditorViewportClient* Client = ViewportClients[ViewIndex].get();
		if (!Client || !Client->IsActive())
			continue;

		const FRect& Rect = Client->GetRect();
		const ImVec2 ViewMin{ ContentOrigin.x + Rect.X,
			ContentOrigin.y + Rect.Y };
		const ImVec2 ViewMax{ ViewMin.x + Rect.Width,
			ViewMin.y + Rect.Height };

		const bool bGameViewport = GameViewportClient && ViewIndex == GameViewportIndex;
		if (bGameViewport)
		{
			GameViewportClient->SetRect(FRect{
				ViewMin.x,
				ViewMin.y,
				Rect.Width,
				Rect.Height
				});
		}


		if (ViewIndex == 0 && bShowNoCamera && !bGameViewport)
		{
			// 메인 원근 뷰포트 영역을 어두운 회색으로 채움
			DrawList->AddRectFilled(ViewMin, ViewMax, IM_COL32(45, 45, 48, 255));
			const char* WarningText = "No Camera";
			ImVec2 TextSize = ImGui::CalcTextSize(WarningText);
			ImVec2 CenterPos{
				ViewMin.x + (Rect.Width - TextSize.x) * 0.5f,
				ViewMin.y + (Rect.Height - TextSize.y) * 0.5f
			};
			DrawList->AddText(CenterPos, IM_COL32(220, 220, 220, 255), WarningText);
			continue;
		}

		FTexture2D* DisplayTarget = bGameViewport
			? GameViewportClient->GetColorTarget()
			: Client->GetColorTarget();
		if (!DisplayTarget || !DisplayTarget->GetSRV())
			continue;

		DrawList->AddImage(DisplayTarget->GetSRV(), ViewMin, ViewMax);

		// 마우스 클릭 시 활성 뷰포트 설정
		const bool bViewHovered = bHovered && ImGui::IsMouseHoveringRect(ViewMin, ViewMax);
		if (bViewHovered && (ImGui::IsMouseClicked(ImGuiMouseButton_Left) ||
			ImGui::IsMouseClicked(ImGuiMouseButton_Right) ||
			ImGui::IsMouseClicked(ImGuiMouseButton_Middle))) {
			ActiveViewIndex = ViewIndex;
			ImGui::SetWindowFocus();
			bFocused = true;
		}
	}
	DrawList->PopClipRect();

	if (CurrentLayoutMode == ELayoutMode::QuadSplit && ViewportClients[0] && ViewportClients[1] && ViewportClients[2]) {
		const FRect& Rect0 = ViewportClients[0]->GetRect();
		const FRect& Rect1 = ViewportClients[1]->GetRect();
		const FRect& Rect2 = ViewportClients[2]->GetRect();
		// 분할선 배치 계산
		const float SplitX =
			(Rect0.X + Rect0.Width + Rect1.X) * 0.5f;
		const float SplitY =
			(Rect0.Y + Rect0.Height + Rect2.Y) * 0.5f;

		const ImVec2 VerticalMin{
				ContentOrigin.x + SplitX - EditorUIScale::Px(SplitterThickness) * 0.5f, ContentOrigin.y };
		const ImVec2 VerticalMax{ VerticalMin.x + EditorUIScale::Px(SplitterThickness),
			ContentOrigin.y + ContentSize.y };
		const ImVec2 HorizontalMin{ ContentOrigin.x, ContentOrigin.y + SplitY -
			EditorUIScale::Px(SplitterThickness) * 0.5f };
		const ImVec2 HorizontalMax{ ContentOrigin.x + ContentSize.x,
			HorizontalMin.y + EditorUIScale::Px(SplitterThickness) };

		ImGui::SetCursorScreenPos(VerticalMin);
		ImGui::InvisibleButton("##MultipleViewportsHorizontalSplitter",
			{ EditorUIScale::Px(SplitterThickness), ContentSize.y });
		ImGui::SetCursorScreenPos(HorizontalMin);
		ImGui::InvisibleButton("##MultipleViewportsVerticalSplitter",
			{ ContentSize.x, EditorUIScale::Px(SplitterThickness) });

		const bool bVerticalHovered =
			bHovered && ImGui::IsMouseHoveringRect(VerticalMin, VerticalMax);
		const bool bHorizontalHovered =
			bHovered && ImGui::IsMouseHoveringRect(HorizontalMin, HorizontalMax);
		if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
			bDraggingVerticalSplitter = bVerticalHovered;
			bDraggingHorizontalSplitter = bHorizontalHovered;
		}
		if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
			bDraggingVerticalSplitter = false;
			bDraggingHorizontalSplitter = false;
		}

		const bool bVerticalHighlighted =
			bDraggingVerticalSplitter || bVerticalHovered;
		const bool bHorizontalHighlighted =
			bDraggingHorizontalSplitter || bHorizontalHovered;
		DrawList->AddRectFilled(VerticalMin, VerticalMax,
			bVerticalHighlighted ? SplitterHoverColor
			: SplitterColor);
		DrawList->AddRectFilled(HorizontalMin, HorizontalMax,
			bHorizontalHighlighted ? SplitterHoverColor
			: SplitterColor);

		if (bDraggingVerticalSplitter)
			PendingHorizontalDrag += ImGui::GetIO().MouseDelta.x;
		if (bDraggingHorizontalSplitter)
			PendingVerticalDrag += ImGui::GetIO().MouseDelta.y;

		if (bVerticalHighlighted && bHorizontalHighlighted)
			ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
		else if (bVerticalHighlighted)
			ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
		else if (bHorizontalHighlighted)
			ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
	}
	else {
		bDraggingVerticalSplitter = false;
		bDraggingHorizontalSplitter = false;
	}

	for (int32 ViewIndex = 0; ViewIndex < 4; ++ViewIndex) {
		FEditorViewportClient* Client = ViewportClients[ViewIndex].get();
		if (!Client || !Client->IsActive())
			continue;
		const FRect& Rect = Client->GetRect();
		const ImVec2 ViewMin{ ContentOrigin.x + Rect.X, ContentOrigin.y + Rect.Y };
		const ImVec2 ViewMax{ ViewMin.x + Rect.Width, ViewMin.y + Rect.Height };
		ImGui::PushClipRect(ViewMin, ViewMax, true);
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(EditorUIScale::Px(8.0f), EditorUIScale::Px(4.0f)));
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(EditorUIScale::Px(4.0f), EditorUIScale::Px(4.0f)));
		ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, EditorUIScale::Px(4.0f));
		ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
		// 메뉴를 여는 컨트롤은 콤보의 화살표까지 같은 중립색으로 맞춘다.
		ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_FrameBg));
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImGui::GetStyleColorVec4(ImGuiCol_FrameBgHovered));
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImGui::GetStyleColorVec4(ImGuiCol_FrameBgActive));
		ImGui::SetCursorScreenPos(
			{ ContentOrigin.x + Rect.X + EditorUIScale::Px(8.0f),
			 ContentOrigin.y + Rect.Y + EditorUIScale::Px(8.0f) });
		bToolbarVisible[ViewIndex] = true;
		ToolbarMin[ViewIndex] = ImGui::GetCursorScreenPos();
		ToolbarMax[ViewIndex] = ToolbarMin[ViewIndex];
		ImDrawListSplitter ToolbarLayers;
		ToolbarLayers.Split(DrawList, 2);
		ToolbarLayers.SetCurrentChannel(DrawList, 1);
		// 현재 뷰의 오른쪽 경계를 기준으로 줄바꿈한다. 다른 분할 뷰로 넘치지 않는다.
		const float AvailableWidth = std::max(1.0f, Rect.Width - EditorUIScale::Px(16.0f));
		float RowWidth = 0.0f;
		float RowY = ToolbarMin[ViewIndex].y;
		const auto PlaceControl = [&](float Width)
		{
			Width = std::min(Width, AvailableWidth);
			const float Spacing = RowWidth > 0.0f ? ImGui::GetStyle().ItemSpacing.x : 0.0f;
			if (RowWidth > 0.0f && RowWidth + Spacing + Width > AvailableWidth)
			{
				RowWidth = 0.0f;
				RowY += ImGui::GetFrameHeightWithSpacing();
			}
			else
				RowWidth += Spacing;
			ImGui::SetCursorScreenPos({ ToolbarMin[ViewIndex].x + RowWidth, RowY });
			ImGui::SetNextItemWidth(Width);
			RowWidth += Width;
			ToolbarMax[ViewIndex].x = std::max(ToolbarMax[ViewIndex].x, ToolbarMin[ViewIndex].x + RowWidth);
			ToolbarMax[ViewIndex].y = RowY + ImGui::GetFrameHeight();
		};
		const auto ComboWidth = [](const char* Label)
		{
			return EditorUIScale::ButtonWidth(Label) + ImGui::GetFrameHeight();
		};
		ImGui::PushID(100 + ViewIndex);
		// 드롭다운·메뉴 팝업 공통 여백. 팝업 창은 Begin 시점의 WindowPadding을 쓰므로
		// 하위 메뉴(BeginMenu)를 열 때도 같은 값을 다시 넣어야 한다.
		const ImVec2 MenuWindowPadding{ EditorUIScale::Px(10.0f), EditorUIScale::Px(8.0f) };
		const float MenuItemSpacingY = EditorUIScale::Px(6.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, MenuWindowPadding);

		const int SelectedType = static_cast<int>(Client->GetViewportType());
		PlaceControl(ComboWidth("Perspective"));
		const bool bTypeComboOpen = ImGui::BeginCombo("##ViewportType", ViewportTypeLabels[SelectedType]);
		if (bTypeComboOpen) {
			ImGui::PushStyleVarY(ImGuiStyleVar_ItemSpacing, MenuItemSpacingY);
			for (int TypeIndex = 0; TypeIndex < IM_ARRAYSIZE(ViewportTypeLabels); ++TypeIndex) {
				const bool bSelected = TypeIndex == SelectedType;
				if (ImGui::Selectable(ViewportTypeLabels[TypeIndex], bSelected))
					Client->SetViewportType(static_cast<ELevelViewportType>(TypeIndex));
				if (bSelected)
					ImGui::SetItemDefaultFocus();
			}
			ImGui::PopStyleVar();
			ImGui::EndCombo();
		}
		// 뷰 모드 (UE처럼 Lit/Unlit/Wireframe + 버퍼 시각화).
		// 라벨 순서는 EViewportMode 값 순서와 같다.
		const int Mode = static_cast<int>(Client->GetViewportMode());
		const char* Labels[] = { "Lit", "Wireframe", "Scene Depth", "World Normal", "Light Culling", "Shadow Cascades", "Shadow Mask", "Unlit" };
		const auto ModeItem = [Client, Mode, &Labels](EViewportMode Item)
		{
			const int Index = static_cast<int>(Item);
			if (ImGui::MenuItem(Labels[Index], nullptr, Mode == Index)) Client->SetViewportMode(Item);
		};
		const auto BeginPaddedMenu = [&MenuWindowPadding](const char* Label)
		{
			ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, MenuWindowPadding);
			const bool bOpen = ImGui::BeginMenu(Label);
			ImGui::PopStyleVar();
			return bOpen;
		};
		PlaceControl(ComboWidth(Labels[Mode]));
		const bool bViewModeOpen = ImGui::BeginCombo("##ViewMode", Labels[Mode], ImGuiComboFlags_HeightLargest);
		if (bViewModeOpen)
		{
			ImGui::PushStyleVarY(ImGuiStyleVar_ItemSpacing, MenuItemSpacingY);
			ModeItem(EViewportMode::Solid);
			ModeItem(EViewportMode::Unlit);
			ModeItem(EViewportMode::Wireframe);
			ImGui::Separator();
			if (BeginPaddedMenu("Buffer Visualization"))
			{
				ModeItem(EViewportMode::SceneDepth);
				ModeItem(EViewportMode::WorldNormal);
				ImGui::EndMenu();
			}
			if (BeginPaddedMenu("Shadows"))
			{
				ModeItem(EViewportMode::Cascade);
				ModeItem(EViewportMode::ShadowMask);
				ImGui::EndMenu();
			}
			ModeItem(EViewportMode::LightCulling);
			ImGui::Separator();
			float DepthRange = Client->GetMaxRange();
			ImGui::SetNextItemWidth(EditorUIScale::Px(130.0f));
			if (ImGui::DragFloat("Scene Depth Range", &DepthRange, 0.5f, 0.1f, 10000.0f, "%.1f", ImGuiSliderFlags_AlwaysClamp))
				Client->SetMaxRange(DepthRange);
			ImGui::SetItemTooltip("White in Scene Depth represents this distance for this viewport only.");
			ImGui::PopStyleVar();
			ImGui::EndCombo();
		}
		ImGui::SetItemTooltip("View mode and buffer visualization for this viewport.");
		PlaceControl(ComboWidth("Lighting"));
		const char* ReflectionNames[] = { "Phong", "Blinn-Phong", "Lambert" };
		const char* ShadingNames[] = { "Gouraud", "Phong" };
		const char* ReflectionPreview = ReflectionNames[static_cast<int>(Client->GetReflectionModel())];
		const char* ShadingPreview = ShadingNames[static_cast<int>(Client->GetShadingModel())];
		ImGui::SetNextWindowSizeConstraints(ImVec2(EditorUIScale::Px(280.0f), 0.0f), ImVec2(FLT_MAX, FLT_MAX));
		if (ImGui::BeginCombo("##Lighting", "Lighting", ImGuiComboFlags_HeightLargest))
		{
			ImGui::PushStyleVarY(ImGuiStyleVar_ItemSpacing, MenuItemSpacingY);
			ImGui::SeparatorText("Reflection");
			ImGui::SetNextItemWidth(EditorUIScale::Px(150.0f));
			if (ImGui::BeginCombo("##ReflectionModel", ReflectionPreview))
			{
				for (int Index = 0; Index < IM_ARRAYSIZE(ReflectionNames); ++Index)
				{
					const bool bSelected = Index == static_cast<int>(Client->GetReflectionModel());
					if (ImGui::Selectable(ReflectionNames[Index], bSelected))
						Client->SetReflectionModel(static_cast<EReflectionModel>(Index));
					if (bSelected) ImGui::SetItemDefaultFocus();
				}
				ImGui::EndCombo();
			}
			ImGui::SetItemTooltip("Phong: R dot V. Blinn-Phong: N dot H. Lambert: diffuse only. Applies independently to this viewport.");
			ImGui::SeparatorText("Shading");
			ImGui::SetNextItemWidth(EditorUIScale::Px(150.0f));
			if (ImGui::BeginCombo("##ShadingModel", ShadingPreview))
			{
				for (int Index = 0; Index < IM_ARRAYSIZE(ShadingNames); ++Index)
				{
					const bool bSelected = Index == static_cast<int>(Client->GetShadingModel());
					const bool bRequiresForward = Index == static_cast<int>(EShadingModel::Gouraud) && RenderingPath == ERenderingPath::Deferred;
					ImGui::BeginDisabled(bRequiresForward);
					if (ImGui::Selectable(ShadingNames[Index], bSelected))
						Client->SetShadingModel(static_cast<EShadingModel>(Index));
					if (bSelected) ImGui::SetItemDefaultFocus();
					ImGui::EndDisabled();
					if (bRequiresForward && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
						ImGui::SetTooltip("Gouraud requires Forward rendering. Set Rendering Path to Forward in Settings.");
				}
				ImGui::EndCombo();
			}
			ImGui::SetItemTooltip("Gouraud: vertex lighting. Phong: per-pixel lighting. Independent of the reflection equation.");
			ImGui::PopStyleVar();
			ImGui::EndCombo();
		}
		ImGui::SetItemTooltip("Reflection: %s\nShading: %s", ReflectionPreview, ShadingPreview);
		PlaceControl(ComboWidth("Camera"));
		ImGui::BeginDisabled(GameViewportClient && ViewIndex == GameViewportIndex);
		ImGui::SetNextWindowSizeConstraints(ImVec2(EditorUIScale::Px(360.0f), 0.0f), ImVec2(FLT_MAX, FLT_MAX));
		if (ImGui::BeginCombo("##ViewportCamera", "Camera", ImGuiComboFlags_HeightLargest))
		{
			ActiveViewIndex = ViewIndex;
			DrawCameraSettings(*Client);
			ImGui::EndCombo();
		}
		ImGui::EndDisabled();
		// 뷰별 표시 항목 (TEAM5 Show Flags). 모든 표시 토글은 여기 한 곳에 있다.
		PlaceControl(ComboWidth("Show"));
		const bool bShowOpen = ImGui::BeginCombo("##ViewShowFlags", "Show", ImGuiComboFlags_HeightLargest);
		if (bShowOpen)
		{
			ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(EditorUIScale::Px(8.0f), EditorUIScale::Px(6.0f)));
			FViewShowFlags& Flags = Client->GetShowFlags();
			const char* CurrentGroup = nullptr;
			for (const FViewShowFlagInfo& Info : ViewShowFlagInfos)
			{
				if (!CurrentGroup || std::strcmp(CurrentGroup, Info.Group) != 0)
				{
					if (CurrentGroup) ImGui::Dummy(ImVec2(0.0f, EditorUIScale::Px(4.0f)));
					CurrentGroup = Info.Group;
					ImGui::SeparatorText(CurrentGroup);
				}
				ImGui::Checkbox(Info.Label, &(Flags.*Info.Member));
				if (Info.Member == &FViewShowFlags::bCascadeBounds && Flags.bCascadeBounds)
					ImGui::TextDisabled("Cascade 0-3: red, yellow, green, blue");
			}
			ImGui::PopStyleVar();
			ImGui::EndCombo();
		}
		PlaceControl(ComboWidth("Layout"));
		if (ImGui::BeginCombo("##ViewportLayout", "Layout"))
		{
			ImGui::PushStyleVarY(ImGuiStyleVar_ItemSpacing, MenuItemSpacingY);
			const auto LayoutItem = [&](const char* Label, ELayoutMode Layout)
			{
				if (ImGui::Selectable(Label, CurrentLayoutMode == Layout))
				{
					RequestedLayoutMode = Layout;
					RequestedSingleViewIndex = ViewIndex;
					bHasLayoutRequest = true;
				}
			};
			LayoutItem("Single View", ELayoutMode::Single);
			LayoutItem("Quad View", ELayoutMode::QuadSplit);
			ImGui::PopStyleVar();
			ImGui::EndCombo();
		}
		if (ViewIndex == ActiveViewIndex && !Client->IsLightCulling() &&
			FStatOverlay::IsAnyEnabled() &&
			(FStatOverlay::IsEnabled(EStatFlags::Profile) ||
				FStatOverlay::IsEnabled(EStatFlags::Decal) ||
				FStatRegistry::Find(EditorStats::STAT_PickingTime))) {
			const float ResetWidth = EditorUIScale::ButtonWidth("Reset Stats");
			PlaceControl(ResetWidth);
			if (ImGui::Button("Reset Stats", ImVec2(std::min(ResetWidth, AvailableWidth), 0.0f)))
			{
				FStatRegistry::Reset();
				Client->GetRenderData().DecalStats.ResetHistory();
			}
			StatResetButtonMin = ImGui::GetItemRectMin();
			StatResetButtonMax = ImGui::GetItemRectMax();
			bStatResetButtonVisible = true;
			ToolbarMax[ViewIndex].y = std::max(ToolbarMax[ViewIndex].y, StatResetButtonMax.y);
		}

		ImGui::PopID();
		ImGui::PopStyleVar(); // WindowPadding
		ToolbarLayers.SetCurrentChannel(DrawList, 0);
		const float Padding = EditorUIScale::Px(4.0f);
		ToolbarMin[ViewIndex].x -= Padding;
		ToolbarMin[ViewIndex].y -= Padding;
		ToolbarMax[ViewIndex].x += Padding;
		ToolbarMax[ViewIndex].y += Padding;
		DrawList->AddRectFilled(ToolbarMin[ViewIndex], ToolbarMax[ViewIndex], IM_COL32(24, 24, 27, 225), EditorUIScale::Px(6.0f));
		DrawList->AddRect(ToolbarMin[ViewIndex], ToolbarMax[ViewIndex], ImGui::GetColorU32(ImGuiCol_Border), EditorUIScale::Px(6.0f));
		ToolbarLayers.Merge(DrawList);
		ImGui::PopStyleColor(3);
		ImGui::PopStyleVar(4);
		ImGui::PopClipRect();
	}



	// 마지막으로 선택된 뷰포트만 오버레이
	for (int32 ViewIndex = 0; ViewIndex < 4; ++ViewIndex) {
		if (ViewIndex != ActiveViewIndex)
			continue;
		FEditorViewportClient* Client = ViewportClients[ViewIndex].get();
		if (!Client || !Client->IsActive() || Client->IsLightCulling())
			continue;
		DrawStatOverlay(DrawList, { ContentOrigin.x + Client->GetRect().X, ContentOrigin.y + Client->GetRect().Y }, Client);
	}
	DrawCameraSpeedNotice(DrawList);
	bToolbarPopupOpen = ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);

	ImGui::End();
	ImGui::PopStyleVar();
}

// This popup edits only the client belonging to its viewport toolbar.
void FViewportsPanel::DrawCameraSettings(FEditorViewportClient& Client)
{
	ImGui::Text("Viewport %d - %s", Client.GetViewIndex() + 1,
		ViewportTypeLabels[static_cast<int>(Client.GetViewportType())]);
	const bool bOrthogonal = !Client.IsPerspective();

	ImGui::SeparatorText("Movement");
	float Speed = Client.GetCameraSpeed();
	ImGui::SetNextItemWidth(EditorUIScale::Px(180.0f));
	if (ImGui::SliderFloat("Camera Speed", &Speed, 0.1f, 10.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp))
		Client.SetCameraSpeed(Speed);
	float Sensitivity = Client.GetMouseSensitivity();
	ImGui::SetNextItemWidth(EditorUIScale::Px(180.0f));
	if (ImGui::SliderFloat("Rotate Sensitivity", &Sensitivity, 0.01f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp))
		Client.SetMouseSensitivity(Sensitivity);

	ImGui::Dummy(ImVec2(0.0f, EditorUIScale::Px(4.0f)));
	ImGui::TextDisabled("Projection");

	// 카메라 투영 설정 표
	if (ImGui::BeginTable("Camera Projection", 2))
	{
		ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed, ImGui::CalcTextSize("Ortho Width").x);
		ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);

		// FOV 또는 Ortho Width
		ImGui::TableNextRow();
		ImGui::TableSetColumnIndex(0);
		ImGui::Text(bOrthogonal ? "Ortho Width" : "FOV");

		ImGui::TableSetColumnIndex(1);
		ImGui::SetNextItemWidth(-1.0f);

		if (bOrthogonal)
		{
			float OrthoWidth = Client.GetOrthoWidth();
			if (ImGui::DragFloat("##OrthoWidth", &OrthoWidth, 0.1f) && std::isfinite(OrthoWidth))
			{
				OrthoWidth = FMath::Clamp(OrthoWidth, 0.01f, 1000000.0f);
				Client.SetOrthoWidth(OrthoWidth);
			}
		}
		else
		{
			float FOV = Client.GetViewFOV();
			if (ImGui::DragFloat("##FOV", &FOV, 0.1f) && std::isfinite(FOV))
			{
				FOV = FMath::Clamp(FOV, 1.0f, 179.0f);
				Client.SetViewFOV(FOV);
			}
		}

		// 가까운 평면과 먼 평면
		ImGui::TableNextRow();

		ImGui::TableSetColumnIndex(0);
		ImGui::Text("Near");

		ImGui::TableSetColumnIndex(1);
		float Near = Client.GetNearClip();

		ImGui::SetNextItemWidth(-1.0f);
		bool Changed = ImGui::InputFloat("##Near", &Near);

		ImGui::TableNextRow();

		ImGui::TableSetColumnIndex(0);
		ImGui::Text("Far");

		ImGui::TableSetColumnIndex(1);
		float Far = Client.GetFarClip();
		ImGui::SetNextItemWidth(-1.0f);
		Changed |= ImGui::InputFloat("##Far", &Far);

		if (Changed && std::isfinite(Near) && std::isfinite(Far) && Near > 0 && Far > Near)
		{
			Client.SetClipPlanes(Near, Far);
		}
		ImGui::EndTable();
	}

	// 카메라 변환 설정 표
	ImGui::Dummy(ImVec2(0.0f, EditorUIScale::Px(4.0f)));
	ImGui::TextDisabled("Transform");

	bool bLocationChanged = false;
	bool bRotationChanged = false;

	if (ImGui::BeginTable("Camera Transform", 2))
	{
		ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed, ImGui::CalcTextSize("Ortho Width").x);
		ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);

		ImGui::TableNextRow();

		// 카메라 위치
		ImGui::TableSetColumnIndex(0);
		ImGui::Text("Location");

		ImGui::TableSetColumnIndex(1);
		float Available = ImGui::GetContentRegionAvail().x;
		float ItemWidth = (Available - ImGui::GetStyle().ItemSpacing.x * 2.0f) / 3.0f;

		FVector Location = Client.GetViewLocation();

		ImGui::SetNextItemWidth(ItemWidth);
		bLocationChanged |= ImGui::DragFloat("##LocationX", &Location.X, 0.1f);

		ImGui::SameLine();

		ImGui::SetNextItemWidth(ItemWidth);
		bLocationChanged |= ImGui::DragFloat("##LocationY", &Location.Y, 0.1f);

		ImGui::SameLine();

		ImGui::SetNextItemWidth(ItemWidth);
		bLocationChanged |= ImGui::DragFloat("##LocationZ", &Location.Z, 0.1f);

		if (bLocationChanged && std::isfinite(Location.X) && std::isfinite(Location.Y) && std::isfinite(Location.Z))
		{
			Client.SetViewLocation(Location);
		}

		ImGui::TableNextRow();

		// 카메라 회전
		ImGui::TableSetColumnIndex(0);
		ImGui::Text("Rotation");

		ImGui::TableSetColumnIndex(1);
		FRotator Rotation = Client.GetViewRotation();

		ImGui::SetNextItemWidth(ItemWidth);
		bRotationChanged |= ImGui::DragFloat("##Pitch", &Rotation.Pitch, 0.1f);

		ImGui::SameLine();

		ImGui::SetNextItemWidth(ItemWidth);
		bRotationChanged |= ImGui::DragFloat("##Yaw", &Rotation.Yaw, 0.1f);

		ImGui::SameLine();

		ImGui::SetNextItemWidth(ItemWidth);
		ImGui::BeginDisabled(!bOrthogonal);
		bRotationChanged |= ImGui::DragFloat("##Roll", &Rotation.Roll, 0.1f);
		ImGui::EndDisabled();

		if (bRotationChanged && std::isfinite(Rotation.Pitch) && std::isfinite(Rotation.Yaw) && std::isfinite(Rotation.Roll))
		{
			Client.SetViewRotation(Rotation);
		}
		ImGui::EndTable();
	}
}

void FViewportsPanel::ShowCameraSpeed(const int32 ViewIndex, const float Speed)
{
	CameraSpeedViewIndex = ViewIndex;
	CameraSpeedValue = Speed;
	CameraSpeedShownTime = ImGui::GetTime();
}

// View 왼쪽 아래에 반투명 배경과 함께 이동 속도를 1.5초 동안 그린다.
void FViewportsPanel::DrawCameraSpeedNotice(ImDrawList* DrawList) const
{
	constexpr double NoticeSeconds = 1.5;
	const float Margin = EditorUIScale::Px(8.0f);
	const float Padding = EditorUIScale::Px(6.0f);
	if (CameraSpeedViewIndex < 0 || CameraSpeedViewIndex >= 4 || ImGui::GetTime() - CameraSpeedShownTime > NoticeSeconds)
		return;
	const FEditorViewportClient* Client = ViewportClients[CameraSpeedViewIndex].get();
	if (!Client || !Client->IsActive())
		return;

	const FRect& Rect = Client->GetRect();
	const FString Text = std::format("Camera Speed  {:.2f}", CameraSpeedValue);
	const ImVec2 TextSize = ImGui::CalcTextSize(Text.c_str());
	const ImVec2 TextMin{
		ContentOrigin.x + Rect.X + Margin + Padding,
		ContentOrigin.y + Rect.Y + Rect.Height - Margin - Padding - TextSize.y };
	DrawList->AddRectFilled(
		{ TextMin.x - Padding, TextMin.y - Padding },
		{ TextMin.x + TextSize.x + Padding, TextMin.y + TextSize.y + Padding },
		IM_COL32(0, 0, 0, 140), 4.0f);
	DrawList->AddText(TextMin, IM_COL32(235, 235, 235, 255), Text.c_str());
}

// 항목별 줄을 모아 한 번에 배경과 텍스트를 그린다.
void FViewportsPanel::DrawStatOverlay(ImDrawList* DrawList, const ImVec2& ViewMin, FEditorViewportClient* Client) const
{
	if (!DrawList || !FStatOverlay::IsAnyEnabled())
		return;

	constexpr float BytesPerMegabyte = 1024.0f * 1024.0f;

	// 제목은 UE처럼 노란색, 값은 흰색으로 구분한다.
	struct FStatLine {
		FString Text;
		ImU32 Color;
	};
	TArray<FStatLine> Lines;

	if (FStatOverlay::IsEnabled(EStatFlags::FPS)) {
		Lines.Add({ "FPS", TitleColor });
		Lines.Add(
			{ std::format("  {:.1f} fps", FStatOverlay::GetFPS()), ValueColor });
		Lines.Add({ std::format("  {:.2f} ms", FStatOverlay::GetFrameTimeMs()),
			ValueColor });
	}

	if (FStatOverlay::IsEnabled(EStatFlags::Memory)) {
		Lines.Add({ "Memory", TitleColor });
		Lines.Add({ std::format("  Object  {:.2f} MB ({} allocs)",
			static_cast<double>(
			FStatOverlay::GetObjectAllocationBytes()) /
			BytesPerMegabyte,
			FStatOverlay::GetObjectAllocationCount()),
			ValueColor });
		Lines.Add({ std::format("  Process {:.2f} MB",
			static_cast<double>(
			FStatOverlay::GetProcessWorkingSetBytes()) /
			BytesPerMegabyte),
			ValueColor });
	}

	struct FProfileStatLine {
		FString Text;
		ImU32 Color;
		FString Values[3];
	};
	TArray<FProfileStatLine> ProfileLines;
	if (FStatOverlay::IsEnabled(EStatFlags::Profile)) {
		ProfileLines.Add({ "CPU Profile (ms)", TitleColor,{ "Last", "Avg", "Max" } });

		for (const auto& [Name, Data] : FStatRegistry::GetAll()) {
			if (TStatId{ Name } == EditorStats::STAT_PickingTime)
				continue;

			FProfileStatLine Line{ "  " + FString(Name), ValueColor };
			Line.Values[0] = std::format("{:.2f}", Data.GetLastMs());
			Line.Values[1] = std::format("{:.2f}", Data.GetRecentAverageMs());
			Line.Values[2] = std::format("{:.2f}", Data.GetMaxMs());
			ProfileLines.Add(Line);
		}
	}

	if (const FCycleStatData* PickingData =
		FStatRegistry::Find(EditorStats::STAT_PickingTime)) {
		ProfileLines.Add({ "Picking", TitleColor });
		FProfileStatLine Line{ "  Time (ms)", ValueColor };
		Line.Values[0] = std::format("Last: {:.2f}", PickingData->GetLastMs());
		Line.Values[1] = std::format("Attempts: {}", PickingData->CallCount);
		Line.Values[2] = std::format("Acc.: {:.2f}", PickingData->GetTotalMs());
		ProfileLines.Add(Line);
	}

	FSceneRenderData& RenderData = Client->GetRenderData();
	const FRenderStats& Stats = RenderData.RenderStats;
	if (FStatOverlay::IsEnabled(EStatFlags::Light)) {
		Lines.Add({ "Light", TitleColor });
		Lines.Add({ std::format("  Total Lights {} ", Stats.DirectionalCount + Stats.PointCount + Stats.SpotCount), ValueColor });
		Lines.Add({ std::format("  Directional {} ", Stats.DirectionalCount), ValueColor });
		Lines.Add({ std::format("  Point {} ", Stats.PointCount), ValueColor });
		Lines.Add({ std::format("  Spot {} ", Stats.SpotCount), ValueColor });

		Lines.Add({ "Shadow Atlas", TitleColor });
		Lines.Add({ std::format("  Atlas Capacity  {:.2f} MB",
			static_cast<double>(Stats.AtlasCapacity) /
			BytesPerMegabyte),
			ValueColor });
		Lines.Add({ std::format("  Shadow Tiles  {:.2f} MB",
			static_cast<double>(Stats.ShadowTiles) /
			BytesPerMegabyte),
			ValueColor });
		Lines.Add({ std::format("  Tile Usage  {:.1f} %",
			static_cast<double>(Stats.ShadowTiles) /
			static_cast<double>(Stats.AtlasCapacity) * 100),
			ValueColor });
		Lines.Add({ std::format("  Shadow Views  Redrawn {} / Cached {}",
			Stats.ShadowViewsRedrawn, Stats.ShadowViewsCached),
			ValueColor });
		Lines.Add({ std::format("  ShadowLightsSkippedOutsideView {}",
			Stats.ShadowLightsSkippedOutsideView),
			ValueColor });
		Lines.Add({ std::format("  ShadowLightsSkippedNoReceiver {}",
			Stats.ShadowLightsSkippedNoReceiver),
			ValueColor });
		Lines.Add({ std::format("  ShadowLightsSkippedNoCaster {}",
			Stats.ShadowLightsSkippedNoCaster),
			ValueColor });
		Lines.Add({ std::format("  ShadowCastersSkippedSmall {}",
			Stats.ShadowCastersSkippedSmall),
			ValueColor });

		if (RenderData.CascadeShadowCount > 0)
		{
			Lines.Add({ "Cascaded Shadow Map", TitleColor });
			Lines.Add({ std::format("  Cascades {}", RenderData.CascadeShadowCount), ValueColor });

			// 첫 번째 Directional의 그림자 뷰들 = 캐스케이드들
			const uint32 DirOffset = RenderData.GetDirectionalLightOffset();
			const bool bHasDirShadow = RenderData.GetDirectionalLightCount() > 0 &&
				DirOffset < static_cast<uint32>(RenderData.VisibleLightShadowViews.Num()) &&
				RenderData.VisibleLightShadowViews[DirOffset] != FSceneRenderData::InvalidShadowView;

			float PrevDistance = 0.0f;
			for (uint32 c = 0; c < RenderData.CascadeShadowCount; ++c)
			{
				const float Far = RenderData.CascadeShadowDistances[c];
				FString Texel = "-";
				if (bHasDirShadow)
				{
					const uint32 ShadowIdx = RenderData.VisibleLightShadowViews[DirOffset] + c;
					if (ShadowIdx < uint32(RenderData.LightShadows.Num()))
						Texel = std::format("{:.3f}", RenderData.LightShadows[ShadowIdx].WorldUnitsPerTexel);
				}
				Lines.Add({ std::format("  C{}  {:.1f} ~ {:.1f}   texel {}", c, PrevDistance, Far, Texel), ValueColor });
				PrevDistance = Far;
			}
			Lines.Add({ std::format("  Cascade Views  Redrawn {} / Cached {}",
				Stats.CascadeViewsRedrawn, Stats.CascadeViewsCached), ValueColor });
		}
	}

	if (FStatOverlay::IsEnabled(EStatFlags::Decal))
	{
		const FDecalStats& DecalStats = RenderData.DecalStats;
		const FDecalFrameStats& Frame = DecalStats.Frame;

		Lines.Add({ "Decal", TitleColor });

		if (!Frame.bValid)
		{
			Lines.Add({
				" Waiting for a successful view render",
				ValueColor
				});
		}
		else if (!Frame.bHasDBufferPasses)
		{
			Lines.Add({
				" DBuffer path inactive",
				ValueColor
				});
		}
		else
		{
			const char* Method =
				Frame.Method ==
				EDecalRenderMethod::DepthReconstruction
				? "Depth Reconstruction"
				: "Mesh Re-rendering";
			Lines.Add({
				std::format(" Mode: {}", Method),
				ValueColor
				});

			Lines.Add({
				std::format(
					" View: {} x {}",
					Frame.Width,
					Frame.Height),
				ValueColor
				});

			Lines.Add({
		   std::format(
			   "  World / Eligible / Submitted: {} / {} / {}",
			   Frame.WorldCount,
			   Frame.EligibleCount,
			   Frame.SubmittedLit + Frame.SubmittedUnlit),
		   ValueColor
				});

			Lines.Add({
				std::format(
					"  Hidden / Rejected: {} / {}",
					Frame.HiddenCount,
					Frame.RenderDataRejected),
				ValueColor
				});

			Lines.Add({
				std::format(
					"  Submitted Lit / Unlit: {} / {}",
					Frame.SubmittedLit,
					Frame.SubmittedUnlit),
				ValueColor
				});

			Lines.Add({
				std::format(
					"  Receiver Batches / Items: {} / {}",
					Frame.ReceiverBatchCount,
					Frame.ReceiverItemCount),
				ValueColor
				});

			Lines.Add({
				std::format(
					"  Draw Calls: {}",
					Frame.DrawCalls),
				ValueColor
				});

			Lines.Add({
				std::format(
					"  Submitted Triangles: {}",
					Frame.SubmittedTriangles),
				ValueColor
				});

			Lines.Add({
				std::format(
					"  DBuffer Logical Size: {:.2f} MiB",
					static_cast<double>(Frame.DBufferBytes) /
						(1024.0 * 1024.0)),
				ValueColor
				});

			ProfileLines.Add({
				"Decal CPU (ms)",
				TitleColor,
				{ "Last", "Avg120", "Max120" }
				});

			auto AddCpuLine =
				[&](const char* Name, const FCycleStatData& History)
				{
					FProfileStatLine Line{
						FString("  ") + Name,
						ValueColor
					};

					Line.Values[0] =
						std::format("{:.3f}", History.GetLastMs());

					Line.Values[1] =
						std::format(
							"{:.3f}",
							History.GetRecentAverageMs());

					Line.Values[2] =
						std::format(
							"{:.3f}",
							GetDecalRecentMaxMs(History));

					ProfileLines.Add(Line);
				};

			AddCpuLine("Gather", DecalStats.GatherHistory);
			AddCpuLine("Sort", DecalStats.SortHistory);
			AddCpuLine("Submit", DecalStats.SubmitHistory);
			if (Frame.Method == EDecalRenderMethod::MeshRerendering)
			{
				AddCpuLine("Receiver Selection", DecalStats.ReceiverSelectionHistory);
			}


			ProfileLines.Add({
				"Decal GPU incl. Clear (ms)",
				TitleColor,
				{ "Last", "Avg120", "Max120" }
				});

			auto AddGpuLine =
				[&](const char* Name,
					const std::shared_ptr<FGpuTimingHistory>& History)
				{
					FProfileStatLine Line{
						FString("  ") + Name,
						ValueColor
					};

					if (!History || History->Count == 0)
					{
						Line.Values[0] = "Pending";
						Line.Values[1] = "-";
						Line.Values[2] = "-";
					}
					else
					{
						Line.Values[0] =
							std::format("{:.3f}", History->LastMs);

						Line.Values[1] =
							std::format(
								"{:.3f}",
								History->GetAverageMs());

						Line.Values[2] =
							std::format(
								"{:.3f}",
								History->GetMaxMs());
					}

					ProfileLines.Add(Line);
				};

			AddGpuLine("Lit", DecalStats.GpuLit);
			AddGpuLine("Unlit", DecalStats.GpuUnlit);

			if (DecalStats.GpuLit &&
				DecalStats.GpuLit->Count != 0)
			{
				const uint64 CurrentFrame =
					FGpuProfiler::Get().GetFrameId();

				const uint64 SampleFrame =
					DecalStats.GpuLit->LastFrameId;

				Lines.Add({
					std::format(
						"  GPU Lit sample age: {} frames",
						CurrentFrame - SampleFrame),
					ValueColor
					});
			}

			Lines.Add({
				std::format(
					"  GPU profiler skipped frames/scopes: {} / {}",
					FGpuProfiler::Get().GetSkippedFrames(),
					FGpuProfiler::Get().GetSkippedScopes()),
				ValueColor
				});

			Lines.Add({
				std::format(
					"  GPU profiler errors: {}",
					FGpuProfiler::Get().GetErrorCount()),
				ValueColor
				});

			Lines.Add({
				std::format(
					"  View Frustum Rejected: {}",
					Frame.ViewFrustumRejectedDecals),
				ValueColor
				});

			if (Frame.Method == EDecalRenderMethod::MeshRerendering)
			{
				Lines.Add({
					std::format(
						"  BVH Bounds Tests: {}",
						Frame.MeshBVHBoundsTests),
					ValueColor
					});

				Lines.Add({
					std::format(
						"  BVH Proxy Candidates: {}",
						Frame.MeshBVHProxyCandidates),
					ValueColor
					});

				Lines.Add({
					std::format(
						"  Refine Rejected Proxies: {}",
						Frame.MeshRefineRejectedProxies),
					ValueColor
					});

				Lines.Add({
					std::format(
						"  Matched Items: {}",
						Frame.MeshMatchedItems),
					ValueColor
					});

				Lines.Add({
					std::format(
						"  Fallback Items: {}",
						Frame.MeshFallbackItems),
					ValueColor
					});

				Lines.Add({
					std::format(
						"  No Receiver Decals: {}",
						Frame.MeshNoReceiverDecals),
					ValueColor
					});
			}
			else if (Frame.Method == EDecalRenderMethod::DepthReconstruction)
			{
				Lines.Add({
					std::format(
						"  Volume / Far Fallback: {} / {}",
						Frame.VolumeDrawCalls,
						Frame.FarFallbackDrawCalls),
					ValueColor
					});

				Lines.Add({
					std::format(
						"  Frustum Rejected: {}",
						Frame.FrustumRejectedDecals),
					ValueColor
					});

				Lines.Add({
					std::format(
						"  Invalid Geometry: {}",
						Frame.InvalidGeometryDecals),
					ValueColor
					});
			}
		}
	}


	if (Lines.Num() == 0 && ProfileLines.Num() == 0)
		return;

	// Start below this viewport toolbar so the stats never overlap its controls.
	const float LineHeight = ImGui::GetTextLineHeight();
	const ImVec2 Origin{ ViewMin.x + EditorUIScale::Px(StatOverlayMargin),
			ToolbarMax[Client->GetViewIndex()].y + EditorUIScale::Px(StatOverlayMargin) };

	// 최대 너비 계산
	float MaxWidth = 0.0f;
	for (int32 Index = 0; Index < Lines.Num(); ++Index)
		MaxWidth =
		std::max(MaxWidth, ImGui::CalcTextSize(Lines[Index].Text.c_str()).x);

	const float MinValueWidth = ImGui::CalcTextSize("00000.00").x;
	float NameWidth = 0.0f;
	float ValueWidths[3] = { MinValueWidth, MinValueWidth, MinValueWidth };
	bool bHasColumns = false;

	for (const FProfileStatLine& Line : ProfileLines) {
		const float TextWidth = ImGui::CalcTextSize(Line.Text.c_str()).x;
		bHasColumns = true;
		NameWidth = std::max(NameWidth, TextWidth);
		for (int32 Column = 0; Column < 3; ++Column)
			ValueWidths[Column] = std::max(
				MinValueWidth, ImGui::CalcTextSize(Line.Values[Column].c_str()).x);
	}

	float ColumnRight[3]{};
	if (bHasColumns) {
		float Width = NameWidth;
		for (int32 Column = 0; Column < 3; ++Column) {
			Width += ImGui::GetFontSize() + ValueWidths[Column];
			ColumnRight[Column] = Origin.x + Width;
		}
		MaxWidth = std::max(MaxWidth, Width);
	}

	const ImVec2 BackgroundMin{ Origin.x - EditorUIScale::Px(StatOverlayPadding),
			Origin.y - EditorUIScale::Px(StatOverlayPadding) };
	const ImVec2 BackgroundMax{
			Origin.x + MaxWidth + EditorUIScale::Px(StatOverlayPadding),
			Origin.y +
					LineHeight * static_cast<float>(Lines.Num() + ProfileLines.Num()) +
					EditorUIScale::Px(StatOverlayPadding) };
	DrawList->AddRectFilled(BackgroundMin, BackgroundMax,
		StatOverlayBackgroundColor, EditorUIScale::Px(4.0f));

	// Lines 그리기
	for (int32 Index = 0; Index < Lines.Num(); ++Index) {
		DrawList->AddText(
			{ Origin.x, Origin.y + LineHeight * static_cast<float>(Index) },
			Lines[Index].Color, Lines[Index].Text.c_str());
	}

	// ProfileLines 그리기
	for (int32 Index = 0; Index < ProfileLines.Num(); ++Index) {
		const FProfileStatLine& Line = ProfileLines[Index];
		const float Y =
			Origin.y + LineHeight * static_cast<float>(Lines.Num() + Index);
		DrawList->AddText({ Origin.x, Y }, Line.Color, Line.Text.c_str());

		for (int32 Column = 0; Column < 3; ++Column) {
			const FString& Text = Line.Values[Column];
			const float TextWidth = ImGui::CalcTextSize(Text.c_str()).x;
			DrawList->AddText({ ColumnRight[Column] - TextWidth, Y }, Line.Color,
				Text.c_str());
		}
	}
}
