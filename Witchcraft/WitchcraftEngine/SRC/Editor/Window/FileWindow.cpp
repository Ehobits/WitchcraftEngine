#include "FileWindow.h"
#include <filesystem>
#include "String/SStringUtils.h"
#include "D3DWindow/D3DWindow.h"

void FileWindow::Init(D3DWindow* dx, AssetsWindow* assetsWindow, ID3D12DescriptorHeap* GUISrvDescriptorHeap)
{
	m_dx = dx;
	m_assetsWindow = assetsWindow;
	SrvDescriptorHeap = GUISrvDescriptorHeap;
}

void FileWindow::Update()
{
	FILEs* selected = m_assetsWindow->GetSelFile();
	if (selected == nullptr || selected->is_dir)
		return;

	if (selected->file_type != FILEs::File_Type::PNGFILE &&
		selected->file_type != FILEs::File_Type::DDSFILE)
	{
		return;
	}

	const std::filesystem::path fullPath = std::filesystem::path(selected->file_path) / selected->file_name;
	if (m_previewTexture.GetResource() != nullptr && m_previewTexturePath == fullPath.wstring())
		return;

	LoadPreviewTexture(fullPath, static_cast<FILEs::File_Type>(selected->file_type));
}

void FileWindow::Render()
{
	if (!renderFile)
		return;

	ImGui::Begin("文件信息");
	{
		FILEs* selected = m_assetsWindow->GetSelFile();

		if (selected != nullptr)
		{
			if (!selected->is_dir)
			{
				ImGui::Text("文件名：%s",SString::WstringToUTF8(selected->file_name).c_str());
				ImGui::Text("文件大小：%i KB", selected->file_size / 1024); /* B TO KB */
				ImGui::Separator();
			}

			if (selected->file_type == FILEs::File_Type::PNGFILE || selected->file_type == FILEs::File_Type::DDSFILE)
			{
				ImGui::Text("一般图像文件");
				const std::filesystem::path fullPath = std::filesystem::path(selected->file_path) / selected->file_name;

				if (m_previewTexture.GetResource() != nullptr && m_previewTexturePath == fullPath.wstring())
				{
					ImGui::Image((ImTextureID)m_previewTexture.GetGPUTexDescriptor().ptr, ImVec2(256, 256));
				}
				else
				{
					ImGui::TextDisabled("预览未就绪：%s", SString::WstringToUTF8(fullPath.wstring()).c_str());
				}
			}
			else if (selected->file_type == FILEs::File_Type::LUAFILE)
				ImGui::Text("LUA脚本文件");
			else if (selected->file_type == FILEs::File_Type::WAVFILE)
				ImGui::Text("音频文件");
			else if (selected->file_type == FILEs::File_Type::TXTFILE)
				ImGui::Text("文本文件");
			else if (selected->file_type == FILEs::File_Type::TTFFILE)
				ImGui::Text("字体文件");
			else if (selected->file_type == FILEs::File_Type::OBJFILE
				|| selected->file_type == FILEs::File_Type::GLTFFILE
				|| selected->file_type == FILEs::File_Type::GLBFILE
				|| selected->file_type == FILEs::File_Type::WMODELFILE)
				ImGui::Text("模型文件");
			else if (selected->file_type == FILEs::File_Type::MATFILE)
				ImGui::Text("材质文件");
			else if (selected->file_type == FILEs::File_Type::SKYFILE)
				ImGui::Text("SKY文件");
			else
			{
				if (!selected->is_dir)
				{
					ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.5f, 1.0f));
					ImGui::Text("未知文件类型");
					ImGui::PopStyleColor();
				}
			}
		}
	}
	ImGui::End();
}

void FileWindow::NeedRender(bool render)
{
	renderFile = render;
}

void FileWindow::LoadPreviewTexture(const std::filesystem::path& fullPath, FILEs::File_Type fileType)
{
	if (!std::filesystem::exists(fullPath))
		return;

	TextureType textureType;
	if (fileType == FILEs::File_Type::PNGFILE)
		textureType = TextureType::PNG;
	else if (fileType == FILEs::File_Type::DDSFILE)
		textureType = TextureType::DDS;
	else
		return;

	ResourceUploadBatch resourceUpload(m_dx->GetDevice());
	resourceUpload.Begin();

	Texture previewTexture;
	previewTexture.Create(
		m_dx->GetDevice(),
		SrvDescriptorHeap,
		&resourceUpload,
		fullPath.filename().wstring(),
		fullPath.wstring(),
		textureType,
		SrvDescriptorHeapIndex);
	SrvDescriptorHeapIndex++;

	auto uploadResourcesFinished = resourceUpload.End(
		m_dx->GetCommandQueue());
	uploadResourcesFinished.wait();

	m_previewTexture = previewTexture;
	m_previewTexturePath = fullPath.wstring();
}
