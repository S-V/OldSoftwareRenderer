#include "SoftMath.h"

static inline
void RasterizeTriangle_SSE(
						   const XVertex& rV1, const XVertex& rV2, const XVertex& rV3,
						   const SoftRenderContext& context
						   )
{
	mxPROFILE_SCOPE("Rasterize Triangle");

	const XVertex* __restrict v1 = &rV1;
	const XVertex* __restrict v2 = &rV2;
	const XVertex* __restrict v3 = &rV3;

	TSwap( v2, v3 );


	//enum { SHIFT = 2 };
	//enum { SHIFT = 3 };
	enum { SHIFT = 4 };	//<= works best (OKeish)
	//enum { SHIFT = 5 };
	//enum { SHIFT = 6 };
	//enum { SHIFT = 7 };
	//enum { SHIFT = 8 };	<= very BAD


	const int W = context.W;	// viewport width
	//const int H = context.H;	// viewport height


	const F4 fX1 = v1->P.x;
	const F4 fX2 = v2->P.x;
	const F4 fX3 = v3->P.x;

	const F4 fY1 = v1->P.y;
	const F4 fY2 = v2->P.y;
	const F4 fY3 = v3->P.y;

	// 28.4 fixed-point coordinates
	// (12 bit resolution, enough for a 4096x4096 color buffer)
	// (4 bits of sub-pixel precision, enough for a 2048x2048 color buffer)
	// 
	const INT32 X1 = iround( F4(1<<SHIFT) * fX1 );
	const INT32 X2 = iround( F4(1<<SHIFT) * fX2 );
	const INT32 X3 = iround( F4(1<<SHIFT) * fX3 );

	const INT32 Y1 = iround( F4(1<<SHIFT) * fY1 );
	const INT32 Y2 = iround( F4(1<<SHIFT) * fY2 );
	const INT32 Y3 = iround( F4(1<<SHIFT) * fY3 );

	//const INT32 Z1 = iround( 16.0f * fZ1 );
	//const INT32 Z2 = iround( 16.0f * fZ2 );
	//const INT32 Z3 = iround( 16.0f * fZ3 );

	//const INT32 W1 = iround( 16.0f * fInvW1 );
	//const INT32 W2 = iround( 16.0f * fInvW2 );
	//const INT32 W3 = iround( 16.0f * fInvW3 );
	// 
	// deltas
	const INT32 DeltaX12 = X1 - X2;
	const INT32 DeltaX23 = X2 - X3;
	const INT32 DeltaX31 = X3 - X1;

	const INT32 DeltaY12 = Y1 - Y2;
	const INT32 DeltaY23 = Y2 - Y3;
	const INT32 DeltaY31 = Y3 - Y1;


	// 24.8 Fixed-point deltas
	const INT32 FDX12 = DeltaX12 << SHIFT;
	const INT32 FDX23 = DeltaX23 << SHIFT;
	const INT32 FDX31 = DeltaX31 << SHIFT;

	const INT32 FDY12 = DeltaY12 << SHIFT;
	const INT32 FDY23 = DeltaY23 << SHIFT;
	const INT32 FDY31 = DeltaY31 << SHIFT;





	// Block size, standard 8x8 (must be power of two)
	// NOTE: should be 8, must be small enough so that accumulative errors don't grow too much
	//enum { BLOCK_SIZE = 8 };	//enum { BLOCK_SIZE_LOG2 = 3 };
	//enum { BLOCK_SIZE = 16 };
	//enum { BLOCK_SIZE = 32 };
	//enum { BLOCK_SIZE = 4 };
	//enum { BLOCK_SIZE = 2 };
	// 

	enum { BLOCK_SIZE_X = 16 };
	enum { BLOCK_SIZE_Y = 8 };

	enum { SSE_REG_WIDTH = 4 };


	// "A Parallel Algorithm for Polygon Rasterization":
	// Since the edge function is linear,  it is possible to compute the value 
	// of the  edge function for a pixel an arbitrary distance L away from a 
	// given point (x,y) :

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


	// Bounding rectangle of this triangle in screen space

	// Adding 0xF and shifting right 4 bits is the equivalent of a ceil() function for the 28.4 fixed-point format.
	// The first pixel to be filled is the one strictly to the right of the first edge,
	// so that's why a ceil() operation is used (the fill convention for colorBuffer spot on the edge is taken care of a few lines lower).

	INT32 nMinX = (Min3(X1, X2, X3) + 0xF) >> SHIFT;
	INT32 nMaxX = (Max3(X1, X2, X3) + 0xF) >> SHIFT;
	INT32 nMinY = (Min3(Y1, Y2, Y3) + 0xF) >> SHIFT;
	INT32 nMaxY = (Max3(Y1, Y2, Y3) + 0xF) >> SHIFT;


	// Start in corner of 8x8 block
	nMinX &= ~(BLOCK_SIZE_X - 1);
	nMinY &= ~(BLOCK_SIZE_Y - 1);


	//const UINT stride = W * sizeof SoftPixel;	// aka 'pitch'
	SoftPixel* startPixels = context.colorBuffer + nMinY * W;	// color buffer
	ZBufElem* startZBuffer = context.depthBuffer + nMinY * W;	// depth buffer


	SPixelShaderParameters	pixelShaderArgs;
	pixelShaderArgs.globals = context.globals;


	const __m128i qiOffsetDY12 = _mm_set_epi32( FDY12 * 3, FDY12 * 2, FDY12 * 1, FDY12 * 0 );
	const __m128i qiOffsetDY23 = _mm_set_epi32( FDY23 * 3, FDY23 * 2, FDY23 * 1, FDY23 * 0 );
	const __m128i qiOffsetDY31 = _mm_set_epi32( FDY31 * 3, FDY31 * 2, FDY31 * 1, FDY31 * 0 );

	const __m128i qiFDY12_4 = _mm_set1_epi32( FDY12 * 4 );
	const __m128i qiFDY23_4 = _mm_set1_epi32( FDY23 * 4 );
	const __m128i qiFDY31_4 = _mm_set1_epi32( FDY31 * 4 );


	for( UINT iBlockY = nMinY; iBlockY < nMaxY; iBlockY += BLOCK_SIZE_Y )
	{
		for( UINT iBlockX = nMinX; iBlockX < nMaxX; iBlockX += BLOCK_SIZE_X )
		{
			// Corners of block in 28.4 fixed-point (4 bits of sub-pixel accuracy)
			const UINT FBlockX0 = (iBlockX << SHIFT);
			const UINT FBlockX1 = (iBlockX + (BLOCK_SIZE_X - 1)) << SHIFT;
			const UINT FBlockY0 = (iBlockY << SHIFT);
			const UINT FBlockY1 = (iBlockY + (BLOCK_SIZE_Y - 1)) << SHIFT;


			// Evaluate half-space functions in the 4 corners of the block

			const UINT a00 = 1-INT32_SIGN_BIT_SET( C1 + DeltaX12 * FBlockY0 - DeltaY12 * FBlockX0 );
			const UINT a10 = 1-INT32_SIGN_BIT_SET( C1 + DeltaX12 * FBlockY0 - DeltaY12 * FBlockX1 );
			const UINT a01 = 1-INT32_SIGN_BIT_SET( C1 + DeltaX12 * FBlockY1 - DeltaY12 * FBlockX0 );
			const UINT a11 = 1-INT32_SIGN_BIT_SET( C1 + DeltaX12 * FBlockY1 - DeltaY12 * FBlockX1 );
			const UINT a = (a00 << 0) | (a10 << 1) | (a01 << 2) | (a11 << 3);	// compose 4-bit mask for all four corners

			const UINT b00 = 1-INT32_SIGN_BIT_SET( C2 + DeltaX23 * FBlockY0 - DeltaY23 * FBlockX0 );
			const UINT b10 = 1-INT32_SIGN_BIT_SET( C2 + DeltaX23 * FBlockY0 - DeltaY23 * FBlockX1 );
			const UINT b01 = 1-INT32_SIGN_BIT_SET( C2 + DeltaX23 * FBlockY1 - DeltaY23 * FBlockX0 );
			const UINT b11 = 1-INT32_SIGN_BIT_SET( C2 + DeltaX23 * FBlockY1 - DeltaY23 * FBlockX1 );
			const UINT b = (b00 << 0) | (b10 << 1) | (b01 << 2) | (b11 << 3);	// compose 4-bit mask for all four corners

			const UINT c00 = 1-INT32_SIGN_BIT_SET( C3 + DeltaX31 * FBlockY0 - DeltaY31 * FBlockX0 );
			const UINT c10 = 1-INT32_SIGN_BIT_SET( C3 + DeltaX31 * FBlockY0 - DeltaY31 * FBlockX1 );
			const UINT c01 = 1-INT32_SIGN_BIT_SET( C3 + DeltaX31 * FBlockY1 - DeltaY31 * FBlockX0 );
			const UINT c11 = 1-INT32_SIGN_BIT_SET( C3 + DeltaX31 * FBlockY1 - DeltaY31 * FBlockX1 );
			const UINT c = (c00 << 0) | (c10 << 1) | (c01 << 2) | (c11 << 3);	// compose 4-bit mask for all four corners



			// Skip block when outside an edge => outside the triangle

			if( a == 0 || b == 0 || c == 0 ) {
				continue;
			}


			SoftPixel* pixels = startPixels;
			ZBufElem* zbuffer = startZBuffer;


			{
				// in 28.4
				INT32 CY1 = C1 + DeltaX12 * FBlockY0 - DeltaY12 * FBlockX0;
				INT32 CY2 = C2 + DeltaX23 * FBlockY0 - DeltaY23 * FBlockX0;
				INT32 CY3 = C3 + DeltaX31 * FBlockY0 - DeltaY31 * FBlockX0;

				for( UINT iY = iBlockY; iY < iBlockY + BLOCK_SIZE_Y; iY++ )
				{
					const __m128i	qiCY1 = _mm_set1_epi32( CY1 );
					const __m128i	qiCY2 = _mm_set1_epi32( CY2 );
					const __m128i	qiCY3 = _mm_set1_epi32( CY3 );




					__m128i cx1_mask;
					__m128i cx2_mask;
					__m128i cx3_mask;
					__m128i cx_mask_comp;
					UINT32 mask;



					__m128i qiCX1 = qiCY1;
					__m128i qiCX2 = qiCY2;
					__m128i qiCX3 = qiCY3;

					qiCX1 = _mm_sub_epi32( qiCX1, qiOffsetDY12 );
					qiCX2 = _mm_sub_epi32( qiCX2, qiOffsetDY23 );
					qiCX3 = _mm_sub_epi32( qiCX3, qiOffsetDY31 );

					cx1_mask = _mm_cmpgt_epi32( qiCX1, _mm_setzero_si128() );
					cx2_mask = _mm_cmpgt_epi32( qiCX2, _mm_setzero_si128() );
					cx3_mask = _mm_cmpgt_epi32( qiCX3, _mm_setzero_si128() );

					cx_mask_comp = _mm_and_si128( cx1_mask, _mm_and_si128( cx2_mask, cx3_mask ) );
					///*const*/ UINT32 mask = _mm_movemask_ps((__m128&)_mm_and_si128(cx_mask_comp, _mm_set1_epi32(0xF0000000)));
					mask = _mm_movemask_ps((__m128&)cx_mask_comp);

					if( mask & 1 )	pixels[ iBlockX + 0 ] = RGBA8_WHITE;
					if( mask & 2 )	pixels[ iBlockX + 1 ] = RGBA8_WHITE;
					if( mask & 4 )	pixels[ iBlockX + 2 ] = RGBA8_WHITE;
					if( mask & 8 )	pixels[ iBlockX + 3 ] = RGBA8_WHITE;





					qiCX1 = _mm_sub_epi32( qiCX1, qiFDY12_4 );
					qiCX2 = _mm_sub_epi32( qiCX2, qiFDY23_4 );
					qiCX3 = _mm_sub_epi32( qiCX3, qiFDY31_4 );

					cx1_mask = _mm_cmpgt_epi32( qiCX1, _mm_setzero_si128() );
					cx2_mask = _mm_cmpgt_epi32( qiCX2, _mm_setzero_si128() );
					cx3_mask = _mm_cmpgt_epi32( qiCX3, _mm_setzero_si128() );

					cx_mask_comp = _mm_and_si128( cx1_mask, _mm_and_si128( cx2_mask, cx3_mask ) );
					mask = _mm_movemask_ps((__m128&)cx_mask_comp);

					if( mask & 1 )	pixels[ iBlockX+4 + 0 ] = RGBA8_WHITE;
					if( mask & 2 )	pixels[ iBlockX+4 + 1 ] = RGBA8_WHITE;
					if( mask & 4 )	pixels[ iBlockX+4 + 2 ] = RGBA8_WHITE;
					if( mask & 8 )	pixels[ iBlockX+4 + 3 ] = RGBA8_WHITE;






					qiCX1 = _mm_sub_epi32( qiCX1, qiFDY12_4 );
					qiCX2 = _mm_sub_epi32( qiCX2, qiFDY23_4 );
					qiCX3 = _mm_sub_epi32( qiCX3, qiFDY31_4 );

					cx1_mask = _mm_cmpgt_epi32( qiCX1, _mm_setzero_si128() );
					cx2_mask = _mm_cmpgt_epi32( qiCX2, _mm_setzero_si128() );
					cx3_mask = _mm_cmpgt_epi32( qiCX3, _mm_setzero_si128() );

					cx_mask_comp = _mm_and_si128( cx1_mask, _mm_and_si128( cx2_mask, cx3_mask ) );
					mask = _mm_movemask_ps((__m128&)cx_mask_comp);

					if( mask & 1 )	pixels[ iBlockX+4+4 + 0 ] = RGBA8_WHITE;
					if( mask & 2 )	pixels[ iBlockX+4+4 + 1 ] = RGBA8_WHITE;
					if( mask & 4 )	pixels[ iBlockX+4+4 + 2 ] = RGBA8_WHITE;
					if( mask & 8 )	pixels[ iBlockX+4+4 + 3 ] = RGBA8_WHITE;






					qiCX1 = _mm_sub_epi32( qiCX1, qiFDY12_4 );
					qiCX2 = _mm_sub_epi32( qiCX2, qiFDY23_4 );
					qiCX3 = _mm_sub_epi32( qiCX3, qiFDY31_4 );

					cx1_mask = _mm_cmpgt_epi32( qiCX1, _mm_setzero_si128() );
					cx2_mask = _mm_cmpgt_epi32( qiCX2, _mm_setzero_si128() );
					cx3_mask = _mm_cmpgt_epi32( qiCX3, _mm_setzero_si128() );

					cx_mask_comp = _mm_and_si128( cx1_mask, _mm_and_si128( cx2_mask, cx3_mask ) );
					mask = _mm_movemask_ps((__m128&)cx_mask_comp);

					if( mask & 1 )	pixels[ iBlockX+4+4+4 + 0 ] = RGBA8_WHITE;
					if( mask & 2 )	pixels[ iBlockX+4+4+4 + 1 ] = RGBA8_WHITE;
					if( mask & 4 )	pixels[ iBlockX+4+4+4 + 2 ] = RGBA8_WHITE;
					if( mask & 8 )	pixels[ iBlockX+4+4+4 + 3 ] = RGBA8_WHITE;






					CY1 += FDX12;
					CY2 += FDX23;
					CY3 += FDX31;

					pixels += W;
					zbuffer += W;
				}//for y

				Dbg_BlockRasterizer_DrawPartiallyCoveredRect( context, FBlockX0 >> SHIFT, FBlockY0 >> SHIFT, BLOCK_SIZE_X, BLOCK_SIZE_Y );

			}//Partially covered block

		}//for each block on X axis

		startPixels += W * BLOCK_SIZE_Y;
		startZBuffer += W * BLOCK_SIZE_Y;

	}//for each block on Y axis
}
