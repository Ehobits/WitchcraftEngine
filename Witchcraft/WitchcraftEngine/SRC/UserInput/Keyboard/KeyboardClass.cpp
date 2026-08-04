#include "KeyboardClass.h"

KeyboardClass::KeyboardClass()
{
	for (int i = 0; i < 256; i++)
	{
		this->keyStates[i] = false; //将所有关键状态初始化为关闭（false）
		this->keyTriggered[i] = false;
	}
}

KeyboardClass::~KeyboardClass()
{
}

bool KeyboardClass::KeyIsTriggered(const BYTE keycode)
{
	std::lock_guard<std::mutex> lock(mMutex);
	const bool triggered = keyTriggered[keycode];
	keyTriggered[keycode] = false;
	return triggered;
}

bool KeyboardClass::KeyIsPressed(const BYTE keycode)
{
	std::lock_guard<std::mutex> lock(mMutex);
	return this->keyStates[keycode];
}

bool KeyboardClass::IsModifierPressed(const BYTE modifierKey) const
{
	std::lock_guard<std::mutex> lock(mMutex);
	return this->keyStates[modifierKey];
}

bool KeyboardClass::IsComboTriggered(const BYTE key, const BYTE modifierKey)
{
	std::lock_guard<std::mutex> lock(mMutex);
	const bool triggered = keyTriggered[key];
	keyTriggered[key] = false;
	return keyStates[modifierKey] && triggered;
}

BYTE KeyboardClass::GetModifierState() const
{
	std::lock_guard<std::mutex> lock(mMutex);
	BYTE state = 0;
	if (keyStates[VK_CONTROL] || keyStates[VK_LCONTROL] || keyStates[VK_RCONTROL]) state |= 1;
	if (keyStates[VK_SHIFT]   || keyStates[VK_LSHIFT]   || keyStates[VK_RSHIFT])   state |= 2;
	if (keyStates[VK_MENU]    || keyStates[VK_LMENU]    || keyStates[VK_RMENU])    state |= 4;
	return state;
}

bool KeyboardClass::KeyBufferIsEmpty()
{
	std::lock_guard<std::mutex> lock(mMutex);
	return this->keyBuffer.empty();
}

bool KeyboardClass::CharBufferIsEmpty()
{
	std::lock_guard<std::mutex> lock(mMutex);
	return this->charBuffer.empty();
}

KeyboardEvent KeyboardClass::ReadKey()
{
	std::lock_guard<std::mutex> lock(mMutex);
	if (this->keyBuffer.empty()) //If no keys to be read?
	{
		return KeyboardEvent(); //return empty keyboard event
	}
	else
	{
		KeyboardEvent e = this->keyBuffer.front(); //Get first Keyboard Event from queue
		this->keyBuffer.pop(); //Remove first item from queue
		return e; //Returns keyboard event
	}
}

BYTE KeyboardClass::ReadChar()
{
	std::lock_guard<std::mutex> lock(mMutex);
	if (this->charBuffer.empty()) //If no keys to be read?
	{
		return 0u; //return 0 (NULL char)
	}
	else
	{
		BYTE e = this->charBuffer.front(); //Get first char from queue
		this->charBuffer.pop(); //Remove first char from queue
		return e; //Returns char
	}
}

void KeyboardClass::OnKeyPressed(const BYTE key)
{
	std::lock_guard<std::mutex> lock(mMutex);
	if (!this->keyStates[key])
		this->keyTriggered[key] = true;
	this->keyStates[key] = true;
	this->keyBuffer.push(KeyboardEvent(KeyboardEvent::EventType::Press, key));
}

void KeyboardClass::OnKeyReleased(const BYTE key)
{
	std::lock_guard<std::mutex> lock(mMutex);
	this->keyStates[key] = false;
	this->keyTriggered[key] = false;
	this->keyBuffer.push(KeyboardEvent(KeyboardEvent::EventType::Release, key));
}

void KeyboardClass::OnChar(const BYTE key)
{
	std::lock_guard<std::mutex> lock(mMutex);
	this->charBuffer.push(key);
}

void KeyboardClass::EnableAutoRepeatKeys()
{
	std::lock_guard<std::mutex> lock(mMutex);
	this->autoRepeatKeys = true;
}

void KeyboardClass::DisableAutoRepeatKeys()
{
	std::lock_guard<std::mutex> lock(mMutex);
	this->autoRepeatKeys = false;
}

void KeyboardClass::EnableAutoRepeatChars()
{
	std::lock_guard<std::mutex> lock(mMutex);
	this->autoRepeatChars = true;
}

void KeyboardClass::DisableAutoRepeatChars()
{
	std::lock_guard<std::mutex> lock(mMutex);
	this->autoRepeatChars = false;
}

bool KeyboardClass::IsKeysAutoRepeat()
{
	std::lock_guard<std::mutex> lock(mMutex);
	return this->autoRepeatKeys;
}

bool KeyboardClass::IsCharsAutoRepeat()
{
	std::lock_guard<std::mutex> lock(mMutex);
	return this->autoRepeatChars;
}

void KeyboardClass::ClearState()
{
	std::lock_guard<std::mutex> lock(mMutex);
	for (int i = 0; i < 256; i++)
	{
		keyStates[i] = false;
		keyTriggered[i] = false;
	}

	std::queue<KeyboardEvent> emptyKeyBuffer;
	std::queue<BYTE> emptyCharBuffer;
	keyBuffer.swap(emptyKeyBuffer);
	charBuffer.swap(emptyCharBuffer);
}

