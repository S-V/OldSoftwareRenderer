#include "SoftRender_PCH.h"
#pragma hdrstop
#include "SoftRender.h"
#include "SoftRender_Internal.h"
#include "TriangleClipping.inl"
#include "Rasterizer_FPU.inl"
#include "Rasterizer_SSE.inl"
#include "Rasterizer_AVX.inl"

#if 0
namespace SoftRenderer
{

static F_RenderTriangles* g_ftblProcessTriangles[Fill_MAX][Cull_MAX];
static F_RenderSingleTriangle* g_ftblDrawTriangle[Fill_MAX];


void F_CacheTransformedFace( const XVertex& v1, const XVertex& v2, const XVertex& v3, const SoftRenderContext& context )
{
	mxSTATIC_ASSERT_ISPOW2( FACE_BUFFER_SIZE );
UNDONE;//BUGS
	TriangleProcessing::AddTriangle( v1, v2, v3, context );
}

void F_DrawWireframeTriangle( const XVertex& v1, const XVertex& v2, const XVertex& v3, const SoftRenderContext& context )
{
	context.renderer->DrawWireframeTriangle( v1.P.ToVec2(), v2.P.ToVec2(), v3.P.ToVec2() );
}

void F_DrawSolidTriangle( const XVertex& v1, const XVertex& v2, const XVertex& v3, const SoftRenderContext& context )
{
	RasterizeTriangle_FPU( v1, v2, v3, context );
}

template< EFillMode FILL_MODE, ECullMode CULL_MODE >
static
void Template_ProcessTriangles(
							   F_RenderSingleTriangle* drawTriangleFunction,
							   const SVertex* vertices, UINT numVertices,
							   const SIndex* indices, UINT numIndices,
							   const SoftRenderContext& context
							   )
{
	XVertex	v[ CLIP_BUFFER_SIZE ];

	for( UINT i = 0; i < numIndices; i += 3 )
	{
		// execute vertex shader, get clip-space vertex positions
		TransformVertex( vertices[indices[i+0]], &v[0], context );
		TransformVertex( vertices[indices[i+1]], &v[1], context );
		TransformVertex( vertices[indices[i+2]], &v[2], context );

		ClipTriangle< CULL_MODE, FILL_MODE >( drawTriangleFunction, v, context );
	}//for each triangle
}

void ModifySettings( const Settings& newSettings )
{
	this->Flush();

	//DEVOUT("\tsize of face buffer = %u bytes:\n", sizeof TriangleProcessing::g_transformedFaces);

	m_settings.bImmediateRasterization = newSettings.bImmediateRasterization;


	//-----------------------------------------------------------------


#define INSTALL_PROCESS_TRIANGLES_FUNC( CULL, FILL )\
	g_ftblProcessTriangles[CULL][FILL] = &Template_ProcessTriangles<CULL,FILL>

#define INSTALL_PROCESS_TRIANGLES_FUNC_CULL( CULL )\
	g_ftblProcessTriangles[Fill_Solid][CULL] = &Template_ProcessTriangles<Fill_Solid,CULL>;\
	g_ftblProcessTriangles[Fill_Wireframe][CULL] = &Template_ProcessTriangles<Fill_Wireframe,CULL>;\



	INSTALL_PROCESS_TRIANGLES_FUNC_CULL( Cull_None );
	INSTALL_PROCESS_TRIANGLES_FUNC_CULL( Cull_CW );
	INSTALL_PROCESS_TRIANGLES_FUNC_CULL( Cull_CCW );



#undef INSTALL_PROCESS_TRIANGLES_FUNC_CULL
#undef INSTALL_PROCESS_TRIANGLES_FUNC


	//-----------------------------------------------------------------

	mxSystemInfo		sysInfo;
	mxGetSystemInfo( sysInfo );

	const mxCpuInfo & cpu = sysInfo.cpu;


	ZERO_OUT(g_ftblDrawTriangle[Fill_Solid]);

	if( newSettings.bImmediateRasterization )
	{
		if( newSettings.mode == CpuMode_Use_FPU && cpu.has_FPU ) {
			g_ftblDrawTriangle[Fill_Solid] = &RasterizeTriangle_FPU;
			m_settings.mode = newSettings.mode;
		}

		if( newSettings.mode == CpuMode_Use_SSE && cpu.has_SSE_4_1 ) {
			//g_ftblDrawTriangle[Fill_Solid] = &RasterizeTriangleImmediateDepthOnly_SSE<16,8>;//<= BEST
			//g_ftblDrawTriangle[Fill_Solid] = &RasterizeTriangleImmediateColorOnly_SSE<32,8>;//<= GOOD
			//g_ftblDrawTriangle[Fill_Solid] = &RasterizeTriangleImmediateColorOnly_SSE<16,16>;
			// 

			//g_ftblDrawTriangle[Fill_Solid] = &RasterizeTriangleImmediateUserShader_SSE<16,4>;
			g_ftblDrawTriangle[Fill_Solid] = &RasterizeTriangleImmediateUserShader_SSE<16,8>;

			m_settings.mode = newSettings.mode;
		}

		//if( newSettings.mode == CpuMode_Use_AVX && cpu.has_AVX ) {
		//	g_ftblDrawTriangle[Fill_Solid] = &RasterizeTriangle_AVX;
		//	m_settings.mode = newSettings.mode;
		//}
	}
	else
	{
		UNDONE;
		//g_ftblDrawTriangle[Fill_Solid] = &F_CacheTransformedFace;
	}

	g_ftblDrawTriangle[Fill_Wireframe] = &F_DrawWireframeTriangle;

	mxREFACTOR(:)
	//g_ftblDrawTriangle[Fill_Wireframe][CpuMode_Use_FPU] = &F_DrawWireframeTriangle;
	//g_ftblDrawTriangle[Fill_Wireframe][CpuMode_Use_SSE] = &F_DrawWireframeTriangle;
	//g_ftblDrawTriangle[Fill_Wireframe][CpuMode_Use_AVX] = &F_DrawWireframeTriangle;
}

// clip, transform and enqueue triangles;
// splits triangles against the six frustum planes into a maximum of 5 sub-triangles.
//
void ProcessTriangles( const SVertex* vertices, UINT numVertices, const SIndex* indices, UINT numIndices )
{
	mxPROFILE_SCOPE("Process Triangles");

	F_RenderSingleTriangle* drawTriangleFunction = g_ftblDrawTriangle[m_fillMode];

	(*g_ftblProcessTriangles[m_fillMode][m_cullMode])( drawTriangleFunction, vertices, numVertices, indices, numIndices, context );

	if( TriangleProcessing::g_nTransformedTris )
	{
		UNDONE;//BUGS
		for( UINT iTriangle = 0; iTriangle < TriangleProcessing::g_nTransformedTris; iTriangle++ )
		{
			TriangleProcessing::RasterizeTriangle_FPU( iTriangle, context );
		}
		TriangleProcessing::g_nTransformedTris = 0;
	}
}

}//namespace SoftRenderer
#endif
