#pragma once

// D3D11 백엔드의 렌더링 리소스 정의 (Engine 내부 전용).
//
// 참고 프로젝트(D2D11) 대비 변경점
//  - VertexBuffer/IndexBuffer/Geometry/TMesh 를 하나의 Mesh로 합쳤다. 정점 형식은 Vertex 하나로 고정.
//  - 입력 레이아웃은 메시가 아니라 셰이더가 소유한다. (참고 프로젝트는 정점 타입만으로 캐시해서
//    시그니처가 다른 셰이더에 같은 레이아웃을 재사용하는 문제가 있었다)
//  - 인덱스는 16비트 (작은 메시에는 대역폭 절반)

#include "Handle.h"
#include "Types.h"

#include <string>
#include <vector>

// 정점 형식: 위치 + UV. 셰이더 입력(POSITION, TEXCOORD0)과 1:1 대응.
struct Vertex
{
	vec3 position;
	vec2 uv;
};

inline constexpr D3D11_INPUT_ELEMENT_DESC kVertexLayout[] =
{
	{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,  D3D11_INPUT_PER_VERTEX_DATA, 0 },
	{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
};

class Mesh
{
public:
	std::string name;
	ComPtr<ID3D11Buffer> vertexBuffer;
	ComPtr<ID3D11Buffer> indexBuffer;
	u32 indexCount = 0;
	D3D11_PRIMITIVE_TOPOLOGY topology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;

	// CPU 사본 (에디터 마우스 피킹, 선택 외곽선용). 로컬 공간.
	std::vector<vec3> positions;
	std::vector<u16>  indices;
	vec3 boundsMin{ 0.0f, 0.0f, 0.0f };
	vec3 boundsMax{ 0.0f, 0.0f, 0.0f };
};

class Texture
{
public:
	ComPtr<ID3D11ShaderResourceView> srv;
	u32 width  = 0;
	u32 height = 0;
	std::string path;

	// 파일 감시 (ResourceManager::PollFileChanges)
	u64 fileTime        = 0; // 마지막으로 로드한 파일의 수정 시간
	u64 pendingFileTime = 0; // 변경 감지 후 안정될 때까지 기다리는 값
};

class Shader
{
public:
	ComPtr<ID3D11VertexShader> vertexShader;
	ComPtr<ID3D11PixelShader>  pixelShader;
	ComPtr<ID3D11InputLayout>  inputLayout;
	std::string path;

	u64 fileTime        = 0;
	u64 pendingFileTime = 0;
};

class Material
{
public:
	std::string   name;
	ShaderHandle  shader;  // 셰이더/텍스처가 삭제되면 핸들이 무효가 되어 안전하게 감지된다
	TextureHandle texture;
};
