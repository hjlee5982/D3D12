#include "pch.h"
#include "Engine.h"

namespace
{
	constexpr WCHAR kWindowClassName[] = L"D3D12ClientWindow";
	constexpr WCHAR kWindowTitle[]     = L"D3D12";
	constexpr i32   kDefaultWidth      = 1280;
	constexpr i32   kDefaultHeight     = 720;

	std::unique_ptr<Engine> kEngine = nullptr;

	LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
	{
		switch (msg)
		{
		case WM_KEYDOWN:

			switch (wParam)
			{
			case VK_ESCAPE:
				DestroyWindow(hWnd);
				break;
			}
			return 0;

		case WM_DESTROY:
			PostQuitMessage(0);
			return 0;

		default:
			return DefWindowProcW(hWnd, msg, wParam, lParam);
		}
	}

	HWND CreateClientWindow(HINSTANCE instance, i32 showCommand)
	{
		WNDCLASSEXW windowClass{};
		{
			windowClass.cbSize        = sizeof(windowClass);
			windowClass.style         = CS_HREDRAW | CS_VREDRAW;
			windowClass.lpfnWndProc   = WndProc;
			windowClass.hInstance     = instance;
			windowClass.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
			windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
			windowClass.lpszClassName = kWindowClassName;
		}
		if (RegisterClassExW(&windowClass) == 0)
		{
			return nullptr;
		}

		RECT windowRect{ 0, 0, kDefaultWidth, kDefaultHeight };
		AdjustWindowRect(&windowRect, WS_OVERLAPPEDWINDOW, FALSE);

		const i32 windowWidth  = windowRect.right  - windowRect.left;
		const i32 windowHeight = windowRect.bottom - windowRect.top;

		HWND hWnd = CreateWindowExW(
			0,
			kWindowClassName,
			kWindowTitle,
			WS_OVERLAPPEDWINDOW,
			CW_USEDEFAULT,
			CW_USEDEFAULT,
			windowWidth,
			windowHeight,
			nullptr,
			nullptr,
			instance,
			nullptr);

		if (hWnd == nullptr)
		{
			return nullptr;
		}

		ShowWindow(hWnd, showCommand);
		UpdateWindow(hWnd);
		return hWnd;
	}
}

i32 WINAPI wWinMain(_In_ HINSTANCE instance, _In_opt_ HINSTANCE, _In_ LPWSTR, _In_ i32 showCommand)
{
	HWND hWnd = CreateClientWindow(instance, showCommand);
	if (hWnd == nullptr)
	{
		return 1;
	}
	
	kEngine = std::make_unique<Engine>(11);

	if (!kEngine->EngineInitialize(hWnd))
	{
		DestroyWindow(hWnd);
		return 1;
	}

	bool running = true;
	while (running)
	{
		MSG message{};
		while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
		{
			if (message.message == WM_QUIT)
			{
				running = false;
				break;
			}

			TranslateMessage(&message);
			DispatchMessageW(&message);
		}

		if (!running)
		{
			break;
		}
		
		kEngine->EngineRenderBegin();

		kEngine->EngineRenderEnd();
	}
	
	kEngine->EngineShutdown();

	return 0;
}