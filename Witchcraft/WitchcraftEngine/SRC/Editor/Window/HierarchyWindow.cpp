#include "HierarchyWindow.h"

#include "ServicesContainer/COMPONENT/GeneralComponent.h"
#include "ENGINE/EngineUtils.h"
#include "String/SStringUtils.h"

void HierarchyWindow::Init(ConsoleWindow* consoleWindow, AssimpLoader* assimpLoader, ServicesContainer* ComponentServices)
{
	m_consoleWindow = consoleWindow;
	m_assimpLoader = assimpLoader;
	m_ComponentServices = ComponentServices;
}

void HierarchyWindow::Render()
{
	if (!renderHierarchy)
		return;

	ImGui::Begin(u8"层次");
	if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
		ImGui::OpenPopup(u8"菜单"); 
	
	
	Transform transform;
	if(ImGui::BeginPopup(u8"菜单"))
	{
		if (ImGui::MenuItem(u8"空的"))
		{
			openCreateWindow = true;
		}
		ImGui::Separator();
		if (ImGui::MenuItem(u8"天空"))
		{
			openCreateWindow = true;
			name = L"天空";
		}
		if (ImGui::MenuItem(u8"盒子"))
		{
			openCreateWindow = true;
			name = L"盒子";
		}
		if (ImGui::MenuItem(u8"球体"))
		{
			openCreateWindow = true;
			name = L"球体";
		}
		if (ImGui::MenuItem(u8"胶囊"))
		{
			openCreateWindow = true;
			name = L"胶囊";
		}
		if (ImGui::MenuItem(u8"平面"))
		{
			openCreateWindow = true;
			name = L"平面";
		}
		ImGui::Separator();
		if (ImGui::MenuItem(u8"相机"))
		{
			openCreateWindow = true;
			name = L"相机";
		}
		ImGui::End();
	}

	if (openCreateWindow)
	{
		if (CreateComponentWindow(&openCreateWindow, &name, &transform))
		{
			//m_ComponentServices->CreateCameraEntity(entity, name, &transform);
			//m_ComponentServices->selected = entity;
		}
	}

	RenderTree();
	
	ImGui::End();
}

bool HierarchyWindow::CreateComponentWindow(bool* pOpen, std::wstring* name, Transform* transform)
{
	if (ImGui::Begin(u8"创建实体", pOpen, ImGuiWindowFlags_NoDocking))
	{
		ImGui::Text(u8"名称：");
		ImGui::SameLine();
		std::string tmp = "";
		tmp = SString::WstringToUTF8(*name);
		if (ImGui::InputText("##NameComponent", &tmp, ImGuiInputTextFlags_EnterReturnsTrue))
			*name = SString::UTF8ToWstring(tmp);

		// 如果transform是空的就不需要设置，这部分也就不需要显示
		if (transform)
		{
			// 位置
			{
				ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x / 3 / (2 * 0.875));

				ImGui::Text(u8"位置：");
				ImGui::SameLine();
				ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.0f, 0.0f, 1.0f));
				ImGui::Text(u8"X");
				ImGui::PopStyleColor(1);
				ImGui::SameLine();
				char X[12] = "0.000000000";
				sprintf_s(X, 12, "%f", transform->position.x);
				std::string mX = X;
				if (ImGui::InputText("##PotXTransformComp", &mX))
					transform->position.x = atof(mX.c_str());

				ImGui::SameLine();
				ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.0f, 1.0f, 0.0f, 1.0f));
				ImGui::Text(u8"Y");
				ImGui::PopStyleColor(1);
				ImGui::SameLine();
				char Y[12] = "0.000000000";
				sprintf_s(Y, 12, "%f", transform->position.y);
				std::string mY = Y;
				if (ImGui::InputText("##PotYTransformComp", &mY))
					transform->position.y = atof(mY.c_str());

				ImGui::SameLine();
				ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.0f, 0.0f, 1.0f, 1.0f));
				ImGui::Text(u8"Z");
				ImGui::PopStyleColor(1);
				ImGui::SameLine();
				char Z[12] = "0.000000000";
				sprintf_s(Z, 12, "%f", transform->position.z);
				std::string mZ = Z;
				if (ImGui::InputText("##PotZTransformComp", &mZ))
					transform->position.z = atof(mZ.c_str());
			}
			// 旋转
			{
				ImGui::Text(u8"旋转：");
				ImGui::SameLine();
				ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.0f, 0.0f, 1.0f));
				ImGui::Text(u8"X");
				ImGui::PopStyleColor(1);
				ImGui::SameLine();
				char X[12] = "0.000000000";
				sprintf_s(X, 12, "%f", transform->rotation.x);
				std::string mX = X;
				if (ImGui::InputText("##RotXTransformComp", &mX))
					transform->rotation.x = atof(mX.c_str());

				ImGui::SameLine();
				ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.0f, 1.0f, 0.0f, 1.0f));
				ImGui::Text(u8"Y");
				ImGui::PopStyleColor(1);
				ImGui::SameLine();
				char Y[12] = "0.000000000";
				sprintf_s(Y, 12, "%f", transform->rotation.y);
				std::string mY = Y;
				if (ImGui::InputText("##RotYTransformComp", &mY))
					transform->rotation.y = atof(mY.c_str());

				ImGui::SameLine();
				ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.0f, 0.0f, 1.0f, 1.0f));
				ImGui::Text(u8"Z");
				ImGui::PopStyleColor(1);
				ImGui::SameLine();
				char Z[12] = "0.000000000";
				sprintf_s(Z, 12, "%f", transform->rotation.z);
				std::string mZ = Z;
				if (ImGui::InputText("##RotZTransformComp", &mZ))
					transform->rotation.z = atof(mZ.c_str());
			}
			// 缩放
			{
				ImGui::Text(u8"缩放：");
				ImGui::SameLine();
				ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.0f, 0.0f, 1.0f));
				ImGui::Text(u8"X");
				ImGui::PopStyleColor(1);
				ImGui::SameLine();
				char X[12] = "1.000000000";
				sprintf_s(X, 12, "%f", transform->scale.x);
				std::string mX = X;
				if (ImGui::InputText("##ScaXTransformComp", &mX))
					transform->scale.x = atof(mX.c_str());

				ImGui::SameLine();
				ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.0f, 1.0f, 0.0f, 1.0f));
				ImGui::Text(u8"Y");
				ImGui::PopStyleColor(1);
				ImGui::SameLine();
				char Y[12] = "1.000000000";
				sprintf_s(Y, 12, "%f", transform->scale.y);
				std::string mY = Y;
				if (ImGui::InputText("##ScaYTransformComp", &mY))
					transform->scale.y = atof(mY.c_str());

				ImGui::SameLine();
				ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.0f, 0.0f, 1.0f, 1.0f));
				ImGui::Text(u8"Z");
				ImGui::PopStyleColor(1);
				ImGui::SameLine();
				char Z[12] = "1.000000000";
				sprintf_s(Z, 12, "%f", transform->scale.z);
				std::string mZ = Z;
				if (ImGui::InputText("##ScaZTransformComp", &mZ))
					transform->scale.z = atof(mZ.c_str());
			}
		}

		if (ImGui::Button(u8"确定"))
		{
			// 判断是否存在同名称的对象
			//auto view = m_ComponentServices->registry.view<GeneralComponent>();
			//for (auto entity : view)
			//{
			//	std::wstring name = m_ComponentServices->registry.get<GeneralComponent>(entity).GetName();
			//	if (wcscmp(SString::UTF8ToWstring(tmp).c_str(), name.c_str()) == 0)
			//	{
			//		ImGui::End();
			//		MessageBox(nullptr, L"名称不得与任何现有项目重名！", L"信息", MB_OK);
			//		return false;
			//	}
			//}
			ImGui::End();
			*pOpen = false;
			return true;
		}
		else
			ImGui::SameLine();
		if (ImGui::Button(u8"取消")) { ImGui::End(); *pOpen = false; return false; }
		else
			ImGui::End();
	}

	return false;
}

void HierarchyWindow::NeedRender(bool render)
{
	renderHierarchy = render;
}

void HierarchyWindow::RenderTree()
{
	///////////////////////////////////////////////////////////

	ImGuiTreeNodeFlags tree_flags = ImGuiTreeNodeFlags_OpenOnDoubleClick
		| ImGuiTreeNodeFlags_SpanAvailWidth
		| ImGuiTreeNodeFlags_OpenOnArrow
		| ImGuiTreeNodeFlags_DefaultOpen;

	///////////////////////////////////////////////////////////

	///////////////////////////////////////////////////////////

	static const char* type = "DND_DEMO_CELL";

	//// DRAG ////
	if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None))
	{
		//ImGui::SetDragDropPayload(type, &entity, sizeof(entt::entity));
		//ImGui::Text(SString::WstringToUTF8(genComp.GetName()).c_str());
		ImGui::EndDragDropSource();
	}

	//// DROP ENTITY ////
	if (ImGui::BeginDragDropTarget())
	{
		if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(type))
		{
		}
		ImGui::EndDragDropTarget();
	}

	//// DROP ASS ////
	if (ImGui::BeginDragDropTarget())
	{
		if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("DND_DEMO_ASS"))
		{
			FILEs payload_n = *(FILEs*)payload->Data;
			std::wstring buffer = EngineUtils::GetProjectDirPath() + L"\\" + payload_n.file_name;
			if (payload_n.file_type == FILEs::File_Type::OBJFILE)
			{
				m_consoleWindow->AddDebugMessage(L"读取模型... %s", buffer.c_str());
				m_assimpLoader->LoadModel(buffer);
			}
		}
		ImGui::EndDragDropTarget();
	}
}