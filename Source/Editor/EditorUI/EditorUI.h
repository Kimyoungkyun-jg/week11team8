#pragma once

#include "Editor/EditorUI/EditorPanel.h"
#include "Editor/LevelEditor/LevelEditorToolbar.h"

#include <functional>
#include "Asset/FbxImporter/FbxAssetData.h"

struct ImGuiWindowClass;

class FEditorUI
{
public:
	// 2단 도킹 (TEAM5). 루트 창 클래스가 있는 창(Level Editor, 에셋 에디터)만 바깥 도킹 공간에 탭으로 붙는다.
	static const ImGuiWindowClass& GetRootWindowClass();
	static const ImGuiWindowClass& GetLevelWindowClass();
	static unsigned int GetRootDockId();

	bool Init(bool bInUseDockSpace = true, bool bInPassthruCentralNode = false);
	void Tick(float DeltaTime);
	void OnRender();
	void OpenFbxImportDialog(const FString& Source = {}, const FString& Output = {}, const FFbxImportOptions& Options = {});
	void SetFbxImportedCallback(std::function<void()> Callback) { OnFbxImported = std::move(Callback); }

	template <typename T>
	T* AddEditorPanel()
	{
		TUniquePtr<T> newPanel = MakeUnique<T>();
		T* Ret = newPanel.get();
		Ret->Init();
		Panels.Add(std::move(newPanel));

		return Ret;
	}

	FLevelEditorToolbar* GetToolbar() const { return Toolbar.get(); }

	void SetNewSceneCallback(std::function<void()> InCallback) { OnNewScene = InCallback; }
	void SetOpenSceneCallback(std::function<void()> InCallback) { OnOpenScene = InCallback; }
	void SetSaveSceneCallback(std::function<void()> InCallback) { OnSaveScene = InCallback; }
	void SetSaveSceneAsCallback(std::function<void()> InCallback) { OnSaveSceneAs = InCallback; }
	void SetPackageGameCallback(std::function<void()> InCallback) { OnPackageGame = InCallback; }
	// 패키징 중이면 true. Build 메뉴 항목을 비활성화하는 데 쓴다.
	void SetIsPackagingQuery(std::function<bool()> InQuery) { IsPackaging = InQuery; }

private:
	bool bUseDockSpace = true;
	bool bPassthruCentralNode = false;

	void DrawMainMenuBar();
	void DrawFbxImportDialog();
	void BrowseFbxSource();
	void BrowseFbxOutputDirectory();
	void UpdateFbxOutputDirectory();
	bool bRequestFbxImportDialog = false;
	bool bFbxImportDialogOpen = false;
	char FbxSourcePath[2048]{};
	char FbxOutputDirectory[2048]{};
	bool bFbxOutputDirectoryEdited = false;
	FFbxImportOptions FbxOptions;
	FFbxImportResult FbxResult;
	std::function<void()> OnFbxImported;

	TArray<TUniquePtr<IEditorPanel>> Panels;
	TUniquePtr<FLevelEditorToolbar> Toolbar;

	std::function<void()> OnNewScene;
	std::function<void()> OnOpenScene;
	std::function<void()> OnSaveScene;
	std::function<void()> OnSaveSceneAs;
	std::function<void()> OnPackageGame;
	std::function<bool()> IsPackaging;
};
