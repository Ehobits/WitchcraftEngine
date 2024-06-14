#pragma once

#include <xstring>
#include <vector>
#include <sstream>
#include <imgui.h>

#include <Windows.h>

enum MessageType : BYTE
{
	DebugMessage   = 0x10,
	InfoMessage    = 0x50,
	WarningMessage = 0x60,
	ErrorMessage   = 0x70,
};

struct ConsoleMessage
{
	//std::string time;
	std::wstring message;
	BYTE type = 0x00;
	ConsoleMessage(/*std::string time, */std::wstring message, BYTE type)
	{
		//this->time = time;
		this->message = message;
		this->type = type;
	}
};

class ConsoleWindow
{
public:
	void Init();
	void Render();
	//std::string GetNowTime();
	void ClearConsole();

	void NeedRender(bool render);

public:
	void AddDebugMessage(const wchar_t* text, ...);
	void AddInfoMessage(const wchar_t* text, ...);
	void AddWarningMessage(const wchar_t* text, ...);
	void AddErrorMessage(const wchar_t* text, ...);

private:
	bool renderConsole = true;
	
	std::vector<ConsoleMessage> messages;
	ConsoleMessage* selected_message = nullptr;
	size_t idx = -1;

private:
	bool clear_on_play = false;
	bool pause_on_error = false;
	bool view_error = true;
	bool view_warning = true;
	bool view_info = true;

private:
	UINT error_count = 0;
	UINT warning_count = 0;
	UINT info_count = 0;
};
