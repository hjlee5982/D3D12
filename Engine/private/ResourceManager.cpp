#include "pch.h"
#include "ResourceManager.h"
#include "RenderResources.h"
#include "ResourcePool.h"
#include "Log.h"

#include <filesystem>
#include <unordered_map>

namespace
{
	std::wstring ToWide(const std::string& utf8)
	{
		if (utf8.empty())
		{
			return {};
		}
		const int size = ::MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.size()), nullptr, 0);
		std::wstring result(static_cast<size_t>(size), L'\0');
		::MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.size()), result.data(), size);
		return result;
	}

	std::string ToUtf8Path(const std::wstring& wide)
	{
		if (wide.empty())
		{
			return {};
		}
		const int size = ::WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr);
		std::string result(static_cast<size_t>(size), '\0');
		::WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), static_cast<int>(wide.size()), result.data(), size, nullptr, nullptr);
		return result;
	}

	// Client.exe 폴더에서 위로 올라가며 Assets 폴더를 찾는다 (Bin\x64\Debug → 솔루션 루트)
	std::string FindAssetRoot()
	{
		WCHAR exePath[MAX_PATH]{};
		::GetModuleFileNameW(nullptr, exePath, MAX_PATH);

		std::error_code ec;
		std::filesystem::path dir = std::filesystem::path(exePath).parent_path();
		for (int depth = 0; depth < 6 && !dir.empty(); ++depth)
		{
			const std::filesystem::path candidate = dir / L"Assets";
			if (std::filesystem::is_directory(candidate, ec))
			{
				const std::wstring wide = (candidate / L"").wstring();
				const int size = ::WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr);
				std::string utf8(static_cast<size_t>(size), '\0');
				::WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), static_cast<int>(wide.size()), utf8.data(), size, nullptr, nullptr);
				return utf8;
			}

			if (dir == dir.parent_path())
			{
				break;
			}
			dir = dir.parent_path();
		}

		return {};
	}

	u32 CalculateMipCount(u32 width, u32 height)
	{
		u32 count = 1;
		while (width > 1 || height > 1)
		{
			width  = width  > 1 ? width  / 2 : 1;
			height = height > 1 ? height / 2 : 1;
			++count;
		}
		return count;
	}

	// WIC(Windows Imaging Component)로 PNG/JPG/BMP 등을 RGBA8로 디코딩해 밉맵이 있는 텍스처를 만든다.
	// 외부 라이브러리(DirectXTex) 없이 OS 기본 기능만 사용한다.
	bool LoadTextureWIC(ID3D11Device* device, const std::wstring& path, Texture& out)
	{
		ComPtr<IWICImagingFactory> factory;
		if (FAILED(::CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(factory.GetAddressOf()))))
		{
			EngineLog("[Resource] WIC factory creation failed (CoInitialize missing?)");
			return false;
		}

		ComPtr<IWICBitmapDecoder> decoder;
		if (FAILED(factory->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, decoder.GetAddressOf())))
		{
			return false;
		}

		ComPtr<IWICBitmapFrameDecode> frame;
		if (FAILED(decoder->GetFrame(0, frame.GetAddressOf())))
		{
			return false;
		}

		// 팔레트/24bit 등 어떤 원본 형식이든 32bpp RGBA로 변환
		ComPtr<IWICFormatConverter> converter;
		if (FAILED(factory->CreateFormatConverter(converter.GetAddressOf())) ||
			FAILED(converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom)))
		{
			return false;
		}

		UINT width = 0, height = 0;
		converter->GetSize(&width, &height);
		if (width == 0 || height == 0)
		{
			return false;
		}

		const UINT rowPitch = width * 4;
		std::vector<u8> pixels(static_cast<size_t>(rowPitch) * height);
		if (FAILED(converter->CopyPixels(nullptr, rowPitch, static_cast<UINT>(pixels.size()), pixels.data())))
		{
			return false;
		}

		// 밉맵 자동 생성을 위해 RENDER_TARGET 바인드 + GENERATE_MIPS 플래그 사용
		D3D11_TEXTURE2D_DESC desc{};
		{
			desc.Width            = width;
			desc.Height           = height;
			desc.MipLevels        = CalculateMipCount(width, height);
			desc.ArraySize        = 1;
			desc.Format           = DXGI_FORMAT_R8G8B8A8_UNORM;
			desc.SampleDesc.Count = 1;
			desc.Usage            = D3D11_USAGE_DEFAULT;
			desc.BindFlags        = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
			desc.MiscFlags        = D3D11_RESOURCE_MISC_GENERATE_MIPS;
		}

		ComPtr<ID3D11Texture2D> texture;
		if (FAILED(device->CreateTexture2D(&desc, nullptr, texture.GetAddressOf())))
		{
			return false;
		}

		if (FAILED(device->CreateShaderResourceView(texture.Get(), nullptr, out.srv.GetAddressOf())))
		{
			return false;
		}

		ComPtr<ID3D11DeviceContext> context;
		device->GetImmediateContext(context.GetAddressOf());
		context->UpdateSubresource(texture.Get(), 0, nullptr, pixels.data(), rowPitch, 0);
		context->GenerateMips(out.srv.Get());

		out.width  = width;
		out.height = height;
		return true;
	}

	// 다른 프로그램이 쓰는 중이어도 읽을 수 있도록 공유 모드로 연다. 실패하면 false.
	bool ReadFileBytes(const std::wstring& path, std::string& out)
	{
		const HANDLE file = ::CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
			nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
		if (file == INVALID_HANDLE_VALUE)
		{
			return false;
		}

		LARGE_INTEGER size{};
		bool ok = ::GetFileSizeEx(file, &size) && size.QuadPart < (64ll << 20);
		if (ok)
		{
			out.resize(static_cast<size_t>(size.QuadPart));
			DWORD read = 0;
			ok = out.empty() || (::ReadFile(file, out.data(), static_cast<DWORD>(out.size()), &read, nullptr) && read == out.size());
		}

		::CloseHandle(file);
		return ok;
	}

	// source: 셰이더 소스 (UTF-8 BOM은 호출 전에 제거), sourceName: 오류 메시지와 #include 기준 경로
	ComPtr<ID3DBlob> CompileShader(const std::string& source, const std::string& sourceName, const char* entry, const char* target)
	{
		UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
#if defined(_DEBUG)
		flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
		flags |= D3DCOMPILE_OPTIMIZATION_LEVEL3;
#endif

		ComPtr<ID3DBlob> byteCode;
		ComPtr<ID3DBlob> errors;
		const HRESULT hr = ::D3DCompile(source.data(), source.size(), sourceName.c_str(), nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE,
			entry, target, flags, 0, byteCode.GetAddressOf(), errors.GetAddressOf());

		// 경고/오류 메시지를 반드시 로그로 남긴다 (참고 프로젝트는 errorBlob을 버렸다)
		if (errors != nullptr && errors->GetBufferSize() > 0)
		{
			EngineLog("[Shader] %s (%s): %s", entry, target, static_cast<const char*>(errors->GetBufferPointer()));
		}

		if (FAILED(hr))
		{
			return nullptr;
		}
		return byteCode;
	}

	u64 GetFileWriteTime(const std::wstring& path)
	{
		WIN32_FILE_ATTRIBUTE_DATA data{};
		if (!::GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data))
		{
			return 0;
		}
		return (static_cast<u64>(data.ftLastWriteTime.dwHighDateTime) << 32) | data.ftLastWriteTime.dwLowDateTime;
	}

	// 한 파일에 VSMain / PSMain 진입점을 둔다. 실패하면 out은 건드리지 않는다.
	bool BuildShader(ID3D11Device* device, const std::wstring& path, Shader& out)
	{
		// D3DCompileFromFile은 UTF-8 BOM을 "Illegal character"로 거부한다.
		// Visual Studio 등은 BOM을 붙여 저장하는 경우가 많으므로 직접 읽어 BOM을 제거한 뒤 컴파일한다.
		std::string source;
		if (!ReadFileBytes(path, source))
		{
			EngineLog("[Resource] Cannot read shader file");
			return false;
		}
		if (source.size() >= 3 && static_cast<u8>(source[0]) == 0xEF && static_cast<u8>(source[1]) == 0xBB && static_cast<u8>(source[2]) == 0xBF)
		{
			source.erase(0, 3);
		}

		const std::string sourceName = ToUtf8Path(path);
		ComPtr<ID3DBlob> vsCode = CompileShader(source, sourceName, "VSMain", "vs_5_0");
		ComPtr<ID3DBlob> psCode = CompileShader(source, sourceName, "PSMain", "ps_5_0");
		if (vsCode == nullptr || psCode == nullptr)
		{
			return false;
		}

		ComPtr<ID3D11VertexShader> vertexShader;
		ComPtr<ID3D11PixelShader>  pixelShader;
		ComPtr<ID3D11InputLayout>  inputLayout;
		if (FAILED(device->CreateVertexShader(vsCode->GetBufferPointer(), vsCode->GetBufferSize(), nullptr, vertexShader.GetAddressOf())) ||
			FAILED(device->CreatePixelShader(psCode->GetBufferPointer(), psCode->GetBufferSize(), nullptr, pixelShader.GetAddressOf())) ||
			FAILED(device->CreateInputLayout(kVertexLayout, static_cast<UINT>(std::size(kVertexLayout)), vsCode->GetBufferPointer(), vsCode->GetBufferSize(), inputLayout.GetAddressOf())))
		{
			EngineLog("[Resource] Failed to create shader objects");
			return false;
		}

		out.vertexShader = std::move(vertexShader);
		out.pixelShader  = std::move(pixelShader);
		out.inputLayout  = std::move(inputLayout);
		return true;
	}
}

struct ResourceManager::Impl
{
	ID3D11Device* device = nullptr;
	std::string   assetRoot;

	ResourcePool<Mesh>     meshes;
	ResourcePool<Texture>  textures;
	ResourcePool<Shader>   shaders;
	ResourcePool<Material> materials;

	// 경로/이름 → 핸들 (중복 로드 방지)
	std::unordered_map<std::string, TextureHandle>  texturesByPath;
	std::unordered_map<std::string, ShaderHandle>   shadersByPath;
	std::unordered_map<std::string, MaterialHandle> materialsByName;
	MeshHandle quadMesh;

	bool fileWatchEnabled = true;
	u64  lastPollTick     = 0;
};

namespace
{
	template <typename Map, typename H>
	void EraseByHandle(Map& map, H handle)
	{
		for (auto it = map.begin(); it != map.end(); ++it)
		{
			if (it->second == handle)
			{
				map.erase(it);
				return;
			}
		}
	}

	template <typename Map>
	typename Map::mapped_type FindIn(const Map& map, const std::string& key)
	{
		const auto it = map.find(key);
		return it != map.end() ? it->second : typename Map::mapped_type{};
	}
}

ResourceManager::ResourceManager(ID3D11Device* device)
	: _impl(std::make_unique<Impl>())
{
	_impl->device    = device;
	_impl->assetRoot = FindAssetRoot();

	if (_impl->assetRoot.empty())
	{
		EngineLog("[Resource] Assets folder not found above the executable.");
	}
	else
	{
		EngineLog("[Resource] Asset root: %s", _impl->assetRoot.c_str());
	}
}

ResourceManager::~ResourceManager() = default;

TextureHandle ResourceManager::LoadTexture(const std::string& assetPath)
{
	if (const TextureHandle existing = FindTexture(assetPath))
	{
		return existing;
	}

	auto texture = std::make_unique<Texture>();
	texture->path = assetPath;

	const std::wstring path = ToWide(ResolveAssetPath(assetPath));
	texture->fileTime = GetFileWriteTime(path);

	if (!LoadTextureWIC(_impl->device, path, *texture))
	{
		EngineLog("[Resource] Failed to load texture: %s", ResolveAssetPath(assetPath).c_str());
		return {};
	}

	EngineLog("[Resource] Texture loaded: %s (%ux%u)", assetPath.c_str(), texture->width, texture->height);

	const TextureHandle handle = _impl->textures.Add(std::move(texture));
	_impl->texturesByPath.emplace(assetPath, handle);
	return handle;
}

ShaderHandle ResourceManager::LoadShader(const std::string& assetPath)
{
	if (const ShaderHandle existing = FindShader(assetPath))
	{
		return existing;
	}

	auto shader = std::make_unique<Shader>();
	shader->path = assetPath;

	const std::wstring path = ToWide(ResolveAssetPath(assetPath));
	shader->fileTime = GetFileWriteTime(path);

	if (!BuildShader(_impl->device, path, *shader))
	{
		EngineLog("[Resource] Failed to load shader: %s", ResolveAssetPath(assetPath).c_str());
		return {};
	}

	EngineLog("[Resource] Shader loaded: %s", assetPath.c_str());

	const ShaderHandle handle = _impl->shaders.Add(std::move(shader));
	_impl->shadersByPath.emplace(assetPath, handle);
	return handle;
}

MeshHandle ResourceManager::GetQuadMesh()
{
	if (_impl->meshes.Get(_impl->quadMesh) != nullptr)
	{
		return _impl->quadMesh;
	}

	// 왼손 좌표계에서 -Z 방향(카메라 쪽)에서 볼 때 시계 방향 = 앞면
	// UV는 왼쪽 위가 (0, 0)
	const Vertex vertices[] =
	{
		{ vec3(-0.5f,  0.5f, 0.0f), vec2(0.0f, 0.0f) },
		{ vec3( 0.5f,  0.5f, 0.0f), vec2(1.0f, 0.0f) },
		{ vec3( 0.5f, -0.5f, 0.0f), vec2(1.0f, 1.0f) },
		{ vec3(-0.5f, -0.5f, 0.0f), vec2(0.0f, 1.0f) },
	};
	const u16 indices[] = { 0, 1, 2, 0, 2, 3 };

	auto mesh = std::make_unique<Mesh>();

	D3D11_BUFFER_DESC vbDesc{};
	vbDesc.ByteWidth = sizeof(vertices);
	vbDesc.Usage     = D3D11_USAGE_IMMUTABLE;
	vbDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
	D3D11_SUBRESOURCE_DATA vbData{ vertices };

	D3D11_BUFFER_DESC ibDesc{};
	ibDesc.ByteWidth = sizeof(indices);
	ibDesc.Usage     = D3D11_USAGE_IMMUTABLE;
	ibDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;
	D3D11_SUBRESOURCE_DATA ibData{ indices };

	if (FAILED(_impl->device->CreateBuffer(&vbDesc, &vbData, mesh->vertexBuffer.GetAddressOf())) ||
		FAILED(_impl->device->CreateBuffer(&ibDesc, &ibData, mesh->indexBuffer.GetAddressOf())))
	{
		EngineLog("[Resource] Failed to create quad mesh buffers");
		return {};
	}

	mesh->name       = "Quad";
	mesh->indexCount = static_cast<u32>(std::size(indices));
	mesh->indices.assign(std::begin(indices), std::end(indices));
	for (const Vertex& vertex : vertices)
	{
		mesh->positions.push_back(vertex.position);
	}
	mesh->boundsMin = vec3(-0.5f, -0.5f, 0.0f);
	mesh->boundsMax = vec3( 0.5f,  0.5f, 0.0f);
	_impl->quadMesh = _impl->meshes.Add(std::move(mesh));
	return _impl->quadMesh;
}

MaterialHandle ResourceManager::CreateMaterial(const std::string& name, ShaderHandle shader, TextureHandle texture)
{
	if (const MaterialHandle existing = FindMaterial(name))
	{
		return existing;
	}

	auto material = std::make_unique<Material>();
	material->name    = name;
	material->shader  = shader;
	material->texture = texture;

	const MaterialHandle handle = _impl->materials.Add(std::move(material));
	_impl->materialsByName.emplace(name, handle);
	return handle;
}

TextureHandle ResourceManager::FindTexture(const std::string& assetPath) const
{
	return FindIn(_impl->texturesByPath, assetPath);
}

ShaderHandle ResourceManager::FindShader(const std::string& assetPath) const
{
	return FindIn(_impl->shadersByPath, assetPath);
}

MaterialHandle ResourceManager::FindMaterial(const std::string& name) const
{
	return FindIn(_impl->materialsByName, name);
}

MeshHandle ResourceManager::FindMesh(const std::string& name)
{
	MeshHandle found;
	_impl->meshes.ForEach([&](MeshHandle handle, const Mesh& mesh)
	{
		if (!found && mesh.name == name)
		{
			found = handle;
		}
	});

	if (!found && name == "Quad")
	{
		found = GetQuadMesh();
	}
	return found;
}

Mesh*     ResourceManager::Get(MeshHandle handle) const     { return _impl->meshes.Get(handle); }
Texture*  ResourceManager::Get(TextureHandle handle) const  { return _impl->textures.Get(handle); }
Shader*   ResourceManager::Get(ShaderHandle handle) const   { return _impl->shaders.Get(handle); }
Material* ResourceManager::Get(MaterialHandle handle) const { return _impl->materials.Get(handle); }

template <typename H, typename Pool, typename NameOf>
static std::vector<ResourceEntry<H>> ListPool(const Pool& pool, NameOf nameOf)
{
	std::vector<ResourceEntry<H>> entries;
	pool.ForEach([&](H handle, const auto& resource) { entries.push_back(ResourceEntry<H>{ handle, nameOf(resource) }); });
	return entries;
}

std::vector<ResourceEntry<MeshHandle>> ResourceManager::ListMeshes() const
{
	return ListPool<MeshHandle>(_impl->meshes, [](const Mesh& m) { return m.name; });
}

std::vector<ResourceEntry<TextureHandle>> ResourceManager::ListTextures() const
{
	return ListPool<TextureHandle>(_impl->textures, [](const Texture& t) { return t.path; });
}

std::vector<ResourceEntry<ShaderHandle>> ResourceManager::ListShaders() const
{
	return ListPool<ShaderHandle>(_impl->shaders, [](const Shader& s) { return s.path; });
}

std::vector<ResourceEntry<MaterialHandle>> ResourceManager::ListMaterials() const
{
	return ListPool<MaterialHandle>(_impl->materials, [](const Material& m) { return m.name; });
}

ShaderHandle ResourceManager::GetMaterialShader(MaterialHandle material) const
{
	const Material* target = Get(material);
	return target != nullptr ? target->shader : ShaderHandle{};
}

TextureHandle ResourceManager::GetMaterialTexture(MaterialHandle material) const
{
	const Material* target = Get(material);
	return target != nullptr ? target->texture : TextureHandle{};
}

bool ResourceManager::ReloadTexture(TextureHandle handle)
{
	Texture* texture = Get(handle);
	if (texture == nullptr)
	{
		return false;
	}

	const std::wstring path = ToWide(ResolveAssetPath(texture->path));

	Texture loaded;
	if (!LoadTextureWIC(_impl->device, path, loaded))
	{
		EngineLog("[Resource] Texture reload failed, keeping previous version: %s", texture->path.c_str());
		return false;
	}

	// 핸들과 Texture 객체는 그대로, GPU 리소스만 교체 → 이 텍스처를 쓰는 모든 머티리얼에 즉시 반영
	texture->srv      = std::move(loaded.srv);
	texture->width    = loaded.width;
	texture->height   = loaded.height;
	texture->fileTime = GetFileWriteTime(path);

	EngineLog("[Resource] Texture reloaded: %s (%ux%u)", texture->path.c_str(), texture->width, texture->height);
	return true;
}

bool ResourceManager::ReloadShader(ShaderHandle handle)
{
	Shader* shader = Get(handle);
	if (shader == nullptr)
	{
		return false;
	}

	const std::wstring path = ToWide(ResolveAssetPath(shader->path));
	shader->fileTime = GetFileWriteTime(path); // 컴파일 오류여도 같은 저장본을 계속 다시 시도하지 않도록

	// 컴파일 오류면 BuildShader가 shader를 건드리지 않으므로 이전 셰이더로 계속 그린다
	if (!BuildShader(_impl->device, path, *shader))
	{
		EngineLog("[Resource] Shader reload failed, keeping previous version: %s", shader->path.c_str());
		return false;
	}

	EngineLog("[Resource] Shader reloaded: %s", shader->path.c_str());
	return true;
}

void ResourceManager::PollFileChanges()
{
	if (!_impl->fileWatchEnabled)
	{
		return;
	}

	const u64 now = ::GetTickCount64();
	if (now - _impl->lastPollTick < 250)
	{
		return;
	}
	_impl->lastPollTick = now;

	// 파일 시간이 바뀌면 바로 읽지 않고, 다음 검사에서도 같은 값이면(저장이 끝났으면) 다시 로드한다
	auto check = [this](const std::string& assetPath, u64& fileTime, u64& pendingFileTime) -> bool
	{
		const u64 current = GetFileWriteTime(ToWide(ResolveAssetPath(assetPath)));
		if (current == 0 || current == fileTime)
		{
			pendingFileTime = 0;
			return false;
		}

		if (current != pendingFileTime)
		{
			pendingFileTime = current;
			return false;
		}

		pendingFileTime = 0;
		return true;
	};

	std::vector<TextureHandle> changedTextures;
	_impl->textures.ForEach([&](TextureHandle handle, Texture& texture)
	{
		if (check(texture.path, texture.fileTime, texture.pendingFileTime))
		{
			changedTextures.push_back(handle);
		}
	});

	std::vector<ShaderHandle> changedShaders;
	_impl->shaders.ForEach([&](ShaderHandle handle, Shader& shader)
	{
		if (check(shader.path, shader.fileTime, shader.pendingFileTime))
		{
			changedShaders.push_back(handle);
		}
	});

	for (const TextureHandle handle : changedTextures)
	{
		if (!ReloadTexture(handle))
		{
			// 실패해도 같은 저장본을 매번 다시 시도하지 않는다 (다음 저장 때 다시 시도)
			if (Texture* texture = Get(handle))
			{
				texture->fileTime = GetFileWriteTime(ToWide(ResolveAssetPath(texture->path)));
			}
		}
	}

	for (const ShaderHandle handle : changedShaders)
	{
		ReloadShader(handle);
	}
}

void ResourceManager::SetFileWatchEnabled(bool enabled)
{
	_impl->fileWatchEnabled = enabled;
}

bool ResourceManager::IsFileWatchEnabled() const
{
	return _impl->fileWatchEnabled;
}

bool ResourceManager::SetMaterialShader(MaterialHandle material, ShaderHandle shader)
{
	Material* target = Get(material);
	if (target == nullptr)
	{
		return false;
	}
	target->shader = shader;
	return true;
}

bool ResourceManager::SetMaterialTexture(MaterialHandle material, TextureHandle texture)
{
	Material* target = Get(material);
	if (target == nullptr)
	{
		return false;
	}
	target->texture = texture;
	return true;
}

// Destroy 직후 GPU 객체를 바로 해제해도 안전하다: D3D11 런타임은 바인딩 중이거나 제출된 명령이
// 참조하는 리소스를 GPU 사용이 끝날 때까지 내부적으로 살려 둔다.
// (D3D12 백엔드에서는 프레임 펜스 기준 지연 해제가 필요하다)
bool ResourceManager::Destroy(MeshHandle handle)
{
	if (handle == _impl->quadMesh)
	{
		_impl->quadMesh = {};
	}
	return _impl->meshes.Remove(handle);
}

bool ResourceManager::Destroy(TextureHandle handle)
{
	if (const Texture* texture = Get(handle))
	{
		EngineLog("[Resource] Texture destroyed: %s", texture->path.c_str());
	}
	EraseByHandle(_impl->texturesByPath, handle);
	return _impl->textures.Remove(handle);
}

bool ResourceManager::Destroy(ShaderHandle handle)
{
	if (const Shader* shader = Get(handle))
	{
		EngineLog("[Resource] Shader destroyed: %s", shader->path.c_str());
	}
	EraseByHandle(_impl->shadersByPath, handle);
	return _impl->shaders.Remove(handle);
}

bool ResourceManager::Destroy(MaterialHandle handle)
{
	if (const Material* material = Get(handle))
	{
		EngineLog("[Resource] Material destroyed: %s", material->name.c_str());
	}
	EraseByHandle(_impl->materialsByName, handle);
	return _impl->materials.Remove(handle);
}

ResourceManager::Stats ResourceManager::GetStats() const
{
	return Stats{ _impl->meshes.GetCount(), _impl->textures.GetCount(), _impl->shaders.GetCount(), _impl->materials.GetCount() };
}

const std::string& ResourceManager::GetAssetRoot() const
{
	return _impl->assetRoot;
}

std::string ResourceManager::ResolveAssetPath(const std::string& assetPath) const
{
	return _impl->assetRoot + assetPath;
}

void ResourceManager::Clear()
{
	_impl->materials.Clear();
	_impl->meshes.Clear();
	_impl->shaders.Clear();
	_impl->textures.Clear();

	_impl->materialsByName.clear();
	_impl->shadersByPath.clear();
	_impl->texturesByPath.clear();
	_impl->quadMesh = {};
}
