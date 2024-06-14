#pragma once

#include "KeyboardEvent.h"
#include <queue>

#include "Engine/EngineUtils.h"

class KeyboardClass
{
public:
	KeyboardClass();
	~KeyboardClass();

	bool KeyIsPressed(const BYTE keycode);
	bool KeyIsTriggered(const BYTE keycode);
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
private:
	bool autoRepeatKeys = false;//自動的にキーを繰り返し
	bool autoRepeatChars = false;//自動的に入力を繰り返し
	bool keyStates[256];//キー情報
	bool oldkeyStates[256];//キー情報
	std::queue<KeyboardEvent> keyBuffer;//キーバッファ
	std::queue<BYTE> charBuffer;//入力バッファ
};