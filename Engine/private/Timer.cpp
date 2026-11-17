#include "pch.h"
#include "Timer.h"

#include <timeapi.h>

#pragma comment(lib, "winmm.lib")

#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif

namespace
{
	// 이 시간보다 적게 남으면 OS 대기 대신 짧게 양보하며 정확히 맞춘다.
	constexpr f64 kSpinThresholdSeconds = 0.001;
}

Timer::Timer()
{
	timeBeginPeriod(1);

	// Windows 10 1803+ : 1ms 미만 정밀도의 대기 타이머. 실패하면 Sleep으로 대체한다.
	_waitTimer = ::CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);

	LARGE_INTEGER frequency{};
	QueryPerformanceFrequency(&frequency);
	_frequency = frequency.QuadPart;

	Reset();
	SetTargetFPS(60);
}

Timer::~Timer()
{
	if (_waitTimer != nullptr)
	{
		::CloseHandle(_waitTimer);
		_waitTimer = nullptr;
	}

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
	_totalTime = 0.0;
}

void Timer::Update()
{
	LARGE_INTEGER now{};
	QueryPerformanceCounter(&now);

	_currentTime    = now.QuadPart;
	_frameStartTime = _currentTime;
	_deltaTime      = static_cast<f32>(static_cast<f64>(_currentTime - _previousTime) / static_cast<f64>(_frequency));
	_totalTime      = static_cast<f64>(_currentTime - _startTime) / static_cast<f64>(_frequency);
	_previousTime   = _currentTime;

	if (_deltaTime < 0.0f)
	{
		_deltaTime = 0.0f;
	}
}

void Timer::WaitForTargetFrameTime()
{
	if (_targetFrameTime <= 0.0 || _frequency <= 0)
	{
		return;
	}

	const i64 targetTicks = _frameStartTime + static_cast<i64>(_targetFrameTime * static_cast<f64>(_frequency));

	for (;;)
	{
		LARGE_INTEGER now{};
		QueryPerformanceCounter(&now);

		if (now.QuadPart >= targetTicks)
		{
			break;
		}

		const f64 remainingSeconds = static_cast<f64>(targetTicks - now.QuadPart) / static_cast<f64>(_frequency);

		if (remainingSeconds <= kSpinThresholdSeconds)
		{
			// 마지막 1ms 이하 구간만 CPU를 양보하며 대기
			::Sleep(0);
			continue;
		}

		const f64 waitSeconds = remainingSeconds - kSpinThresholdSeconds;

		if (_waitTimer != nullptr)
		{
			LARGE_INTEGER dueTime{};
			dueTime.QuadPart = -static_cast<i64>(waitSeconds * 10'000'000.0); // 음수 = 상대 시간(100ns 단위)

			if (::SetWaitableTimerEx(_waitTimer, &dueTime, 0, nullptr, nullptr, nullptr, 0))
			{
				::WaitForSingleObject(_waitTimer, INFINITE);
				continue;
			}
		}

		::Sleep(1);
	}
}

void Timer::SetTargetFPS(u32 fps)
{
	_targetFPS = fps;
	_targetFrameTime = (fps > 0) ? (1.0 / static_cast<f64>(fps)) : 0.0;
}

u32 Timer::GetTargetFPS() const
{
	return _targetFPS;
}

f32 Timer::GetDeltaTime() const
{
	return _deltaTime;
}

f64 Timer::GetTotalTime() const
{
	return _totalTime;
}
