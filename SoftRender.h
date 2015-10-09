#pragma once

#include <Graphics/Graphics.h>

class ThreadPool;

//-------------------------------------------------------------------
//	Compile config
//-------------------------------------------------------------------

// compile with debug code
//#define SOFT_RENDER_DEBUG		(MX_DEBUG)
#define SOFT_RENDER_DEBUG		1

// use SIMD instructions
#define SOFT_RENDER_USE_SSE		(1)

// use multiple threads
#define SOFT_RENDER_ASYNC_JOBS	(1)

// 1 - use halfspaces, else use scan lines
#define SOFT_RENDER_USE_HALFSPACE_RASTERIZER	(1)

// 1 - transform,clip and rasterize triangles immediately, without buffering.
#define SOFT_RENDER_USE_IMMEDIATE_RASTERIZATION		(0)

enum { SOFT_RENDER_MAX_WINDOW_WIDTH = 2048 };
enum { SOFT_RENDER_MAX_WINDOW_HEIGHT = 1024 };


//-------------------------------------------------------------------
//	Types
//-------------------------------------------------------------------


// 32-bit ARGB color (A-MSB, B-LSB on little-endian systems)
typedef UINT32 ARGB32;

#define MAKE_RGBA32(R,G,B,A)	\
	((DWORD)(BYTE)(B) | ((DWORD)(BYTE)(G) << 8) | ((DWORD)(BYTE)(R) << 16) | ((DWORD)(BYTE)(A) << 24 ))

#define ARGB8_BLACK		MAKE_RGBA32(0,0,0,0)
#define ARGB8_WHITE		MAKE_RGBA32(255,255,255,255)
#define ARGB8_RED		MAKE_RGBA32(255,0,0,0)
#define ARGB8_GREEN		MAKE_RGBA32(0,255,0,0)
#define ARGB8_BLUE		MAKE_RGBA32(0,0,255,0)
#define ARGB8_YELLOW	MAKE_RGBA32(255,255,0,0)

typedef ARGB32 SoftPixel;

enum { SOFT_STRIDE = sizeof SoftPixel };



#if 1

	#define SOFT_RENDER_USES_FLOATING_POINT_DEPTH_BUFFER	(1)

	// floating-point depth buffer
	typedef F4 ZBufElem;

	//#define SOFT_MAX_DEPTH	1.0f
	#define SOFT_MAX_DEPTH	9999999.0f

	static FORCEINLINE
	ZBufElem FloatDepthToZBufferValue( FLOAT depth )
	{
		return depth;
	}

#elif 0

	#define SOFT_RENDER_USES_FLOATING_POINT_DEPTH_BUFFER	(0)

	// unsigned shorts
	typedef UINT16 ZBufElem;

	#define SOFT_MAX_DEPTH	MAX_UINT16

	static FORCEINLINE
	ZBufElem FloatDepthToZBufferValue( FLOAT depth )
	{
		// [0.f..1.f] -> [0..MAX_UINT16]
		//const F4 bias = 0.01f;	// because depth can be slightly negative -> artifacts
		depth = mxFabs(depth);
		Assert( depth > 0.0f );
		Assert( depth < 1.0f );
		return (UINT)asmFloatToInt( depth * 65535.0f );
	}

#else

	#define SOFT_RENDER_USES_FLOATING_POINT_DEPTH_BUFFER	(0)

	// signed shorts
	typedef INT16 ZBufElem;

	#define SOFT_MAX_DEPTH	MAX_INT16

	static FORCEINLINE
	ZBufElem FloatDepthToZBufferValue( FLOAT depth )
	{
		// [0.f..1.f] -> [0..MAX_INT16]
		return (UINT)asmFloatToInt( depth * 32767.0f );
	}

#endif








// Forward declarations.
class SoftRenderContext;
class SoftTexture2D;
class SoftFrameBuffer;

// input vertex (app to vertex shader)
mxSIMDALIGNED struct SVertex
{
	Vec3D	position;	// 12
	Vec3D	normal;		// 12
	Vec2D	texCoord;	// 8

public:
	FORCEINLINE const Vec3D& GetPosition() const
	{ return position; }

	FORCEINLINE void SetPosition( const Vec3D& newPos )
	{ position = newPos; }

	FORCEINLINE const Vec3D& GetNormal() const
	{ return normal; }

	FORCEINLINE void SetNormal( const Vec3D& N )
	{ normal = N; }
};

typedef UINT16 SIndex;



mxSIMDALIGNED struct ShaderGlobals
{
	float4x4	worldMatrix;
	//float4x4	viewMatrix;
	//float4x4	projectionMatrix;

	float4x4	WVP;	// world-view-projection matrix

	SoftTexture2D *	texture;	// can be null
};




// number of interpolated vertex parameters
enum { NUM_VARYINGS = 4 };

// transformed vertex (vertex shader -> pixel shader interpolants)
mxSIMDALIGNED struct XVertex
{
	Vec4D		P;	//16 position
	FLOAT		vars[ NUM_VARYINGS ];
};
mxSTATIC_ASSERT_ISPOW2( sizeof XVertex );



mxSIMDALIGNED struct VS_INPUT
{
	const ShaderGlobals *	globals;
	const SVertex *	vertex;
};

mxSIMDALIGNED struct VS_OUTPUT
{
	class XVertex *	vertex_out;
};


mxSIMDALIGNED struct SPixelShaderParameters
{
	//Vec4D	position;	// screen-space position
	//Vec4D	normal;	// already normalized
	FLOAT	vars[ NUM_VARYINGS ];	//+in perspectively-correct interpolated parameters
	FLOAT	depth;	//+in interpolated depth
	const ShaderGlobals *	globals;	//+in global parameters
	SoftPixel *	pixel;	//+out output
};

mxSIMDALIGNED struct PS_OUTPUT
{
	FColor	color;
};


typedef void F_VertexShader( const VS_INPUT& inputs, VS_OUTPUT &outputs );
typedef void F_PixelShader( SPixelShaderParameters & args );





enum ECullMode
{
	Cull_None = 0,
	Cull_CW,	// cull clockwise
	Cull_CCW,	// cull counterclockwise
	Cull_MAX
};
enum EFillMode
{
	Fill_Solid = 0,
	Fill_Wireframe,
	Fill_MAX
};

const char* ECullMode_To_Chars( ECullMode cullMode );




typedef void F_RenderSingleTriangle( const XVertex& v1, const XVertex& v2, const XVertex& v3, const SoftRenderContext& context );

typedef void F_RenderTriangles( F_RenderSingleTriangle* drawTriangleFunction,
							   const SVertex* vertices, UINT numVertices,
							   const SIndex* indices, UINT numIndices,
							   const SoftRenderContext& context );



enum ECpuMode
{
	CpuMode_Use_FPU,
	CpuMode_Use_SSE,
	//CpuMode_Use_AVX,
	CpuMode_MAX
};
const char* ECpuMode_To_Chars( ECpuMode cpuMode );


namespace SoftRenderer
{
	struct Settings
	{
		ECpuMode	mode;

	public:
		Settings();
	};

	struct InitArgs
	{
		int width;
		int height;
		SoftPixel* rgba;	// pixel data (32-bit ARGB32)

		Settings	settings;

	public:
		InitArgs();
		bool isOk() const;
	};


	bool Initialize( const InitArgs& initArgs );
	void ModifySettings( const Settings& newSettings );
	void Shutdown();

	const Settings& CurrentSettings();

	void BeginFrame();
	void EndFrame();

	void SetWorldMatrix( const float4x4& newWorldMatrix );
	void SetViewMatrix( const float4x4& newViewMatrix );
	void SetProjectionMatrix( const float4x4& newProjectionMatrix );

	void SetCullMode( ECullMode newCullMode );
	void SetFillMode( EFillMode newFillMode );

	void SetVertexShader( F_VertexShader* newVertexShader );
	void SetPixelShader( F_PixelShader* newPixelShader );

	void SetTexture( SoftTexture2D* newTexture2D );

	void DrawTriangles( const SVertex* vertices, UINT numVertices, const SIndex* indices, UINT numIndices );

	void DrawLine2D(
		UINT iStartX, UINT iStartY,
		UINT iEndX, UINT iEndY,
		ARGB32 color = ARGB8_WHITE
	);

	void DrawWireframeTriangle( const Vec2D& vp1, const Vec2D& vp2, const Vec2D& vp3, ARGB32 color = ARGB8_WHITE );

	void GetViewportSize( UINT &W, UINT &H );
	SoftPixel* GetColorBuffer();

	ThreadPool& GetThreadPool();

	struct Stats
	{
		UINT	numTrianglesRendered;
		UINT	numVertices;
		UINT	numIndices;

	public:
		void Reset();
	};

	extern Stats stats;

	// debug switches

	extern bool bDbg_DrawBlockBounds;
	extern bool bDbg_EnableThreading;

}//namespace SoftRenderer


class SoftTexture2D
{
	// Texture resolution - hardcoded for speed
	enum { SIZE_X = 64 };
	enum { SIZE_Y = 64 };

	TSmallList< ARGB32 >	m_data;

public:
	SoftTexture2D();
	~SoftTexture2D();

	// black & white checkerboard
	void Setup_Checkerboard();

	void Clear();

	ARGB32 Sample_Point_Wrap( F4 u, F4 v ) const;
};


struct SoftMaterial
{
	SoftTexture2D *	diffuseMap;
	SoftTexture2D *	normalMap;
};

//--------------------------------------------------------------//
//				End Of File.									//
//--------------------------------------------------------------//
