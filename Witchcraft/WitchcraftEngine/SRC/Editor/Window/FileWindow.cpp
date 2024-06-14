#include "FileWindow.h"
#include "String/SStringUtils.h"
#include "D3DWindow/D3DWindow.h"

void FileWindow::Init(D3DWindow* dx, AssetsWindow* assetsWindow, ID3D12DescriptorHeap* GUISrvDescriptorHeap)
{
	m_dx = dx;
	m_assetsWindow = assetsWindow;
	SrvDescriptorHeap = GUISrvDescriptorHeap;
}

void FileWindow::Render()
{
	if (!renderFile)
		return;

	ImGui::Begin(u8"文件信息");
	{
		FILEs* selected = m_assetsWindow->GetSelFile();

		if (selected != nullptr)
		{
			if (!selected->is_dir)
			{
				ImGui::Text(u8"文件名：%s",SString::WstringToUTF8(selected->file_name).c_str());
				ImGui::Text(u8"文件大小：%i KB", selected->file_size / 1024); /* B TO KB */
				ImGui::Separator();
			}

			if (selected->file_type == FILEs::File_Type::PNGFILE || selected->file_type == FILEs::File_Type::DDSFILE)
			{
				ImGui::Text(u8"一般图像文件");
				if (!selected->texture.GetResource())
				{
					if (!selected->is_dir)
					{
						std::wstring buffer = EngineUtils::GetProjectDirPath() + L"\\" + selected->file_name;

						TextureType txtttype;
						if (selected->file_type == FILEs::File_Type::PNGFILE)
							txtttype = TextureType::PNG;
						else if (selected->file_type == FILEs::File_Type::DDSFILE)
							txtttype = TextureType::DDS;
						else
							return;

						ResourceUploadBatch resourceUpload(m_dx->GetDevice());
						resourceUpload.Begin();

						selected->texture.Create(
							m_dx->GetDevice(),
							SrvDescriptorHeap,
							&resourceUpload,
							selected->file_name, buffer,
							txtttype,
							SrvDescriptorHeapIndex);
						SrvDescriptorHeapIndex++;

						auto uploadResourcesFinished = resourceUpload.End(
							m_dx->GetCommandQueue());
						uploadResourcesFinished.wait();
					}
				}

				ImGui::Image((ImTextureID)selected->texture.GetGPUTexDescriptor().ptr, ImVec2(256, 256));
			}
			//else if (selected->file_type == FILEs::File_Type::HDRFILE)
			//	ImGui::Text(u8"HDR图像文件");
			else if (selected->file_type == FILEs::File_Type::LUAFILE)
				ImGui::Text(u8"LUA脚本文件");
			else if (selected->file_type == FILEs::File_Type::WAVFILE)
				ImGui::Text(u8"音频文件");
			else if (selected->file_type == FILEs::File_Type::TXTFILE)
				ImGui::Text(u8"文本文件");
			else if (selected->file_type == FILEs::File_Type::TTFFILE)
				ImGui::Text(u8"字体文件");
			else if (selected->file_type == FILEs::File_Type::OBJFILE)
				ImGui::Text(u8"模型文件");
			else if (selected->file_type == FILEs::File_Type::MATFILE)
				ImGui::Text(u8"材质文件");
			else if (selected->file_type == FILEs::File_Type::SKYFILE)
				ImGui::Text(u8"SKY文件");
			else
			{
				if (!selected->is_dir)
				{
					ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.5f, 1.0f));
					ImGui::Text(u8"未知文件类型");
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
