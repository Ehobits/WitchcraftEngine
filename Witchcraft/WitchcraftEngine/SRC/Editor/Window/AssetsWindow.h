#pragma once

#include "System/Assets.h"
#include "D3DWindow/D3DWindow.h"
#include "Helpers/Helpers.h"

#include <imgui.h>

class Editor;

class AssetsWindow
{
public:
	void Init(D3DWindow* dx, Editor* editor, ID3D12DescriptorHeap* GUISrvDescriptorHeap);
	void Render();

	void NeedRender(bool render);

public:
	void CreateDir(std::wstring path);
	void OpenDir(std::wstring path);
	void RemoveAsset(std::wstring path);
	void GoBackDir();
	void RefreshDir();
	void CreateFolderInCurrentDirectory();
	void CreateLuaScriptInCurrentDirectory();
	void RequestCreateMaterialDialog();
	void RequestCreateAnimationDialog();
	void RequestRemoveSelectedAsset();
	void RequestRenameSelectedAsset();
	FILEs* GetSelFile();
	UINT GetSafeName(std::wstring path, FILEs::File_Type type = FILEs::File_Type::Count);
	void GetFileNameFromProjectDir(std::wstring path, FILEs::File_Type fileType, std::vector<std::pair<std::wstring, std::wstring>>& data);

private:
	bool renderAssets = true;
	
	ID3D12DescriptorHeap* SrvDescriptorHeap = nullptr;

	Texture folderTexture;
	Texture fileTexture;
	Texture ttfTexture;
	Texture imageTexture;
	Texture materialTexture;
	Texture modelTexture;
	Texture skyTexture;
	Texture audioTexture;
	Texture luaTexture;
	UINT SrvDescriptorHeapIndex = 2;

private:
	std::vector<FILEs> files;
	float size = 64.0f;
	FILEs* selectedFile = nullptr;
	std::wstring m_currentDirPath;
	void HideSelected();
	bool thumbnail = true;
	dir_list dirs;
	void RenderDirList(const dir_list& dir);
	void FillDirList(dir_list* dir);
	void RenderHeaderBar();
	void RenderDirectoryPane();
	void RenderFilePane();
	void RenderFilteredFileItems(size_t* visibleFileCount);
	void RenderFileItem(size_t fileIndex, float* space);
	void RenderPathBreadcrumbs();
	void RenderFileIconButton(const FILEs& file, bool imageButton);
	void RenderFileDragPreview(const FILEs& file);
	bool HandleFileItemInteraction(FILEs& file);
	void RenderContextMenu(const char* popupId);
	void RenderCreateMaterialPopup();
	void RenderCreateAnimationPopup();
	void RenderCreateLuaScriptPopup();
	std::wstring MakeLuaScriptModuleName(const std::wstring& rawName);
	void RenderRemoveConfirmPopup();
	void RenderRenamePopup();
	std::wstring GetCurrentDirectoryPath() const;
	std::wstring SanitizeFileStem(const std::wstring& value) const;
	void QueueRemoveAsset(const std::wstring& fullPath, const std::wstring& displayName);
	void QueueSelectAsset(const std::wstring& fullPath);
	void QueueOpenDir(const std::wstring& path);
	void ProcessPendingOpenDir();
	bool SelectAssetByPath(const std::wstring& fullPath);
	bool PassesSearchFilter(const FILEs& file) const;
	std::wstring GetDirectoryPathRelativeToProject(const std::wstring& fullPath) const;
	static const std::wstring ToLowerCopy(std::wstring text);

	D3DWindow* m_dx = nullptr;
	Editor* m_editor = nullptr;
	char m_searchText[128] = {};
	bool m_openCreateMaterialPopup = false;
	char m_createMaterialFileStem[128] = {};
	char m_createMaterialDisplayName[128] = {};
	DirectX::XMFLOAT4 m_createMaterialDiffuseColor = { 1.0f, 1.0f, 1.0f, 1.0f };
	DirectX::XMFLOAT3 m_createMaterialEmissive = { 0.0f, 0.0f, 0.0f };
	bool m_createMaterialUseNormalTexture = false;
	bool m_createMaterialUseMetallicTexture = false;
	bool m_createMaterialUseRoughnessTexture = false;
	bool m_createMaterialUseOpacityTexture = false;
	float m_createMaterialMetallic = 0.0f;
	float m_createMaterialRoughness = 1.0f;
	float m_createMaterialOpacity = 1.0f;
	bool m_createMaterialDisplayNameEditedManually = false;
	std::wstring m_createMaterialErrorMessage;
	bool m_openCreateAnimationPopup = false;
	char m_createAnimationFileStem[128] = {};
	char m_createAnimationClipName[128] = {};
	float m_createAnimationDuration = 1.0f;
	float m_createAnimationTicksPerSecond = 30.0f;
	bool m_createAnimationLoop = true;
	bool m_createAnimationClipNameEditedManually = false;
	std::wstring m_createAnimationErrorMessage;
	bool m_openCreateLuaScriptPopup = false;
	char m_createLuaScriptFileStem[128] = {};
	std::wstring m_createLuaScriptErrorMessage;
	bool m_openRemoveConfirmPopup = false;
	std::wstring m_pendingRemoveAssetPath;
	std::wstring m_pendingRemoveAssetDisplayName;
	bool m_openRenamePopup = false;
	std::wstring m_pendingRenameAssetPath;
	std::wstring m_pendingRenameAssetDisplayName;
	char m_renameAssetStem[260] = {};
	std::wstring m_renameAssetErrorMessage;
	std::wstring m_pendingSelectAssetPath;
	std::wstring m_pendingOpenDirPath;
	std::wstring m_lastProjectRootPath;

};
