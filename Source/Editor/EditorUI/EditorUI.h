#pragma once

#include "Editor/EditorUI/EditorPanel.h"
#include "Editor/LevelEditor/LevelEditorToolbar.h"

#include <functional>
#include "Asset/FbxImporter/FbxAssetData.h"

class FEditorUI
{
public:
	bool Init(bool bInUseDockSpace = true, bool bInPassthruCentralNode = false);
	void Tick(float DeltaTime);
	void OnRender();
	// 모든 패널과 기본 열림 상태를 만든 뒤, 첫 ImGui 프레임 전에 호출한다.
	void RegisterPanelSettings();
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
	bool bLevelEditorOpen = true;
	bool bResetLevelLayout = false;

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
