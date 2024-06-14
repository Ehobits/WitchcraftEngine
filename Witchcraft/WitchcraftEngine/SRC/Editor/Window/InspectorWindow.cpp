#include "InspectorWindow.h"

#include "ServicesContainer/COMPONENT/GeneralComponent.h"
#include "ServicesContainer/COMPONENT/TransformComponent.h"
#include "ServicesContainer/COMPONENT/MeshComponent.h"
#include "ServicesContainer/COMPONENT/CameraComponent.h"
#include "ServicesContainer/COMPONENT/RigidbodyComponent.h"
#include "ServicesContainer/COMPONENT/ScriptingComponent.h"
#include "ServicesContainer/COMPONENT/PhysicsComponent.h"
#include "String/SStringUtils.h"
#include "ENGINE/EngineUtils.h"

#include "D3DWindow/D3DWindow.h"

void InspectorWindow::Init(ServicesContainer* ComponentServices, D3DWindow* dx, AssetsWindow* assetsWindow, PhysicsSystem* physicsSystem)
{
	m_ComponentServices = ComponentServices;
	m_dx = dx;
	m_assetsWindow = assetsWindow;
	m_physicsSystem = physicsSystem;
	_Enabled = true;
	_Static = false;
}

void InspectorWindow::Render()
{
	if (!renderInspector)
		return;

	ImGui::Begin(u8"实体信息");
	{
		//if (m_ComponentServices->selected != entt::null)
		{
			//UpdateComponent();
			//RenderComponent();
			//RenderAdd();
		}
	}
	ImGui::End();
}

void InspectorWindow::NeedRender(bool render)
{
	renderInspector = render;
}

void InspectorWindow::RenderAdd()
{
	ImGui::Separator();

	if (ImGui::Button(u8"添加"))
		ImGui::OpenPopup("addComp");

	if (ImGui::BeginPopup("addComp", ImGuiWindowFlags_NoMove))
	{
		//if (ImGui::Selectable(u8"网格组件"))
		//{
		//	if (!m_ComponentServices->HasComponent<MeshComponent>(m_ComponentServices->selected))
		//		m_ComponentServices->AddComponent<MeshComponent>(m_ComponentServices->selected);
		//}
		//ImGui::Separator();
		//if (ImGui::Selectable(u8"CameraComponent"))
		//{
		//	if (!ComponentServices->HasComponent<CameraComponent>(ComponentServices->selected))
		//		ComponentServices->AddComponent<CameraComponent>(ComponentServices->selected);
		//}
		//ImGui::Separator();
		if (ImGui::Selectable(u8"刚体组件"))
		{
			//if (!m_ComponentServices->HasComponent<RigidBodyComponent>(m_ComponentServices->selected))
			{
				//m_ComponentServices->AddComponent<RigidBodyComponent>(m_ComponentServices->selected);
				//m_ComponentServices->GetComponent<RigidBodyComponent>(m_ComponentServices->selected).CreateActor(m_ComponentServices, m_physicsSystem);
			}
		}

		if (ImGui::BeginMenu(u8"碰撞体"))
		{
			if (ImGui::MenuItem(u8"盒子"))
			{
				auto tmp = (PhysicsComponent*)m_ComponentServices->FindService(L"PhysicsComponent");
				tmp->AddBoxCollider(m_ComponentServices);
			}

			ImGui::EndMenu();
		}

		ImGui::Separator();

		static std::vector<std::pair<std::wstring, std::wstring>> data;

		if (ImGui::BeginMenu(u8"脚本"))
		{
			if (ImGui::IsWindowAppearing())
				m_assetsWindow->GetFileNameFromProjectDir(EngineUtils::GetProjectDirPath(), FILEs::File_Type::LUAFILE, data);
			for (size_t i = 0; i < data.size(); i++)
				if (ImGui::MenuItem(SString::WstringToUTF8(data[i].second).c_str()))
				{
					auto tmp = (ScriptingComponent*)m_ComponentServices->FindService(L"ScriptingComponent");
					tmp->AddScript(data[i].first.c_str());
				}
			ImGui::EndMenu();
		}
		else
		{
			data.clear();
		}

		/****/
		ImGui::EndPopup();
	}
}

void InspectorWindow::UpdateComponent()
{
	// GeneralComponent
	{
		auto generalComponent = (GeneralComponent*)m_ComponentServices->FindService(L"GeneralComponent");
		_Enabled= generalComponent->IsEnabled();
		if (_Enabled)
		{
			generalComponent->SetEnabled(true);
			_Enabled = false;
		}
		else generalComponent->SetEnabled(false);

		_Static = generalComponent->IsStatic();
		if (_Static)
		{
			generalComponent->SetStatic(true);
			_Static = false;
		}
		else generalComponent->SetStatic(false);
	}

	// 更新相机的Transform信息
	auto cameraComponent = (CameraComponent*)m_ComponentServices->FindService(L"CameraComponent");
	bool _Present = cameraComponent->IsPresent();
	if (_Present)
	{
		auto transformComponent = (TransformComponent*)m_ComponentServices->FindService(L"TransformComponent");
			
		transformComponent->SetPosition3f(m_dx->GetPosition3f());
		transformComponent->SetRotation3f(m_dx->GetRotation3f());
	}
}

void InspectorWindow::RenderComponent()
{
	auto generalComponent = (GeneralComponent*)m_ComponentServices->FindService(L"GeneralComponent");
	bool _Present = false;
	// GeneralComponent
	if (ImGui::CollapsingHeader(u8"一般", ImGuiTreeNodeFlags_DefaultOpen))
	{
		if (ImGui::BeginPopupContextItem())
		{
			if (ImGui::MenuItem(u8"复制")) {}
			if (ImGui::MenuItem(u8"粘贴")) {}
			ImGui::Separator();
			ImGui::MenuItem(u8"移除", "", false, false);
			ImGui::EndPopup();
		}

		if (ImGui::BeginTable(u8"GeneralComponentTable", 2, ImGuiTableFlags_Resizable))
		{
			ImGui::TableNextRow();
			ImGui::TableNextColumn();
			{
				ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 2);
				ImGui::Text(u8"有效");
				ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 2 + 5);
				ImGui::Text(u8"名称");
				ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 2 + 5);
				ImGui::Text(u8"静态");
				ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 2 + 5);
				ImGui::Text(u8"标签");
				ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 2 + 5);
			}
			ImGui::TableNextColumn();
			ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x);
			{
				_Enabled = generalComponent->IsEnabled();
				ImGui::Checkbox("##EnabledGeneralComponent", &_Enabled); // 有效复选框

				std::string tmp = "";

				std::wstring _Name = generalComponent->GetName();
				tmp = SString::WstringToUTF8(_Name);
				if(ImGui::InputText("##NameGeneralComponent", &tmp, ImGuiInputTextFlags_EnterReturnsTrue))
					generalComponent->SetName(SString::UTF8ToWstring(tmp));

				_Static = generalComponent->IsStatic();
				ImGui::Checkbox("##StaticGeneralComponent", &_Static);

				std::wstring _Tag = generalComponent->GetTag();
				tmp = SString::WstringToUTF8(_Tag);
				if (ImGui::InputText("##TagGeneralComponent", &tmp, ImGuiInputTextFlags_EnterReturnsTrue))
					generalComponent->SetTag(SString::UTF8ToWstring(tmp));
			}
			ImGui::PopItemWidth();
			ImGui::EndTable();
			if (generalComponent->GetComponentType() == ComponentType::Co_Camera)
			{
				ImGui::Text(u8"设为当前相机");
				ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 2 + 5);
				ImGui::SameLine();
				auto cameraComponent = (CameraComponent*)m_ComponentServices->FindService(L"CameraComponent");
				_Present = cameraComponent->IsPresent();
				ImGui::Checkbox("##PresentGeneralComponent", &_Present); // 当前复选框
				if (_Present != cameraComponent->IsPresent())
					cameraComponent->SetPresent(_Present);	
			}
		}
	}
	
	// TransformComponent
	if (ImGui::CollapsingHeader(u8"变换", ImGuiTreeNodeFlags_DefaultOpen))
	{
		auto transformComponent = (TransformComponent*)m_ComponentServices->FindService(L"TransformComponent");
		if (ImGui::BeginPopupContextItem())
		{
			if (ImGui::MenuItem(u8"复制")) {}
			if (ImGui::MenuItem(u8"粘贴")) {}
			ImGui::Separator();
			ImGui::MenuItem(u8"移除", "", false, false);
			ImGui::EndPopup();
		}

		/////////////////////////////////////////////////////////////

		if (ImGui::BeginTable(u8"TransformComponentTable", 2, ImGuiTableFlags_Resizable))
		{
			ImGui::TableNextRow();
			ImGui::TableNextColumn();
			{
				ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 2);
				ImGui::Text(u8"位置");
				ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 2 + 5);
				ImGui::Text(u8"旋转");
				ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 2 + 5);
				ImGui::Text(u8"缩放");
			}
			ImGui::TableNextColumn();

			MeshComponent* meshComponent = nullptr;
			bool IsMesh = false;
			if (generalComponent->GetComponentType() == ComponentType::Co_Mesh)
			{
				IsMesh = true;
				meshComponent = (MeshComponent*)m_ComponentServices->FindService(L"MeshComponent");
			}
			// 位置
			{
				DirectX::XMFLOAT3 Position = transformComponent->GetLocalPosition();
				ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x / 3);
				ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.0f, 0.0f, 1.0f));
				ImGui::Text(u8"X");
				ImGui::PopStyleColor(1); 
				ImGui::SameLine();
				ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x / 3 / (2 * 0.875));
				float X = Position.x;
				if (ImGui::DragFloat("##PotXTransformComp", &X, 0.5f, MININT, MAXINT))
				{
					transformComponent->SetPosition(X, Position.y, Position.z);
					if (generalComponent->GetComponentType() == ComponentType::Co_Camera && _Present)
						m_dx->SetPosition3f(DirectX::XMFLOAT3(X, Position.y, Position.z));
					else if (IsMesh)
						meshComponent->UpdateMesh(m_ComponentServices, m_dx, transformComponent->GetLocalTransform(), DirectX::XMFLOAT3(1.0f, 1.0f, 1.0f));
				}

				ImGui::SameLine();
				ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x / 2);
				ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.0f, 1.0f, 0.0f, 1.0f));
				ImGui::Text(u8"Y");
				ImGui::PopStyleColor(1);
				ImGui::SameLine();
				ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x / 2 / (2 * 0.875));
				float Y = Position.y;
				if (ImGui::DragFloat("##PotYTransformComp", &Y, 0.5f, MININT, MAXINT))
				{
					transformComponent->SetPosition(Position.x, Y, Position.z);
					if (generalComponent->GetComponentType() == ComponentType::Co_Camera && _Present)
						m_dx->SetPosition3f(DirectX::XMFLOAT3(Position.x, Y, Position.z));
					else if (IsMesh)
						meshComponent->UpdateMesh(m_ComponentServices, m_dx, transformComponent->GetLocalTransform(), DirectX::XMFLOAT3(1.0f, 1.0f, 1.0f));
				}
				ImGui::SameLine();
				ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x / 1);
				ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.0f, 0.0f, 1.0f, 1.0f));
				ImGui::Text(u8"Z");
				ImGui::PopStyleColor(1);
				ImGui::SameLine();
				ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x / 1 / (2 * 0.875));
				float Z = Position.z;
				if (ImGui::DragFloat("##PotZTransformComp", &Z, 0.5f, MININT, MAXINT))
				{
					transformComponent->SetPosition(Position.x, Position.y, Z);
					if (generalComponent->GetComponentType() == ComponentType::Co_Camera && _Present)
						m_dx->SetPosition3f(DirectX::XMFLOAT3(Position.x, Position.y, Z));
					else if (IsMesh)
						meshComponent->UpdateMesh(m_ComponentServices, m_dx, transformComponent->GetLocalTransform(), DirectX::XMFLOAT3(1.0f, 1.0f, 1.0f));
				}
			}
			// 旋转
			{
				DirectX::XMFLOAT3 Rotation = transformComponent->GetLocalRotation();
				ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x / 3);
				ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.0f, 0.0f, 1.0f));
				ImGui::Text(u8"X");
				ImGui::PopStyleColor(1);
				ImGui::SameLine();
				ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x / 3 / (2 * 0.875));
				float X = Rotation.x;
				if (ImGui::DragFloat("##RotXTransformComp", &X, 0.5f, -180.0f, +180.0f))
				{
					transformComponent->SetRotation(X, Rotation.y, Rotation.z);
					if (generalComponent->GetComponentType() == ComponentType::Co_Camera && _Present)
						m_dx->SetRotation3f(DirectX::XMFLOAT3(X, Rotation.y, Rotation.z));
					else if (IsMesh)
						meshComponent->UpdateMesh(m_ComponentServices, m_dx, transformComponent->GetLocalTransform(), DirectX::XMFLOAT3(1.0f, 1.0f, 1.0f));
				}

				ImGui::SameLine();
				ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x / 2);
				ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.0f, 1.0f, 0.0f, 1.0f));
				ImGui::Text(u8"Y");
				ImGui::PopStyleColor(1);
				ImGui::SameLine();
				ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x / 2 / (2 * 0.875));
				float Y = Rotation.y;
				if (ImGui::DragFloat("##RotYTransformComp", &Y, 0.5f, -180.0f, +180.0f))
				{
					transformComponent->SetRotation(Rotation.x, Y, Rotation.z);
					if (generalComponent->GetComponentType() == ComponentType::Co_Camera && _Present)
						m_dx->SetRotation3f(DirectX::XMFLOAT3(Rotation.x, Y, Rotation.z));
					else if (IsMesh)
						meshComponent->UpdateMesh(m_ComponentServices, m_dx, transformComponent->GetLocalTransform(), DirectX::XMFLOAT3(1.0f, 1.0f, 1.0f));
				}

				ImGui::SameLine();
				ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x / 1);
				ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.0f, 0.0f, 1.0f, 1.0f));
				ImGui::Text(u8"Z");
				ImGui::PopStyleColor(1);
				ImGui::SameLine();
				ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x / 1 / (2 * 0.875));
				float Z = Rotation.z;
				if (ImGui::DragFloat("##RotZTransformComp", &Z, 0.5f, -180.0f, +180.0f))
				{
					transformComponent->SetRotation(Rotation.x, Rotation.y, Z);
					if (generalComponent->GetComponentType() == ComponentType::Co_Camera && _Present)
						m_dx->SetRotation3f(DirectX::XMFLOAT3(Rotation.x, Rotation.y, Z));
					else if (IsMesh)
						meshComponent->UpdateMesh(m_ComponentServices, m_dx, transformComponent->GetLocalTransform(), DirectX::XMFLOAT3(1.0f, 1.0f, 1.0f));
				}
			}
			// 缩放
			{
				DirectX::XMFLOAT3 Scale = transformComponent->GetLocalScale();
				ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x / 3);
				ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.0f, 0.0f, 1.0f));
				ImGui::Text(u8"X");
				ImGui::PopStyleColor(1);
				ImGui::SameLine();
				ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x / 3 / (2 * 0.875));
				float X = Scale.x;
				if (ImGui::DragFloat("##ScaXTransformComp", &X, 0.5f, MININT, MAXINT))
				{
					transformComponent->SetScale(X, Scale.y, Scale.z);
					if (IsMesh)
						meshComponent->UpdateMesh(m_ComponentServices, m_dx, transformComponent->GetLocalTransform(), DirectX::XMFLOAT3(1.0f, 1.0f, 1.0f));
				}
				ImGui::SameLine();
				ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x / 2);
				ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.0f, 1.0f, 0.0f, 1.0f));
				ImGui::Text(u8"Y");
				ImGui::PopStyleColor(1);
				ImGui::SameLine();
				ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x / 2 / (2 * 0.875));
				float Y = Scale.y;
				if (ImGui::DragFloat("##ScaYTransformComp", &Y, 0.5f, MININT, MAXINT))
				{
					transformComponent->SetScale(Scale.x, Y, Scale.z);
				if (IsMesh)
					meshComponent->UpdateMesh(m_ComponentServices, m_dx, transformComponent->GetLocalTransform(), DirectX::XMFLOAT3(1.0f, 1.0f, 1.0f));
				}

				ImGui::SameLine();
				ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x / 1);
				ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.0f, 0.0f, 1.0f, 1.0f));
				ImGui::Text(u8"Z");
				ImGui::PopStyleColor(1);
				ImGui::SameLine();
				ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x / 1 / (2 * 0.875));
				float Z = Scale.z;
				if (ImGui::DragFloat("##ScaZTransformComp", &Z, 0.5f, MININT, MAXINT))
				{
					transformComponent->SetScale(Scale.x, Scale.y, Z);
					if (IsMesh)
						meshComponent->UpdateMesh(m_ComponentServices, m_dx, transformComponent->GetLocalTransform(), DirectX::XMFLOAT3(1.0f, 1.0f, 1.0f));
				}
			}
			ImGui::PopItemWidth();
			ImGui::EndTable();
		}
	}

//	// MeshComponent
//	if (m_ComponentServices->HasComponent<MeshComponent>(m_ComponentServices->selected))
//	{
//		auto& meshComponent = m_ComponentServices->registry.get<MeshComponent>(m_ComponentServices->selected);
//		//for (auto entity : view)
//		{
//			if (ImGui::CollapsingHeader(u8"模型", ImGuiTreeNodeFlags_DefaultOpen))
//			{
//				if (ImGui::BeginPopupContextItem())
//				{
//					if (ImGui::MenuItem(u8"复制")) {}
//					if (ImGui::MenuItem(u8"粘贴")) {}
//					ImGui::Separator();
//					ImGui::MenuItem(u8"移除");
//					ImGui::EndPopup();
//				}
//
//				if (ImGui::BeginTable(u8"MeshComponentTable", 2, ImGuiTableFlags_Resizable))
//				{
//					ImGui::TableNextRow();
//					ImGui::TableNextColumn();
//					{
//						ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 2);
//						ImGui::Text(u8"顶点");
//						ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 2 + 5);
//						ImGui::Text(u8"表面");
//						ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 2 + 5);
//						ImGui::Text(u8"材质");
//					}
//					ImGui::TableNextColumn();
//					{
//						ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 2);
//						ImGui::Text(u8"%u", meshComponent.GetNumVertices());
//
//						ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 2 + 5);
//						ImGui::Text(u8"%u", meshComponent.GetNumFaces());
//
//						ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 2 + 5);
//						std::string tmp = SString::WstringToUTF8(meshComponent.GetMaterialName());
//						std::vector<std::wstring> nameList = m_dx->GetMaterialNameList();
//						if (ImGui::BeginMenu(tmp.c_str()))
//						{
//							for (UINT n = 0; n < nameList.size(); n++)
//							{
//								std::string nameTmp = SString::WstringToUTF8(nameList[n]);
//								if (ImGui::MenuItem(nameTmp.c_str()))
//									meshComponent.SetMaterial(SString::UTF8ToWstring(nameTmp));
//							}
//							ImGui::EndMenu();
//						}
//					}
//					ImGui::EndTable();
//				}
//			}
//		}
//	}
//
//	// CameraComponent
//	if (m_ComponentServices->HasComponent<CameraComponent>(m_ComponentServices->selected))
//	{
//		auto view = m_ComponentServices->registry.view<CameraComponent>();
//		for (auto entity : view)
//		{
//			auto& cameraComponent = m_ComponentServices->registry.get<CameraComponent>(entity);
//
//			if (ImGui::CollapsingHeader(u8"相机", ImGuiTreeNodeFlags_DefaultOpen))
//			{
//				/* [CAMERA COMPONENT SECTION] */
//
//				if (ImGui::BeginPopupContextItem())
//				{
//					if (ImGui::MenuItem(u8"复制")) {}
//					if (ImGui::MenuItem(u8"粘贴")) {}
//					ImGui::Separator();
//					ImGui::MenuItem(u8"移除", "", false, false);
//					ImGui::EndPopup();
//				}
//
//				if (ImGui::BeginTable(u8"CameraComponentTable", 2, ImGuiTableFlags_Resizable))
//				{
//					ImGui::TableNextRow();
//					ImGui::TableNextColumn();
//					{
//						ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 2);
//						ImGui::Text(u8"视角场");
//						ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 2 + 5);
//						ImGui::Text(u8"Near");
//						ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 2 + 5);
//						ImGui::Text(u8"Far");
//						ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 2 + 5);
//						ImGui::Text(u8"比例");
//						ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 2 + 5);
//						ImGui::Text(u8"");
//						ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 2 + 5);
//					}
//					ImGui::TableNextColumn();
//					ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x);
//					{
//						float _Fov = cameraComponent.GetFov()*256.0f;
//						if (ImGui::SliderFloat("##FovCamComp", &_Fov, MIN_FOV, MAX_FOV))
//							cameraComponent.SetFov(_Fov/256.0f);
//						
//						float _Near = cameraComponent.GetNear();
//						if (ImGui::DragFloat("##NearCamComp", &_Near, 0.01f, 0.0f, cameraComponent.GetFar()))
//							cameraComponent.SetNear(_Near);
//
//						float _Far = cameraComponent.GetFar();
//						if (ImGui::DragFloat("##FarCamComp", &_Far, 0.01f, cameraComponent.GetNear(), FLT_MAX))
//							cameraComponent.SetFar(_Far);
//					
//						float _Scale = cameraComponent.GetScale();
//						if (ImGui::DragFloat("##ScaleCamComp", &_Scale, 0.01f, 0.1f, FLT_MAX))
//							cameraComponent.SetScale(_Scale);
//						if(ImGui::Button(u8"还原比例"))
//							cameraComponent.RestoreScale();
//					}
//					ImGui::PopItemWidth();
//					ImGui::EndTable();
//				}
//			}
//		}
//	}
//
//	// RigidBodyComponent
//	if (generalComponent.GetComponentType() != ComponentType::Co_Camera)
//		if (ImGui::CollapsingHeader(u8"刚体", ImGuiTreeNodeFlags_DefaultOpen))
//	{
//		auto view = m_ComponentServices->registry.view<RigidBodyComponent>();
//		for (auto entity : view)
//		{
//			auto& rigidBodyComponent = m_ComponentServices->registry.get<RigidBodyComponent>(entity);
//			if (ImGui::BeginTable(u8"RigidBodyComponentTable", 2, ImGuiTableFlags_Resizable))
//			{
//				ImGui::TableNextRow();
//				ImGui::TableNextColumn();
//				{
//					ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 2);
//					ImGui::Text(u8"质量");
//					ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 2 + 5);
//					ImGui::Text(u8"线性阻尼");
//					ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 2 + 5);
//					ImGui::Text(u8"角度阻尼");
//					ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 2 + 5);
//					ImGui::Text(u8"使用重力");
//					ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 2 + 5);
//					ImGui::Text(u8"是运动学");
//					ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 2 + 5);
//					ImGui::Text(u8"冻结");
//					ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 2 + 5);
//					ImGui::Text(u8"	位置");
//					ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 2 + 5);
//					ImGui::Text(u8"	旋转");
//				}
//				ImGui::TableNextColumn();
//				ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x);
//				{
//					//float mass = rigidBodyComponent.GetMass();
//					//if (ImGui::DragFloat("##MassRigidBodyComponent", &mass, 0.1f, 0.0f, FLT_MAX))
//					//	rigidBodyComponent.SetMass(mass);
//
//					//float linearDamping = rigidBodyComponent.GetLinearDamping();
//					//if (ImGui::DragFloat("##LinearDampingRigidBodyComponent", &linearDamping, 0.1f, 0.0f, FLT_MAX))
//					//	rigidBodyComponent.SetLinearDamping(linearDamping);
//
//					//float angularDamping = rigidBodyComponent.GetAngularDamping();
//					//if (ImGui::DragFloat("##AngularDampingRigidBodyComponent", &angularDamping, 0.1f, 0.0f, FLT_MAX))
//					//	rigidBodyComponent.SetAngularDamping(angularDamping);
//
//					//bool useGravity = rigidBodyComponent.HasUseGravity();
//					//if (ImGui::Checkbox("##UseGravityRigidBodyComponent", &useGravity))
//					//	rigidBodyComponent.UseGravity(useGravity);
//
//					//bool getKinematic = rigidBodyComponent.IsKinematic();
//					//if (ImGui::Checkbox("##IsKinematicRigidBodyComponent", &getKinematic))
//					//	rigidBodyComponent.SetKinematic(getKinematic);
//
//					//ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 2);
//					//ImGui::Text("");
//					//ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 5);
//
//					//bool px = rigidBodyComponent.GetLinearLockX();
//					//if (ImGui::Checkbox("X##PositionRigidBodyComponent", &px))
//					//	rigidBodyComponent.SetLinearLockX(px);
//					//ImGui::SameLine();
//					//bool py = rigidBodyComponent.GetLinearLockY();
//					//if (ImGui::Checkbox("Y##PositionRigidBodyComponent", &py))
//					//	rigidBodyComponent.SetLinearLockY(py);
//					//ImGui::SameLine();
//					//bool pz = rigidBodyComponent.GetLinearLockZ();
//					//if (ImGui::Checkbox("Z##PositionRigidBodyComponent", &pz))
//					//	rigidBodyComponent.SetLinearLockZ(pz);
//
//					//bool rx = rigidBodyComponent.GetAngularLockX();
//					//if (ImGui::Checkbox("X##RotationRigidBodyComponent", &rx))
//					//	rigidBodyComponent.SetAngularLockX(rx);
//					//ImGui::SameLine();
//					//bool ry = rigidBodyComponent.GetAngularLockY();
//					//if (ImGui::Checkbox("Y##RotationRigidBodyComponent", &ry))
//					//	rigidBodyComponent.SetAngularLockY(ry);
//					//ImGui::SameLine();
//					//bool rz = rigidBodyComponent.GetAngularLockZ();
//					//if (ImGui::Checkbox("Z##RotationRigidBodyComponent", &rz))
//					//	rigidBodyComponent.SetAngularLockZ(rz);
//				}
//				ImGui::PopItemWidth();
//				ImGui::EndTable();
//			}
//		}
//	}
//
//	// ScriptingComponent
//	if (m_ComponentServices->HasComponent<ScriptingComponent>(m_ComponentServices->selected))
//	{
//		auto& scriptingComponent = m_ComponentServices->registry.get<ScriptingComponent>(m_ComponentServices->selected);
//		for (size_t i = 0; i < scriptingComponent.scripts.size(); i++)
//		{
//			ImGui::PushID((int)i);
//			{
//				ImGui::Checkbox("##SCRIPT", &scriptingComponent.scripts[i].activeComponent);
//				ImGui::SameLine();
//
//				std::wstring buffer = scriptingComponent.scripts[i].fileNameToUpper + L" (SCRIPT)";
//				if (ImGui::CollapsingHeader(SString::WstringToUTF8(buffer).c_str(), ImGuiTreeNodeFlags_DefaultOpen))
//				{
//					/* code */
//				}
//
//				if (ImGui::BeginPopupContextItem())
//				{
//					if (ImGui::MenuItem("Copy")) {}
//					if (ImGui::MenuItem("Paste")) {}
//					ImGui::Separator();
//					if (ImGui::MenuItem("Remove"))
//					{
//						scriptingComponent.scripts.erase(scriptingComponent.scripts.begin() + i);
//					}
//					ImGui::EndPopup();
//				}
//			}
//			ImGui::PopID();
//		}
//	}
//
//	// PhysicsComponent
//	if (m_ComponentServices->HasComponent<PhysicsComponent>(m_ComponentServices->selected))
//	{
//		auto& physicsComponent = m_ComponentServices->registry.get<PhysicsComponent>(m_ComponentServices->selected);
//		for (size_t box_collidersIndex = 0; box_collidersIndex < physicsComponent.box_colliders.size(); box_collidersIndex++)
//		{
//			ImGui::PushID((int)box_collidersIndex);
//			{
//				ImGui::Checkbox("##BOXCOLLIDER", &physicsComponent.box_colliders[box_collidersIndex].activeComponent);
//				ImGui::SameLine();
//				if (ImGui::CollapsingHeader(u8"盒子碰撞箱", ImGuiTreeNodeFlags_DefaultOpen))
//				{
//					if (ImGui::BeginPopupContextItem())
//					{
//						if (ImGui::MenuItem(u8"复制")) {}
//						if (ImGui::MenuItem(u8"粘贴")) {}
//						ImGui::Separator();
//						if (ImGui::MenuItem(u8"移除"))
//						{
//							//if (m_ComponentServices->registry.any_of<RigidBodyComponent>(m_ComponentServices->selected))
//							//	m_ComponentServices->registry.get<RigidBodyComponent>(m_ComponentServices->selected).GetRigidBody()->detachShape(*physicsComponent.box_colliders[box_collidersIndex].GetShape());
//							physicsComponent.box_colliders.erase(physicsComponent.box_colliders.begin() + box_collidersIndex);
//						}
//						ImGui::EndPopup();
//					}
//
//					if (ImGui::BeginTable(u8"PhysicsComponentTable", 2, ImGuiTableFlags_Resizable))
//					{
//						ImGui::TableNextRow();
//						ImGui::TableNextColumn();
//						{
//							ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 2);
//							ImGui::Text(u8"静态摩擦");
//							ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 2 + 5);
//							ImGui::Text(u8"动态摩擦力");
//							ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 2 + 5);
//							ImGui::Text(u8"还原补偿");
//							ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 2 + 5);
//							ImGui::Text(u8"居中");
//							ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 2 + 5);
//							ImGui::Text(u8"大小");
//						}
//						ImGui::TableNextColumn();
//						ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x);
//						{
///*							float _StaticFriction = physicsComponent.box_colliders[box_collidersIndex].GetStaticFriction();
//							if (ImGui::DragFloat("##StaticFrictionPhysicsComponent", &_StaticFriction, 0.1f, 0.0f, FLT_MAX))
//								physicsComponent.box_colliders[box_collidersIndex].SetStaticFriction(_StaticFriction);
//
//							float _DynamicFriction = physicsComponent.box_colliders[box_collidersIndex].GetDynamicFriction();
//							if (ImGui::DragFloat("##DynamicFrictionPhysicsComponent", &_DynamicFriction, 0.1f, 0.0f, FLT_MAX))
//								physicsComponent.box_colliders[box_collidersIndex].SetDynamicFriction(_DynamicFriction);
//
//
//							float _Restitution = physicsComponent.box_colliders[box_collidersIndex].GetRestitution();
//							if (ImGui::DragFloat("##RestitutionPhysicsComponent", &_Restitution, 0.1f, 0.0f, FLT_MAX))
//								physicsComponent.box_colliders[box_collidersIndex].SetRestitution(_Restitution);
//
//							DirectX::XMFLOAT3 _Center = physicsComponent.box_colliders[box_collidersIndex].GetCenter();
//							if (ImGui::DragFloat3("##CenterPhysicsComponent", (float*)&_Center, 0.1f))
//								physicsComponent.box_colliders[box_collidersIndex].SetCenter(_Center);
//
//							DirectX::XMFLOAT3 _Size = physicsComponent.box_colliders[box_collidersIndex].size;
//							if (ImGui::DragFloat3("##Size", (float*)&_Size, 0.1f))
//								physicsComponent.box_colliders[box_collidersIndex].size = _Size;
//						*/}
//						ImGui::PopItemWidth();
//						ImGui::EndTable();
//					}
//				}
//			}
//			ImGui::PopID();
//		}
//	}
}
