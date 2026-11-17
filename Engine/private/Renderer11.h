#pragma once

#include "Types.h"

class Device11;
class Scene;
class ResourceManager;

// D3D11 씬 렌더러.
//
// 참고 프로젝트(D2D11 RenderManager/RenderPass) 대비 변경점
//  - 파이프라인 상태(래스터라이저/깊이/블렌드/샘플러)를 매 프레임 바인딩한다.
//    (참고 프로젝트는 시작 시 한 번만 설정해서 ClearState 등 이후 상태가 사라졌고,
//     래스터라이저 desc를 엉뚱한 변수로 초기화해 쓰레기 값이 들어가는 버그가 있었다)
//  - 상수 버퍼는 DYNAMIC + Map(WRITE_DISCARD) 로 갱신 (매 드로우 갱신에 적합)
//  - 뷰*투영 행렬을 CPU에서 미리 곱해 정점마다 행렬 곱 1회를 줄였다.
//  - 같은 셰이더/텍스처/메시가 연속되면 다시 바인딩하지 않는다.
//  - 리소스는 핸들로 조회한다. 삭제된 메시/머티리얼/셰이더를 가리키면 그 오브젝트만 건너뛴다.
class Renderer11
{
public:
	explicit Renderer11(Device11& device);
	~Renderer11();

	bool Initialize();
	struct CameraMatrices
	{
		matx view;
		matx projection;
	};

	// 씬에서 처음 찾은 활성 카메라의 행렬. 없으면 (0, 0, -5)에서 +Z를 보는 기본 카메라.
	// (렌더러와 에디터 피킹이 같은 규칙을 쓰도록 공유)
	static CameraMatrices ResolveSceneCamera(const Scene* scene, f32 aspect);

	// 현재 바인딩된 렌더 타깃(창 또는 화면 밖 텍스처)에 그린다. width/height는 그 타깃 크기(종횡비 계산용).
	// camera를 주면 씬의 카메라 대신 그 행렬로 그린다 (에디터 Scene 창의 에디터 카메라).
	void Render(const Scene* scene, const ResourceManager& resources, u32 width, u32 height, const CameraMatrices* camera = nullptr);

private:
	struct alignas(16) PerFrameConstants
	{
		matx viewProjection;
	};

	struct alignas(16) PerObjectConstants
	{
		matx world;
	};

	template <typename T>
	void UpdateConstantBuffer(ID3D11Buffer* buffer, const T& data);

private:
	Device11& _device;

	ComPtr<ID3D11Buffer>            _perFrameBuffer;
	ComPtr<ID3D11Buffer>            _perObjectBuffer;
	ComPtr<ID3D11RasterizerState>   _rasterizerState;
	ComPtr<ID3D11DepthStencilState> _depthState;
	ComPtr<ID3D11BlendState>        _blendState;
	ComPtr<ID3D11SamplerState>      _samplerState;
};
