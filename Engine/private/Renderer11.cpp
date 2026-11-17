#include "pch.h"
#include "Renderer11.h"
#include "Device11.h"
#include "RenderResources.h"
#include "Log.h"

#include "Camera.h"
#include "GameObject.h"
#include "MeshRenderer.h"
#include "ResourceManager.h"
#include "Scene.h"
#include "Transform.h"

Renderer11::Renderer11(Device11& device)
	: _device(device)
{
}

Renderer11::~Renderer11() = default;

bool Renderer11::Initialize()
{
	ID3D11Device* device = _device.GetDevice();
	if (device == nullptr)
	{
		return false;
	}

	// 상수 버퍼: 매 프레임/매 드로우 갱신 → DYNAMIC + CPU 쓰기
	auto createConstantBuffer = [device](UINT size, ComPtr<ID3D11Buffer>& out)
	{
		D3D11_BUFFER_DESC desc{};
		desc.ByteWidth      = size;
		desc.Usage          = D3D11_USAGE_DYNAMIC;
		desc.BindFlags      = D3D11_BIND_CONSTANT_BUFFER;
		desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		return SUCCEEDED(device->CreateBuffer(&desc, nullptr, out.GetAddressOf()));
	};

	if (!createConstantBuffer(sizeof(PerFrameConstants), _perFrameBuffer) ||
		!createConstantBuffer(sizeof(PerObjectConstants), _perObjectBuffer))
	{
		EngineLog("[Renderer] Failed to create constant buffers");
		return false;
	}

	D3D11_RASTERIZER_DESC rasterizer{};
	{
		rasterizer.FillMode        = D3D11_FILL_SOLID;
		rasterizer.CullMode        = D3D11_CULL_BACK;
		rasterizer.DepthClipEnable = TRUE;
	}

	D3D11_DEPTH_STENCIL_DESC depth{};
	{
		depth.DepthEnable    = TRUE;
		depth.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
		depth.DepthFunc      = D3D11_COMPARISON_LESS;
	}

	// 알파 블렌딩 (반투명 텍스처 대비). 불투명/반투명 정렬은 아직 없다.
	D3D11_BLEND_DESC blend{};
	{
		auto& rt = blend.RenderTarget[0];
		rt.BlendEnable           = TRUE;
		rt.SrcBlend              = D3D11_BLEND_SRC_ALPHA;
		rt.DestBlend             = D3D11_BLEND_INV_SRC_ALPHA;
		rt.BlendOp               = D3D11_BLEND_OP_ADD;
		rt.SrcBlendAlpha         = D3D11_BLEND_ONE;
		rt.DestBlendAlpha        = D3D11_BLEND_INV_SRC_ALPHA;
		rt.BlendOpAlpha          = D3D11_BLEND_OP_ADD;
		rt.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
	}

	// 밉맵 + 비등방성 필터링 (비스듬히 볼 때도 선명하게)
	D3D11_SAMPLER_DESC sampler{};
	{
		sampler.Filter         = D3D11_FILTER_ANISOTROPIC;
		sampler.AddressU       = D3D11_TEXTURE_ADDRESS_WRAP;
		sampler.AddressV       = D3D11_TEXTURE_ADDRESS_WRAP;
		sampler.AddressW       = D3D11_TEXTURE_ADDRESS_WRAP;
		sampler.MaxAnisotropy  = 8;
		sampler.ComparisonFunc = D3D11_COMPARISON_NEVER;
		sampler.MaxLOD         = D3D11_FLOAT32_MAX;
	}

	if (FAILED(device->CreateRasterizerState(&rasterizer, _rasterizerState.GetAddressOf())) ||
		FAILED(device->CreateDepthStencilState(&depth, _depthState.GetAddressOf())) ||
		FAILED(device->CreateBlendState(&blend, _blendState.GetAddressOf())) ||
		FAILED(device->CreateSamplerState(&sampler, _samplerState.GetAddressOf())))
	{
		EngineLog("[Renderer] Failed to create pipeline states");
		return false;
	}

	return true;
}

template <typename T>
void Renderer11::UpdateConstantBuffer(ID3D11Buffer* buffer, const T& data)
{
	ID3D11DeviceContext* context = _device.GetContext();

	D3D11_MAPPED_SUBRESOURCE mapped{};
	if (SUCCEEDED(context->Map(buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
	{
		std::memcpy(mapped.pData, &data, sizeof(T));
		context->Unmap(buffer, 0);
	}
}

Renderer11::CameraMatrices Renderer11::ResolveSceneCamera(const Scene* scene, f32 aspect)
{
	CameraMatrices result;
	result.view       = matx::CreateTranslation(0.0f, 0.0f, 5.0f); // 카메라 (0,0,-5)의 역행렬
	result.projection = DirectX::XMMatrixPerspectiveFovLH(DirectX::XMConvertToRadians(60.0f), aspect, 0.1f, 1000.0f);

	if (scene == nullptr)
	{
		return result;
	}

	for (const auto& object : scene->GetGameObjects())
	{
		if (object == nullptr || !object->IsActive() || object->IsPendingDestroy())
		{
			continue;
		}

		const Camera* camera = object->GetComponent<Camera>();
		if (camera != nullptr && camera->IsEnabled())
		{
			result.view       = camera->GetViewMatrix();
			result.projection = camera->GetProjectionMatrix(aspect);
			break;
		}
	}
	return result;
}

void Renderer11::Render(const Scene* scene, const ResourceManager& resources, u32 width, u32 height, const CameraMatrices* camera)
{
	ID3D11DeviceContext* context = _device.GetContext();
	if (scene == nullptr || context == nullptr || width == 0 || height == 0)
	{
		return;
	}

	const auto& objects = scene->GetGameObjects();

	// 1) 카메라: 지정된 카메라(에디터) 또는 씬의 활성 카메라
	const f32 aspect = static_cast<f32>(width) / static_cast<f32>(height);
	const CameraMatrices matrices = camera != nullptr ? *camera : ResolveSceneCamera(scene, aspect);

	PerFrameConstants frame{};
	frame.viewProjection = matrices.view * matrices.projection;
	UpdateConstantBuffer(_perFrameBuffer.Get(), frame);

	// 2) 프레임 공통 상태 바인딩
	context->RSSetState(_rasterizerState.Get());
	context->OMSetDepthStencilState(_depthState.Get(), 0);
	context->OMSetBlendState(_blendState.Get(), nullptr, 0xFFFFFFFF);

	ID3D11Buffer* constantBuffers[] = { _perFrameBuffer.Get(), _perObjectBuffer.Get() };
	context->VSSetConstantBuffers(0, 2, constantBuffers);
	context->PSSetSamplers(0, 1, _samplerState.GetAddressOf());

	// 3) 드로우: 이전과 같은 리소스는 다시 바인딩하지 않는다
	const Shader*  boundShader  = nullptr;
	const Texture* boundTexture = nullptr;
	const Mesh*    boundMesh    = nullptr;
	bool           textureBound = false; // 첫 드로우는 null 텍스처라도 반드시 바인딩 (이전 프레임 SRV가 남지 않도록)

	for (const auto& object : objects)
	{
		if (object == nullptr || !object->IsActive() || object->IsPendingDestroy() || object->transform == nullptr)
		{
			continue;
		}

		const MeshRenderer* renderer = object->GetComponent<MeshRenderer>();
		if (renderer == nullptr || !renderer->IsEnabled())
		{
			continue;
		}

		// 핸들 → 실제 리소스. 삭제된 리소스면 nullptr이므로 건너뛴다.
		const Mesh*     mesh     = resources.Get(renderer->GetMesh());
		const Material* material = resources.Get(renderer->GetMaterial());
		const Shader*   shader   = material != nullptr ? resources.Get(material->shader) : nullptr;
		if (mesh == nullptr || shader == nullptr)
		{
			continue;
		}

		// 텍스처가 없거나 삭제됐으면 SRV를 null로 바인딩한다 (셰이더 샘플 결과는 0)
		const Texture* texture = resources.Get(material->texture);

		PerObjectConstants perObject{};
		perObject.world = object->transform->GetWorldMatrix();
		UpdateConstantBuffer(_perObjectBuffer.Get(), perObject);

		if (shader != boundShader)
		{
			boundShader = shader;
			context->IASetInputLayout(boundShader->inputLayout.Get());
			context->VSSetShader(boundShader->vertexShader.Get(), nullptr, 0);
			context->PSSetShader(boundShader->pixelShader.Get(), nullptr, 0);
		}

		if (!textureBound || texture != boundTexture)
		{
			textureBound = true;
			boundTexture = texture;
			ID3D11ShaderResourceView* srv = boundTexture != nullptr ? boundTexture->srv.Get() : nullptr;
			context->PSSetShaderResources(0, 1, &srv);
		}

		if (mesh != boundMesh)
		{
			boundMesh = mesh;
			const UINT stride = sizeof(Vertex);
			const UINT offset = 0;
			context->IASetPrimitiveTopology(mesh->topology);
			context->IASetVertexBuffers(0, 1, mesh->vertexBuffer.GetAddressOf(), &stride, &offset);
			context->IASetIndexBuffer(mesh->indexBuffer.Get(), DXGI_FORMAT_R16_UINT, 0);
		}

		context->DrawIndexed(mesh->indexCount, 0, 0);
	}
}
