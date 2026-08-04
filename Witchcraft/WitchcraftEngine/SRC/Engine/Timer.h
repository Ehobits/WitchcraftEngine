#pragma once

class Timer
{
public:
	Timer();

	float TotalTime()const; // 以秒为单位
	float DeltaTime()const; // 以秒为单位

	void Reset(); // 在消息循环之前调用。
	void Start(); // 取消暂停时调用。
	void Stop();  // 暂停时调用。
	void Tick();  // 每帧调用。

	bool IsStop();
private:
	double mSecondsPerCount;
	double mDeltaTime;

	__int64 mBaseTime;
	__int64 mPausedTime;
	__int64 mStopTime;
	__int64 mPrevTime;
	__int64 mCurrTime;

	bool mStopped;
};
