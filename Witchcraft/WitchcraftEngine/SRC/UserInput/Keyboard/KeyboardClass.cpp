#include "KeyboardClass.h"

KeyboardClass::KeyboardClass()
{
	for (int i = 0; i < 256; i++)
	{
		this->keyStates[i] = false; //Initialize all key states to off (false)
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

