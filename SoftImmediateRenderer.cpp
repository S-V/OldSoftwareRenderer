#include "SoftRender_PCH.h"
#pragma hdrstop
#include "SoftMesh.h"
#include "SoftFrameBuffer.h"
#include "TriangleClipping.inl"
#include "SoftImmediateRenderer.h"
#include "Rasterizer_FPU.inl"
#include "Rasterizer_SSE.inl"

namespace SoftRenderer
{

static
void F_DrawSolidTriangle( const XVertex& v1, const XVertex& v2, const XVertex& v3, const SoftRenderContext& context )
{
	RasterizeTriangle_FPU( v1, v2, v3, context );
}

srImmediateRenderer::srImmediateRenderer()
{
	m_worldMatrix = XMMatrixIdentity();
	m_viewMatrix = XMMatrixIdentity();
	m_projectionMatrix = XMMatrixIdentity();

	m_vertexShader = nil;
	m_pixelShader = nil;

	m_cullMode = ECullMode::Cull_CCW;
	m_fillMode = EFillMode::Fill_Solid;


	//-----------------------------------------------------------------

	ZERO_OUT(m_ftblProcessTriangles);
	ZERO_OUT(m_ftblDrawTriangle);

#define INSTALL_PROCESS_TRIANGLES_FUNC( CULL, FILL )\
	m_ftblProcessTriangles[CULL][FILL] = &Template_ProcessTriangles<CULL,FILL>

#define INSTALL_PROCESS_TRIANGLES_FUNC_CULL( CULL )\
	m_ftblProcessTriangles[Fill_Solid][CULL] = &Template_ProcessTriangles<Fill_Solid,CULL>;\
	m_ftblProcessTriangles[Fill_Wireframe][CULL] = &Template_ProcessTriangles<Fill_Wireframe,CULL>;\



	INSTALL_PROCESS_TRIANGLES_FUNC_CULL( Cull_None );
	INSTALL_PROCESS_TRIANGLES_FUNC_CULL( Cull_CW );
	INSTALL_PROCESS_TRIANGLES_FUNC_CULL( Cull_CCW );



#undef INSTALL_PROCESS_TRIANGLES_FUNC_CULL
#undef INSTALL_PROCESS_TRIANGLES_FUNC


	//-----------------------------------------------------------------


	//m_ftblDrawTriangle[Fill_Solid] = &RasterizeTriangleImmediateDepthOnly_SSE<16,8>;//<= BEST
	//m_ftblDrawTriangle[Fill_Solid] = &RasterizeTriangleImmediateColorOnly_SSE<32,8>;//<= GOOD
	//m_ftblDrawTriangle[Fill_Solid] = &RasterizeTriangleImmediateColorOnly_SSE<16,16>;
	//m_ftblDrawTriangle[Fill_Solid] = &RasterizeTriangleImmediateUserShader_SSE<16,4>;
	m_ftblDrawTriangle[Fill_Solid] = &RasterizeTriangleImmediateUserShader_SSE<16,8>;

	m_ftblDrawTriangle[Fill_Wireframe] = &F_DrawWireframeTriangle;
}

srImmediateRenderer::~srImmediateRenderer()
{

}

void srImmediateRenderer::SetWorldMatrix( const float4x4& newWorldMatrix )
{
	m_worldMatrix = newWorldMatrix;
}

void srImmediateRenderer::SetViewMatrix( const float4x4& newViewMatrix )
{
	m_viewMatrix = newViewMatrix;
}

void srImmediateRenderer::SetProjectionMatrix( const float4x4& newProjectionMatrix )
{
	m_projectionMatrix = newProjectionMatrix;
}

void srImmediateRenderer::SetCullMode( ECullMode newCullMode )
{
	m_cullMode = newCullMode;
}

void srImmediateRenderer::SetFillMode( EFillMode newFillMode )
{
	m_fillMode = newFillMode;
}

void srImmediateRenderer::SetVertexShader( F_VertexShader* newVertexShader )
{
	m_vertexShader = newVertexShader;
}

void srImmediateRenderer::SetPixelShader( F_PixelShader* newPixelShader )
{
	m_pixelShader = newPixelShader;
}

void srImmediateRenderer::SetTexture( SoftTexture2D* newTexture2D )
{
	m_texture = newTexture2D;
}

void srImmediateRenderer::DrawTriangles( SoftFrameBuffer& frameBuffer, const SVertex* vertices, UINT numVertices, const SIndex* indices, UINT numIndices )
{
	CHK_VRET_IF_NIL(m_vertexShader);
	CHK_VRET_IF_NIL(m_pixelShader);

	mxPROFILE_SCOPE("Draw Triangles");


	//const float4x4	worldViewMatrix = XMMatrixMultiply( m_worldMatrix, m_viewMatrix );
	const float4x4  viewProjectionMatrix = XMMatrixMultiply( m_viewMatrix, m_projectionMatrix );
	const float4x4	worldViewProjectionMatrix = XMMatrixMultiply( m_worldMatrix, viewProjectionMatrix );

	ShaderGlobals	shaderGlobals;
	shaderGlobals.worldMatrix = m_worldMatrix;
	//shaderGlobals.viewMatrix = m_viewMatrix;
	//shaderGlobals.projectionMatrix = m_projectionMatrix;
	shaderGlobals.WVP = worldViewProjectionMatrix;
	shaderGlobals.texture = m_texture;

	SoftRenderContext	renderContext;
	renderContext.globals = &shaderGlobals;
	renderContext.vertexShader = m_vertexShader;
	renderContext.pixelShader = m_pixelShader;
	renderContext.colorBuffer = frameBuffer.m_colorBuffer;
	renderContext.depthBuffer = frameBuffer.m_depthBuffer;
	renderContext.userPointer = nil;

	renderContext.W = frameBuffer.m_viewportWidth;
	renderContext.H = frameBuffer.m_viewportHeight;
	renderContext.W2 = frameBuffer.m_viewportWidth * 0.5f;
	renderContext.H2 = frameBuffer.m_viewportHeight * 0.5f;



	F_RenderSingleTriangle* drawTriangleFunction = m_ftblDrawTriangle[m_fillMode];

	(*m_ftblProcessTriangles[m_fillMode][m_cullMode])( drawTriangleFunction, vertices, numVertices, indices, numIndices, renderContext );
}

}//namespace SoftRenderer
