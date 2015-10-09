#pragma once

#include <SoftRender/SoftRender.h>
#include <SoftRender/SoftRender_Internal.h>

namespace SoftRenderer
{

//enum { TILE_SIZE_X = 32 };	//<= must be a power of two
//enum { TILE_SIZE_Y = 8 };	//<= must be a power of two

enum { TILE_SIZE_X = 16 };	//<= must be a power of two
enum { TILE_SIZE_Y = 8 };	//<= must be a power of two

//enum { TILE_SIZE_X = 8 };	//<= must be a power of two
//enum { TILE_SIZE_Y = 4 };	//<= must be a power of two

//enum { MAX_TILES = 1<<15 };

// 28.4 fixed-point coordinates
// (4 bits of sub-pixel precision, enough for a 2048x2048 color buffer)
enum { FP_SHIFT = 4 };

#if 1
	union srTile
	{
		UINT32		sort;
		// NOTE: field order is important! (for radix sort)
		struct {
			// Visual C++, Win7-64, x86-64
			BITFIELD	iFace : 12;	// triangle index
			BITFIELD	iX : 9;		// realX = iX * TILE_SIZE_X
			BITFIELD	iY : 10;	// realY = iY * TILE_SIZE_Y
			BITFIELD	bFullyCovered : 1;	// 1 - fully covered, 0 - partially covered
		};

	public:
		FORCEINLINE void SetX( UINT x )
		{
			Assert( x % TILE_SIZE_X == 0 );
			iX = x / TILE_SIZE_X;
		}
		FORCEINLINE void SetY( UINT y )
		{
			Assert( y % TILE_SIZE_Y == 0 );
			iY = y / TILE_SIZE_Y;
		}
		FORCEINLINE UINT GetX() const
		{
			return iX * TILE_SIZE_X;
		}
		FORCEINLINE UINT GetY() const
		{
			return iY * TILE_SIZE_Y;
		}
	};
	mxSTATIC_ASSERT( sizeof srTile == sizeof UINT32 );
	mxSTATIC_ASSERT( SOFT_RENDER_MAX_WINDOW_WIDTH <= 2048 );
	mxSTATIC_ASSERT( SOFT_RENDER_MAX_WINDOW_HEIGHT <= 1024 );
#else
	struct srTile
	{
		UINT16		iFace;	// triangle index
		UINT16		iX;
		UINT16		iY;
		UINT16		bFullyCovered;

	public:
		FORCEINLINE void SetX( UINT x )
		{
			iX = x;
		}
		FORCEINLINE void SetY( UINT y )
		{
			iY = y;
		}
		FORCEINLINE UINT GetX() const
		{
			return iX;
		}
		FORCEINLINE UINT GetY() const
		{
			return iY;
		}
	};
	mxSTATIC_ASSERT( sizeof srTile == sizeof UINT64 );
	mxSTATIC_ASSERT( SOFT_RENDER_MAX_WINDOW_WIDTH <= MAX_UINT16 );
	mxSTATIC_ASSERT( SOFT_RENDER_MAX_WINDOW_HEIGHT <= MAX_UINT16 );
#endif


struct Fragment
{
	UINT16		iFace;	// triangle index
	UINT16		iX;
	UINT16		iY;
	UINT16		bFullyCovered;	// 1 - fully covered, 0 - partially covered
};




class srTileRenderer : public ATriangleRenderer
	, SingleInstance<srTileRenderer>
{
public_internal:
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


	//TStaticList< XVertex, MAX_BATCHED_VERTICES >	m_batchedVertices;
	mxSIMDALIGNED XTriangle	m_transformedFaces[FACE_BUFFER_SIZE];
	UINT					m_nTransformedTris;

	srTile *				m_tiles;	// grows in powers of two
	UINT					m_numTiles;	// current number of tiles
	//UINT					m_numFullyCoveredTiles;	// number of trivially accepted tiles
	UINT					m_maxTiles;	// number of dynamically allocated tiles

	srTile *				m_sortedTiles;	// grows in powers of two

public:
	srTileRenderer( UINT width, UINT height );
	~srTileRenderer();

	void SetWorldMatrix( const float4x4& newWorldMatrix ) override;
	void SetViewMatrix( const float4x4& newViewMatrix ) override;
	void SetProjectionMatrix( const float4x4& newProjectionMatrix ) override;

	void SetCullMode( ECullMode newCullMode ) override;
	void SetFillMode( EFillMode newFillMode ) override;

	void SetVertexShader( F_VertexShader* newVertexShader ) override;
	void SetPixelShader( F_PixelShader* newPixelShader ) override;

	void SetTexture( SoftTexture2D* newTexture2D ) override;

	void DrawTriangles( SoftFrameBuffer& frameBuffer, const SVertex* vertices, UINT numVertices, const SIndex* indices, UINT numIndices ) override;

private:
	void ProcessTriangles( const SVertex* vertices, UINT numVertices, const SIndex* indices, UINT numTriangles, const SoftRenderContext& context );
};

}//namespace SoftRenderer
