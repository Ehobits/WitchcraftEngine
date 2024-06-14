#include "KeyboardEvent.h"

KeyboardEvent::KeyboardEvent()
	:
	type(EventType::Invalid),
	key(0u)
{
}

KeyboardEvent::KeyboardEvent(const EventType type, const BYTE key)
	:
	type(type),
	key(key)
{
}

KeyboardEvent::~KeyboardEvent()
{
}

bool KeyboardEvent::IsPress() const
{
	return this->type == EventType::Press;
}

bool KeyboardEvent::IsRelease() const
{
	return this->type == EventType::Release;
}

bool KeyboardEvent::IsValid() const
{
	return this->type != EventType::Invalid;
}

BYTE KeyboardEvent::GetKeyCode() const
{
	return this->key;
}