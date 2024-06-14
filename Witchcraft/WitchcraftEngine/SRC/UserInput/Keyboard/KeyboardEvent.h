#pragma once

#include <Windows.h>

#include "Engine/EngineUtils.h"

class KeyboardEvent
{
public:
	//键盘实事件种类
	enum EventType
	{
		Press,//按下
		Release,//释放
		Invalid//禁止
	};

	KeyboardEvent();
	KeyboardEvent(const EventType type, const BYTE key);
	~KeyboardEvent();

	bool IsPress() const;
	bool IsRelease() const;
	bool IsValid() const;
	BYTE GetKeyCode() const;

private:
	EventType type;//キーボード事件種類
	BYTE key;//キー
};