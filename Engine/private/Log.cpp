#include "pch.h"
#include "Log.h"

#include <cstdarg>
#include <cstdio>
#include <mutex>

namespace
{
	std::mutex g_logMutex;
	FILE*      g_logFile = nullptr;
	bool       g_logOpenAttempted = false;

	FILE* GetLogFile()
	{
		if (!g_logOpenAttempted)
		{
			g_logOpenAttempted = true;

			WCHAR exePath[MAX_PATH]{};
			::GetModuleFileNameW(nullptr, exePath, MAX_PATH);

			std::wstring path(exePath);
			const size_t slash = path.find_last_of(L"\\/");
			path.resize(slash == std::wstring::npos ? 0 : slash + 1);
			path += L"Engine.log";

			// 실행할 때마다 새로 작성
			if (_wfopen_s(&g_logFile, path.c_str(), L"w") != 0)
			{
				g_logFile = nullptr;
			}
		}
		return g_logFile;
	}
}

void EngineLog(const char* format, ...)
{
	char message[1024];

	va_list args;
	va_start(args, format);
	vsnprintf(message, sizeof(message), format, args);
	va_end(args);

	SYSTEMTIME now{};
	::GetLocalTime(&now);

	char line[1100];
	snprintf(line, sizeof(line), "[%02u:%02u:%02u.%03u] %s\n", now.wHour, now.wMinute, now.wSecond, now.wMilliseconds, message);

	std::lock_guard lock(g_logMutex);

	::OutputDebugStringA(line);

	if (FILE* file = GetLogFile())
	{
		fputs(line, file);
		fflush(file);
	}
}
