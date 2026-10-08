#pragma once

#include "Types.h"

class Timer
{
public:
	Timer();
	~Timer();

	void Reset();
	void Update();
	void WaitForTargetFrameTime();

	void SetTargetFPS(u32 fps);
	u32 GetTargetFPS() const;

	f32 GetDeltaTime() const;
	f32 GetTotalTime() const;

private:
	i64 _frequency       = 0;
	i64 _previousTime    = 0;
	i64 _currentTime     = 0;
	i64 _startTime       = 0;
	i64 _frameStartTime  = 0;
	f32 _deltaTime       = 0.0f;
	f32 _totalTime       = 0.0f;
	f32 _targetFrameTime = 0.0f;
	u32 _targetFPS       = 0;
};
