#include "EnginePCH.h"
#include "Editor/EditorUI/EditorSort.h"
#include "Editor/EditorUI/EditorUI.h"
#include <imgui_internal.h>   // DockBuilder

namespace
{
	// Settings/imgui.ini가 없을 때(처음 실행, 설정 삭제 후)만 쓰는 UE 비슷한 기본 배치.
	// 가운데 뷰포트, 오른쪽 위 Outliner·아래 Details, 아래 Content Drawer·Output Log·Sequencer·도구 창, 왼쪽 Editor Controls·Settings
	void BuildDefaultDockLayout(ImGuiID DockspaceId, const ImVec2& Size)
	{
		ImGui::DockBuilderRemoveNode(DockspaceId);
		ImGui::DockBuilderAddNode(DockspaceId, ImGuiDockNodeFlags_DockSpace);
		ImGui::DockBuilderSetNodeSize(DockspaceId, Size);

		ImGuiID Center = DockspaceId;
		ImGuiID Right = ImGui::DockBuilderSplitNode(Center, ImGuiDir_Right, 0.22f, nullptr, &Center);
		const ImGuiID RightBottom = ImGui::DockBuilderSplitNode(Right, ImGuiDir_Down, 0.55f, nullptr, &Right);
		const ImGuiID Bottom = ImGui::DockBuilderSplitNode(Center, ImGuiDir_Down, 0.28f, nullptr, &Center);
		const ImGuiID Left = ImGui::DockBuilderSplitNode(Center, ImGuiDir_Left, 0.18f, nullptr, &Center);

		ImGui::DockBuilderDockWindow("Viewports", Center);
		ImGui::DockBuilderDockWindow("Outliner", Right);
		ImGui::DockBuilderDockWindow("Details", RightBottom);
		ImGui::DockBuilderDockWindow("Content Drawer", Bottom);
		ImGui::DockBuilderDockWindow("Output Log", Bottom);
		ImGui::DockBuilderDockWindow("Sequencer", Bottom);
		// 도구 창도 떠 있지 않게 아래 탭으로 넣는다. 스켈레탈 메시 뷰어는 FBX마다 창이 따로 열린다.
		ImGui::DockBuilderDockWindow("UI Editor", Bottom);
		ImGui::DockBuilderDockWindow("Pose Retarget", Bottom);
		ImGui::DockBuilderDockWindow("Settings", Left);
		ImGui::DockBuilderFinish(DockspaceId);
	}
}


const ImGuiWindowClass& FEditorUI::GetRootWindowClass()
{
	static const ImGuiWindowClass RootClass = []
	{
		ImGuiWindowClass Class;
		Class.ClassId = 0x524F4F54;   // 'ROOT'
		Class.DockingAllowUnclassed = false;
		return Class;
	}();
	return RootClass;
}

const ImGuiWindowClass& FEditorUI::GetLevelWindowClass()
{
	static const ImGuiWindowClass LevelClass = []
	{
		ImGuiWindowClass Class;
		Class.ClassId = 0x4C564C43; // LVLC
		Class.DockingAllowUnclassed = false;
		return Class;
	}();
	return LevelClass;
}

ImGuiID FEditorUI::GetRootDockId()
{
	return 0x524F4F44;   // 'ROOD'. 바깥 도킹 공간. 에셋 에디터가 처음 열릴 때 여기에 탭으로 붙는다
}

bool FEditorUI::Init(bool bInUseDockSpace, bool bInPassthruCentralNode)
{
	bUseDockSpace = bInUseDockSpace;
	bPassthruCentralNode = bInPassthruCentralNode;

	Toolbar = MakeUnique<FLevelEditorToolbar>();

	return true;
}

void FEditorUI::Tick(float DeltaTime)
{
	for (auto& Panel : Panels)
	{

		Panel->Tick(DeltaTime);
	}
}

void FEditorUI::OnRender()
{
	DrawMainMenuBar();

	if (bUseDockSpace)
	{
		static bool dockspaceOpen = true;
		static bool optFullscreen = true;
		static bool optPadding = false;
		static ImGuiDockNodeFlags dockspaceFlags = bPassthruCentralNode
			? ImGuiDockNodeFlags_PassthruCentralNode
			: ImGuiDockNodeFlags_None;

		// We are using the ImGuiWindowFlags_NoDocking flag to make the parent window not dockable into,
		// because it would be confusing to have two docking targets within each others.
		//ImGuiWindowFlags window_flags = ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking;
		ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoDocking;
		if (optFullscreen)
		{
			const ImGuiViewport* viewport = ImGui::GetMainViewport();
			ImGui::SetNextWindowPos(viewport->WorkPos);
			ImGui::SetNextWindowSize(viewport->WorkSize);
			ImGui::SetNextWindowViewport(viewport->ID);
			ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
			ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
			windowFlags |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove;
			windowFlags |= ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;
		}
		else
		{
			dockspaceFlags &= ~ImGuiDockNodeFlags_PassthruCentralNode;
		}

		// When using ImGuiDockNodeFlags_PassthruCentralNode, DockSpace() will render our background
		// and handle the pass-thru hole, so we ask Begin() to not render a background.
		if (dockspaceFlags & ImGuiDockNodeFlags_PassthruCentralNode)
			windowFlags |= ImGuiWindowFlags_NoBackground;

		// Important: note that we proceed even if Begin() returns false (aka window is collapsed).
		// This is because we want to keep our DockSpace() active. If a DockSpace() is inactive,
		// all active windows docked into it will lose their parent and become undocked.
		// We cannot preserve the docking relationship between an active window and an inactive docking, otherwise
		// any change of dockspace/settings would lead to windows being stuck in limbo and never being visible.
		if (!optPadding)
			ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));

		//ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, Daydream::Application::GetInstance().isMaximzed ? ImVec2(6.0f, 6.0f) : ImVec2(0.0f, 0.0f));
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, false ? ImVec2(6.0f, 6.0f) : ImVec2(0.0f, 0.0f));
		ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 2.0f);
		ImGui::PushStyleColor(ImGuiCol_MenuBarBg, ImVec4{ 0.0f, 0.0f, 0.0f, 0.0f });

		ImGui::Begin("DockingSpace", &dockspaceOpen, windowFlags);
		//m_DockSpacePos = { ImGui::GetWindowPos().x, ImGui::GetWindowPos().y };
		//STS_CORE_WARN("Dock Space Coord = {0}, {1}", m_DockSpacePos.x, m_DockSpacePos.y);
		ImGui::PopStyleColor(); // MenuBarBg
		ImGui::PopStyleVar(2);

		if (!optPadding)
			ImGui::PopStyleVar();

		if (optFullscreen)
			ImGui::PopStyleVar(2);

		// 2단 도킹 (TEAM5). 바깥 공간에는 루트 창(Level Editor, 스켈레탈 메시 뷰어 같은 에셋 에디터)만 탭으로 붙는다.
		// 레벨 패널은 창 클래스가 없어서 바깥 공간과 에셋 에디터에 붙지 않고, Level Editor 안의 공간에만 붙는다.
		ImGuiIO& io = ImGui::GetIO();
		const bool bDockingEnabled = (io.ConfigFlags & ImGuiConfigFlags_DockingEnable) != 0;
		const ImGuiWindowClass& RootClass = GetRootWindowClass();
		if (bDockingEnabled)
			ImGui::DockSpace(GetRootDockId(), ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_None, &RootClass);

		ImGui::End();

		if (bDockingEnabled)
		{
			ImGui::SetNextWindowClass(&RootClass);
			ImGui::SetNextWindowDockID(GetRootDockId(), ImGuiCond_FirstUseEver);
			ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
			ImGui::Begin("Level Editor###LevelEditorRootWindow", nullptr,
				ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
			ImGui::PopStyleVar();

			// 툴바는 Level Editor 안에 그려서 다른 루트 탭(에셋 에디터)을 보면 함께 가려진다.
			if (Toolbar) Toolbar->Draw();

			// 저장된 배치가 있으면 imgui.ini가 노드를 만들어 두므로 처음 실행할 때만 기본 배치를 만든다.
			constexpr ImGuiID LevelDockSpaceId = 0x4C564C44;   // 'LVLD'. 고정 값이라 창 ID 스택과 무관하게 배치가 유지된다
			if (ImGui::DockBuilderGetNode(LevelDockSpaceId) == nullptr)
				BuildDefaultDockLayout(LevelDockSpaceId, ImGui::GetContentRegionAvail());
			// 탭이 가려져 Begin이 false여도 호출해야 패널의 도킹이 풀리지 않는다
			ImGui::DockSpace(LevelDockSpaceId, ImVec2(0.0f, 0.0f), dockspaceFlags, &GetLevelWindowClass());
			ImGui::End();
		}
	}
	else if (Toolbar)
	{
		Toolbar->Draw();
	}

	for (auto& Panel : Panels)
	{
		if (Panel->IsOpen())
		{
			ImGui::SetNextWindowClass(&GetLevelWindowClass());
			Panel->OnRender();
		}
	}
	DrawFbxImportDialog();
}

// 파일 메뉴의 요청을 애플리케이션 콜백으로 전달한다.
void FEditorUI::DrawMainMenuBar()
{
	// 메뉴바 높이만 글자 크기에 맞춰 키운다. 간격을 함께 바꾸면 펼친 메뉴까지 물려받는다 (TEAM5).
	ImGui::PushStyleVarY(ImGuiStyleVar_FramePadding, ImGui::GetFontSize() * 0.5f);

	if (ImGui::BeginMainMenuBar())
	{
		if (ImGui::BeginMenu("File"))
		{
			if (ImGui::MenuItem("New Scene")) { if (OnNewScene) OnNewScene(); }
			if (ImGui::MenuItem("Open Scene...")) { if (OnOpenScene) OnOpenScene(); }

			ImGui::Separator();

			if (ImGui::MenuItem("Save")) { if (OnSaveScene) OnSaveScene(); }
			if (ImGui::MenuItem("Save As...")) { if (OnSaveSceneAs) OnSaveSceneAs(); }

			ImGui::Separator();

			if (ImGui::MenuItem("Import FBX...")) OpenFbxImportDialog();

			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu("Window"))
		{
			// 패널 등록 순서는 그리기 순서라 그대로 두고, 메뉴에만 이름순으로 보여 준다.
			TArray<IEditorPanel*> SortedPanels;
			for (auto& Panel : Panels)
				SortedPanels.Add(Panel.get());
			EditorSort::SortByName(SortedPanels, [](const IEditorPanel* Panel) { return FString(Panel->GetPanelName()); });
			for (IEditorPanel* Panel : SortedPanels)
			{
				bool bOpen = Panel->IsOpen();

				if (ImGui::MenuItem(Panel->GetPanelName(), nullptr, bOpen))
				{
					Panel->SetOpen(!bOpen);
				}
			}
			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu("Build"))
		{
			const bool bCanPackage = !IsPackaging || !IsPackaging();
			if (ImGui::MenuItem("Package Game (Release)", nullptr, false, bCanPackage)) { if (OnPackageGame) OnPackageGame(); }
			ImGui::EndMenu();
		}

		ImGui::EndMainMenuBar();
	}
	ImGui::PopStyleVar();
}
