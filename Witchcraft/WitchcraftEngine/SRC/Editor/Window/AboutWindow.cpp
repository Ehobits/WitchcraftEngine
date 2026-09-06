#include "AboutWindow.h"
#include "HELPERS/Helpers.h"
#include "String/SStringUtils.h"
#include "ENGINE/EngineUtils.h"
#include <shellapi.h>


static const char* kGithubProjectUrl = "https://github.com/Ehobits/WitchcraftEngine";

static void OpenProjectUrl(const char* url)
{
	ShellExecuteA(nullptr, "open", url, nullptr, nullptr, SW_SHOWNORMAL);
}

void AboutWindow::Render()
{
	if (!renderAbout) return;
	ImGuiStyle* style = &ImGui::GetStyle();
	ImVec2 windowPadding = style->WindowPadding;

	ImGui::SetNextWindowSize(ImVec2(960.0f, 1120.0f), ImGuiCond_Always);
	ImGui::Begin("关于", &renderAbout, ImGuiWindowFlags_NoDocking
		| ImGuiWindowFlags_NoResize
		| ImGuiWindowFlags_NoScrollWithMouse);
	{
		const float logoSize = 144.0f;
		ImGui::Image(ImTextureRef((ImTextureID)Longer.GetGPUTexDescriptor().ptr), ImVec2(logoSize, logoSize));

		ImGui::SameLine();
		ImGui::BeginGroup();
		{
			ImGui::TextUnformatted("Witchcraft Engine");
			ImGui::TextUnformatted(SString::WstringToUTF8(_VersionText).c_str());
			ImGui::Spacing();
			ImGui::TextWrapped("这是一个以 DirectX 12 为核心的实时渲染引擎与编辑器原型，当前聚焦于渲染地基、ECS 场景、Lua 脚本、动画、物理和资源管线的整合。");
			ImGui::Spacing();

		}
		ImGui::EndGroup();

		ImGui::TextUnformatted("GitHub项目地址：");
		ImGui::SameLine();
		if (ImGui::Button(kGithubProjectUrl))
		{
			OpenProjectUrl(kGithubProjectUrl);
		}

		ImGui::Separator();
		ImGui::TextUnformatted("主要功能");
		ImGui::BulletText("D3D12 渲染主循环与 pass 模块化");
		ImGui::BulletText("ECS 场景编辑、层级与组件管理");
		ImGui::BulletText("Lua + sol2 脚本编辑与运行时回调");
		ImGui::BulletText("IBL / RTT / 阴影 / AO / 体积光 / 后处理链路");
		ImGui::BulletText("动画、物理、资源导入与场景保存读取");

		ImGui::Spacing();
		ImGui::TextUnformatted("使用的第三方库");
		ImGui::BeginChild("AboutChild", ImVec2(0, 180), true);
		{
			ImGui::TextUnformatted("Dear ImGui  https://github.com/ocornut/imgui");
			ImGui::TextUnformatted("ImGuiColorTextEdit  https://github.com/goossens/ImGuiColorTextEdit");
			ImGui::TextUnformatted("assimp  https://github.com/assimp/assimp");
			ImGui::TextUnformatted("Flecs  https://github.com/SanderMertens/flecs");
			ImGui::TextUnformatted("DirectXTK12  https://github.com/Microsoft/DirectXTK12");
			ImGui::TextUnformatted("pugixml  https://github.com/zeux/pugixml");
			ImGui::TextUnformatted("JoltPhysics  https://github.com/jrouwe/JoltPhysics");
			ImGui::TextUnformatted("Lua  https://github.com/lua/lua");
			ImGui::TextUnformatted("sol2  https://github.com/ThePhD/sol2");
			ImGui::TextUnformatted("libpng  https://github.com/pnggroup/libpng");
			ImGui::TextUnformatted("zlib  https://github.com/madler/zlib");
		}
		ImGui::EndChild();

		const float footerWidth = windowPadding.x + ImGui::CalcTextSize("Made with DirectX 12").x;
		ImGui::SetCursorPos(ImVec2(760.0f - footerWidth, ImGui::GetCursorPos().y));
		ImGui::TextUnformatted("Made with DirectX 12");
		ImGui::Spacing();
		const float confirmWidth = ImGui::GetContentRegionAvail().x;
		if (ImGui::Button("确定", ImVec2(confirmWidth, 44.0f)))
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

void AboutWindow::NeedRender(bool render)
{
	renderAbout = render;
}
