#include "pch.h"
#include "Device12.h"

Device12::~Device12() = default;

bool Device12::Initialize(HWND hWnd)
{
	// TODO: D3D12 백엔드 미구현
	::OutputDebugStringW(L"[Engine] Device12 is not implemented yet.\n");
	return false;
}

void Device12::Resize(u32 width, u32 height)
{
}

void Device12::RenderBegin()
{
}

void Device12::RenderEnd()
{
}

void Device12::Shutdown()
{
}
