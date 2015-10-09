#pragma once

#include "SoftMath.h"
#include "SoftRender.h"

#if SOFT_RENDER_DEBUG
	// for DbgDrawRect
	#include <Base/Util/Rectangle.h>
#endif // SOFT_RENDER_DEBUG


struct SoftRenderContext
{
	const ShaderGlobals *	globals;
	F_VertexShader *		vertexShader;
	F_PixelShader *			pixelShader;
	SoftPixel *				colorBuffer;
	ZBufElem *				depthBuffer;
	void *					userPointer;

	// for viewport transform
	U4	W;	// screen width
	U4	H;	// screen height
	F4	W2;	// half viewport width
	F4	H2;	// half viewport height
};


namespace SoftRenderer
{


// size of post transform cache
//enum { VERTEX_CACHE_SIZE = 32 };

// an intermediate vertex buffer for transformed primitives;
// it contains both vertex and face info for the primitives;
// size of this buffer is dependent on the total batch size;
//enum { BATCHED_VERTEX_BUFFER_SIZE = (1<<14) };	// 16384
enum { MAX_BATCHED_VERTICES = (1<<12) };

// each triangle can be split by the 6 frustum planes into a maximum of 5 sub-triangles.
//enum { VERTEX_BUFFER_SIZE_CLIP = BATCHED_VERTEX_BUFFER_SIZE*5 };

// capacity of buffer for storing transformed triangles
//enum { FACE_BUFFER_SIZE = 4096 };
//enum { FACE_BUFFER_SIZE = 2048 };
enum { FACE_BUFFER_SIZE = 1024 };

// The maximum amount of vertices after clipping is 2*6 + 1.
//enum { CLIP_BUFFER_SIZE = 2*6 + 1 };
enum { CLIP_BUFFER_SIZE = 16 };

enum
{
	PLANE_NEG_X	= 0,
	PLANE_POS_X,

	PLANE_NEG_Y,
	PLANE_POS_Y,

	PLANE_NEG_Z,
	//PLANE_POS_Z,	//<= don't clip against the far plane
	NUM_CLIP_PLANES
};


static inline void InterpolateVertex( float t, const XVertex& a, const XVertex& b, XVertex & v )
{
	INTERPOLATE_VEC4( v.P, a.P, b.P, t );
	//INTERPOLATE_VEC4( v.N, a.N, b.N, t );
	//INTERPOLATE_SIMPLE( v.UV, a.UV, b.UV, t );
	for( UINT i = 0; i < NUM_VARYINGS; i++ ) {
		INTERPOLATE_SIMPLE( v.vars[i], a.vars[i], b.vars[i], t );
	}
}

static inline F4 Dot_Product( const SIMD_VEC4& plane, const XVertex& vertex )
{
#if SOFT_RENDER_USE_SSE
	return XMVectorGetX( XMVector4Dot( vertex.P.q, plane.v ) );
#else
	return ( (vertex.P.x * plane.x + vertex.P.y * plane.y) + (vertex.P.z * plane.z + vertex.P.w * plane.w) );
#endif
}

// executes vertex shader
static inline
void TransformVertex( const SVertex& vertex, XVertex *transformedVertex, const SoftRenderContext& renderContext )
{
	mxPROFILE_SCOPE("Transform Vertex");

	VS_INPUT	vertexShaderInput;
	vertexShaderInput.globals = renderContext.globals;
	vertexShaderInput.vertex = &vertex;

	VS_OUTPUT	vertexShaderOutput;
	vertexShaderOutput.vertex_out = transformedVertex;

	(*renderContext.vertexShader)( vertexShaderInput, vertexShaderOutput );
}

// performs perspective division and viewport mapping
// perspective divide and viewport transform of vertices
static inline
void ProjectVertex( XVertex *v_trans, const SoftRenderContext& context ) 
{
	mxPROFILE_SCOPE("Project Vertex");

	Vec4D *	pos = &v_trans->P;

	const FLOAT invW = 1.0f / pos->w;	// RHW

	// concatenate homogeneous divide and viewport transform
	// NOTE: we don't take into account:
	// m_viewport.TopLeftX, m_viewport.TopLeftY
	// m_viewport.MinDepth, m_viewport.MaxDepth

	pos->x = pos->x * invW * context.W2 + context.W2;
	pos->y = context.H2 - pos->y * invW * context.H2;
	pos->z *= invW;	// NOTE: now Z should be in range (0..1]
	pos->w = invW;	// needed for interpolating texture coordinates

	//v_trans->uv *= invW;

	// now pos->x and pos->y are in viewport coordinates

	//Assert(  0.0f <= pos->z && pos->z <= 1.0f );
	//DBGOUT("z = %3.3f, w = %3.3f\n",pos->z,pos->w);
}





// Transformed face
mxALIGN_BY_CACHE_LINE struct XTriangle
{
	XVertex		v1, v2, v3;

	//// Cached start values for varyings
	//F4		vars1OverW1[NUM_VARYINGS];	// v1.vars * invW1

	//// varyings multiplied by inverse W
	//// (so that they can be linearly interpolated in screen space)
	//Vec2D	varsOverW[NUM_VARYINGS];

	//Vec2D	vInvW;	// inverse W

	Vec2D	vZ;	// depth gradient

	// Cached fixed point coordinates
	INT32	FPX[3];
	INT32	FPY[3];
 
	// Cached half edge constants
	INT32	C1, C2, C3;

	// Cached screen space bounding box
	INT32	minX, maxX, minY, maxY;
};
//mxSTATIC_ASSERT_ISPOW2( sizeof XTriangle );






#if SOFT_RENDER_DEBUG
	inline
	void DbgDrawRect( const SoftRenderContext& context, SoftPixel color, int topLeftX, int topLeftY, int width, int height )
	{
		const int W = context.W;	// viewport width
		const int H = context.H;	// viewport height

		TRectArea<int>	screen(0,0,W-1,H-1);
		TRectArea<int>	rect(topLeftX,topLeftY,width,height);
		rect.Clip(screen);

		SoftRenderer::DrawLine2D( rect.TopLeftX, rect.TopLeftY, rect.TopLeftX + rect.Width, rect.TopLeftY, color );
		SoftRenderer::DrawLine2D( rect.TopLeftX + rect.Width, rect.TopLeftY, rect.TopLeftX + rect.Width, rect.TopLeftY + rect.Height, color );
		SoftRenderer::DrawLine2D( rect.TopLeftX + rect.Width, rect.TopLeftY + rect.Height, rect.TopLeftX, rect.TopLeftY + rect.Height, color );
		SoftRenderer::DrawLine2D( rect.TopLeftX, rect.TopLeftY + rect.Height, rect.TopLeftX, rect.TopLeftY, color );
	}
#else
	inline
	void DbgDrawRect( const SoftRenderContext& context, SoftPixel color, int topLeftX, int topLeftY, int width, int height )
	{}
#endif // SOFT_RENDER_DEBUG


inline void Dbg_BlockRasterizer_DrawFullyCoveredRect( const SoftRenderContext& context, int topLeftX, int topLeftY, int width, int height )
{
	if( SOFT_RENDER_DEBUG && SoftRenderer::bDbg_DrawBlockBounds ) {
		DbgDrawRect( context, ARGB8_GREEN, topLeftX, topLeftY, width, height );
	}
}
inline void Dbg_BlockRasterizer_DrawPartiallyCoveredRect( const SoftRenderContext& context, int topLeftX, int topLeftY, int width, int height )
{
	if( SOFT_RENDER_DEBUG && SoftRenderer::bDbg_DrawBlockBounds ) {
		DbgDrawRect( context, ARGB8_RED, topLeftX, topLeftY, width, height );
	}
}
inline void Dbg_BlockRasterizer_DrawBoundingRect( const SoftRenderContext& context, int topLeftX, int topLeftY, int width, int height )
{
	if( SOFT_RENDER_DEBUG && SoftRenderer::bDbg_DrawBlockBounds ) {
		DbgDrawRect( context, ARGB8_YELLOW, topLeftX, topLeftY, width, height );
	}
}

static inline
void F_DrawWireframeTriangle( const XVertex& v1, const XVertex& v2, const XVertex& v3, const SoftRenderContext& context )
{
	DrawWireframeTriangle( v1.P.ToVec2(), v2.P.ToVec2(), v3.P.ToVec2() );
}



class ATriangleRenderer
{
public:

	virtual void SetWorldMatrix( const float4x4& newWorldMatrix ) = 0;
	virtual void SetViewMatrix( const float4x4& newViewMatrix ) = 0;
	virtual void SetProjectionMatrix( const float4x4& newProjectionMatrix ) = 0;

	virtual void SetCullMode( ECullMode newCullMode ) = 0;
	virtual void SetFillMode( EFillMode newFillMode ) = 0;

	virtual void SetVertexShader( F_VertexShader* newVertexShader ) = 0;
	virtual void SetPixelShader( F_PixelShader* newPixelShader ) = 0;

	virtual void SetTexture( SoftTexture2D* newTexture2D ) = 0;

	virtual void DrawTriangles( SoftFrameBuffer& frameBuffer, const SVertex* vertices, UINT numVertices, const SIndex* indices, UINT numIndices ) = 0;

	// wait for queued jobs to finish
	virtual void Flush() {}

	//virtual void ModifySettings( const Settings& newSettings ) {}

	virtual ~ATriangleRenderer() {}
};


}//namespace SoftRenderer

//--------------------------------------------------------------//
//				End Of File.									//
//--------------------------------------------------------------//
