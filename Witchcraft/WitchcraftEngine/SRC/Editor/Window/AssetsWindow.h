#pragma once

#include "System/Assets.h"
#include "D3DWindow/D3DWindow.h"
#include "ServicesContainer/Component/MeshComponent.h"
#include "Helpers/Helpers.h"

#include <imgui.h>

class Editor;

class AssetsWindow
{
public:
	void Init(D3DWindow* dx, Editor* editor, ID3D12DescriptorHeap* GUISrvDescriptorHeap);
	void Render();
	void Shutdown();

	void NeedRender(bool render);

public:
	void CreateDir(std::wstring path);
	void OpenDir(std::wstring path);
	void RemoveAsset(std::wstring path);
	void GoBackDir();
	void RefreshDir();
	FILEs* GetSelFile();
	UINT GetSafeName(std::wstring path, FILEs::File_Type type = FILEs::File_Type::Count);
	void GetFileNameFromProjectDir(std::wstring path, FILEs::File_Type fileType, std::vector<std::pair<std::wstring, std::wstring>>& data);

public:
	void SaveMaterialFile(std::wstring path, const MaterialBuffer& buffer);
	void OpenMaterialFile(std::wstring path, MaterialBuffer& buffer);

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
	void HideSelected();
	bool thumbnail = true;
	dir_list dirs;
	void RenderDirList(dir_list dir);
	void FillDirList(dir_list* dir);

	D3DWindow* m_dx = nullptr;
	Editor* m_editor = nullptr;

};
