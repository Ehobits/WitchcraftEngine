#include "AboutWindow.h"
#include "HELPERS/Helpers.h"
#include "String/SStringUtils.h"
#include "ENGINE/EngineUtils.h"

void AboutWindow::Render()
{
	if (!renderAbout) return;
	ImGuiStyle* style = &ImGui::GetStyle();
	ImVec2 windowPadding = style->WindowPadding;

	ImGui::SetNextWindowSize(ImVec2(0, 0));
	ImGui::Begin(u8"关于", &renderAbout, ImGuiWindowFlags_NoResize
		| ImGuiWindowFlags_NoDocking
		| ImGuiWindowFlags_NoScrollbar
		| ImGuiWindowFlags_NoScrollWithMouse);
	{
		D3D12_RESOURCE_DESC Desc;
		Desc = Longer.GetResource()->GetDesc();
		ImGui::Image((ImTextureID)Longer.GetGPUTexDescriptor().ptr, ImVec2(Desc.Height, Desc.Width));

		ImGui::SameLine();
		ImGui::Text(
			u8"Witchcraft Engine\n"
			u8"%s\n",
			SString::WstringToUTF8(_VersionText).c_str());
		ImGui::Text(u8"使用的第三方库：");
		ImGui::BeginChild("AboutChild", ImVec2(0, 128));
		{
			ImGui::Text(u8"Dear ImGui https://github.com/ocornut/imgui");
			ImGui::Text(u8"assimp https://github.com/assimp/assimp");
			ImGui::Text(u8"Flecs https://github.com/SanderMertens/flecs");
			ImGui::Text(u8"DirectXTK12 https://github.com/Microsoft/DirectXTK12");
			ImGui::Text(u8"Box2D https://github.com/erincatto/box2d");
			ImGui::Text(u8"JoltPhysics https://github.com/jrouwe/JoltPhysics");
			ImGui::Text(u8"Lua https://github.com/lua/lua");
			ImGui::Text(u8"sol2 https://github.com/ThePhD/sol2");
			ImGui::Text(u8"zlib");
		}
		ImGui::EndChild();
		float _X = windowPadding.x + ImGui::CalcTextSize(u8"Made in XXXXX").x;
		ImGui::SetCursorPos(ImVec2(400 - _X, ImGui::GetCursorPos().y));
		ImGui::Text(u8"Made in XXXXX");
		if (ImGui::Button(u8"确定"))
			renderAbout = false;
	}
	ImGui::End();
}

void AboutWindow::Init(D3DWindow* dx, ID3D12DescriptorHeap* GUISrvDescriptorHeap)
{
	ResourceUploadBatch resourceUpload(dx->GetDevice());
	resourceUpload.Begin();

	Longer.Create(
		dx->GetDevice(),
		GUISrvDescriptorHeap,
		&resourceUpload,
		L"longer", L"DATA/Images/Witchcraft.png",
		TextureType::PNG,
		SrvDescriptorHeapIndex);
	SrvDescriptorHeapIndex++;

	auto uploadResourcesFinished = resourceUpload.End(
		dx->GetCommandQueue());
	uploadResourcesFinished.wait();

	std::wstring _Ver = std::to_wstring(MAJOR) + L"." + std::to_wstring(MINOR) + L"." + std::to_wstring(PATCH);
	_VersionText = L"Version " + _Ver + L" (" + _Ver + L")";
}

void AboutWindow::Shutdown()
{
}

void AboutWindow::NeedRender(bool render)
{
	renderAbout = render;
}
