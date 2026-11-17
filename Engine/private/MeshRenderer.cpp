#include "pch.h"
#include "MeshRenderer.h"
#include "ComponentRegistry.h"

MeshRenderer::MeshRenderer() = default;

MeshRenderer::MeshRenderer(MeshHandle mesh, MaterialHandle material)
	: _mesh(mesh)
	, _material(material)
{
}

MeshRenderer::~MeshRenderer() = default;

MeshRenderer* MeshRenderer::CreateInstance()
{
	return new MeshRenderer();
}

MeshRenderer* MeshRenderer::CreateInstance(MeshHandle mesh, MaterialHandle material)
{
	return new MeshRenderer(mesh, material);
}

MeshHandle MeshRenderer::GetMesh() const
{
	return _mesh;
}

MaterialHandle MeshRenderer::GetMaterial() const
{
	return _material;
}

void MeshRenderer::SetMesh(MeshHandle mesh)
{
	_mesh = mesh;
}

void MeshRenderer::SetMaterial(MaterialHandle material)
{
	_material = material;
}

void MeshRenderer::Reflect(TypeBuilder<MeshRenderer>& type)
{
	type.Field("mesh", &MeshRenderer::_mesh)
		.Field("material", &MeshRenderer::_material);
}
