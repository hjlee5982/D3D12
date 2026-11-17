#pragma once

#include "EngineAPI.h"
#include "Types.h"

class ENGINE_API Timer
{
public:
	Timer();
	~Timer();

	Timer(const Timer&) = delete;
	Timer& operator=(const Timer&) = delete;

	void Reset();
	void Update();
	void WaitForTargetFrameTime();

	void SetTargetFPS(u32 fps);
	u32 GetTargetFPS() const;

	f32 GetDeltaTime() const;
	f64 GetTotalTime() const;

private:
	i64   _frequency       = 0;
	i64   _previousTime    = 0;
	i64   _currentTime     = 0;
	i64   _startTime       = 0;
	i64   _frameStartTime  = 0;
	f32   _deltaTime       = 0.0f;
	f64   _totalTime       = 0.0;
	f64   _targetFrameTime = 0.0;
	u32   _targetFPS       = 0;
	void* _waitTimer       = nullptr; // HANDLE (고해상도 대기 타이머)
};
