#include "AssetsWindow.h"
#include "ConsoleWindow.h"
#include "../Editor.h"

#include "String/SStringUtils.h"
#include "ENGINE/EngineUtils.h"

#include <filesystem>
#include <fstream>

#define FOLDER_ICON_PATH   L"DATA\\Icons\\64px\\Folder.png"   /**/
#define IMAGE_ICON_PATH    L"DATA\\Icons\\64px\\Image.png"    /**/
#define FILE_ICON_PATH     L"DATA\\Icons\\64px\\File.png"     /**/
#define LUA_ICON_PATH      L"DATA\\Icons\\64px\\Lua.png"      /**/
#define MODEL_ICON_PATH    L"DATA\\Icons\\64px\\Model.png"    /**/
#define FONT_ICON_PATH     L"DATA\\Icons\\64px\\Font.png"     /**/
#define AUDIO_ICON_PATH    L"DATA\\Icons\\64px\\Audio.png"    /**/
#define SKY_ICON_PATH      L"DATA\\Icons\\64px\\Sky.png"      /**/
#define MATERIAL_ICON_PATH L"DATA\\Icons\\64px\\Material.png" /**/

void AssetsWindow::Init(D3DWindow* dx, Editor* editor, ID3D12DescriptorHeap* GUISrvDescriptorHeap)
{
	m_dx = dx;
	m_editor = editor;
	SrvDescriptorHeap = GUISrvDescriptorHeap;

	// 读取图标
	ResourceUploadBatch resourceUpload(dx->GetDevice());
	resourceUpload.Begin();

	folderTexture.Create(
		dx->GetDevice(),
		SrvDescriptorHeap,
		&resourceUpload,
		L"folderTexture", FOLDER_ICON_PATH,
		TextureType::PNG,
		SrvDescriptorHeapIndex);
	SrvDescriptorHeapIndex++;

	fileTexture.Create(
		dx->GetDevice(),
		SrvDescriptorHeap,
		&resourceUpload,
		L"fileTexture", FILE_ICON_PATH,
		TextureType::PNG,
		SrvDescriptorHeapIndex);
	SrvDescriptorHeapIndex++;

	ttfTexture.Create(
		dx->GetDevice(),
		SrvDescriptorHeap,
		&resourceUpload,
		L"ttfTexture", FONT_ICON_PATH,
		TextureType::PNG,
		SrvDescriptorHeapIndex);
	SrvDescriptorHeapIndex++;

	imageTexture.Create(
		dx->GetDevice(),
		SrvDescriptorHeap,
		&resourceUpload,
		L"imageTexture", IMAGE_ICON_PATH,
		TextureType::PNG,
		SrvDescriptorHeapIndex);
	SrvDescriptorHeapIndex++;

	materialTexture.Create(
		dx->GetDevice(),
		SrvDescriptorHeap,
		&resourceUpload,
		L"materialTexture", MATERIAL_ICON_PATH,
		TextureType::PNG,
		SrvDescriptorHeapIndex);
	SrvDescriptorHeapIndex++;

	modelTexture.Create(
		dx->GetDevice(),
		SrvDescriptorHeap,
		&resourceUpload,
		L"modelTexture", MODEL_ICON_PATH,
		TextureType::PNG,
		SrvDescriptorHeapIndex);
	SrvDescriptorHeapIndex++;

	skyTexture.Create(
		dx->GetDevice(),
		SrvDescriptorHeap,
		&resourceUpload,
		L"skyTexture", SKY_ICON_PATH,
		TextureType::PNG,
		SrvDescriptorHeapIndex);
	SrvDescriptorHeapIndex++;

	audioTexture.Create(
		dx->GetDevice(),
		SrvDescriptorHeap,
		&resourceUpload,
		L"audioTexture", AUDIO_ICON_PATH,
		TextureType::PNG,
		SrvDescriptorHeapIndex);
	SrvDescriptorHeapIndex++;

	luaTexture.Create(
		dx->GetDevice(),
		SrvDescriptorHeap,
		&resourceUpload,
		L"luaTexture", LUA_ICON_PATH,
		TextureType::PNG,
		SrvDescriptorHeapIndex);
	SrvDescriptorHeapIndex++;

	auto uploadResourcesFinished = resourceUpload.End(
		dx->GetCommandQueue());
	uploadResourcesFinished.wait();

	//////////////////////////////////////////////////////////////

	//projectDirPath = GetAppDirPath() + FOLDER;
	OpenDir(EngineUtils::GetProjectDirPath());
}

void AssetsWindow::Render()
{
	if (!renderAssets)
		return;

	ImGui::Begin(u8"资源");
	{
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.0f, 4.0f));
		//if (outCore)
		//{
		//	ImGui::PushFont(m_editor->icons);
		//	{
		//		/*******************************************************/
		//		{
		//			if (EngineUtils::GetProjectDirPath() == EngineUtils::GetProjectDirPath())
		//			{
		//				ImGui::BeginDisabled();
		//				ImGui::Button(ICON_FA_ARROW_LEFT);
		//				ImGui::EndDisabled();
		//			}
		//			else
		//			{
		//				if (ImGui::Button(ICON_FA_ARROW_LEFT))
		//					GoBackDir();
		//			}
		//		}
		//		ImGui::SameLine();
		//		/*******************************************************/
		//		{
		//			ImGui::BeginDisabled();
		//			ImGui::Button(ICON_FA_ARROW_RIGHT);
		//			ImGui::EndDisabled();
		//		}
		//		ImGui::SameLine();
		//		/*******************************************************/
		//		{
		//			if (ImGui::Button(ICON_FA_SYNC))
		//				RefreshDir();
		//		}
		//		ImGui::SameLine();
		//		/*******************************************************/
		//	}
		//	ImGui::PopFont();
		//}
		//else
		//{
		//	ImGui::BeginDisabled();
		//	ImGui::PushFont(m_editor->icons);
		//	{
		//		ImGui::Button(ICON_FA_ARROW_LEFT);
		//		ImGui::SameLine();
		//		ImGui::Button(ICON_FA_ARROW_RIGHT);
		//		ImGui::SameLine();
		//		ImGui::Button(ICON_FA_SYNC);
		//		ImGui::SameLine();
		//	}
		//	ImGui::PopFont();
		//	ImGui::EndDisabled();
		//}

		static char str1[128] = "";
		ImGui::PushItemWidth(128.0f);
		ImGui::InputTextWithHint("##AssetsSearch", u8"查找...", str1, IM_ARRAYSIZE(str1));
		ImGui::PopItemWidth();
		ImGui::SameLine();

		size_t pos = EngineUtils::GetProjectDirPath().find(FOLDER);
		std::wstring str = EngineUtils::GetProjectDirPath().substr(pos + 1);

		ImGui::Text(SString::WstringToUTF8(str).c_str());
		ImGui::PopStyleVar();
		ImGui::Separator();

		///////////////////////////////////////////////////////

		ImGui::BeginChild("table111", ImVec2(0, 0), false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
		{
			if (ImGui::BeginTable(u8"table1", 2, ImGuiTableFlags_Resizable))
			{
				ImGui::TableNextRow();
				/***** LEFT  *****/
				ImGui::TableSetColumnIndex(0);
				{
					ImGui::BeginChild(u8"AAPPOO");
					{
						RenderDirList(dirs);
					}
					ImGui::EndChild();
				}

				/***** RIGHT *****/
				ImGui::TableSetColumnIndex(1);
				{
					ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2.0f, 2.0f));
					ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));
					ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0.0f);
					ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);

					///////////////////////////////////////////////////////

					if (!files.empty())
					{
						ImGui::BeginChild(u8"资源");
						{
							float space = size;
							for (size_t i = 0; i < files.size(); i++)
							{
								ImGui::BeginGroup();
								ImGui::PushID((int)i);
								{
									if (files[i].is_selected)
										ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyle().Colors[ImGuiCol_ButtonHovered]);

									///////////////////////////////////////

									if (files[i].is_dir) /* is dir */
									{
										ImGui::ImageButton((ImTextureID)folderTexture.GetGPUTexDescriptor().ptr, ImVec2(size, size));
									}
									else if (files[i].file_type == FILEs::File_Type::PNGFILE || files[i].file_type == FILEs::File_Type::DDSFILE)
									{
										//if (files[i].texture.GetResource()) /* if active */
										//{
										//	ImGui::ImageButton((ImTextureID)files[i].texture.GetGPUTexDescriptor().ptr, ImVec2(size, size));
										//}
										//else
										{
											ImGui::ImageButton((ImTextureID)imageTexture.GetGPUTexDescriptor().ptr, ImVec2(size, size));
										}
									}
									else if (files[i].file_type == FILEs::File_Type::LUAFILE)
									{
										ImGui::ImageButton((ImTextureID)luaTexture.GetGPUTexDescriptor().ptr, ImVec2(size, size));
									}
									else if (files[i].file_type == FILEs::File_Type::OBJFILE)
									{
										ImGui::ImageButton((ImTextureID)modelTexture.GetGPUTexDescriptor().ptr, ImVec2(size, size));
									}
									else if (files[i].file_type == FILEs::File_Type::TTFFILE)
									{
										ImGui::ImageButton((ImTextureID)ttfTexture.GetGPUTexDescriptor().ptr, ImVec2(size, size));
									}
									else if (files[i].file_type == FILEs::File_Type::WAVFILE)
									{
										ImGui::ImageButton((ImTextureID)audioTexture.GetGPUTexDescriptor().ptr, ImVec2(size, size));
									}
									else if (files[i].file_type == FILEs::File_Type::SKYFILE)
									{
										ImGui::ImageButton((ImTextureID)skyTexture.GetGPUTexDescriptor().ptr, ImVec2(size, size));
									}
									else if (files[i].file_type == FILEs::File_Type::MATFILE)
									{
										ImGui::ImageButton((ImTextureID)materialTexture.GetGPUTexDescriptor().ptr, ImVec2(size, size));
									}
									else /* any file */
									{
										ImGui::ImageButton((ImTextureID)fileTexture.GetGPUTexDescriptor().ptr, ImVec2(size, size));
									}

									///////////////////////////////////////

									//// DRAG ////
									if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None))
									{
										ImGui::SetDragDropPayload("DND_DEMO_ASS", &files[i], sizeof(FILEs));

										///////////////////////////////////////

										if (files[i].is_dir) /* is dir */
										{
											ImGui::Image((ImTextureID)folderTexture.GetGPUTexDescriptor().ptr, ImVec2(size, size));
										}
										else if (files[i].file_type == FILEs::File_Type::PNGFILE || files[i].file_type == FILEs::File_Type::DDSFILE)
										{
											//if (files[i].texture.GetResource()) /* if active */
											//{
											//	ImGui::Image((ImTextureID)files[i].texture.GetGPUTexDescriptor().ptr, ImVec2(size, size));
											//}
											//else
											{
												ImGui::Image((ImTextureID)imageTexture.GetGPUTexDescriptor().ptr, ImVec2(size, size));
											}
										}
										else if (files[i].file_type == FILEs::File_Type::LUAFILE)
										{
											ImGui::Image((ImTextureID)luaTexture.GetGPUTexDescriptor().ptr, ImVec2(size, size));
										}
										else if (files[i].file_type == FILEs::File_Type::OBJFILE)
										{
											ImGui::Image((ImTextureID)modelTexture.GetGPUTexDescriptor().ptr, ImVec2(size, size));
										}
										else if (files[i].file_type == FILEs::File_Type::TTFFILE)
										{
											ImGui::Image((ImTextureID)ttfTexture.GetGPUTexDescriptor().ptr, ImVec2(size, size));
										}
										else if (files[i].file_type == FILEs::File_Type::WAVFILE)
										{
											ImGui::Image((ImTextureID)audioTexture.GetGPUTexDescriptor().ptr, ImVec2(size, size));
										}
										else if (files[i].file_type == FILEs::File_Type::SKYFILE)
										{
											ImGui::Image((ImTextureID)skyTexture.GetGPUTexDescriptor().ptr, ImVec2(size, size));
										}
										else if (files[i].file_type == FILEs::File_Type::MATFILE)
										{
											ImGui::Image((ImTextureID)materialTexture.GetGPUTexDescriptor().ptr, ImVec2(size, size));
										}
										else /* any file */
										{
											ImGui::Image((ImTextureID)fileTexture.GetGPUTexDescriptor().ptr, ImVec2(size, size));
										}

										///////////////////////////////////////

										ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + size);
										ImGui::TextWrapped(SString::WstringToUTF8(files[i].file_name_only).c_str());
										ImGui::PopTextWrapPos();
										//ImGui::Text(files[i].file_name.c_str());
										ImGui::EndDragDropSource();
									}

									///////////////////////////////////////

									if (files[i].is_selected)
										ImGui::PopStyleColor(1);

									///////////////////////////////////////

									if (ImGui::IsItemHovered())
									{
										if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
										{
											if (files[i].is_dir)
											{
												std::wstring buffer = EngineUtils::GetProjectDirPath() + L"\\" + files[i].file_name;
												OpenDir(buffer);

												ImGui::PopID();
												ImGui::EndGroup();
												break;
											}
										}
										else if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
										{
											HideSelected();
											files[i].is_selected = true;
											selectedFile = &files[i];
										}
									}

									///////////////////////////////////////////////////////
									ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + size);
									ImGui::TextWrapped(SString::WstringToUTF8(files[i].file_name_only).c_str());
									ImGui::PopTextWrapPos();
								}
								ImGui::PopID();
								ImGui::EndGroup();

								space += ImGui::GetItemRectSize().x;
								if (space < ImGui::GetWindowSize().x)
									ImGui::SameLine();
								else
									space = size;
							}

							if (ImGui::IsWindowHovered() && !ImGui::IsItemHovered())
								if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
									HideSelected();
						}
						ImGui::EndChild();
					}
					ImGui::PopStyleVar(4);

					if (files.empty())
					{
						ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.5f, 1.0f));
						ImGui::Text(u8"此文件夹为空。");
						ImGui::PopStyleColor(1);
					}
				}
				ImGui::EndTable();
			}
		}
		ImGui::EndChild();
	}
	ImGui::End();
}

void AssetsWindow::OpenDir(std::wstring path)
{
	assert(!path.empty());
	assert(std::filesystem::exists(path));

	selectedFile = nullptr;         /* selected file */
	files.clear();                  /* -//- */
	dirs.dir_name = EngineUtils::GetProjectDirPath(); /* -//- */
	dirs.dir_child.clear();         /* -//- */
	FillDirList(&dirs);             /* -//- */

	//// dir ////
	for (const auto& index : std::filesystem::directory_iterator(path))
	{
		std::filesystem::path file(index.path());

		if (index.is_directory())
		{
			FILEs::File_Type file_type = FILEs::extensionToFileType(file.extension().wstring());
			//// vector ////
			files.push_back(FILEs(
				file.parent_path().wstring(),
				file.filename().wstring(), // file name
				file.stem().wstring(), // file name only
				file_type, // file type
				index.is_directory(), // is dir
				0)); // file size
		}
	}

	//// file ////
	for (const auto& index : std::filesystem::directory_iterator(path))
	{
		std::filesystem::path file(index.path());

		if (!index.is_directory())
		{
			FILEs::File_Type file_type = FILEs::extensionToFileType(file.extension().wstring());
			//// vector ////
			files.push_back(FILEs(
				file.parent_path().wstring(),
				file.filename().wstring(), // file name
				file.stem().wstring(), // file name only
				file_type, // file type
				index.is_directory(), // is dir
				(int)index.file_size())); // file size
		}
	}
}

void AssetsWindow::GoBackDir()
{
	if (EngineUtils::GetProjectDirPath() == EngineUtils::GetProjectDirPath())
		return;

	std::filesystem::path file(EngineUtils::GetProjectDirPath());
	OpenDir(file.parent_path().wstring());
}

void AssetsWindow::RefreshDir()
{
	OpenDir(EngineUtils::GetProjectDirPath());
}

void AssetsWindow::Shutdown()
{
}

void AssetsWindow::NeedRender(bool render)
{
	renderAssets = render;
}

//void AssetsWindow::OutCore(std::wstring path)
//{
//	for (size_t i = 0; i < files.size(); i++)
//	{
//		if (!thumbnail)
//			break;
//
//		if (!files[i].is_dir)
//		{
//			std::wstring buffer = EngineUtils::GetProjectDirPath() + L"\\" + files[i].file_name;
//			
//			TextureType txtttype;
//			if (files[i].file_type == PNGFILE)
//				txtttype = TextureType::PNG;
//			else if (files[i].file_type == DDSFILE)
//				txtttype = TextureType::DDS;
//			else
//				break;
//
//			ResourceUploadBatch resourceUpload(m_dx->GetDevice());
//			resourceUpload.Begin();
//
//			files[i].texture.Create(
//				m_dx->GetDevice(),
//				SrvDescriptorHeap,
//				&resourceUpload,
//				files[i].file_name, buffer,
//				txtttype,
//				SrvDescriptorHeapIndex);
//			SrvDescriptorHeapIndex++;
//			
//			//if (files[i].file_type == PNGFILE || files[i].file_type == DDSFILE)
//			//{
//
//			//	//ID3D11ShaderResourceView* normal_image;
//			//	//ID3D11ShaderResourceView* resized_image;
//			//	//ID3D11ShaderResourceView* converted_image;
//
//			//	///////////////////////////////////////////////
//
//
//			//	if (files[i].file_type == PNGFILE)
//			//	{
//			//		if (FAILED(CreateWICTextureFromFile(
//			//			m_dx->GetDevice(),
//			//			buffer.c_str(),
//			//			nullptr,
//			//			&normal_image)))
//			//			continue;
//			//	}
//
//			//	///////////////////////////////////////////////
//
//			//	if (files[i].file_type == DDSFILE)
//			//	{
//			//		if (FAILED(CreateDDSTextureFromFile(
//			//			m_dx->GetDevice(),
//			//			buffer.c_str(),
//			//			nullptr,
//			//			&normal_image)))
//			//			continue;
//			//	}
//
//			//	/////////////////////////////////////////////////
//
//			//	//if (FAILED(Resize(
//			//	//	normal_image.GetImages(),
//			//	//	normal_image.GetImageCount(),
//			//	//	normal_image.GetMetadata(),
//			//	//	(size_t)size, (size_t)size, TEX_FILTER_DEFAULT, resized_image)))
//			//	//	continue;
//
//			//	/////////////////////////////////////////////////
//
//			//	//if (FAILED(Convert(
//			//	//	resized_image.GetImages(),
//			//	//	resized_image.GetImageCount(),
//			//	//	resized_image.GetMetadata(),
//			//	//	DXGI_FORMAT_R32G32B32A32_FLOAT, TEX_FILTER_DEFAULT, TEX_THRESHOLD_DEFAULT,
//			//	//	converted_image)))
//			//	//	continue;
//
//			//	/////////////////////////////////////////////////
//			//	//ID3D11Texture2D* tex = nullptr;
//			//	//if (FAILED(m_dx->GetDevice()->CreateShaderResourceView(m_dx->GetDevice(),
//			//	//	converted_image.GetImages(),
//			//	//	converted_image.GetImageCount(),
//			//	//	converted_image.GetMetadata(),
//			//	//	&files[i].texture)))
//			//	//	continue;
//
//			//	/////////////////////////////////////////////////
//
//			//	//normal_image.Release();
//			//	//resized_image.Release();
//			//	//converted_image.Release();
//
//			//}
//			auto uploadResourcesFinished = resourceUpload.End(
//				m_dx->GetCommandQueue());
//			uploadResourcesFinished.wait();
//		}
//	}
//}

void AssetsWindow::CreateDir(std::wstring path)
{
	assert(!path.empty());
	//assert(!std::filesystem::exists(path));
	std::filesystem::create_directory(path);
}

FILEs* AssetsWindow::GetSelFile()
{
	return selectedFile;
}

void AssetsWindow::HideSelected()
{
	for (size_t i = 0; i < files.size(); i++)
		files[i].is_selected = false;

	selectedFile = nullptr;
}

void AssetsWindow::RenderDirList(dir_list dir)
{
	ImGuiTreeNodeFlags tree_flags = ImGuiTreeNodeFlags_OpenOnDoubleClick
		| ImGuiTreeNodeFlags_SpanAvailWidth
		| ImGuiTreeNodeFlags_OpenOnArrow
		| ImGuiTreeNodeFlags_DefaultOpen;

	if (!dir.dir_child.size())
		tree_flags |= ImGuiTreeNodeFlags_Leaf;

	if (!dir.dir_name.compare(EngineUtils::GetProjectDirPath()))
		tree_flags |= ImGuiTreeNodeFlags_Selected;

	size_t pos = dir.dir_name.find(FOLDER);
	std::wstring str = dir.dir_name.substr(pos + 1);

	bool node_open = ImGui::TreeNodeEx((void*)NULL, tree_flags, SString::WstringToUTF8(str).c_str());

	if (ImGui::IsItemHovered())
		if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
			OpenDir(dir.dir_name);

	if (node_open)
	{
		for (size_t i = 0; i < dir.dir_child.size(); i++)
			RenderDirList(dir.dir_child[i]);
		ImGui::TreePop();
	}
}

void AssetsWindow::FillDirList(dir_list* dir)
{
	for (const auto& index : std::filesystem::directory_iterator(dir->dir_name))
	{
		if (index.is_directory())
		{
			dir_list entry;
			entry.dir_name = index.path().wstring();
			dir->dir_child.push_back(entry);
		}
	}

	for (size_t i = 0; i < dir->dir_child.size(); i++)
		FillDirList(&dir->dir_child[i]);
}

void AssetsWindow::RemoveAsset(std::wstring path)
{
	std::wstring fix = path + std::wstring(1, '\0');
	std::wstring buff = std::wstring(fix.begin(), fix.end());
	/********/
	SHFILEOPSTRUCT sh;
	ZeroMemory(&sh, sizeof(SHFILEOPSTRUCT));
	sh.wFunc = FO_DELETE;
	sh.fFlags = FOF_ALLOWUNDO;
	sh.pFrom = buff.c_str();
	SHFileOperation(&sh);
}

UINT AssetsWindow::GetSafeName(std::wstring path, FILEs::File_Type type)
{
	UINT i = 0;
	while (true)
	{
		std::wstring str = path + std::to_wstring(i) + FILEs::fileTypeToExtension(type);
		if (!std::filesystem::exists(str))
			break;
		i++;
	}
	return i;
}

void AssetsWindow::SaveMaterialFile(std::wstring path, const MaterialBuffer& buffer)
{

}

void AssetsWindow::OpenMaterialFile(std::wstring path, MaterialBuffer& buffer)
{
}

void AssetsWindow::GetFileNameFromProjectDir(std::wstring path, FILEs::File_Type fileType, std::vector<std::pair<std::wstring, std::wstring>>& data)
{
	for (const auto& index : std::filesystem::directory_iterator(path))
	{
		std::filesystem::path file(index.path());

		if (index.is_directory())
		{
			GetFileNameFromProjectDir(index.path().wstring(), fileType, data);
		}
		else
		{
			if (!file.extension().wstring().compare(FILEs::fileTypeToExtension(fileType)))
				data.push_back(std::pair(index.path().wstring(), file.stem().wstring()));
		}
	}
}