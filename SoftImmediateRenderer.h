#pragma once

#include <SoftRender/SoftRender.h>
#include <SoftRender/SoftRender_Internal.h>

namespace SoftRenderer
{

class srImmediateRenderer : public ATriangleRenderer
	, SingleInstance<srImmediateRenderer>
{
	// current states
	float4x4	m_worldMatrix;
	float4x4	m_viewMatrix;
	float4x4	m_projectionMatrix;

	F_VertexShader *	m_vertexShader;
	F_PixelShader *		m_pixelShader;

	ECullMode	m_cullMode;
	EFillMode	m_fillMode;

	TPtr< SoftTexture2D >	m_texture;

	F_RenderTriangles *			m_ftblProcessTriangles[Fill_MAX][Cull_MAX];
	F_RenderSingleTriangle *	m_ftblDrawTriangle[Fill_MAX];

public:
	srImmediateRenderer();
	~srImmediateRenderer();

	void SetWorldMatrix( const float4x4& newWorldMatrix ) override;
	void SetViewMatrix( const float4x4& newViewMatrix ) override;
	void SetProjectionMatrix( const float4x4& newProjectionMatrix ) override;

	void SetCullMode( ECullMode newCullMode ) override;
	void SetFillMode( EFillMode newFillMode ) override;

	void SetVertexShader( F_VertexShader* newVertexShader ) override;
	void SetPixelShader( F_PixelShader* newPixelShader ) override;

	void SetTexture( SoftTexture2D* newTexture2D ) override;

	void DrawTriangles( SoftFrameBuffer& frameBuffer, const SVertex* vertices, UINT numVertices, const SIndex* indices, UINT numIndices ) override;
};

}//namespace SoftRenderer
