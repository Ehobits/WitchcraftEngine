#include "MouseClass.h"

MouseClass::MouseClass()
{
}

MouseClass::~MouseClass()
{
}

void MouseClass::OnLeftPressed(int x, int y)
{
	std::lock_guard<std::mutex> lock(mMutex);
	this->leftIsDown = true;
	this->x = x;
	this->y = y;
	this->eventBuffer.push(MouseEvent(MouseEvent::EventType::LPress, x, y));
}

void MouseClass::OnLeftReleased(int x, int y)
{
	std::lock_guard<std::mutex> lock(mMutex);
	this->leftIsDown = false;
	this->x = x;
	this->y = y;
	this->eventBuffer.push(MouseEvent(MouseEvent::EventType::LRelease, x, y));
}

void MouseClass::OnRightPressed(int x, int y)
{
	std::lock_guard<std::mutex> lock(mMutex);
	this->rightIsDown = true;
	this->x = x;
	this->y = y;
	this->eventBuffer.push(MouseEvent(MouseEvent::EventType::RPress, x, y));
}

void MouseClass::OnRightReleased(int x, int y)
{
	std::lock_guard<std::mutex> lock(mMutex);
	this->rightIsDown = false;
	this->x = x;
	this->y = y;
	this->eventBuffer.push(MouseEvent(MouseEvent::EventType::RRelease, x, y));
}

void MouseClass::OnMiddlePressed(int x, int y)
{
	std::lock_guard<std::mutex> lock(mMutex);
	this->mbuttonDown = true;
	this->x = x;
	this->y = y;
	this->eventBuffer.push(MouseEvent(MouseEvent::EventType::MPress, x, y));
}

void MouseClass::OnMiddleReleased(int x, int y)
{
	std::lock_guard<std::mutex> lock(mMutex);
	this->mbuttonDown = false;
	this->x = x;
	this->y = y;
	this->eventBuffer.push(MouseEvent(MouseEvent::EventType::MRelease, x, y));
}

void MouseClass::OnWheelUp(int x, int y)
{
	std::lock_guard<std::mutex> lock(mMutex);
	this->x = x;
	this->y = y;
	this->eventBuffer.push(MouseEvent(MouseEvent::EventType::WheelUp, x, y));
}

void MouseClass::OnWheelDown(int x, int y)
{
	std::lock_guard<std::mutex> lock(mMutex);
	this->x = x;
	this->y = y;
	this->eventBuffer.push(MouseEvent(MouseEvent::EventType::WheelDown, x, y));
}

void MouseClass::OnMouseMove(int x, int y)
{
	std::lock_guard<std::mutex> lock(mMutex);
	this->x = x;
	this->y = y;
	this->eventBuffer.push(MouseEvent(MouseEvent::EventType::Move, x, y));
}

bool MouseClass::IsLeftDown()
{
	std::lock_guard<std::mutex> lock(mMutex);
	return this->leftIsDown;
}

bool MouseClass::IsMiddleDown()
{
	std::lock_guard<std::mutex> lock(mMutex);
	return this->mbuttonDown;
}

bool MouseClass::IsRightDown()
{
	std::lock_guard<std::mutex> lock(mMutex);
	return this->rightIsDown;
}

int MouseClass::GetPosX()
{
	std::lock_guard<std::mutex> lock(mMutex);
	return this->x;
}

int MouseClass::GetPosY()
{
	std::lock_guard<std::mutex> lock(mMutex);
	return this->y;
}

MousePoint MouseClass::GetPos()
{
	std::lock_guard<std::mutex> lock(mMutex);
	return{ this->x, this->y };
}

MouseEvent MouseClass::ReadEvent()
{
	std::lock_guard<std::mutex> lock(mMutex);
	if (this->eventBuffer.empty())
		return MouseEvent();

	MouseEvent e = this->eventBuffer.front();
	this->eventBuffer.pop();
	return e;
}

bool MouseClass::EventBufferIsEmpty()
{
	std::lock_guard<std::mutex> lock(mMutex);
	return this->eventBuffer.empty();
}

void MouseClass::Reset()
{
	std::lock_guard<std::mutex> lock(mMutex);
	leftIsDown = false;
	rightIsDown = false;
	mbuttonDown = false;
	x = 0;
	y = 0;

	std::queue<MouseEvent> emptyBuffer;
	eventBuffer.swap(emptyBuffer);
}

