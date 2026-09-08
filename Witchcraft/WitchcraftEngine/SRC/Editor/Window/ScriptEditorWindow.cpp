#include "ScriptEditorWindow.h"

#include <algorithm>
#include <fstream>
#include <cstring>
#include <cstdlib>
#include <cwctype>
#include <iterator>

extern "C"
{
#include <lua.h>
#include <lauxlib.h>
}

#include "Engine/EngineUtils.h"
#include "Helpers/Helpers.h"
#include "String/SStringUtils.h"

static ImFont* FindCodeEditorFont()
{
	ImGuiIO& io = ImGui::GetIO();
	for (ImFont* font : io.Fonts->Fonts)
	{
		if (font == nullptr)
			continue;

		const char* debugName = font->GetDebugName();
		if (debugName != nullptr && std::strstr(debugName, "Cousine") != nullptr)
			return font;
	}

	return io.FontDefault;
}

static bool TryParseLuaErrorLine(const std::string& errorText, std::size_t* outLine)
{
	if (outLine == nullptr)
		return false;

	const char* text = errorText.c_str();
	const char* cursor = text;
	while ((cursor = std::strchr(cursor, ':')) != nullptr)
	{
		++cursor;
		char* endPtr = nullptr;
		const long lineValue = std::strtol(cursor, &endPtr, 10);
		if (endPtr == cursor || lineValue <= 0 || *endPtr != ':')
			continue;

		*outLine = static_cast<std::size_t>(lineValue - 1);
		return true;
	}

	return false;
}

static std::filesystem::path NormalizeScriptPath(const std::filesystem::path& path)
{
	if (path.empty())
		return {};

	std::filesystem::path normalizedPath = path;
	if (normalizedPath.is_relative())
		normalizedPath = std::filesystem::absolute(normalizedPath);
	return normalizedPath.lexically_normal();
}

static std::wstring MakeScriptPathKey(const std::filesystem::path& path)
{
	std::wstring key = NormalizeScriptPath(path).wstring();
	std::transform(key.begin(), key.end(), key.begin(), [](wchar_t ch)
	{
		return static_cast<wchar_t>(std::towlower(ch));
	});
	return key;
}

static std::string MakeProjectRelativePathText(const std::filesystem::path& absolutePath)
{
	const std::filesystem::path projectRoot = std::filesystem::path(EngineUtils::GetProjectDirPath()).lexically_normal();
	const std::filesystem::path normalizedPath = NormalizeScriptPath(absolutePath);
	const std::filesystem::path relativePath = normalizedPath.lexically_relative(projectRoot);
	if (!relativePath.empty() && relativePath != L"." && relativePath.native().rfind(L"..", 0) != 0)
		return SString::WstringToUTF8(relativePath.wstring());

	return SString::WstringToUTF8(normalizedPath.filename().wstring());
}

static std::wstring GetFileDialogInitialDirectory(const std::filesystem::path& path)
{
	if (!path.empty())
		return path.parent_path().wstring();
	return EngineUtils::GetProjectDirPath();
}

static void RenderHorizontalSplitter(
	const char* id,
	float* firstPaneHeight,
	float totalAvailableHeight,
	float firstPaneMinHeight,
	float secondPaneMinHeight,
	float splitterHeight)
{
	ImGui::PushID(id);

	const float availableWidth = ImGui::GetContentRegionAvail().x;
	const ImVec2 splitterPos = ImGui::GetCursorScreenPos();
	ImGui::InvisibleButton("##splitter", ImVec2(availableWidth, splitterHeight));
	if (ImGui::IsItemHovered() || ImGui::IsItemActive())
		ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);

	if (ImGui::IsItemActive())
	{
		*firstPaneHeight += ImGui::GetIO().MouseDelta.y;
	}

	const float maxFirstPaneHeight = std::max(firstPaneMinHeight, totalAvailableHeight - secondPaneMinHeight - splitterHeight);
	*firstPaneHeight = std::clamp(*firstPaneHeight, firstPaneMinHeight, maxFirstPaneHeight);

	ImDrawList* drawList = ImGui::GetWindowDrawList();
	const ImU32 splitterColor = ImGui::GetColorU32(ImGuiCol_Separator);
	drawList->AddRectFilled(
		splitterPos,
		ImVec2(splitterPos.x + availableWidth, splitterPos.y + splitterHeight),
		splitterColor);

	ImGui::PopID();
}

void ScriptEditorWindow::Init(HWND ownerHwnd)
{
	m_ownerHwnd = ownerHwnd;
	m_luaLanguage = *TextEditor::Language::Lua();
	m_westernFont = FindCodeEditorFont();
}

void ScriptEditorWindow::ConfigureEditor(TextEditor& editor)
{
	m_luaLanguage = *TextEditor::Language::Lua();

	editor.SetLanguage(&m_luaLanguage);
	editor.SetTabSize(4);
	editor.SetInsertSpacesOnTabs(false);
	editor.SetAutoIndentEnabled(true);
	editor.SetCompletePairedGlyphs(true);
	editor.SetShowLineNumbersEnabled(true);
	editor.SetLineFoldingEnabled(true);
	editor.SetShowMiniMapEnabled(false);
	editor.SetShowScrollbarMiniMapEnabled(true);
	editor.SetMiddleMousePanMode();

	m_westernFont = FindCodeEditorFont();
}

void ScriptEditorWindow::NeedRender(bool render)
{
	renderScriptEditor = render;
}

bool ScriptEditorWindow::IsRendering() const
{
	return renderScriptEditor;
}

void ScriptEditorWindow::AppendOutputMessage(const std::string& text)
{
	if (text.empty())
		return;

	m_outputMessages.push_back(text);
	if (m_outputMessages.size() > 2000)
		m_outputMessages.erase(m_outputMessages.begin(), m_outputMessages.begin() + 500);
	m_outputScrollToBottom = true;
}

void ScriptEditorWindow::ClearOutputMessages()
{
	m_outputMessages.clear();
	m_outputScrollToBottom = false;
}

std::size_t ScriptEditorWindow::FindScriptTabIndex(const std::filesystem::path& path) const
{
	const std::wstring targetKey = MakeScriptPathKey(path);
	for (std::size_t index = 0; index < m_scriptTabs.size(); ++index)
	{
		if (MakeScriptPathKey(m_scriptTabs[index].path) == targetKey)
			return index;
	}

	return kInvalidTabIndex;
}

ScriptEditorWindow::ScriptTab* ScriptEditorWindow::GetActiveScriptTab()
{
	if (m_activeScriptTabIndex >= m_scriptTabs.size())
		return nullptr;
	return &m_scriptTabs[m_activeScriptTabIndex];
}

const ScriptEditorWindow::ScriptTab* ScriptEditorWindow::GetActiveScriptTab() const
{
	if (m_activeScriptTabIndex >= m_scriptTabs.size())
		return nullptr;
	return &m_scriptTabs[m_activeScriptTabIndex];
}

std::string ScriptEditorWindow::MakeScriptTabTitle(const std::filesystem::path& path) const
{
	return MakeProjectRelativePathText(NormalizeScriptPath(path));
}

std::wstring ScriptEditorWindow::SanitizeFileStem(const std::wstring& value) const
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

void ScriptEditorWindow::CloseScriptTabAtIndex(std::size_t tabIndex)
{
	if (tabIndex >= m_scriptTabs.size())
		return;

	m_scriptTabs.erase(m_scriptTabs.begin() + static_cast<std::ptrdiff_t>(tabIndex));

	if (m_scriptTabs.empty())
	{
		m_activeScriptTabIndex = kInvalidTabIndex;
		m_splitterInitialized = false;
		m_editorPaneHeight = 0.0f;
		return;
	}

	if (m_activeScriptTabIndex == tabIndex)
	{
		m_activeScriptTabIndex = std::min(tabIndex, m_scriptTabs.size() - 1);
	}
	else if (m_activeScriptTabIndex > tabIndex)
	{
		--m_activeScriptTabIndex;
	}
}

bool ScriptEditorWindow::OpenScriptFile(const std::wstring& path)
{
	renderScriptEditor = true;
	m_statusMessage.clear();

	const std::filesystem::path normalizedPath = NormalizeScriptPath(path);
	const std::size_t existingIndex = FindScriptTabIndex(normalizedPath);
	if (existingIndex != kInvalidTabIndex)
	{
		m_activeScriptTabIndex = existingIndex;
		m_scriptTabs[existingIndex].requestEditorFocus = true;
		return true;
	}

	std::string scriptText;
	if (!LoadScriptFileText(normalizedPath, &scriptText))
	{
		m_statusMessage = "无法读取脚本文件";
		m_statusColor = ImVec4(1.0f, 0.4f, 0.4f, 1.0f);
		return false;
	}

	m_scriptTabs.emplace_back();
	ScriptTab& tab = m_scriptTabs.back();
	tab.path = normalizedPath;
	tab.requestEditorFocus = true;
	tab.isDirty = false;
	tab.statusMessage.clear();
	tab.statusColor = ImVec4(0.8f, 0.8f, 0.8f, 1.0f);
	ConfigureEditor(tab.editor);
	tab.editor.SetText(scriptText);
	tab.editor.SetFocus();
	ValidateScriptText(tab, scriptText, true);
	tab.statusMessage.clear();

	m_activeScriptTabIndex = m_scriptTabs.size() - 1;
	return true;
}

bool ScriptEditorWindow::ReloadScriptTabFromDisk(ScriptTab& tab)
{
	std::string scriptText;
	if (!LoadScriptFileText(tab.path, &scriptText))
	{
		tab.statusMessage = "无法读取脚本文件";
		tab.statusColor = ImVec4(1.0f, 0.4f, 0.4f, 1.0f);
		return false;
	}

	tab.editor = TextEditor();
	ConfigureEditor(tab.editor);
	tab.editor.SetText(scriptText);
	tab.editor.SetFocus();
	tab.isDirty = false;
	tab.requestEditorFocus = true;
	ClearSyntaxMarkers(tab);
	ValidateScriptText(tab, scriptText, true);
	tab.statusMessage.clear();
	return true;
}

bool ScriptEditorWindow::SaveScriptFile()
{
	ScriptTab* tab = GetActiveScriptTab();
	if (tab == nullptr || tab->path.empty())
		return false;

	return SaveScriptFileToPath(*tab, tab->path, false, false);
}

bool ScriptEditorWindow::SaveScriptFileAs()
{
	ScriptTab* tab = GetActiveScriptTab();
	if (tab == nullptr)
		return false;

	std::wstring outputPath;
	const std::wstring initialDir = GetFileDialogInitialDirectory(tab->path);
	if (!EngineHelpers::TrySaveFileDialog(
		m_ownerHwnd,
		initialDir.c_str(),
		L"Lua Script (*.lua)\0*.lua\0All Files (*.*)\0*.*\0\0",
		L"Save Script As",
		L"lua",
		&outputPath))
	{
		return false;
	}

	std::filesystem::path savePath = NormalizeScriptPath(outputPath);
	if (savePath.extension().empty())
		savePath.replace_extension(L".lua");

	const std::size_t targetTabIndex = FindScriptTabIndex(savePath);
	if (targetTabIndex != kInvalidTabIndex && targetTabIndex != m_activeScriptTabIndex)
	{
		ScriptTab* tab = GetActiveScriptTab();
		if (tab != nullptr)
		{
			tab->statusMessage = "目标文件已在其他标签页中打开";
			tab->statusColor = ImVec4(1.0f, 0.4f, 0.4f, 1.0f);
		}
		return false;
	}

	return SaveScriptFileToPath(*tab, savePath, true, false);
}

bool ScriptEditorWindow::SaveScriptFileToPath(ScriptTab& tab, const std::filesystem::path& path, bool switchCurrentPath, bool removeCurrentPath)
{
	const std::filesystem::path savePath = NormalizeScriptPath(path);
	if (savePath.empty())
	{
		tab.statusMessage = "保存路径无效";
		tab.statusColor = ImVec4(1.0f, 0.4f, 0.4f, 1.0f);
		return false;
	}

	const std::string scriptText = tab.editor.GetText();
	if (!WriteScriptFileText(savePath, scriptText))
	{
		tab.statusMessage = "写入脚本文件失败";
		tab.statusColor = ImVec4(1.0f, 0.4f, 0.4f, 1.0f);
		return false;
	}

	if (switchCurrentPath)
	{
		const std::filesystem::path oldPath = tab.path;
		if (removeCurrentPath && !oldPath.empty() && oldPath != savePath)
		{
			std::error_code removeError;
			std::filesystem::remove(oldPath, removeError);
		}
		tab.path = savePath;
	}

	tab.isDirty = false;
	tab.statusMessage.clear();
	tab.statusColor = ImVec4(0.4f, 0.9f, 0.4f, 1.0f);
	ValidateScriptText(tab, scriptText, true);
	return true;
}

bool ScriptEditorWindow::LoadScriptFileText(const std::filesystem::path& path, std::string* outText) const
{
	if (outText == nullptr)
		return false;

	std::ifstream script(path, std::ios::binary);
	if (!script)
		return false;

	outText->assign(std::istreambuf_iterator<char>(script), std::istreambuf_iterator<char>());
	return true;
}

bool ScriptEditorWindow::WriteScriptFileText(const std::filesystem::path& path, const std::string& text) const
{
	std::ofstream script(path, std::ios::binary | std::ios::trunc);
	if (!script)
		return false;

	script.write(text.data(), static_cast<std::streamsize>(text.size()));
	return script.good();
}

void ScriptEditorWindow::Render()
{
	if (!renderScriptEditor)
		return;

	const ImGuiWindowFlags windowFlags = ImGuiWindowFlags_MenuBar;
	if (!ImGui::Begin("脚本编辑器##ScriptEditorWindow", &renderScriptEditor, windowFlags))
	{
		ImGui::End();
		return;
	}

	if (ImGui::BeginMenuBar())
	{
		RenderFileMenuBar();
		ImGui::EndMenuBar();
	}

	ScriptTab* activeTab = GetActiveScriptTab();
	if (activeTab == nullptr)
	{
		if (!m_statusMessage.empty())
			ImGui::TextColored(m_statusColor, "%s", m_statusMessage.c_str());
		else
			ImGui::TextDisabled("没有打开的脚本。");
		RenderRenamePopup();
		ImGui::End();
		return;
	}

	if (!m_scriptTabs.empty())
	{
		if (m_activeScriptTabIndex >= m_scriptTabs.size())
			m_activeScriptTabIndex = 0;

		std::vector<std::size_t> closedTabIndices;
		if (ImGui::BeginTabBar("##ScriptEditorTabs"))
		{
			for (std::size_t tabIndex = 0; tabIndex < m_scriptTabs.size(); ++tabIndex)
			{
				ScriptTab& tab = m_scriptTabs[tabIndex];
				const std::string tabTitle = MakeScriptTabTitle(tab.path);
				const ImGuiTabItemFlags tabFlags = tab.isDirty ? ImGuiTabItemFlags_UnsavedDocument : 0;
				bool tabOpen = true;

				ImGui::PushID(static_cast<int>(tabIndex));
				if (ImGui::BeginTabItem(tabTitle.c_str(), &tabOpen, tabFlags))
				{
					if (m_activeScriptTabIndex != tabIndex)
					{
						m_activeScriptTabIndex = tabIndex;
						tab.requestEditorFocus = true;
					}

					ImGui::EndTabItem();
				}
				if (!tabOpen)
					closedTabIndices.push_back(tabIndex);
				ImGui::PopID();
			}

			ImGui::EndTabBar();
		}

		for (auto it = closedTabIndices.rbegin(); it != closedTabIndices.rend(); ++it)
			CloseScriptTabAtIndex(*it);

		if (m_scriptTabs.empty())
		{
			ImGui::TextDisabled("没有打开的脚本。");
			ImGui::End();
			return;
		}

		ImGui::Separator();

		activeTab = GetActiveScriptTab();
		if (activeTab == nullptr)
		{
			ImGui::End();
			return;
		}

		const float splitterHeight = 6.0f;
		const float minimumTopPaneHeight = 220.0f;
		const float minimumBottomPaneHeight = 120.0f;
		const float availableHeight = ImGui::GetContentRegionAvail().y;
		if (!m_splitterInitialized)
		{
			m_editorPaneHeight = std::max(minimumTopPaneHeight, availableHeight * 0.72f);
			m_splitterInitialized = true;
		}

		const float maxTopPaneHeight = std::max(minimumTopPaneHeight, availableHeight - minimumBottomPaneHeight - splitterHeight);
		m_editorPaneHeight = std::clamp(m_editorPaneHeight, minimumTopPaneHeight, maxTopPaneHeight);

		ImGui::BeginChild("##ScriptEditorTopPane", ImVec2(0.0f, m_editorPaneHeight), false, ImGuiWindowFlags_NoScrollbar);
		ImGui::PushID(static_cast<int>(m_activeScriptTabIndex));
		RenderSourceEditor(*activeTab);
		ImGui::PopID();
		ImGui::EndChild();

		RenderHorizontalSplitter(
			"ScriptEditorSplitter",
			&m_editorPaneHeight,
			availableHeight,
			minimumTopPaneHeight,
			minimumBottomPaneHeight,
			splitterHeight);

		const float bottomPaneHeight = std::max(minimumBottomPaneHeight, ImGui::GetContentRegionAvail().y);
		ImGui::BeginChild("##ScriptEditorBottomPane", ImVec2(0.0f, bottomPaneHeight), true, ImGuiWindowFlags_HorizontalScrollbar);
		if (ImGui::BeginTabBar("##ScriptEditorBottomTabs"))
		{
			if (ImGui::BeginTabItem("错误列表"))
			{
				ImGui::PushID(static_cast<int>(m_activeScriptTabIndex));
				RenderErrorPanel(*activeTab);
				ImGui::PopID();
				ImGui::EndTabItem();
			}
			if (ImGui::BeginTabItem("输出"))
			{
				RenderOutputPanel();
				ImGui::EndTabItem();
			}
			ImGui::EndTabBar();
		}
		ImGui::EndChild();
	}

	RenderRenamePopup();

	ImGui::End();
}

void ScriptEditorWindow::RenderFileMenuBar()
{
	const ScriptTab* activeTab = GetActiveScriptTab();
	const bool hasActiveTab = (activeTab != nullptr);

	if (ImGui::BeginMenu("文件"))
	{
		if (ImGui::MenuItem("打开脚本"))
		{
			std::wstring outputPath;
			const std::wstring initialDir = hasActiveTab ? GetFileDialogInitialDirectory(activeTab->path) : EngineUtils::GetProjectDirPath();
			if (EngineHelpers::TryOpenFileDialog(
				m_ownerHwnd,
				initialDir.c_str(),
				L"Lua 脚本 (*.lua)\0*.lua\0所有文件 (*.*)\0*.*\0\0",
				L"打开脚本",
				&outputPath))
			{
				OpenScriptFile(outputPath);
			}
		}

		if (ImGui::MenuItem("保存脚本", nullptr, false, hasActiveTab))
			SaveScriptFile();

		if (ImGui::MenuItem("另存为...", nullptr, false, hasActiveTab))
			SaveScriptFileAs();

		if (ImGui::MenuItem("重命名该脚本", nullptr, false, hasActiveTab))
		{
			m_pendingRenamePath = activeTab->path;
			m_pendingRenameDisplayName = activeTab->path.filename().wstring();
			const std::wstring defaultStem = SanitizeFileStem(activeTab->path.stem().wstring());
			const std::string utf8Stem = SString::WstringToUTF8(defaultStem.empty() ? activeTab->path.stem().wstring() : defaultStem);
			strncpy_s(m_renameScriptStem, IM_ARRAYSIZE(m_renameScriptStem), utf8Stem.c_str(), _TRUNCATE);
			m_renameErrorMessage.clear();
			m_openRenamePopup = true;
		}

		ImGui::EndMenu();
	}
}

void ScriptEditorWindow::RenderSourceEditor(ScriptTab& tab)
{
	const ImVec2 editorSize(-1.0f, 0.0f);
	if (tab.requestEditorFocus)
	{
		tab.editor.SetFocus();
		tab.requestEditorFocus = false;
	}

	if (m_westernFont != nullptr)
		ImGui::PushFont(m_westernFont);

	if (tab.editor.Render("##ScriptTextEditor", editorSize))
	{
		const std::string text = tab.editor.GetText();
		tab.isDirty = true;
		tab.statusMessage.clear();
		ValidateScriptText(tab, text, true);
	}

	if (m_westernFont != nullptr)
		ImGui::PopFont();
}

void ScriptEditorWindow::RenderErrorPanel(ScriptTab& tab)
{
	ImGui::TextUnformatted("错误列表");
	ImGui::Separator();

	if (tab.syntaxIssues.empty())
	{
		ImGui::TextDisabled("没有错误。");
		return;
	}

	const ImGuiTableFlags tableFlags =
		ImGuiTableFlags_Resizable |
		ImGuiTableFlags_Hideable |
		ImGuiTableFlags_RowBg |
		ImGuiTableFlags_BordersInnerV |
		ImGuiTableFlags_BordersOuterH |
		ImGuiTableFlags_ScrollY |
		ImGuiTableFlags_SizingStretchProp;

	if (ImGui::BeginTable("##LuaSyntaxErrorTable", 4, tableFlags))
	{
		ImGui::TableSetupColumn("代码", ImGuiTableColumnFlags_WidthFixed, 72.0f);
		ImGui::TableSetupColumn("说明", ImGuiTableColumnFlags_WidthStretch);
		ImGui::TableSetupColumn("文件", ImGuiTableColumnFlags_WidthStretch);
		ImGui::TableSetupColumn("行号", ImGuiTableColumnFlags_WidthFixed, 60.0f);
		ImGui::TableHeadersRow();

		for (std::size_t issueIndex = 0; issueIndex < tab.syntaxIssues.size(); ++issueIndex)
		{
			const SyntaxIssue& issue = tab.syntaxIssues[issueIndex];
			const bool selected = std::find(tab.selectedIssueIndices.begin(), tab.selectedIssueIndices.end(), issueIndex) != tab.selectedIssueIndices.end();
			const float rowHeight = ImGui::GetTextLineHeightWithSpacing();

			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::PushID(static_cast<int>(issueIndex));

			if (selected)
			{
				const ImU32 rowColor = ImGui::GetColorU32(ImVec4(0.20f, 0.35f, 0.55f, 0.45f));
				ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, rowColor);
				ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg1, rowColor);
			}

			const bool rowClicked = ImGui::Selectable(
				issue.code.c_str(),
				selected,
				ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick,
				ImVec2(0.0f, rowHeight));
			const bool rowDoubleClicked = ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
			if (rowClicked)
			{
				const bool ctrlPressed = ImGui::GetIO().KeyCtrl;
				const bool shiftPressed = ImGui::GetIO().KeyShift;
				if (shiftPressed && tab.selectionAnchor != kInvalidSelectionIndex)
				{
					const std::size_t beginIndex = std::min(tab.selectionAnchor, issueIndex);
					const std::size_t endIndex = std::max(tab.selectionAnchor, issueIndex);
					tab.selectedIssueIndices.clear();
					for (std::size_t i = beginIndex; i <= endIndex; ++i)
						tab.selectedIssueIndices.push_back(i);
				}
				else if (ctrlPressed)
				{
					const auto foundIt = std::find(tab.selectedIssueIndices.begin(), tab.selectedIssueIndices.end(), issueIndex);
					if (foundIt != tab.selectedIssueIndices.end())
						tab.selectedIssueIndices.erase(foundIt);
					else
						tab.selectedIssueIndices.push_back(issueIndex);
					tab.selectionAnchor = issueIndex;
				}
				else
				{
					tab.selectedIssueIndices.clear();
					tab.selectedIssueIndices.push_back(issueIndex);
					tab.selectionAnchor = issueIndex;
				}
			}

			if (rowDoubleClicked && issue.line > 0)
			{
				tab.editor.SetCursor(TextEditor::DocPos(issue.line - 1, 0));
				tab.editor.SetFocus();
			}

			ImGui::PopID();

			ImGui::TableSetColumnIndex(1);
			ImGui::TextUnformatted(issue.description.c_str());

			ImGui::TableSetColumnIndex(2);
			ImGui::TextUnformatted(issue.file.c_str());

			ImGui::TableSetColumnIndex(3);
			if (issue.line > 0)
				ImGui::Text("%zu", issue.line);
			else
				ImGui::TextUnformatted("-");
		}

		ImGui::EndTable();
	}
}

void ScriptEditorWindow::RenderOutputPanel()
{
	if (ImGui::Button("清空输出"))
		ClearOutputMessages();

	ImGui::Separator();

	if (m_outputMessages.empty())
	{
		ImGui::TextDisabled("没有输出。");
		return;
	}

	ImGui::BeginChild("##ScriptOutputList", ImVec2(0.0f, 0.0f), false, ImGuiWindowFlags_HorizontalScrollbar);
	for (std::size_t index = 0; index < m_outputMessages.size(); ++index)
		ImGui::TextUnformatted(m_outputMessages[index].c_str());

	if (m_outputScrollToBottom)
	{
		ImGui::SetScrollHereY(1.0f);
		m_outputScrollToBottom = false;
	}
	ImGui::EndChild();
}

void ScriptEditorWindow::RenderRenamePopup()
{
	if (m_openRenamePopup)
	{
		ImGui::OpenPopup("重命名脚本文件");
		m_openRenamePopup = false;
	}

	constexpr ImGuiWindowFlags popupFlags =
		ImGuiWindowFlags_AlwaysAutoResize |
		ImGuiWindowFlags_NoSavedSettings;

	if (!ImGui::BeginPopupModal("重命名脚本文件", nullptr, popupFlags))
		return;

	ImGui::TextDisabled("只修改文件名，不改变扩展名。");
	if (!m_pendingRenameDisplayName.empty())
		ImGui::TextWrapped("当前：%s", SString::WstringToUTF8(m_pendingRenameDisplayName).c_str());

	ImGui::Separator();
	ImGui::InputText("新文件名", m_renameScriptStem, IM_ARRAYSIZE(m_renameScriptStem));

	if (!m_renameErrorMessage.empty())
		ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", SString::WstringToUTF8(m_renameErrorMessage).c_str());

	if (ImGui::Button("取消"))
	{
		m_pendingRenamePath.clear();
		m_pendingRenameDisplayName.clear();
		m_renameErrorMessage.clear();
		m_renameScriptStem[0] = '\0';
		ImGui::CloseCurrentPopup();
		ImGui::EndPopup();
		return;
	}

	ImGui::SameLine();
	if (ImGui::Button("重命名"))
	{
		ScriptTab* tab = GetActiveScriptTab();
		const std::filesystem::path sourcePath = NormalizeScriptPath(m_pendingRenamePath);
		const std::wstring newStem = SanitizeFileStem(SString::UTF8ToWstring(m_renameScriptStem));
		if (tab == nullptr || sourcePath.empty() || sourcePath != NormalizeScriptPath(tab->path))
		{
			m_renameErrorMessage = L"当前脚本状态已变化，请重新打开后再试。";
		}
		else if (newStem.empty())
		{
			m_renameErrorMessage = L"文件名不能为空。";
		}
		else
		{
			const std::filesystem::path targetPath = sourcePath.parent_path() / (newStem + sourcePath.extension().wstring());
			if (targetPath == sourcePath)
			{
				m_pendingRenamePath.clear();
				m_pendingRenameDisplayName.clear();
				m_renameErrorMessage.clear();
				m_renameScriptStem[0] = '\0';
				ImGui::CloseCurrentPopup();
				ImGui::EndPopup();
				return;
			}

			if (FindScriptTabIndex(targetPath) != kInvalidTabIndex)
			{
				m_renameErrorMessage = L"目标文件已在其他标签页中打开。";
			}
			else if (std::filesystem::exists(targetPath))
			{
				m_renameErrorMessage = L"目标文件已存在。";
			}
			else if (!SaveScriptFileToPath(*tab, targetPath, true, true))
			{
				m_renameErrorMessage = L"重命名失败。";
			}
			else
			{
				m_pendingRenamePath.clear();
				m_pendingRenameDisplayName.clear();
				m_renameErrorMessage.clear();
				m_renameScriptStem[0] = '\0';
				ImGui::CloseCurrentPopup();
				ImGui::EndPopup();
				return;
			}
		}
	}

	ImGui::EndPopup();
}

bool ScriptEditorWindow::ValidateScriptText(ScriptTab& tab, const std::string& text, bool updateEditorMarks)
{
	ClearSyntaxMarkers(tab);

	lua_State* luaState = luaL_newstate();
	if (luaState == nullptr)
	{
		tab.statusMessage = "无法创建 Lua 检查器";
		tab.statusColor = ImVec4(1.0f, 0.4f, 0.4f, 1.0f);
		return false;
	}

	const std::string chunkName = "@" + MakeProjectRelativePathText(tab.path);
	const int loadResult = luaL_loadbuffer(luaState, text.c_str(), text.size(), chunkName.c_str());
	if (loadResult != LUA_OK)
	{
		const char* errorText = lua_tostring(luaState, -1);
		const std::string errorMessage = errorText != nullptr ? errorText : "Lua 语法检查失败";
		SyntaxIssue issue;
		issue.code = "Lua";
		issue.description = errorMessage;
		issue.file = MakeProjectRelativePathText(tab.path);
		issue.line = 0;

		std::size_t errorLine = 0;
		if (TryParseLuaErrorLine(errorMessage, &errorLine))
		{
			issue.line = errorLine + 1;
			if (updateEditorMarks)
			{
				tab.editor.AddMarker(
					errorLine,
					IM_COL32(180, 60, 60, 255),
					IM_COL32(80, 25, 25, 40),
					"Lua 语法错误",
					errorMessage);
			}
		}

		tab.syntaxIssues.push_back(std::move(issue));
		tab.statusMessage = errorMessage;
		tab.statusColor = ImVec4(1.0f, 0.4f, 0.4f, 1.0f);
		lua_close(luaState);
		return false;
	}

	tab.statusMessage.clear();
	lua_close(luaState);
	return true;
}

void ScriptEditorWindow::ClearSyntaxMarkers(ScriptTab& tab)
{
	tab.syntaxIssues.clear();
	tab.selectedIssueIndices.clear();
	tab.selectionAnchor = kInvalidSelectionIndex;
	tab.editor.ClearMarkers();
}
