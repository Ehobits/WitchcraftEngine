#pragma once

#include "KeyboardEvent.h"
#include <mutex>
#include <queue>

class KeyboardClass
{
public:
	KeyboardClass();
	~KeyboardClass();

	bool KeyIsPressed(const BYTE keycode);
	bool KeyIsTriggered(const BYTE keycode);
	bool IsModifierPressed(const BYTE modifierKey) const;
	bool IsComboTriggered(const BYTE key, const BYTE modifierKey);
	BYTE GetModifierState() const; // bit0=Ctrl, bit1=Shift, bit2=Alt
	bool KeyBufferIsEmpty();
	bool CharBufferIsEmpty();
	KeyboardEvent ReadKey();
	BYTE ReadChar();
	void OnKeyPressed(const BYTE key);
	void OnKeyReleased(const BYTE key);
	void OnChar(const BYTE key);
	void EnableAutoRepeatKeys();
	void DisableAutoRepeatKeys();
	void EnableAutoRepeatChars();
	void DisableAutoRepeatChars();
	bool IsKeysAutoRepeat();
	bool IsCharsAutoRepeat();
	void ClearState();
private:
	bool autoRepeatKeys = false;//自动重复键
	bool autoRepeatChars = false;//自动重复输入
	bool keyStates[256];//键值
	bool keyTriggered[256];//按下触发状态
	std::queue<KeyboardEvent> keyBuffer;//键缓冲队列
	std::queue<BYTE> charBuffer;//输入缓冲队列
	mutable std::mutex mMutex;
};

