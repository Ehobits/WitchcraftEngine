#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

#include <Windows.h>

#include <imgui.h>
#include <TextEditor.h>

class ScriptEditorWindow
{
public:
	void Init(HWND ownerHwnd);
	void Render();

	void NeedRender(bool render);
	bool IsRendering() const;
	bool OpenScriptFile(const std::wstring& path);

private:
	struct SyntaxIssue
	{
		std::string code;
		std::string description;
		std::string file;
		std::size_t line = 0;
	};

	struct ScriptTab
	{
		std::filesystem::path path;
		std::string statusMessage;
		ImVec4 statusColor = ImVec4(0.8f, 0.8f, 0.8f, 1.0f);
		std::vector<SyntaxIssue> syntaxIssues;
		std::vector<std::size_t> selectedIssueIndices;
		std::size_t selectionAnchor = static_cast<std::size_t>(-1);
		bool isDirty = false;
		bool requestEditorFocus = false;
		TextEditor editor;
	};

	static constexpr std::size_t kInvalidTabIndex = static_cast<std::size_t>(-1);
	static constexpr std::size_t kInvalidSelectionIndex = static_cast<std::size_t>(-1);

	void ConfigureEditor(TextEditor& editor);
	std::size_t FindScriptTabIndex(const std::filesystem::path& path) const;
	ScriptTab* GetActiveScriptTab();
	const ScriptTab* GetActiveScriptTab() const;
	void CloseScriptTabAtIndex(std::size_t tabIndex);
	bool SaveScriptFile();
	bool SaveScriptFileAs();
	bool ReloadScriptTabFromDisk(ScriptTab& tab);
	bool LoadScriptFileText(const std::filesystem::path& path, std::string* outText) const;
	bool WriteScriptFileText(const std::filesystem::path& path, const std::string& text) const;
	bool SaveScriptFileToPath(ScriptTab& tab, const std::filesystem::path& path, bool switchCurrentPath, bool removeCurrentPath);
	bool ValidateScriptText(ScriptTab& tab, const std::string& text, bool updateEditorMarks);
	void ClearSyntaxMarkers(ScriptTab& tab);
	void RenderErrorPanel(ScriptTab& tab);
	void RenderSourceEditor(ScriptTab& tab);
	void RenderFileMenuBar();
	void RenderRenamePopup();
	std::string MakeScriptTabTitle(const std::filesystem::path& path) const;
	std::wstring SanitizeFileStem(const std::wstring& value) const;

private:
	bool renderScriptEditor = false;
	std::string m_statusMessage;
	ImVec4 m_statusColor = ImVec4(0.8f, 0.8f, 0.8f, 1.0f);
	std::vector<ScriptTab> m_scriptTabs;
	std::size_t m_activeScriptTabIndex = kInvalidTabIndex;
	float m_editorPaneHeight = 0.0f;
	bool m_splitterInitialized = false;
	TextEditor::Language m_luaLanguage;
	ImFont* m_westernFont = nullptr;
	HWND m_ownerHwnd = nullptr;
	bool m_openRenamePopup = false;
	std::filesystem::path m_pendingRenamePath;
	std::wstring m_pendingRenameDisplayName;
	char m_renameScriptStem[260] = {};
	std::wstring m_renameErrorMessage;
};
