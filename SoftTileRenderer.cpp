#include "SoftRender_PCH.h"
#pragma hdrstop
#include <Base/JobSystem/ThreadPool.h>
#include <Base/Templates/Algorithm/RadixSort.h>
#include "SoftMesh.h"
#include "SoftTileRenderer.h"
#include "SoftFrameBuffer.h"
#include "TriangleClipping.inl"
#include "SoftImmediateRenderer.h"
#include "Rasterizer_FPU.inl"
#include "Rasterizer_SSE.inl"
#include "SoftThreads.h"

namespace SoftRenderer
{

#if SOFT_RENDER_DEBUG
static inline
float Dbg_Touch__m128( float* address )
{
	float x0 = address[0];
	float x1 = address[1];
	float x2 = address[2];
	float x3 = address[3];
	return x0+x1+x2+x3;
}
#endif // SOFT_RENDER_DEBUG

static inline
srTile& AllocateTile( srTileRenderer* renderer )
{
	const UINT currCapacity = renderer->m_maxTiles;
	Assert(IsPowerOfTwo(currCapacity));

	const UINT newTileIndex = (renderer->m_numTiles++) & (currCapacity-1);//avoid checking for overflow
	Assert( newTileIndex < currCapacity );

	srTile & newTile = renderer->m_tiles[ newTileIndex ];
	return newTile;
}

static inline
void RasterizeFullyCoveredTile( const srTile& tile, const SoftRenderContext& context, srTileRenderer* renderer )
{
	//Assert( tile.bFullyCovered );

	const int W = context.W;	// viewport width
	//const int H = context.H;	// viewport height

	const UINT iFace = tile.iFace;
	const XTriangle& face = renderer->m_transformedFaces[ iFace ];

	const F4 fX1 = face.v1.P.x;
	const F4 fX2 = face.v2.P.x;
	const F4 fX3 = face.v3.P.x;

	const F4 fY1 = face.v1.P.y;
	const F4 fY2 = face.v2.P.y;
	const F4 fY3 = face.v3.P.y;

	const F4 fZ1 = face.v1.P.z;
	const F4 fZ2 = face.v2.P.z;
	const F4 fZ3 = face.v3.P.z;

	const INT32 nMinX = face.minX;
	const INT32 nMaxX = face.maxX;
	const INT32 nMinY = face.minY;
	const INT32 nMaxY = face.maxY;

	//Assert( nMinX % TILE_SIZE_X == 0 );
	//Assert( nMinY % TILE_SIZE_Y == 0 );

	const UINT iBlockX = tile.GetX();
	const UINT iBlockY = tile.GetY();

	const __m128 qf3210 = _mm_set_ps( 3.0f, 2.0f, 1.0f, 0.0f );
	//const __m128 qf4444 = _mm_set_ps1( 4.0f );
	static const __m128 qf255x4 = _mm_set_ps1( 255.0f );

	SoftPixel *	colorBufferStart = context.colorBuffer + iBlockY * W;	// color buffer
	ZBufElem *	depthBufferStart = context.depthBuffer + iBlockY * W;	// depth buffer

	for( UINT iY = iBlockY; iY < iBlockY + TILE_SIZE_Y; iY++ )
	{
		for( UINT iX = iBlockX; iX < iBlockX + TILE_SIZE_X; iX += SSE_REG_WIDTH )
		{
			F4* depth = (depthBufferStart + iX);
			//Assert(IS_16_BYTE_ALIGNED(depth));

			__m128i* dest = (__m128i*) (colorBufferStart + iX);
			//Assert(IS_16_BYTE_ALIGNED(dest));

			//#######[LOAD] load previous depth
			const __m128 qfOldDepth = _mm_load_ps( depth );

			//###[LHS]
			// start value for x and y
			const F4 fX = (F4)iX - fX1;	//<=###[LHS]
			const F4 fY = (F4)iY - fY1;	//<=###[LHS]

			// interpolate depth
			const __m128 qfvZx = _mm_set1_ps( face.vZ.x );
			const __m128 qfZ0 = _mm_set1_ps( fZ1 + face.vZ.x * fX + face.vZ.y * fY );
			const __m128 qfZ = _mm_add_ps( qfZ0, _mm_mul_ps( qfvZx, qf3210 ) );

			// perform depth testing
			const __m128i qiDepthMask = (__m128i&) _mm_cmple_ps( qfZ, qfOldDepth );
			if( !_mm_movemask_ps( (__m128&)qiDepthMask) ) {
				continue;	// this quad is occluded
			}

			//$$$@@@[STORE] write depth to framebuffer
			_mm_store_ps( depth,
							_mm_or_ps(
								_mm_and_ps( (__m128&)qiDepthMask, qfZ ),
								_mm_andnot_ps( (__m128&)qiDepthMask, qfOldDepth )
							)
			);	//write

			//#######[LOAD] load previous color
			const __m128i qiOldColor = _mm_load_si128( dest );

			// convert depth to color

			__m128 qfTmp;
			qfTmp = _mm_mul_ps( qfZ, qf255x4 );	// [0..1] => [0..255]
			qfTmp = _mm_min_ps( qfTmp, qf255x4 );	// clamp to [0..255]

			__m128i	qiTmp;
			qiTmp = _mm_cvtps_epi32( qfTmp );	// convert float => int

			// convert to ARGB: (i<<16)|(i<<8)|i;	// Alpha=0
			__m128i qiNewColor = _mm_or_si128(
				_mm_or_si128(
					_mm_slli_epi32( qiTmp, 16 ),
					_mm_slli_epi32( qiTmp, 8 )
				),
				qiTmp
			);

			const __m128i result = _mm_or_si128(
				_mm_and_si128( qiDepthMask, qiNewColor ),
				_mm_andnot_si128( qiDepthMask, qiOldColor )
			);

			//$$$@@@[STORE] write color to framebuffer
			_mm_store_si128( dest, result );	//write

		}//for x

		colorBufferStart += W;
		depthBufferStart += W;
	}//for y

	//SoftRenderer::Dbg_BlockRasterizer_DrawFullyCoveredRect( context, iBlockX, iBlockY, TILE_SIZE_X, TILE_SIZE_Y );
}

static inline
void RasterizePartiallyCoveredTile( const srTile& tile, const SoftRenderContext& context, srTileRenderer* renderer )
{
	//Assert( !tile.bFullyCovered );

	const int W = context.W;	// viewport width
	const int H = context.H;	// viewport height

	const UINT iFace = tile.iFace;
	Assert( iFace < renderer->m_nTransformedTris );

	const XTriangle& face = renderer->m_transformedFaces[ iFace ];

	const UINT iBlockX = tile.GetX();
	const UINT iBlockY = tile.GetY();

	//Assert( iBlockX <= W-TILE_SIZE_X );
	//Assert( iBlockY <= H-TILE_SIZE_Y );

	const F4 fX1 = face.v1.P.x;
	const F4 fX2 = face.v2.P.x;
	const F4 fX3 = face.v3.P.x;

	const F4 fY1 = face.v1.P.y;
	const F4 fY2 = face.v2.P.y;
	const F4 fY3 = face.v3.P.y;

	const F4 fZ1 = face.v1.P.z;
	const F4 fZ2 = face.v2.P.z;
	const F4 fZ3 = face.v3.P.z;

	// 24.8 fixed-point
	const INT32 X1 = face.FPX[0];
	const INT32 X2 = face.FPX[1];
	const INT32 X3 = face.FPX[2];

	const INT32 Y1 = face.FPY[0];
	const INT32 Y2 = face.FPY[1];
	const INT32 Y3 = face.FPY[2];

	// deltas
	const INT32 DeltaX12 = X1 - X2;
	const INT32 DeltaX23 = X2 - X3;
	const INT32 DeltaX31 = X3 - X1;

	const INT32 DeltaY12 = Y1 - Y2;
	const INT32 DeltaY23 = Y2 - Y3;
	const INT32 DeltaY31 = Y3 - Y1;

	// 24.8 Fixed-point deltas
	const INT32 FDX12 = DeltaX12 << FP_SHIFT;
	const INT32 FDX23 = DeltaX23 << FP_SHIFT;
	const INT32 FDX31 = DeltaX31 << FP_SHIFT;

	const INT32 FDY12 = DeltaY12 << FP_SHIFT;
	const INT32 FDY23 = DeltaY23 << FP_SHIFT;
	const INT32 FDY31 = DeltaY31 << FP_SHIFT;


	// Half-edge constants in 28.4

	const INT32 C1 = face.C1;
	const INT32 C2 = face.C2;
	const INT32 C3 = face.C3;


	const INT32 nMinX = face.minX;
	const INT32 nMaxX = face.maxX;
	const INT32 nMinY = face.minY;
	const INT32 nMaxY = face.maxY;

	//Assert( nMinX % TILE_SIZE_X == 0 );
	//Assert( nMinY % TILE_SIZE_Y == 0 );

	SoftPixel* pixels = context.colorBuffer + iBlockY * W;	// color buffer
	ZBufElem* zbuffer = context.depthBuffer + iBlockY * W;	// depth buffer


	// Corners of block in 28.4 fixed-point (4 bits of sub-pixel accuracy)
	const UINT FBlockX0 = (iBlockX << FP_SHIFT);
	const UINT FBlockX1 = (iBlockX + (TILE_SIZE_X - 1)) << FP_SHIFT;
	const UINT FBlockY0 = (iBlockY << FP_SHIFT);
	const UINT FBlockY1 = (iBlockY + (TILE_SIZE_Y - 1)) << FP_SHIFT;

	// in 28.4
	INT32 CY1 = C1 + DeltaX12 * FBlockY0 - DeltaY12 * FBlockX0;
	INT32 CY2 = C2 + DeltaX23 * FBlockY0 - DeltaY23 * FBlockX0;
	INT32 CY3 = C3 + DeltaX31 * FBlockY0 - DeltaY31 * FBlockX0;

	const __m128 qf3210 = _mm_set_ps( 3.0f, 2.0f, 1.0f, 0.0f );
	//const __m128 qf4444 = _mm_set_ps1( 4.0f );
	static const __m128 qf255x4 = _mm_set_ps1( 255.0f );

	const __m128i qiOffsetDY12 = _mm_set_epi32( FDY12 * 3, FDY12 * 2, FDY12 * 1, FDY12 * 0 );
	const __m128i qiOffsetDY23 = _mm_set_epi32( FDY23 * 3, FDY23 * 2, FDY23 * 1, FDY23 * 0 );
	const __m128i qiOffsetDY31 = _mm_set_epi32( FDY31 * 3, FDY31 * 2, FDY31 * 1, FDY31 * 0 );

	const __m128i qiFDY12_4 = _mm_set1_epi32( FDY12 * SSE_REG_WIDTH );
	const __m128i qiFDY23_4 = _mm_set1_epi32( FDY23 * SSE_REG_WIDTH );
	const __m128i qiFDY31_4 = _mm_set1_epi32( FDY31 * SSE_REG_WIDTH );

	for( UINT iY = iBlockY; iY < iBlockY + TILE_SIZE_Y; iY++ )
	{
		const __m128i	qiCY1 = _mm_set1_epi32( CY1 );
		const __m128i	qiCY2 = _mm_set1_epi32( CY2 );
		const __m128i	qiCY3 = _mm_set1_epi32( CY3 );

		__m128i qiCX1 = _mm_sub_epi32( qiCY1, qiOffsetDY12 );
		__m128i qiCX2 = _mm_sub_epi32( qiCY2, qiOffsetDY23 );
		__m128i qiCX3 = _mm_sub_epi32( qiCY3, qiOffsetDY31 );

		for( UINT iX = iBlockX; iX < iBlockX + TILE_SIZE_X; iX += SSE_REG_WIDTH )
		{
			const __m128i qiCX1mask = _mm_cmpgt_epi32( qiCX1, _mm_setzero_si128() );
			const __m128i qiCX2mask = _mm_cmpgt_epi32( qiCX2, _mm_setzero_si128() );
			const __m128i qiCX3mask = _mm_cmpgt_epi32( qiCX3, _mm_setzero_si128() );
			const __m128i qiEdgeMask = _mm_and_si128( qiCX1mask, _mm_and_si128( qiCX2mask, qiCX3mask ) );

			F4* depth = (zbuffer + iX);
			//Assert(IS_16_BYTE_ALIGNED(depth));

			__m128i* dest = (__m128i*) (pixels + iX);
			//Assert(IS_16_BYTE_ALIGNED(dest));

			//#######[LOAD] load previous depth
			const __m128 qfOldDepth = _mm_load_ps( depth );

			//###[LHS]
			// start value for x and y
			const F4 fX = (F4)iX - fX1;	//<=###[LHS]
			const F4 fY = (F4)iY - fY1;	//<=###[LHS]

			// interpolate depth
			const __m128 qfvZx = _mm_set1_ps( face.vZ.x );
			const __m128 qfZ0 = _mm_set1_ps( fZ1 + face.vZ.x * fX + face.vZ.y * fY );
			const __m128 qfZ = _mm_add_ps( qfZ0, _mm_mul_ps( qfvZx, qf3210 ) );

			// perform depth testing
			const __m128i qiDepthMask = (__m128i&) _mm_cmple_ps( qfZ, qfOldDepth );
			if( !_mm_movemask_ps( (__m128&)qiDepthMask) ) {
				goto L_Skip_This_Quad;	// this quad is occluded
			}

			const __m128i qiColorMask = _mm_and_si128( qiDepthMask, qiEdgeMask );

			//$$$@@@[STORE] write depth to framebuffer
			_mm_store_ps( depth,
							_mm_or_ps(
								_mm_and_ps( (__m128&)qiColorMask, qfZ ),
								_mm_andnot_ps( (__m128&)qiColorMask, qfOldDepth )
							)
			);	//write
			
			//#######[LOAD] load previous color
			const __m128i qiOldColor = _mm_load_si128( dest );

			// convert depth to color

			__m128 qfTmp;
			qfTmp = _mm_mul_ps( qfZ, qf255x4 );	// [0..1] => [0..255]
			qfTmp = _mm_min_ps( qfTmp, qf255x4 );	// clamp to [0..255]

			__m128i	qiTmp;
			qiTmp = _mm_cvtps_epi32( qfTmp );	// convert float => int

			// convert to ARGB: (i<<16)|(i<<8)|i;	// Alpha=0
			__m128i qiNewColor = _mm_or_si128(
				_mm_or_si128(
					_mm_slli_epi32( qiTmp, 16 ),
					_mm_slli_epi32( qiTmp, 8 )
				),
				qiTmp
			);

			const __m128i result = _mm_or_si128(
				_mm_and_si128( qiColorMask, qiNewColor ),
				_mm_andnot_si128( qiColorMask, qiOldColor )
			);

			//$$$@@@[STORE] write color to framebuffer
			_mm_store_si128( dest, result );	//write


L_Skip_This_Quad:
			qiCX1 = _mm_sub_epi32( qiCX1, qiFDY12_4 );
			qiCX2 = _mm_sub_epi32( qiCX2, qiFDY23_4 );
			qiCX3 = _mm_sub_epi32( qiCX3, qiFDY31_4 );

		}//for x

		CY1 += FDX12;
		CY2 += FDX23;
		CY3 += FDX31;

		pixels += W;
		zbuffer += W;
	}//for y

	//SoftRenderer::Dbg_BlockRasterizer_DrawPartiallyCoveredRect( context, iBlockX, iBlockY, TILE_SIZE_X, TILE_SIZE_Y );
}

// BLOCK_SIZE_X=16 and BLOCK_SIZE_Y=8 are good values
template< UINT BLOCK_SIZE_X, UINT BLOCK_SIZE_Y >
static inline
void F_ProcessTriangle(
								 //NOTABUG: swap v2 and v3 because of triangle winding order and our edge functions sign calculation
								 const XVertex& v1, const XVertex& v3, const XVertex& v2,
								 const SoftRenderContext& context
						   )
{
	mxSTATIC_ASSERT_ISPOW2( BLOCK_SIZE_X );
	mxSTATIC_ASSERT_ISPOW2( BLOCK_SIZE_Y );

	mxPROFILE_SCOPE("Process Triangle (Insert Transformed Triangle)");


	const int W = context.W;	// viewport width
	const int H = context.H;	// viewport height

	srTileRenderer* renderer = c_cast(srTileRenderer*) context.userPointer;

	Assert( renderer->m_nTransformedTris < FACE_BUFFER_SIZE );

	const UINT newFaceIndex = (renderer->m_nTransformedTris++) & (FACE_BUFFER_SIZE-1);//avoid checking for overflow

	XTriangle & face = renderer->m_transformedFaces[ newFaceIndex ];

	face.v1 = v1;
	face.v2 = v2;
	face.v3 = v3;


	const F4 fX1 = v1.P.x;
	const F4 fX2 = v2.P.x;
	const F4 fX3 = v3.P.x;

	const F4 fY1 = v1.P.y;
	const F4 fY2 = v2.P.y;
	const F4 fY3 = v3.P.y;

	const F4 fZ1 = v1.P.z;
	const F4 fZ2 = v2.P.z;
	const F4 fZ3 = v3.P.z;


	const INT32 X1 = iround( F4(1<<FP_SHIFT) * fX1 );
	const INT32 X2 = iround( F4(1<<FP_SHIFT) * fX2 );
	const INT32 X3 = iround( F4(1<<FP_SHIFT) * fX3 );

	const INT32 Y1 = iround( F4(1<<FP_SHIFT) * fY1 );
	const INT32 Y2 = iround( F4(1<<FP_SHIFT) * fY2 );
	const INT32 Y3 = iround( F4(1<<FP_SHIFT) * fY3 );

	//const INT32 Z1 = iround( 16.0f * fZ1 );
	//const INT32 Z2 = iround( 16.0f * fZ2 );
	//const INT32 Z3 = iround( 16.0f * fZ3 );

	//const INT32 W1 = iround( 16.0f * fInvW1 );
	//const INT32 W2 = iround( 16.0f * fInvW2 );
	//const INT32 W3 = iround( 16.0f * fInvW3 );


	face.FPX[0] = X1;
	face.FPX[1] = X2;
	face.FPX[2] = X3;
			
	face.FPY[0] = Y1;
	face.FPY[1] = Y2;
	face.FPY[2] = Y3;



	// deltas
	const INT32 DeltaX12 = X1 - X2;
	const INT32 DeltaX23 = X2 - X3;
	const INT32 DeltaX31 = X3 - X1;

	const INT32 DeltaY12 = Y1 - Y2;
	const INT32 DeltaY23 = Y2 - Y3;
	const INT32 DeltaY31 = Y3 - Y1;


	// 24.8 Fixed-point deltas
	const INT32 FDX12 = DeltaX12 << FP_SHIFT;
	const INT32 FDX23 = DeltaX23 << FP_SHIFT;
	const INT32 FDX31 = DeltaX31 << FP_SHIFT;

	const INT32 FDY12 = DeltaY12 << FP_SHIFT;
	const INT32 FDY23 = DeltaY23 << FP_SHIFT;
	const INT32 FDY31 = DeltaY31 << FP_SHIFT;




	// Compute interpolation data
	//const F4 fDeltaX12 = fX1 - fX2;
	//const F4 fDeltaX23 = fX2 - fX3;
	const F4 fDeltaX31 = fX3 - fX1;

	//const F4 fDeltaY12 = fY1 - fY2;
	//const F4 fDeltaY23 = fY2 - fY3;
	const F4 fDeltaY31 = fY3 - fY1;

	const F4 fDeltaZ21 = fZ2 - fZ1;
	const F4 fDeltaZ31 = fZ3 - fZ1;

	const F4 fDeltaX21 = fX2 - fX1;
	const F4 fDeltaY21 = fY2 - fY1;

	const F4 INTERP_C = fDeltaX21 * fDeltaY31 - fDeltaX31 * fDeltaY21;


	// compute gradient for interpolating depth (aka Z)

	ComputeGradient_FPU(
		INTERP_C,
		fDeltaZ21, fDeltaZ31,
		fDeltaX21, fDeltaX31,
		fDeltaY21, fDeltaY31,
		face.vZ.x, face.vZ.y
		);


	mxSTATIC_ASSERT( SOFT_RENDER_USES_FLOATING_POINT_DEPTH_BUFFER );


	// Half-edge constants in 28.4
	INT32 C1 = DeltaY12 * X1 - DeltaX12 * Y1;
	INT32 C2 = DeltaY23 * X2 - DeltaX23 * Y2;
	INT32 C3 = DeltaY31 * X3 - DeltaX31 * Y3;

	// correct for top-left fill convention
	if( DeltaY12 < 0 || (DeltaY12 == 0 && DeltaX12 > 0) ) {
		++C1;
	}
	if( DeltaY23 < 0 || (DeltaY23 == 0 && DeltaX23 > 0) ) {
		++C2;
	}
	if( DeltaY31 < 0 || (DeltaY31 == 0 && DeltaX31 > 0) ) {
		++C3;
	}


	face.C1 = C1;
	face.C2 = C2;
	face.C3 = C3;


	// Bounding rectangle of this triangle in screen space

#if 0
	const INT32 nMinX = ((Min3(X1, X2, X3) + 0xF) >> FP_SHIFT) & ~(BLOCK_SIZE_X - 1);	// start in block corner
	const INT32 nMaxX = ((Max3(X1, X2, X3) + 0xF) >> FP_SHIFT);
	const INT32 nMinY = ((Min3(Y1, Y2, Y3) + 0xF) >> FP_SHIFT) & ~(BLOCK_SIZE_Y - 1);	// start in block corner
	const INT32 nMaxY = ((Max3(Y1, Y2, Y3) + 0xF) >> FP_SHIFT);
#else
	const INT32 nMinX = Clamp( (Min3(X1, X2, X3) + 0xF) >> 4, 0, W ) & ~(BLOCK_SIZE_X - 1);
	const INT32 nMaxX = Clamp( (Max3(X1, X2, X3) + 0xF) >> 4, 0, W );
	const INT32 nMinY = Clamp( (Min3(Y1, Y2, Y3) + 0xF) >> 4, 0, H ) & ~(BLOCK_SIZE_Y - 1);
	const INT32 nMaxY = Clamp( (Max3(Y1, Y2, Y3) + 0xF) >> 4, 0, H );
#endif

	Assert( nMinX % BLOCK_SIZE_X == 0 );
	Assert( nMinY % BLOCK_SIZE_Y == 0 );

	face.minX = nMinX;
	face.maxX = nMaxX;
	face.minY = nMinY;
	face.maxY = nMaxY;


	mxOPTIMIZE("Use integer SSE instruction?");

	for( UINT iBlockY = nMinY; iBlockY < nMaxY; iBlockY += BLOCK_SIZE_Y )
	{
		for( UINT iBlockX = nMinX; iBlockX < nMaxX; iBlockX += BLOCK_SIZE_X )
		{
			// Corners of block in 28.4 fixed-point (4 bits of sub-pixel accuracy)
			const UINT FBlockX0 = (iBlockX << FP_SHIFT);
			const UINT FBlockX1 = (iBlockX + (BLOCK_SIZE_X - 1)) << FP_SHIFT;
			const UINT FBlockY0 = (iBlockY << FP_SHIFT);
			const UINT FBlockY1 = (iBlockY + (BLOCK_SIZE_Y - 1)) << FP_SHIFT;

			//Assert( iBlockX <= W-BLOCK_SIZE_X );
			//Assert( iBlockY <= H-BLOCK_SIZE_Y );

			// Evaluate half-space functions in the 4 corners of the block

			const UINT a00 = 1-INT32_SIGN_BIT_SET( C1 + DeltaX12 * FBlockY0 - DeltaY12 * FBlockX0 );
			const UINT a10 = 1-INT32_SIGN_BIT_SET( C1 + DeltaX12 * FBlockY0 - DeltaY12 * FBlockX1 );
			const UINT a01 = 1-INT32_SIGN_BIT_SET( C1 + DeltaX12 * FBlockY1 - DeltaY12 * FBlockX0 );
			const UINT a11 = 1-INT32_SIGN_BIT_SET( C1 + DeltaX12 * FBlockY1 - DeltaY12 * FBlockX1 );
			const UINT a = (a00 << 0) | (a10 << 1) | (a01 << 2) | (a11 << 3);	// compose 4-bit mask for all four corners
			if( !a ) {
				continue;// Skip block when outside an edge => outside the triangle
			}
			const UINT b00 = 1-INT32_SIGN_BIT_SET( C2 + DeltaX23 * FBlockY0 - DeltaY23 * FBlockX0 );
			const UINT b10 = 1-INT32_SIGN_BIT_SET( C2 + DeltaX23 * FBlockY0 - DeltaY23 * FBlockX1 );
			const UINT b01 = 1-INT32_SIGN_BIT_SET( C2 + DeltaX23 * FBlockY1 - DeltaY23 * FBlockX0 );
			const UINT b11 = 1-INT32_SIGN_BIT_SET( C2 + DeltaX23 * FBlockY1 - DeltaY23 * FBlockX1 );
			const UINT b = (b00 << 0) | (b10 << 1) | (b01 << 2) | (b11 << 3);	// compose 4-bit mask for all four corners
			if( !b ) {
				continue;// Skip block when outside an edge => outside the triangle
			}
			const UINT c00 = 1-INT32_SIGN_BIT_SET( C3 + DeltaX31 * FBlockY0 - DeltaY31 * FBlockX0 );
			const UINT c10 = 1-INT32_SIGN_BIT_SET( C3 + DeltaX31 * FBlockY0 - DeltaY31 * FBlockX1 );
			const UINT c01 = 1-INT32_SIGN_BIT_SET( C3 + DeltaX31 * FBlockY1 - DeltaY31 * FBlockX0 );
			const UINT c11 = 1-INT32_SIGN_BIT_SET( C3 + DeltaX31 * FBlockY1 - DeltaY31 * FBlockX1 );
			const UINT c = (c00 << 0) | (c10 << 1) | (c01 << 2) | (c11 << 3);	// compose 4-bit mask for all four corners
			if( !c ) {
				continue;// Skip block when outside an edge => outside the triangle
			}

			srTile& newTile = AllocateTile( renderer );
			newTile.iFace = newFaceIndex;
			newTile.SetX( iBlockX );
			newTile.SetY( iBlockY );

			// check if whole block is totally covered
			// (all four corners are inside triangle and their coverage masks are (1|2|4|8) = 15)

			newTile.bFullyCovered = ( a + b + c == (0xF + 0xF + 0xF) );

		}//for each block on X axis

	}//for each block on Y axis

	//renderer->m_numFullyCoveredTiles += numFullyCoveredTiles;
}

srTileRenderer::srTileRenderer( UINT width, UINT height )
{
	m_worldMatrix = XMMatrixIdentity();
	m_viewMatrix = XMMatrixIdentity();
	m_projectionMatrix = XMMatrixIdentity();

	m_vertexShader = nil;
	m_pixelShader = nil;

	m_cullMode = ECullMode::Cull_CCW;
	m_fillMode = EFillMode::Fill_Solid;

	DBGOUT("srTileRenderer(): size of face buffer: %u KiB\n", sizeof m_transformedFaces /mxKIBIBYTE);

	m_nTransformedTris = 0;

	m_maxTiles = 2048;
	m_tiles = (srTile*) mxAlloc( m_maxTiles * sizeof m_tiles[0] );
	m_sortedTiles = (srTile*) mxAlloc( m_maxTiles * sizeof m_sortedTiles[0] );
	m_numTiles = 0;
	//m_numFullyCoveredTiles = 0;

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

	m_ftblDrawTriangle[Fill_Solid] = &F_ProcessTriangle<TILE_SIZE_X,TILE_SIZE_Y>;
	m_ftblDrawTriangle[Fill_Wireframe] = &F_DrawWireframeTriangle;
}

srTileRenderer::~srTileRenderer()
{
	DBGOUT("~srTileRenderer(): %u tiles (%u KiB)\n",
		m_maxTiles, m_maxTiles*sizeof m_tiles[0] /mxKIBIBYTE);

	mxFree( m_tiles );
	mxFree( m_sortedTiles );
	m_tiles = nil;
	m_numTiles = 0;
	//m_numFullyCoveredTiles = 0;
	m_maxTiles = 0;
}

void srTileRenderer::SetWorldMatrix( const float4x4& newWorldMatrix )
{
	m_worldMatrix = newWorldMatrix;
}

void srTileRenderer::SetViewMatrix( const float4x4& newViewMatrix )
{
	m_viewMatrix = newViewMatrix;
}

void srTileRenderer::SetProjectionMatrix( const float4x4& newProjectionMatrix )
{
	m_projectionMatrix = newProjectionMatrix;
}

void srTileRenderer::SetCullMode( ECullMode newCullMode )
{
	m_cullMode = newCullMode;
}

void srTileRenderer::SetFillMode( EFillMode newFillMode )
{
	m_fillMode = newFillMode;
}

void srTileRenderer::SetVertexShader( F_VertexShader* newVertexShader )
{
	m_vertexShader = newVertexShader;
}

void srTileRenderer::SetPixelShader( F_PixelShader* newPixelShader )
{
	m_pixelShader = newPixelShader;
}

void srTileRenderer::SetTexture( SoftTexture2D* newTexture2D )
{
	m_texture = newTexture2D;
}

void srTileRenderer::DrawTriangles( SoftFrameBuffer& frameBuffer, const SVertex* vertices, UINT numVertices, const SIndex* indices, UINT numIndices )
{
	CHK_VRET_IF_NIL(m_vertexShader);
	CHK_VRET_IF_NIL(m_pixelShader);

	mxPROFILE_SCOPE("srTileRenderer :: Draw Triangles");


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
	renderContext.userPointer = this;

	renderContext.W = frameBuffer.m_viewportWidth;
	renderContext.H = frameBuffer.m_viewportHeight;
	renderContext.W2 = frameBuffer.m_viewportWidth * 0.5f;
	renderContext.H2 = frameBuffer.m_viewportHeight * 0.5f;




	const UINT numFaces = numIndices / 3;

	//DBGOUT( "\nBEGIN: srTileRenderer::DrawTriangles: %u faces\n", numFaces );

	// Break this up into batches
	UINT trianglesSoFar = 0;
	while( trianglesSoFar + FACE_BUFFER_SIZE < numFaces )
	{
		this->ProcessTriangles( vertices, numVertices, indices + trianglesSoFar*3, FACE_BUFFER_SIZE, renderContext );
		trianglesSoFar += FACE_BUFFER_SIZE;
	}

	// Handle the last set of indices, if there are any
	UINT trianglesLeft = numFaces - trianglesSoFar;
	if( trianglesLeft )
	{
		this->ProcessTriangles( vertices, numVertices, indices + trianglesSoFar*3, trianglesLeft, renderContext );
	}

	//DBGOUT( "\nEND: srTileRenderer::DrawTriangles: %u faces\n", numFaces );

	SoftRenderer::stats.numTrianglesRendered += numFaces;
}

static const ARGB32 THREAD_COLORS[8] =
{
	ARGB8_RED,
	ARGB8_GREEN,
	ARGB8_BLUE,
	ARGB8_YELLOW,
	ARGB8_WHITE,
	ARGB8_WHITE,
	ARGB8_WHITE,
	ARGB8_WHITE,
};

struct RasterizeTilesJob : AsyncJob
{
	const SoftRenderContext* m_context;
	srTileRenderer*	m_renderer;
	srTile*	m_tiles;
	UINT	m_firstTile;
	UINT	m_numTiles;

public:
	RasterizeTilesJob()
	{
		m_context = nil;
		m_renderer = nil;
		m_tiles = nil;
		m_firstTile = 0;
		m_numTiles = 0;
	}
	virtual void Run( const AsyncJob::Context& context ) override
	{
		const SoftRenderContext& drawContext = *m_context;
		const UINT lastTile = m_firstTile+m_numTiles;
		for( UINT iTile = m_firstTile; iTile < lastTile; iTile++ )
		{
			const srTile& tile = m_tiles[ iTile ];
			if( tile.bFullyCovered )
			{
				RasterizeFullyCoveredTile( tile, drawContext, m_renderer );
			}
			else
			{
				RasterizePartiallyCoveredTile( tile, drawContext, m_renderer );
			}

#if 0
			const UINT threadNum = context.threadNumber;
			const UINT colorIndex = smallest( threadNum, NUMBER_OF(THREAD_COLORS)-1 );
			const ARGB32 color = THREAD_COLORS[ colorIndex ];
			DbgDrawRect( drawContext, color, tile.GetX(), tile.GetY(), TILE_SIZE_X, TILE_SIZE_Y );
#endif
		}
	}
};

void srTileRenderer::ProcessTriangles( const SVertex* vertices, UINT numVertices, const SIndex* indices, UINT numTriangles, const SoftRenderContext& context )
{
	mxPROFILE_SCOPE("srTileRenderer :: Process Triangles");

	Assert(numTriangles <= FACE_BUFFER_SIZE);


	F_RenderSingleTriangle* drawTriangleFunction = m_ftblDrawTriangle[m_fillMode];

	(*m_ftblProcessTriangles[m_fillMode][m_cullMode])( drawTriangleFunction, vertices, numVertices, indices, numTriangles*3, context );


	const UINT oldMaxTiles = m_maxTiles;
	const UINT totalNumTiles = smallest(m_numTiles,oldMaxTiles);

	if( totalNumTiles )
	{
		mxPROFILE_SCOPE("srTileRenderer :: Rasterize Tiles");



		if( bDbg_EnableThreading )
		{
			ThreadPool& threads = GetThreadPool();

			//const UINT numThreads = threads.NumThreads();


			enum { MAX_RASTERIZER_JOBS = 256 };

			RasterizeTilesJob	rasterizeTilesJobs[MAX_RASTERIZER_JOBS];

			//enum { TILES_PER_JOB = 512 };
			enum { TILES_PER_JOB = 128 };
			//enum { TILES_PER_JOB = 64 };

			UINT numJobs = totalNumTiles/TILES_PER_JOB;
			Assert(numJobs < MAX_RASTERIZER_JOBS);
			numJobs = smallest(numJobs,MAX_RASTERIZER_JOBS);

			for( UINT iJob = 0; iJob < numJobs; iJob++ )
			{
				RasterizeTilesJob& job = rasterizeTilesJobs[ iJob ];

				job.m_context = &context;
				job.m_renderer = this;
				job.m_tiles = m_tiles;
				job.m_firstTile = iJob*TILES_PER_JOB;
				job.m_numTiles = TILES_PER_JOB;

				threads.EnqueueJob( &job );
			}

			RasterizeTilesJob	lastJob;

			const UINT tilesLeft = totalNumTiles - numJobs*TILES_PER_JOB;
			if( tilesLeft )
			{
				lastJob.m_context = &context;
				lastJob.m_renderer = this;
				lastJob.m_tiles = m_tiles;
				lastJob.m_firstTile = numJobs*TILES_PER_JOB;
				lastJob.m_numTiles = tilesLeft;

				threads.EnqueueJob( &lastJob );
			}

			threads.RunAllJobs();

			DBGOUT( "srTileRenderer::ProcessTriangles: %u faces, %u tiles (%u jobs)\n",
				m_nTransformedTris, m_numTiles, numJobs );
		}
		else
		{
			UINT numFullyCovered = 0;

			for( UINT iTile = 0; iTile < totalNumTiles; iTile++ )
			{
				const srTile& tile = m_tiles[ iTile ];

				numFullyCovered += tile.bFullyCovered;	// increment or do nothing
			}

			const UINT numPartiallyCovered = totalNumTiles - numFullyCovered;

			DBGOUT( "srTileRenderer::ProcessTriangles: %u faces, %u tiles (%u fully covered, %u partially covered)\n",
				m_nTransformedTris, m_numTiles, numFullyCovered, numPartiallyCovered );

			struct cmp_tiles_predicate
			{
				mxSTATIC_ASSERT( sizeof srTile == sizeof UINT32 );

				FORCEINLINE UINT32 operator() ( const srTile& o ) const
				{
					//return *(UINT32*)&o;
					return o.sort;
				}
			};

			cmp_tiles_predicate	predicate;
			radix_sort_3pass( m_tiles, m_sortedTiles, totalNumTiles, predicate );
			//radix_sort_4pass( m_tiles, m_sortedTiles, totalNumTiles, predicate );

			for( UINT iTile = 0; iTile < numPartiallyCovered; iTile++ )
			{
				const srTile& tile = m_sortedTiles[ iTile ];

				Assert( !tile.bFullyCovered );
				RasterizePartiallyCoveredTile( tile, context, this );
			}
			for( UINT iTile = numPartiallyCovered; iTile < totalNumTiles; iTile++ )
			{
				const srTile& tile = m_sortedTiles[ iTile ];

				Assert( tile.bFullyCovered );
				RasterizeFullyCoveredTile( tile, context, this );
			}
		}//serial





		if( SOFT_RENDER_DEBUG && SoftRenderer::bDbg_DrawBlockBounds )
		{
			//for( UINT iFace = 0; iFace < numTriangles; iFace++ )
			//{
			//	const XTriangle& face = m_transformedFaces[ iFace ];

			//	Dbg_BlockRasterizer_DrawBoundingRect( context, face.minX, face.minY, face.maxX-face.minX, face.maxY-face.minY );
			//}

			for( UINT iTile = 0; iTile < totalNumTiles; iTile++ )
			{
				const srTile& tile = m_tiles[ iTile ];

				if( tile.bFullyCovered )
				{
					Dbg_BlockRasterizer_DrawFullyCoveredRect( context, tile.GetX(), tile.GetY(), TILE_SIZE_X, TILE_SIZE_Y );
				}
				else
				{
					Dbg_BlockRasterizer_DrawPartiallyCoveredRect( context, tile.GetX(), tile.GetY(), TILE_SIZE_X, TILE_SIZE_Y );
				}
			}
		}


		// resize buffers if needed

		if( m_numTiles > oldMaxTiles )
		{
			m_maxTiles = NextPowerOfTwo( totalNumTiles );

			mxFree( m_tiles );
			mxFree( m_sortedTiles );

			m_tiles = (srTile*) mxAlloc( m_maxTiles * sizeof m_tiles[0] );
			m_sortedTiles = (srTile*) mxAlloc( m_maxTiles * sizeof m_sortedTiles[0] );

			//MemCopy( m_sortedTiles, m_tiles, totalNumTiles * sizeof m_tiles[0] );

			DBGOUT("!!! Resizing tile buffer from %u to %u (%u KiB)\n",
				oldMaxTiles,m_maxTiles,m_maxTiles*sizeof m_tiles[0] /mxKIBIBYTE);
		}

		m_numTiles = 0;
	}//if( totalNumTiles )

	m_nTransformedTris = 0;
}

}//namespace SoftRenderer
