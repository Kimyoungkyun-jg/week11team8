#include "EnginePCH.h"
#include "Editor/EditorUI/EditorUI.h"
#include "Editor/EditorUI/EditorUIScale.h"
#include "Asset/FbxImporter/FbxAssetImporter.h"
#include "Asset/FbxImporter/FbxAssetLoader.h"
#include "Asset/FbxImporter/FbxPath.h"
#include <commdlg.h>
#include <shobjidl.h>
#include <thread>
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "uuid.lib")
#pragma comment(lib, "comdlg32.lib")
namespace {
HRESULT ChooseFbxOutputFolder(HWND Owner, const fs::path& InitialDirectory, FString& Selected)
{
	HRESULT Result = E_FAIL;
	// 오디오 등이 메인 스레드를 MTA로 초기화할 수 있어 Shell UI는 별도의 STA에서 연다.
	std::thread Browser([&]
	{
		const HRESULT Init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
		if (FAILED(Init)) { Result = Init; return; }
		{
			try
			{
				ComPtr<IFileOpenDialog> Dialog;
				Result = CoCreateInstance(__uuidof(FileOpenDialog), nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&Dialog));
				if (SUCCEEDED(Result))
				{
					FILEOPENDIALOGOPTIONS Flags{};
					Result = Dialog->GetOptions(&Flags);
					if (SUCCEEDED(Result)) Result = Dialog->SetOptions(Flags | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST | FOS_NOCHANGEDIR);
					Dialog->SetTitle(L"Select FBX output folder inside Assets");
					fs::path Initial = InitialDirectory;
					while (!Initial.empty() && !fs::is_directory(Initial))
					{
						const fs::path Parent = Initial.parent_path();
						if (Parent == Initial) { Initial.clear(); break; }
						Initial = Parent;
					}
					ComPtr<IShellItem> Start;
					if (!Initial.empty() && SUCCEEDED(SHCreateItemFromParsingName(Initial.c_str(), nullptr, IID_PPV_ARGS(&Start))))
						Dialog->SetFolder(Start.Get());
					if (SUCCEEDED(Result)) Result = Dialog->Show(Owner);
					if (SUCCEEDED(Result))
					{
						ComPtr<IShellItem> Item;
						Result = Dialog->GetResult(&Item);
						PWSTR Path = nullptr;
						if (SUCCEEDED(Result)) Result = Item->GetDisplayName(SIGDN_FILESYSPATH, &Path);
						if (SUCCEEDED(Result)) Selected = FbxPath::ToString(fs::path(Path));
						CoTaskMemFree(Path);
					}
				}
			}
			catch (...) { Result = E_FAIL; }
		}
		CoUninitialize();
	});
	// 소유 창으로 오는 메시지도 처리해야 다른 스레드의 모달 대화상자가 정상 동작한다.
	const HANDLE Thread = Browser.native_handle();
	bool Quit = false;
	int QuitCode = 0;
	while (MsgWaitForMultipleObjects(1, &Thread, FALSE, INFINITE, QS_ALLINPUT) == WAIT_OBJECT_0 + 1)
	{
		MSG Message;
		while (PeekMessageW(&Message, nullptr, 0, 0, PM_REMOVE))
		{
			if (Message.message == WM_QUIT) { Quit = true; QuitCode = static_cast<int>(Message.wParam); }
			else { TranslateMessage(&Message); DispatchMessageW(&Message); }
		}
	}
	Browser.join();
	if (Quit) PostQuitMessage(QuitCode);
	return Result;
}
}
void FEditorUI::OpenFbxImportDialog(const FString& Source, const FString& Output, const FFbxImportOptions& Options)
{
	if (Source.size() >= sizeof(FbxSourcePath) || Output.size() >= sizeof(FbxOutputDirectory))
	{ FbxResult.Error = "The selected path is too long."; return; }
	strcpy_s(FbxSourcePath, Source.c_str());
	strcpy_s(FbxOutputDirectory, Output.c_str());
	bFbxOutputDirectoryEdited = !Output.empty();
	FbxOptions = Options;
	bRequestFbxImportDialog = true;
}

void FEditorUI::BrowseFbxSource()
{
	wchar_t Path[32768]{};
	MultiByteToWideChar(CP_UTF8, 0, FbxSourcePath, -1, Path, static_cast<int>(std::size(Path)));
	const std::wstring InitialDirectory = fs::absolute("Assets").wstring();
	OPENFILENAMEW Dialog{};
	Dialog.lStructSize = sizeof(Dialog);
	Dialog.hwndOwner = static_cast<HWND>(ImGui::GetMainViewport()->PlatformHandleRaw);
	Dialog.lpstrFilter = L"FBX files (*.fbx)\0*.fbx\0\0";
	Dialog.lpstrDefExt = L"fbx";
	Dialog.lpstrFile = Path;
	Dialog.nMaxFile = static_cast<DWORD>(std::size(Path));
	Dialog.lpstrInitialDir = InitialDirectory.c_str();
	Dialog.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
	if (GetOpenFileNameW(&Dialog))
	{
		char SelectedPath[sizeof(FbxSourcePath)]{};
		if (WideCharToMultiByte(CP_UTF8, 0, Path, -1, SelectedPath, sizeof(SelectedPath), nullptr, nullptr))
		{
			std::memcpy(FbxSourcePath, SelectedPath, sizeof(FbxSourcePath));
			UpdateFbxOutputDirectory();
			FbxResult = {};
		}
		else FbxResult.Error = "The selected file path is too long.";
	}
	else if (const DWORD Error = CommDlgExtendedError())
	{
		FbxResult.Error = "Could not open the file browser.";
		HTR_LOG(Error, "FBX file dialog failed: {}", Error);
	}
}

void FEditorUI::UpdateFbxOutputDirectory()
{
	if (!bFbxOutputDirectoryEdited)
	{
		const FString Default = FbxPath::DefaultOutputDirectory(FbxSourcePath);
		strcpy_s(FbxOutputDirectory, Default.c_str());
	}
}

void FEditorUI::BrowseFbxOutputDirectory()
{
	FString Selected;
	fs::path Initial;
	FString Ignored;
	if (!FbxPath::GetOutputDirectory(FbxOutputDirectory, Initial, Ignored)) Initial = fs::absolute("Assets");
	const HRESULT Result = ChooseFbxOutputFolder(static_cast<HWND>(ImGui::GetMainViewport()->PlatformHandleRaw), Initial, Selected);
	if (Result == HRESULT_FROM_WIN32(ERROR_CANCELLED)) return;
	if (FAILED(Result))
	{
		FbxResult.Error = "Could not open the output folder browser.";
		HTR_LOG(Error, "FBX output folder dialog failed: {}", static_cast<uint32>(Result));
		return;
	}
	fs::path Directory;
	FString Error;
	if (!FbxPath::GetOutputDirectory(Selected, Directory, Error)) { FbxResult.Error = Error; return; }
	const FString Path = FbxPath::ToString(Directory);
	if (Path.size() >= sizeof(FbxOutputDirectory)) { FbxResult.Error = "The selected folder path is too long."; return; }
	strcpy_s(FbxOutputDirectory, Path.c_str());
	bFbxOutputDirectoryEdited = true;
	FbxResult = {};
}

void FEditorUI::DrawFbxImportDialog()
{
	// File 메뉴의 ID 스택을 벗어난 뒤 같은 위치에서 팝업을 열고 그린다.
	if (bRequestFbxImportDialog)
	{
		bRequestFbxImportDialog = false;
		bFbxImportDialogOpen = true;
		FbxResult = {};
		UpdateFbxOutputDirectory();
		ImGui::OpenPopup("Import FBX");
	}
	ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(.5f, .5f));
	ImGui::SetNextWindowSize(ImVec2(EditorUIScale::Px(600.0f), 0), ImGuiCond_Appearing);
	if (ImGui::BeginPopupModal("Import FBX", &bFbxImportDialogOpen, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings))
	{
		ImGui::TextUnformatted("Source FBX");
		const float PathWidth = ImGui::GetContentRegionAvail().x - EditorUIScale::ButtonWidth("Browse...") - ImGui::GetStyle().ItemSpacing.x;
		ImGui::SetNextItemWidth(PathWidth);
		if (ImGui::InputText("##FbxSource", FbxSourcePath, sizeof(FbxSourcePath)))
		{
			UpdateFbxOutputDirectory();
			FbxResult = {};
		}
		ImGui::SameLine();
		if (ImGui::Button("Browse...")) BrowseFbxSource();
		ImGui::TextUnformatted("Output folder");
		ImGui::SetNextItemWidth(PathWidth);
		if (ImGui::InputText("##FbxOutput", FbxOutputDirectory, sizeof(FbxOutputDirectory)))
		{
			bFbxOutputDirectoryEdited = FbxOutputDirectory[0] != '\0';
			FbxResult = {};
		}
		ImGui::SameLine();
		if (ImGui::Button("Browse...##FbxOutput")) BrowseFbxOutputDirectory();

		ImGui::SeparatorText("Static Meshes");
		ImGui::PushID("StaticFbx");
		ImGui::Checkbox("Bake Meshes", &FbxOptions.bBakeStaticMeshes);
		if (ImGui::IsItemHovered()) ImGui::SetTooltip("On: bake the mesh node and all parent transforms into geometry.\nOff: import local geometry without the mesh node transform.\nCombine does not restore relative placement when Bake is off.");
		ImGui::Checkbox("Combine Meshes", &FbxOptions.bCombineStaticMeshes);
		ImGui::PopID();

		ImGui::SeparatorText("Skeletal Meshes");
		ImGui::PushID("SkeletalFbx");
		ImGui::Checkbox("Bake Meshes", &FbxOptions.bBakeSkeletalMeshes);
		if (ImGui::IsItemHovered()) ImGui::SetTooltip("On: include placement outside the rig in geometry and bind transforms.\nOff: remove the root bone parent's global bind transform; retain the root bone's own transform.");
		ImGui::Checkbox("Combine Meshes", &FbxOptions.bCombineSkeletalMeshes);
		if (ImGui::IsItemHovered()) ImGui::SetTooltip("Combine meshes using the same source rig root. An LOD group remains one asset when disabled.");
		ImGui::PopID();

		if (!FFbxAssetImporter::IsAvailable()) ImGui::TextWrapped("Run Setup/SlnGenerator.bat and rebuild the editor to enable FBX import.");
		if (!FbxResult.Error.empty())
		{
			ImGui::Separator();
			ImGui::TextWrapped("Import failed: %s", FbxResult.Error.c_str());
		}

		ImGui::Separator();
		ImGui::BeginDisabled(!FFbxAssetImporter::IsAvailable() || FbxSourcePath[0] == '\0' || FbxOutputDirectory[0] == '\0');
		if (ImGui::Button("Import", ImVec2(EditorUIScale::Px(100.0f), 0)))
		{
			FbxResult = FFbxAssetLoader::Import(FbxSourcePath, FbxOptions, FbxOutputDirectory);
			if (FbxResult.bSuccess)
			{
				if (OnFbxImported) OnFbxImported();
				HTR_LOG(Info, "FBX import completed: {} assets", FbxResult.AssetKeys.Num());
				bFbxImportDialogOpen = false;
				ImGui::CloseCurrentPopup();
			}
			else HTR_LOG(Error, "FBX import failed: {}", FbxResult.Error);
			for (const auto& Warning : FbxResult.Warnings) HTR_LOG(Warning, "FBX: {}", Warning);
		}
		ImGui::EndDisabled();
		ImGui::SameLine();
		if (ImGui::Button("Cancel", ImVec2(EditorUIScale::Px(100.0f), 0)))
		{
			bFbxImportDialogOpen = false;
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}
}
