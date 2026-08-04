#include "AssetsWindow.h"
#include "ConsoleWindow.h"
#include "../Editor.h"
#include "../EditorAssetCache.h"

#include "String/SStringUtils.h"
#include "ENGINE/EngineUtils.h"
#include "System/WitchcraftFile/WMaterialFile.h"

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
	strncpy_s(m_createMaterialFileStem, "NewMaterial", _TRUNCATE);
	strncpy_s(m_createMaterialDisplayName, "NewMaterial", _TRUNCATE);

	// 读取资源窗口图标

	ResourceUploadBatch resourceUpload(dx->GetDevice());
	resourceUpload.Begin();

	folderTexture.Create(
		m_dx->GetDevice(), GUISrvDescriptorHeap,
		&resourceUpload, L"folderTexture", FOLDER_ICON_PATH,
		TextureType::PNG, SrvDescriptorHeapIndex);
	SrvDescriptorHeapIndex++;

	fileTexture.Create(
		m_dx->GetDevice(), GUISrvDescriptorHeap,
		&resourceUpload, L"fileTexture", FILE_ICON_PATH,
		TextureType::PNG, SrvDescriptorHeapIndex);
	SrvDescriptorHeapIndex++;

	ttfTexture.Create(
		m_dx->GetDevice(), GUISrvDescriptorHeap,
		&resourceUpload, L"ttfTexture", FONT_ICON_PATH,
		TextureType::PNG, SrvDescriptorHeapIndex);
	SrvDescriptorHeapIndex++;

	imageTexture.Create(
		m_dx->GetDevice(), GUISrvDescriptorHeap,
		&resourceUpload, L"imageTexture", IMAGE_ICON_PATH,
		TextureType::PNG, SrvDescriptorHeapIndex);
	SrvDescriptorHeapIndex++;

	materialTexture.Create(
		m_dx->GetDevice(), GUISrvDescriptorHeap,
		&resourceUpload, L"materialTexture", MATERIAL_ICON_PATH,
		TextureType::PNG, SrvDescriptorHeapIndex);
	SrvDescriptorHeapIndex++;

	modelTexture.Create(
		m_dx->GetDevice(), GUISrvDescriptorHeap,
		&resourceUpload, L"modelTexture", MODEL_ICON_PATH,
		TextureType::PNG, SrvDescriptorHeapIndex);
	SrvDescriptorHeapIndex++;

	skyTexture.Create(
		m_dx->GetDevice(), GUISrvDescriptorHeap,
		&resourceUpload, L"skyTexture", SKY_ICON_PATH,
		TextureType::PNG, SrvDescriptorHeapIndex);
	SrvDescriptorHeapIndex++;

	audioTexture.Create(
		m_dx->GetDevice(), GUISrvDescriptorHeap,
		&resourceUpload, L"audioTexture", AUDIO_ICON_PATH,
		TextureType::PNG, SrvDescriptorHeapIndex);
	SrvDescriptorHeapIndex++;

	luaTexture.Create(
		m_dx->GetDevice(), GUISrvDescriptorHeap,
		&resourceUpload, L"luaTexture", LUA_ICON_PATH,
		TextureType::PNG, SrvDescriptorHeapIndex);
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

	ImGui::Begin("资源");
	RenderHeaderBar();
	RenderDirectoryPane();
	RenderCreateMaterialPopup();
	RenderRemoveConfirmPopup();
	RenderRenamePopup();
	ProcessPendingOpenDir();
	ImGui::End();
}

void AssetsWindow::RenderHeaderBar()
{
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.0f, 4.0f));
	ImGui::PushItemWidth(128.0f);
	ImGui::InputTextWithHint("##AssetsSearch", "精确搜索...", m_searchText, IM_ARRAYSIZE(m_searchText));
	ImGui::PopItemWidth();
	ImGui::SameLine();

	const bool canGoBack = !m_currentDirPath.empty() && m_currentDirPath != EngineUtils::GetProjectDirPath();
	ImGui::BeginDisabled(!canGoBack);
	if (ImGui::ArrowButton("##AssetsGoBack", ImGuiDir_Left) && canGoBack)
		GoBackDir();
	ImGui::EndDisabled();
	ImGui::SameLine();
	ImGui::BeginChild("##AssetsBreadcrumbBar", ImVec2(0.0f, ImGui::GetFrameHeight() + 6.0f), ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar);
	RenderPathBreadcrumbs();
	ImGui::EndChild();

	ImGui::PopStyleVar();
	ImGui::Separator();
}

void AssetsWindow::RenderPathBreadcrumbs()
{
	const std::wstring currentPath = GetCurrentDirectoryPath();
	const std::wstring relativePath = GetDirectoryPathRelativeToProject(currentPath);
	const std::wstring projectRoot = EngineUtils::GetProjectDirPath();

	std::vector<std::wstring> segments;
	std::filesystem::path runningPath(projectRoot);
	segments.push_back(L"Assets");

	if (!relativePath.empty())
	{
		std::filesystem::path relative(relativePath);
		for (const auto& part : relative)
		{
			const std::wstring segment = part.wstring();
			if (segment.empty())
				continue;

			runningPath /= part;
			segments.push_back(segment);
		}
	}

	runningPath = std::filesystem::path(projectRoot);
	for (size_t i = 0; i < segments.size(); ++i)
	{
		if (i == 0)
		{
			if (ImGui::SmallButton("Assets"))
				QueueOpenDir(projectRoot);
		}
		else
		{
			runningPath /= segments[i];
			ImGui::SameLine(0.0f, 4.0f);
			ImGui::TextUnformatted(">");
			ImGui::SameLine(0.0f, 4.0f);

			const std::string segmentUtf8 = SString::WstringToUTF8(segments[i]);
			ImGui::PushID(static_cast<int>(i));
			if (ImGui::SmallButton(segmentUtf8.c_str()))
				QueueOpenDir(runningPath.wstring());
			ImGui::PopID();
		}
	}
}

void AssetsWindow::RenderDirectoryPane()
{
	ImGui::BeginChild("table111", ImVec2(0, 0), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
	if (ImGui::BeginTable("table1", 2, ImGuiTableFlags_Resizable))
	{
		ImGui::TableNextRow();
		ImGui::TableSetColumnIndex(0);
		ImGui::BeginChild("AAPPOO");
		RenderDirList(dirs);
		ImGui::EndChild();

		ImGui::TableSetColumnIndex(1);
		RenderFilePane();
		ImGui::EndTable();
	}
	ImGui::EndChild();
}

void AssetsWindow::RenderFilteredFileItems(size_t* visibleFileCount)
{
	if (visibleFileCount != nullptr)
		*visibleFileCount = 0;

	float space = size;
	for (size_t i = 0; i < files.size(); ++i)
	{
		if (!PassesSearchFilter(files[i]))
			continue;

		if (visibleFileCount != nullptr)
			++(*visibleFileCount);

		RenderFileItem(i, &space);
	}
}

void AssetsWindow::RenderFileItem(size_t fileIndex, float* space)
{
	FILEs& file = files[fileIndex];
	const std::wstring fileNameOnly = file.file_name_only;
	ImGui::BeginGroup();
	ImGui::PushID(static_cast<int>(fileIndex));

	if (file.is_selected)
		ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyle().Colors[ImGuiCol_ButtonHovered]);

	RenderFileIconButton(file, true);

	if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None))
	{
		AssetDragPayload payload = {};
		payload.file_type = file.file_type;
		payload.is_dir = file.is_dir;
		std::wstring fullPath = file.file_path + L"\\" + file.file_name;
		wcsncpy_s(payload.full_path, fullPath.c_str(), _TRUNCATE);
		wcsncpy_s(payload.file_name_only, file.file_name_only.c_str(), _TRUNCATE);
		ImGui::SetDragDropPayload("DND_DEMO_ASS", &payload, sizeof(AssetDragPayload));
		RenderFileDragPreview(file);
		ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + size);
		ImGui::TextWrapped(SString::WstringToUTF8(fileNameOnly).c_str());
		ImGui::PopTextWrapPos();
		ImGui::EndDragDropSource();
	}

	if (file.is_selected)
		ImGui::PopStyleColor(1);

	const bool openedDir = HandleFileItemInteraction(file);

	ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + size);
	ImGui::TextWrapped(SString::WstringToUTF8(fileNameOnly).c_str());
	ImGui::PopTextWrapPos();

	ImGui::PopID();
	ImGui::EndGroup();

	if (!openedDir && space != nullptr)
	{
		*space += ImGui::GetItemRectSize().x;
		if (*space < ImGui::GetWindowSize().x)
			ImGui::SameLine();
		else
			*space = size;
	}
}

void AssetsWindow::RenderFilePane()
{
	ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2.0f, 2.0f));
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));
	ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);

	if (!files.empty())
	{
		ImGui::BeginChild("资源");
		size_t visibleFileCount = 0;
		RenderFilteredFileItems(&visibleFileCount);
		if (!m_pendingSelectAssetPath.empty())
			SelectAssetByPath(m_pendingSelectAssetPath);

		if (ImGui::IsWindowHovered() && !ImGui::IsItemHovered() && ImGui::IsMouseDown(ImGuiMouseButton_Left))
			HideSelected();

		RenderContextMenu("AssetsContextMenu");

		if (visibleFileCount == 0)
		{
			ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.5f, 1.0f));
			ImGui::Text("当前目录下没有匹配的资源。");
			ImGui::PopStyleColor();
		}
		ImGui::EndChild();
	}

	ImGui::PopStyleVar(4);

	if (files.empty())
	{
		ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.5f, 1.0f));
		ImGui::Text("此文件夹为空。");
		ImGui::PopStyleColor(1);
		RenderContextMenu("AssetsContextMenuEmpty");
	}
}
void AssetsWindow::RenderFileIconButton(const FILEs& file, bool imageButton)
{
	auto drawTexture = [&](Texture& texture)
		{
			if (imageButton)
				ImGui::ImageButton("IconButton", (ImTextureID)texture.GetGPUTexDescriptor().ptr, ImVec2(size, size));
			else
				ImGui::Image((ImTextureID)texture.GetGPUTexDescriptor().ptr, ImVec2(size, size));
		};

	if (file.is_dir)
		return drawTexture(folderTexture);
	if (file.file_type == FILEs::File_Type::PNGFILE || file.file_type == FILEs::File_Type::DDSFILE)
		return drawTexture(imageTexture);
	if (file.file_type == FILEs::File_Type::LUAFILE)
		return drawTexture(luaTexture);
	if (file.file_type == FILEs::File_Type::OBJFILE
		|| file.file_type == FILEs::File_Type::GLTFFILE
		|| file.file_type == FILEs::File_Type::GLBFILE
		|| file.file_type == FILEs::File_Type::WMODELFILE
		|| file.file_type == FILEs::File_Type::WSKELETONFILE
		|| file.file_type == FILEs::File_Type::WANIMFILE
		|| file.file_type == FILEs::File_Type::WSKINFILE)
		return drawTexture(modelTexture);
	if (file.file_type == FILEs::File_Type::TTFFILE)
		return drawTexture(ttfTexture);
	if (file.file_type == FILEs::File_Type::WAVFILE)
		return drawTexture(audioTexture);
	if (file.file_type == FILEs::File_Type::SKYFILE)
		return drawTexture(skyTexture);
	if (file.file_type == FILEs::File_Type::MATFILE)
		return drawTexture(materialTexture);

	drawTexture(fileTexture);
}

void AssetsWindow::RenderFileDragPreview(const FILEs& file)
{
	RenderFileIconButton(file, false);
}

bool AssetsWindow::HandleFileItemInteraction(FILEs& file)
{
	if (!ImGui::IsItemHovered())
		return false;

	if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
	{
		HideSelected();
		file.is_selected = true;
		selectedFile = &file;

		const std::wstring fullPath = file.file_path + L"\\" + file.file_name;
		if (file.is_dir)
		{
			QueueOpenDir(fullPath);
			return true;
		}
		if (file.file_type == FILEs::File_Type::MATFILE && m_editor != nullptr)
			m_editor->OpenMaterialEditor(fullPath);
		else if (file.file_type == FILEs::File_Type::WANIMFILE && m_editor != nullptr)
			m_editor->OpenAnimationEditor(fullPath);
	}
	else if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) || ImGui::IsMouseClicked(ImGuiMouseButton_Right))
	{
		HideSelected();
		file.is_selected = true;
		selectedFile = &file;
	}

	return false;
}

void AssetsWindow::OpenDir(std::wstring path)
{
	if (path.empty() || !std::filesystem::exists(path))
		return;

	selectedFile = nullptr;
	files.clear();
	m_currentDirPath = path;
	dirs.dir_name = EngineUtils::GetProjectDirPath();
	dirs.dir_child.clear();
	FillDirList(&dirs);

	for (const auto& index : std::filesystem::directory_iterator(path))
	{
		std::filesystem::path file(index.path());
		FILEs::File_Type file_type = FILEs::extensionToFileType(file.extension().wstring());
		const bool isDirectory = index.is_directory();
		files.push_back(FILEs(
			file.parent_path().wstring(),
			file.filename().wstring(),
			file.stem().wstring(),
			file_type,
			isDirectory,
			isDirectory ? 0 : static_cast<int>(index.file_size())));
	}

	std::sort(files.begin(), files.end(), [](const FILEs& lhs, const FILEs& rhs)
		{
			if (lhs.is_dir != rhs.is_dir)
				return lhs.is_dir && !rhs.is_dir;

			const std::wstring lhsName = ToLowerCopy(lhs.file_name);
			const std::wstring rhsName = ToLowerCopy(rhs.file_name);
			if (lhsName != rhsName)
				return lhsName < rhsName;

			return lhs.file_name < rhs.file_name;
		});
}

void AssetsWindow::QueueOpenDir(const std::wstring& path)
{
	if (!path.empty())
		m_pendingOpenDirPath = path;
}

void AssetsWindow::ProcessPendingOpenDir()
{
	if (m_pendingOpenDirPath.empty())
		return;

	std::wstring path = std::move(m_pendingOpenDirPath);
	m_pendingOpenDirPath.clear();
	OpenDir(std::move(path));
}

void AssetsWindow::GoBackDir()
{
	if (m_currentDirPath.empty() || m_currentDirPath == EngineUtils::GetProjectDirPath())
		return;

	std::filesystem::path file(m_currentDirPath);
	const std::filesystem::path parentPath = file.parent_path();
	if (!parentPath.empty())
		OpenDir(parentPath.wstring());
}

void AssetsWindow::RefreshDir()
{
	OpenDir(m_currentDirPath.empty() ? EngineUtils::GetProjectDirPath() : m_currentDirPath);
	if (!m_pendingSelectAssetPath.empty())
		SelectAssetByPath(m_pendingSelectAssetPath);
}

bool AssetsWindow::PassesSearchFilter(const FILEs& file) const
{
	const std::wstring searchText = ToLowerCopy(SString::UTF8ToWstring(m_searchText));
	if (searchText.empty())
		return true;

	const std::wstring fileNameOnly = ToLowerCopy(file.file_name_only);
	if (fileNameOnly == searchText)
		return true;

	const std::wstring fullFileName = ToLowerCopy(file.file_name);
	return fullFileName == searchText;
}

std::wstring AssetsWindow::GetDirectoryPathRelativeToProject(const std::wstring& fullPath) const
{
	const std::wstring projectRoot = EngineUtils::GetProjectDirPath();
	if (fullPath.empty() || fullPath == projectRoot)
		return L"";

	const std::filesystem::path full(fullPath);
	const std::filesystem::path root(projectRoot);
	std::error_code ec;
	const std::filesystem::path relative = std::filesystem::relative(full, root, ec);
	if (ec)
		return fullPath;

	return relative.wstring();
}

const std::wstring AssetsWindow::ToLowerCopy(std::wstring text)
{
	std::transform(text.begin(), text.end(), text.begin(), towlower);
	return text;
}

void AssetsWindow::NeedRender(bool render)
{
	renderAssets = render;
}

void AssetsWindow::CreateDir(std::wstring path)
{
	assert(!path.empty());
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

void AssetsWindow::RenderDirList(const dir_list& dir)
{
	const bool hasChildren = !dir.dir_child.empty();
	ImGuiTreeNodeFlags tree_flags = ImGuiTreeNodeFlags_OpenOnDoubleClick
		| ImGuiTreeNodeFlags_SpanAvailWidth
		| ImGuiTreeNodeFlags_OpenOnArrow
		| ImGuiTreeNodeFlags_DefaultOpen;

	if (!hasChildren)
		tree_flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;

	if (!dir.dir_name.compare(m_currentDirPath))
		tree_flags |= ImGuiTreeNodeFlags_Selected;

	size_t pos = dir.dir_name.find(FOLDER);
	std::wstring str = (pos == std::wstring::npos) ? dir.dir_name : dir.dir_name.substr(pos + 1);
	const std::string dirPathUtf8 = SString::WstringToUTF8(dir.dir_name);
	const std::string dirLabelUtf8 = SString::WstringToUTF8(str);

	ImGui::PushID(dirPathUtf8.c_str());
	bool node_open = ImGui::TreeNodeEx("##DirNode", tree_flags, "%s", dirLabelUtf8.c_str());

	if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
		QueueOpenDir(dir.dir_name);

	if (node_open && hasChildren)
	{
		for (size_t i = 0; i < dir.dir_child.size(); i++)
			RenderDirList(dir.dir_child[i]);
		ImGui::TreePop();
	}
	ImGui::PopID();
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

	std::sort(dir->dir_child.begin(), dir->dir_child.end(),
		[](const dir_list& lhs, const dir_list& rhs)
		{
			const std::wstring lhsName = ToLowerCopy(std::filesystem::path(lhs.dir_name).filename().wstring());
			const std::wstring rhsName = ToLowerCopy(std::filesystem::path(rhs.dir_name).filename().wstring());
			if (lhsName != rhsName)
				return lhsName < rhsName;

			return lhs.dir_name < rhs.dir_name;
		});

	for (size_t i = 0; i < dir->dir_child.size(); i++)
		FillDirList(&dir->dir_child[i]);
}

void AssetsWindow::RenderContextMenu(const char* popupId)
{
	if (!ImGui::BeginPopupContextWindow(popupId, ImGuiPopupFlags_MouseButtonRight))
		return;

	if (ImGui::BeginMenu("创建"))
	{
		if (ImGui::MenuItem("文件夹"))
			CreateFolderInCurrentDirectory();

		ImGui::Separator();

		if (ImGui::MenuItem("Lua 脚本"))
			CreateLuaScriptInCurrentDirectory();

		if (ImGui::MenuItem("材质"))
			RequestCreateMaterialDialog();

		ImGui::EndMenu();
	}

	ImGui::Separator();

	if (ImGui::MenuItem("刷新当前目录"))
		RefreshDir();

	ImGui::Separator();

	const bool canRemove = selectedFile != nullptr;
	const bool canRename = selectedFile != nullptr;
	if (ImGui::MenuItem("重命名", "", false, canRename))
		RequestRenameSelectedAsset();

	ImGui::Separator();

	if (ImGui::MenuItem("移除", "", false, canRemove))
	{
		RequestRemoveSelectedAsset();
	}

	ImGui::EndPopup();
}

void AssetsWindow::RequestCreateMaterialDialog()
{
	const std::wstring basePath = GetCurrentDirectoryPath() + L"\\" + L"NewMaterial";
	const UINT safeIndex = GetSafeName(basePath, FILEs::File_Type::MATFILE);
	const std::wstring defaultStem = L"NewMaterial" + std::to_wstring(safeIndex);
	const std::string defaultStemUtf8 = SString::WstringToUTF8(defaultStem);
	strncpy_s(m_createMaterialFileStem, defaultStemUtf8.c_str(), _TRUNCATE);
	strncpy_s(m_createMaterialDisplayName, defaultStemUtf8.c_str(), _TRUNCATE);
	m_createMaterialDiffuseColor = { 1.0f, 1.0f, 1.0f, 1.0f };
	m_createMaterialEmissive = { 0.0f, 0.0f, 0.0f };
	m_createMaterialUseNormalTexture = false;
	m_createMaterialUseMetallicTexture = false;
	m_createMaterialUseRoughnessTexture = false;
	m_createMaterialUseOpacityTexture = false;
	m_createMaterialMetallic = 0.0f;
	m_createMaterialRoughness = 1.0f;
	m_createMaterialOpacity = 1.0f;
	m_createMaterialDisplayNameEditedManually = false;
	m_createMaterialErrorMessage.clear();
	m_openCreateMaterialPopup = true;
}

std::wstring AssetsWindow::SanitizeFileStem(const std::wstring& value) const
{
	std::wstring result;
	result.reserve(value.size());

	for (wchar_t ch : value)
	{
		switch (ch)
		{
		case L'\\':
		case L'/':
		case L':':
		case L'*':
		case L'?':
		case L'"':
		case L'<':
		case L'>':
		case L'|':
			result.push_back(L'_');
			break;
		default:
			result.push_back(ch);
			break;
		}
	}

	while (!result.empty() && iswspace(result.back()))
		result.pop_back();

	size_t firstNonSpace = 0;
	while (firstNonSpace < result.size() && iswspace(result[firstNonSpace]))
		++firstNonSpace;
	if (firstNonSpace > 0)
		result.erase(0, firstNonSpace);

	return result;
}

void AssetsWindow::RenderCreateMaterialPopup()
{
	if (m_openCreateMaterialPopup)
	{
		ImGui::OpenPopup("新建材质");
		m_openCreateMaterialPopup = false;
	}

	constexpr ImGuiWindowFlags popupFlags =
		ImGuiWindowFlags_AlwaysAutoResize |
		ImGuiWindowFlags_NoSavedSettings;

	if (!ImGui::BeginPopupModal("新建材质", nullptr, popupFlags))
		return;

	ImGui::TextDisabled("将在当前目录创建 .wmat 材质文件。");
	ImGui::Separator();

	const bool fileStemChanged = ImGui::InputText("文件名（不含扩展名）", m_createMaterialFileStem, IM_ARRAYSIZE(m_createMaterialFileStem));
	const bool displayNameChanged = ImGui::InputText("材质名称", m_createMaterialDisplayName, IM_ARRAYSIZE(m_createMaterialDisplayName));
	if (displayNameChanged)
		m_createMaterialDisplayNameEditedManually = true;
	else if (fileStemChanged && !m_createMaterialDisplayNameEditedManually)
		strncpy_s(m_createMaterialDisplayName, m_createMaterialFileStem, _TRUNCATE);

	ImGui::ColorEdit4("漫反射颜色", &m_createMaterialDiffuseColor.x);
	ImGui::ColorEdit3("自发光", &m_createMaterialEmissive.x);
	ImGui::Checkbox("使用法线贴图", &m_createMaterialUseNormalTexture);
	ImGui::Checkbox("使用金属度贴图", &m_createMaterialUseMetallicTexture);
	ImGui::Checkbox("使用粗糙度贴图", &m_createMaterialUseRoughnessTexture);
	ImGui::Checkbox("使用透明贴图", &m_createMaterialUseOpacityTexture);
	ImGui::SliderFloat("金属度", &m_createMaterialMetallic, 0.0f, 1.0f);
	ImGui::SliderFloat("粗糙度", &m_createMaterialRoughness, 0.0f, 1.0f);
	ImGui::SliderFloat("不透明度", &m_createMaterialOpacity, 0.0f, 1.0f);

	if (!m_createMaterialErrorMessage.empty())
		ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", SString::WstringToUTF8(m_createMaterialErrorMessage).c_str());

	if (ImGui::Button("创建"))
	{
		const std::wstring fileStem = SanitizeFileStem(SString::UTF8ToWstring(m_createMaterialFileStem));
		const std::wstring materialName = SString::UTF8ToWstring(m_createMaterialDisplayName);
		if (fileStem.empty())
		{
			m_createMaterialErrorMessage = L"文件名不能为空。";
		}
		else if (materialName.empty())
		{
			m_createMaterialErrorMessage = L"材质名称不能为空。";
		}
		else
		{
			const std::filesystem::path outputPath =
				std::filesystem::path(GetCurrentDirectoryPath()) / (fileStem + WMaterialFile::Extension);
			if (std::filesystem::exists(outputPath))
			{
				m_createMaterialErrorMessage = L"同名 .wmat 已存在，请修改文件名。";
			}
			else
			{
				WMaterialFileData materialData;
				materialData.MaterialName = materialName;
				materialData.DiffuseColor = m_createMaterialDiffuseColor;
				materialData.Emissive = m_createMaterialEmissive;
				materialData.UseNormalTexture = m_createMaterialUseNormalTexture;
				materialData.UseMetallicTexture = m_createMaterialUseMetallicTexture;
				materialData.UseRoughnessTexture = m_createMaterialUseRoughnessTexture;
				materialData.UseOpacityTexture = m_createMaterialUseOpacityTexture;
				materialData.Metallic = m_createMaterialMetallic;
				materialData.Roughness = m_createMaterialRoughness;
				materialData.Opacity = m_createMaterialOpacity;

				if (WMaterialFile::SaveToFile(outputPath, materialData))
				{
					EditorAssetCache::MarkMaterialFilesDirty();
					QueueSelectAsset(outputPath.wstring());
					RefreshDir();
					if (m_editor != nullptr)
						m_editor->OpenMaterialEditor(outputPath.wstring());

					m_createMaterialErrorMessage.clear();
					ImGui::CloseCurrentPopup();
					ImGui::EndPopup();
					return;
				}

				m_createMaterialErrorMessage = L"保存 .wmat 失败。";
			}
		}
	}

	ImGui::SameLine();
	if (ImGui::Button("取消"))
	{
		m_createMaterialErrorMessage.clear();
		ImGui::CloseCurrentPopup();
		ImGui::EndPopup();
		return;
	}

	ImGui::EndPopup();
}

void AssetsWindow::RenderRemoveConfirmPopup()
{
	if (m_openRemoveConfirmPopup)
	{
		ImGui::OpenPopup("确认移除资源");
		m_openRemoveConfirmPopup = false;
	}

	constexpr ImGuiWindowFlags popupFlags =
		ImGuiWindowFlags_AlwaysAutoResize |
		ImGuiWindowFlags_NoSavedSettings;

	if (!ImGui::BeginPopupModal("确认移除资源", nullptr, popupFlags))
		return;

	const std::string displayNameUtf8 = SString::WstringToUTF8(m_pendingRemoveAssetDisplayName);
	ImGui::TextWrapped("确定要移除资源吗？");
	if (!displayNameUtf8.empty())
		ImGui::TextWrapped("目标：%s", displayNameUtf8.c_str());

	ImGui::Separator();
	ImGui::TextDisabled("默认会移动到回收站，不会直接彻底删除。");

	if (ImGui::Button("取消"))
	{
		m_pendingRemoveAssetPath.clear();
		m_pendingRemoveAssetDisplayName.clear();
		ImGui::CloseCurrentPopup();
		ImGui::EndPopup();
		return;
	}

	ImGui::SameLine();
	if (ImGui::Button("移除到回收站"))
	{
		if (!m_pendingRemoveAssetPath.empty())
		{
			RemoveAsset(m_pendingRemoveAssetPath);
			EditorAssetCache::MarkMaterialFilesDirty();
			RefreshDir();
		}

		m_pendingRemoveAssetPath.clear();
		m_pendingRemoveAssetDisplayName.clear();
		ImGui::CloseCurrentPopup();
		ImGui::EndPopup();
		return;
	}

	ImGui::EndPopup();
}

void AssetsWindow::RenderRenamePopup()
{
	if (m_openRenamePopup)
	{
		ImGui::OpenPopup("重命名资源");
		m_openRenamePopup = false;
	}

	constexpr ImGuiWindowFlags popupFlags =
		ImGuiWindowFlags_AlwaysAutoResize |
		ImGuiWindowFlags_NoSavedSettings;

	if (!ImGui::BeginPopupModal("重命名资源", nullptr, popupFlags))
		return;

	ImGui::TextDisabled("第一阶段仅重命名当前资源文件，不自动修复引用。");
	if (!m_pendingRenameAssetDisplayName.empty())
		ImGui::TextWrapped("当前：%s", SString::WstringToUTF8(m_pendingRenameAssetDisplayName).c_str());

	ImGui::Separator();
	ImGui::InputText("新名称（不含扩展名）", m_renameAssetStem, IM_ARRAYSIZE(m_renameAssetStem));

	if (!m_renameAssetErrorMessage.empty())
		ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", SString::WstringToUTF8(m_renameAssetErrorMessage).c_str());

	if (ImGui::Button("取消"))
	{
		m_pendingRenameAssetPath.clear();
		m_pendingRenameAssetDisplayName.clear();
		m_renameAssetErrorMessage.clear();
		m_renameAssetStem[0] = '\0';
		ImGui::CloseCurrentPopup();
		ImGui::EndPopup();
		return;
	}

	ImGui::SameLine();
	if (ImGui::Button("重命名"))
	{
		const std::filesystem::path sourcePath(m_pendingRenameAssetPath);
		const std::wstring newStem = SanitizeFileStem(SString::UTF8ToWstring(m_renameAssetStem));
		if (newStem.empty())
		{
			m_renameAssetErrorMessage = L"名称不能为空。";
		}
		else if (sourcePath.empty() || !std::filesystem::exists(sourcePath))
		{
			m_renameAssetErrorMessage = L"目标资源不存在。";
		}
		else
		{
			const std::filesystem::path targetPath = sourcePath.parent_path() / (newStem + sourcePath.extension().wstring());
			if (targetPath == sourcePath)
			{
				m_renameAssetErrorMessage.clear();
				ImGui::CloseCurrentPopup();
				ImGui::EndPopup();
				return;
			}
			if (std::filesystem::exists(targetPath))
			{
				m_renameAssetErrorMessage = L"同名资源已存在。";
			}
			else
			{
				std::error_code ec;
				std::filesystem::rename(sourcePath, targetPath, ec);
				if (ec)
				{
					m_renameAssetErrorMessage = L"重命名失败。";
				}
				else
				{
					EditorAssetCache::MarkMaterialFilesDirty();
					EditorAssetCache::MarkSkyTexturesDirty();
					QueueSelectAsset(targetPath.wstring());
					RefreshDir();
					m_pendingRenameAssetPath.clear();
					m_pendingRenameAssetDisplayName.clear();
					m_renameAssetErrorMessage.clear();
					m_renameAssetStem[0] = '\0';
					ImGui::CloseCurrentPopup();
					ImGui::EndPopup();
					return;
				}
			}
		}
	}

	ImGui::EndPopup();
}

std::wstring AssetsWindow::GetCurrentDirectoryPath() const
{
	return m_currentDirPath.empty() ? EngineUtils::GetProjectDirPath() : m_currentDirPath;
}

void AssetsWindow::CreateFolderInCurrentDirectory()
{
	const std::wstring basePath = GetCurrentDirectoryPath() + L"\\" + L"Folder";
	const UINT safeIndex = GetSafeName(basePath);
	const std::wstring targetPath = basePath + std::to_wstring(safeIndex);
	CreateDir(targetPath);
	QueueSelectAsset(targetPath);
	RefreshDir();
}

void AssetsWindow::CreateLuaScriptInCurrentDirectory()
{
	const std::wstring basePath = GetCurrentDirectoryPath() + L"\\" + L"LuaScript";
	const UINT safeIndex = GetSafeName(basePath, FILEs::File_Type::LUAFILE);
	const std::wstring scriptPath = basePath + std::to_wstring(safeIndex) + L".lua";
	const std::wstring tableName = L"LuaScript" + std::to_wstring(safeIndex);

	if (m_editor != nullptr)
		m_editor->CreateLuaScriptAsset(scriptPath, tableName);

	QueueSelectAsset(scriptPath);
	RefreshDir();
}

void AssetsWindow::QueueRemoveAsset(const std::wstring& fullPath, const std::wstring& displayName)
{
	if (fullPath.empty())
		return;

	m_pendingRemoveAssetPath = fullPath;
	m_pendingRemoveAssetDisplayName = displayName;
	m_openRemoveConfirmPopup = true;
}

void AssetsWindow::RequestRemoveSelectedAsset()
{
	if (selectedFile == nullptr)
		return;

	const std::wstring removePath = selectedFile->file_path + L"\\" + selectedFile->file_name;
	QueueRemoveAsset(removePath, selectedFile->file_name);
}

void AssetsWindow::RequestRenameSelectedAsset()
{
	if (selectedFile == nullptr)
		return;

	m_pendingRenameAssetPath = selectedFile->file_path + L"\\" + selectedFile->file_name;
	m_pendingRenameAssetDisplayName = selectedFile->file_name;
	m_renameAssetErrorMessage.clear();
	const std::string stemUtf8 = SString::WstringToUTF8(selectedFile->file_name_only);
	strncpy_s(m_renameAssetStem, stemUtf8.c_str(), _TRUNCATE);
	m_openRenamePopup = true;
}

void AssetsWindow::RemoveAsset(std::wstring path)
{
	std::wstring fix = path + std::wstring(1, '\0');
	std::wstring buff = std::wstring(fix.begin(), fix.end());
	/********/
	SHFILEOPSTRUCT sh;
	ZeroMemory(&sh, sizeof(SHFILEOPSTRUCT));
	sh.wFunc = FO_DELETE;
	sh.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_WANTNUKEWARNING;
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

void AssetsWindow::QueueSelectAsset(const std::wstring& fullPath)
{
	m_pendingSelectAssetPath = fullPath;
}

bool AssetsWindow::SelectAssetByPath(const std::wstring& fullPath)
{
	if (fullPath.empty())
		return false;

	for (FILEs& file : files)
	{
		const std::wstring candidatePath = file.file_path + L"\\" + file.file_name;
		if (candidatePath == fullPath)
		{
			HideSelected();
			file.is_selected = true;
			selectedFile = &file;
			m_pendingSelectAssetPath.clear();
			return true;
		}
	}

	return false;
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
