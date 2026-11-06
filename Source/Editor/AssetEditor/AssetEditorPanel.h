#pragma once
#include "Editor/EditorUI/EditorPanel.h"
#include "Editor/Viewports/EditorViewportClient.h"
class URenderAsset;
class UWorld;
class FSkyboxRenderer;
class UEditorEngine;

// Root asset tab for TEAM5 imported static meshes, textures, materials and skeletons.
class FAssetEditorPanel : public IEditorPanel
{
public:
	FAssetEditorPanel();
	~FAssetEditorPanel() override;
	bool Init() override { SetOpen(false); return true; }
	void Tick(float DeltaTime) override;
	void OnRender() override;
	const char* GetPanelName() const override { return "Asset Editor"; }
	void OpenAsset(const FString& Path);
	void RenderPreview(UEditorEngine* Engine);
	void Release();
private:
	void SaveMaterial();
	FString Path;
	FString Status;
	URenderAsset* Asset = nullptr;
	UWorld* PreviewWorld = nullptr;
	FEditorViewportClient Client;
	TUniquePtr<FSkyboxRenderer> Skybox;
	bool bVisible = false;
};
