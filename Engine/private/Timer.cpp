#include "pch.h"
#include "Timer.h"

#include <timeapi.h>

#pragma comment(lib, "winmm.lib")

Timer::Timer()
{
	timeBeginPeriod(1);

	LARGE_INTEGER frequency{};
	QueryPerformanceFrequency(&frequency);
	_frequency = frequency.QuadPart;

	Reset();
	SetTargetFPS(60);
}

Timer::~Timer()
{
	timeEndPeriod(1);
}

void Timer::Reset()
{
	LARGE_INTEGER now{};
	QueryPerformanceCounter(&now);

	_startTime      = now.QuadPart;
	_previousTime   = now.QuadPart;
	_currentTime    = now.QuadPart;
	_frameStartTime = now.QuadPart;
	_deltaTime = 0.0f;
	_totalTime = 0.0f;
}

void Timer::Update()
{
	LARGE_INTEGER now{};
	QueryPerformanceCounter(&now);

	_currentTime = now.QuadPart;
	_frameStartTime = _currentTime;
	_deltaTime = static_cast<f32>(_currentTime - _previousTime) / static_cast<f32>(_frequency);
	_totalTime = static_cast<f32>(_currentTime - _startTime) / static_cast<f32>(_frequency);
	_previousTime = _currentTime;

	if (_deltaTime < 0.0f)
	{
		_deltaTime = 0.0f;
	}
}

void Timer::WaitForTargetFrameTime()
{
	if (_targetFrameTime <= 0.0f || _frequency <= 0)
	{
		return;
	}

	const i64 targetTicks = _frameStartTime + static_cast<i64>(_targetFrameTime * static_cast<f32>(_frequency));

	for (;;)
	{
		LARGE_INTEGER now{};
		QueryPerformanceCounter(&now);

		if (now.QuadPart >= targetTicks)
		{
			break;
		}

		const f32 remainingSeconds =
			static_cast<f32>(targetTicks - now.QuadPart) / static_cast<f32>(_frequency);

		if (remainingSeconds > 0.002f)
		{
			Sleep(1);
		}
	}
}

void Timer::SetTargetFPS(u32 fps)
{
	_targetFPS = fps;
	_targetFrameTime = (fps > 0) ? (1.0f / static_cast<f32>(fps)) : 0.0f;
}

u32 Timer::GetTargetFPS() const
{
	return _targetFPS;
}

f32 Timer::GetDeltaTime() const
{
	return _deltaTime;
}

f32 Timer::GetTotalTime() const
{
	return _totalTime;
}
