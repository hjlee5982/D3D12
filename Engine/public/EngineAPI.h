#pragma once

// Engine.dll 경계 매크로.
// Engine 프로젝트는 ENGINE_EXPORTS를 정의해 심볼을 내보내고,
// Client/Game은 같은 헤더를 dllimport로 사용한다.
#if defined(ENGINE_EXPORTS)
#define ENGINE_API __declspec(dllexport)
#else
#define ENGINE_API __declspec(dllimport)
#endif

// C4251: 내보내는 클래스가 STL 멤버(std::string, std::vector 등)를 가질 때 발생.
// Engine/Client/Game은 항상 같은 컴파일러·같은 CRT(/MD)로 함께 빌드하므로 무시한다.
#pragma warning(disable : 4251)
