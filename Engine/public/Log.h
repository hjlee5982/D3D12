#pragma once

#include "EngineAPI.h"

// 디버거 출력(OutputDebugString)과 실행 파일 옆 Engine.log에 한 줄을 기록한다.
// 디버거 없이 실행 중일 때도 핫 리로드 과정을 확인할 수 있게 파일에도 남긴다.
// printf 형식. 메시지는 UTF-8.
ENGINE_API void EngineLog(const char* format, ...);
