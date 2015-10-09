#include "SoftMath.h"

// BLOCK_SIZE_X=16 and BLOCK_SIZE_Y=8 are good values
template< UINT BLOCK_SIZE_X, UINT BLOCK_SIZE_Y >
static inline
void RasterizeTriangleImmediateColorOnly_SSE(
									//NOTABUG: swap v2 and v3 because of triangle winding order and our edge functions sign calculation
									const XVertex& v1, const XVertex& v3, const XVertex& v2,
									const SoftRenderContext& context
						   )
{
	mxPROFILE_SCOPE("Rasterize Triangle");

	// 28.4 fixed-point coordinates
	// (4 bits of sub-pixel precision, enough for a 2048x2048 color buffer)
	enum { SHIFT = 4 };


	const ARGB32 COLOR = ARGB8_WHITE;


	const int W = context.W;	// viewport width
	//const int H = context.H;	// viewport height


	const F4 fX1 = v1.P.x;
	const F4 fX2 = v2.P.x;
	const F4 fX3 = v3.P.x;

	const F4 fY1 = v1.P.y;
	const F4 fY2 = v2.P.y;
	const F4 fY3 = v3.P.y;

	// 24.8 fixed-point
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
	const INT32 nMinX = ((Min3(X1, X2, X3) + 0xF) >> SHIFT) & ~(BLOCK_SIZE_X - 1);	// start in block corner
	const INT32 nMaxX = ((Max3(X1, X2, X3) + 0xF) >> SHIFT);
	const INT32 nMinY = ((Min3(Y1, Y2, Y3) + 0xF) >> SHIFT) & ~(BLOCK_SIZE_Y - 1);	// start in block corner
	const INT32 nMaxY = ((Max3(Y1, Y2, Y3) + 0xF) >> SHIFT);



	//const UINT stride = W * sizeof SoftPixel;	// aka 'pitch'
	SoftPixel* startPixels = context.colorBuffer + nMinY * W;	// color buffer
	ZBufElem* startZBuffer = context.depthBuffer + nMinY * W;	// depth buffer


	SPixelShaderParameters	pixelShaderArgs;
	pixelShaderArgs.globals = context.globals;


	const __m128i qiOffsetDY12 = _mm_set_epi32( FDY12 * 3, FDY12 * 2, FDY12 * 1, FDY12 * 0 );
	const __m128i qiOffsetDY23 = _mm_set_epi32( FDY23 * 3, FDY23 * 2, FDY23 * 1, FDY23 * 0 );
	const __m128i qiOffsetDY31 = _mm_set_epi32( FDY31 * 3, FDY31 * 2, FDY31 * 1, FDY31 * 0 );

	const __m128i qiFDY12_4 = _mm_set1_epi32( FDY12 * SSE_REG_WIDTH );
	const __m128i qiFDY23_4 = _mm_set1_epi32( FDY23 * SSE_REG_WIDTH );
	const __m128i qiFDY31_4 = _mm_set1_epi32( FDY31 * SSE_REG_WIDTH );


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


			SoftPixel* pixels = startPixels;
			ZBufElem* zbuffer = startZBuffer;

			// Accept whole block when totally covered (all four corners are inside triangle and their coverage masks are (1|2|4|8) = 15)

			//OPTIMIZED: replaced 'and' with sum
#if 0
			if( a == 0xF && b == 0xF && c == 0xF )
#else
			if( a + b + c == (0xF + 0xF + 0xF) )
#endif
			{
				for( UINT iY = iBlockY; iY < iBlockY + BLOCK_SIZE_Y; iY++ )
				{
					for( UINT iX = iBlockX; iX < iBlockX + BLOCK_SIZE_X; iX += SSE_REG_WIDTH )
					{
						__m128i* dest = (__m128i*) (pixels + iX);
						Assert(IS_16_BYTE_ALIGNED(dest));

						_mm_store_si128( dest, _mm_set1_epi32(COLOR) );

					}//for x

					pixels += W;
					zbuffer += W;
				}//for y

				SoftRenderer::Dbg_BlockRasterizer_DrawFullyCoveredRect( context, FBlockX0 >> 4, FBlockY0 >> 4, BLOCK_SIZE_X, BLOCK_SIZE_Y );
				// End of fully covered block
			}
			else // Partially covered block
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

					__m128i qiCX1 = _mm_sub_epi32( qiCY1, qiOffsetDY12 );
					__m128i qiCX2 = _mm_sub_epi32( qiCY2, qiOffsetDY23 );
					__m128i qiCX3 = _mm_sub_epi32( qiCY3, qiOffsetDY31 );

					for( UINT iX = iBlockX; iX < iBlockX + BLOCK_SIZE_X; iX += SSE_REG_WIDTH )
					{
						const __m128i qiCX1mask = _mm_cmpgt_epi32( qiCX1, _mm_setzero_si128() );
						const __m128i qiCX2mask = _mm_cmpgt_epi32( qiCX2, _mm_setzero_si128() );
						const __m128i qiCX3mask = _mm_cmpgt_epi32( qiCX3, _mm_setzero_si128() );
						const __m128i qiCXmask = _mm_and_si128( qiCX1mask, _mm_and_si128( qiCX2mask, qiCX3mask ) );

						__m128i* dest = (__m128i*) (pixels + iX);
						Assert(IS_16_BYTE_ALIGNED(dest));

						// load previous 4 pixels
						const __m128i oldQuad = _mm_load_si128( dest );	

						const __m128i result = _mm_or_si128( oldQuad, _mm_and_si128( _mm_set1_epi32(COLOR), qiCXmask ) );

						_mm_store_si128( dest, result );

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

				SoftRenderer::Dbg_BlockRasterizer_DrawPartiallyCoveredRect( context, FBlockX0 >> SHIFT, FBlockY0 >> SHIFT, BLOCK_SIZE_X, BLOCK_SIZE_Y );

			}//Partially covered block

		}//for each block on X axis

		startPixels += W * BLOCK_SIZE_Y;
		startZBuffer += W * BLOCK_SIZE_Y;

	}//for each block on Y axis
}





// BLOCK_SIZE_X=16 and BLOCK_SIZE_Y=8 are good values
template< UINT BLOCK_SIZE_X, UINT BLOCK_SIZE_Y >
static inline
void RasterizeTriangleImmediateDepthOnly_SSE(
									//NOTABUG: swap v2 and v3 because of triangle winding order and our edge functions sign calculation
									const XVertex& v1, const XVertex& v3, const XVertex& v2,
									const SoftRenderContext& context
						   )
{
	mxPROFILE_SCOPE("Rasterize Triangle");

	// 28.4 fixed-point coordinates
	// (4 bits of sub-pixel precision, enough for a 2048x2048 color buffer)
	enum { SHIFT = 4 };



	const int W = context.W;	// viewport width
	//const int H = context.H;	// viewport height


	const F4 fX1 = v1.P.x;
	const F4 fX2 = v2.P.x;
	const F4 fX3 = v3.P.x;

	const F4 fY1 = v1.P.y;
	const F4 fY2 = v2.P.y;
	const F4 fY3 = v3.P.y;

	const F4 fZ1 = v1.P.z;
	const F4 fZ2 = v2.P.z;
	const F4 fZ3 = v3.P.z;


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

	mxSIMDALIGNED Vec2D	vZ;

	ComputeGradient_FPU(
		INTERP_C,
		fDeltaZ21, fDeltaZ31,
		fDeltaX21, fDeltaX31,
		fDeltaY21, fDeltaY31,
		vZ.x, vZ.y
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


	// Bounding rectangle of this triangle in screen space
	const INT32 nMinX = ((Min3(X1, X2, X3) + 0xF) >> SHIFT) & ~(BLOCK_SIZE_X - 1);	// start in block corner
	const INT32 nMaxX = ((Max3(X1, X2, X3) + 0xF) >> SHIFT);
	const INT32 nMinY = ((Min3(Y1, Y2, Y3) + 0xF) >> SHIFT) & ~(BLOCK_SIZE_Y - 1);	// start in block corner
	const INT32 nMaxY = ((Max3(Y1, Y2, Y3) + 0xF) >> SHIFT);



	//const UINT stride = W * sizeof SoftPixel;	// aka 'pitch'
	SoftPixel* startPixels = context.colorBuffer + nMinY * W;	// color buffer
	ZBufElem* startZBuffer = context.depthBuffer + nMinY * W;	// depth buffer


	SPixelShaderParameters	pixelShaderArgs;
	pixelShaderArgs.globals = context.globals;



	const __m128 qf3210 = _mm_set_ps( 3.0f, 2.0f, 1.0f, 0.0f );
	//const __m128 qf4444 = _mm_set_ps1( 4.0f );
	static const __m128 qf255x4 = _mm_set_ps1( 255.0f );


	const __m128i qiOffsetDY12 = _mm_set_epi32( FDY12 * 3, FDY12 * 2, FDY12 * 1, FDY12 * 0 );
	const __m128i qiOffsetDY23 = _mm_set_epi32( FDY23 * 3, FDY23 * 2, FDY23 * 1, FDY23 * 0 );
	const __m128i qiOffsetDY31 = _mm_set_epi32( FDY31 * 3, FDY31 * 2, FDY31 * 1, FDY31 * 0 );

	const __m128i qiFDY12_4 = _mm_set1_epi32( FDY12 * SSE_REG_WIDTH );
	const __m128i qiFDY23_4 = _mm_set1_epi32( FDY23 * SSE_REG_WIDTH );
	const __m128i qiFDY31_4 = _mm_set1_epi32( FDY31 * SSE_REG_WIDTH );

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

					__m128i qiCX1 = _mm_sub_epi32( qiCY1, qiOffsetDY12 );
					__m128i qiCX2 = _mm_sub_epi32( qiCY2, qiOffsetDY23 );
					__m128i qiCX3 = _mm_sub_epi32( qiCY3, qiOffsetDY31 );

					for( UINT iX = iBlockX; iX < iBlockX + BLOCK_SIZE_X; iX += SSE_REG_WIDTH )
					{
						const __m128i qiCX1mask = _mm_cmpgt_epi32( qiCX1, _mm_setzero_si128() );
						const __m128i qiCX2mask = _mm_cmpgt_epi32( qiCX2, _mm_setzero_si128() );
						const __m128i qiCX3mask = _mm_cmpgt_epi32( qiCX3, _mm_setzero_si128() );
						const __m128i qiEdgeMask = _mm_and_si128( qiCX1mask, _mm_and_si128( qiCX2mask, qiCX3mask ) );

						F4* depth = (zbuffer + iX);
						Assert(IS_16_BYTE_ALIGNED(depth));

						__m128i* dest = (__m128i*) (pixels + iX);
						Assert(IS_16_BYTE_ALIGNED(dest));


						//#######[LOAD] load previous depth
						const __m128 qfOldDepth = _mm_load_ps( depth );

						//###[LHS]
						// start value for x and y
						const F4 fX = (F4)iX - fX1;	//<=###[LHS]
						const F4 fY = (F4)iY - fY1;	//<=###[LHS]

						// interpolate depth
						const __m128 qfvZx = _mm_set1_ps( vZ.x );
						const __m128 qfZ0 = _mm_set1_ps( fZ1 + vZ.x * fX + vZ.y * fY );
						const __m128 qfZ = _mm_add_ps( qfZ0, _mm_mul_ps( qfvZx, qf3210 ) );

						const __m128i qiDepthMask = (__m128i&) _mm_cmple_ps( qfZ, qfOldDepth );
						//####[COND.BRANCH] perform depth testing
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

				SoftRenderer::Dbg_BlockRasterizer_DrawPartiallyCoveredRect( context, FBlockX0 >> SHIFT, FBlockY0 >> SHIFT, BLOCK_SIZE_X, BLOCK_SIZE_Y );

			}//Partially covered block

		}//for each block on X axis

		startPixels += W * BLOCK_SIZE_Y;
		startZBuffer += W * BLOCK_SIZE_Y;

	}//for each block on Y axis
}



// BLOCK_SIZE_X=16 and BLOCK_SIZE_Y=8 are good values
template< UINT BLOCK_SIZE_X, UINT BLOCK_SIZE_Y >
static inline
void RasterizeTriangleImmediateUserShader_SSE(
									//NOTABUG: swap v2 and v3 because of triangle winding order and our edge functions sign calculation
									const XVertex& v1, const XVertex& v3, const XVertex& v2,
									const SoftRenderContext& context
						   )
{
	mxPROFILE_SCOPE("Rasterize Triangle");

	// 28.4 fixed-point coordinates
	// (4 bits of sub-pixel precision, enough for a 2048x2048 color buffer)
	enum { SHIFT = 4 };



	const int W = context.W;	// viewport width
	//const int H = context.H;	// viewport height


	const F4 fX1 = v1.P.x;
	const F4 fX2 = v2.P.x;
	const F4 fX3 = v3.P.x;

	const F4 fY1 = v1.P.y;
	const F4 fY2 = v2.P.y;
	const F4 fY3 = v3.P.y;

	const F4 fZ1 = v1.P.z;
	const F4 fZ2 = v2.P.z;
	const F4 fZ3 = v3.P.z;

	// 24.8 fixed-point
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

	mxSIMDALIGNED Vec2D	vZ;

	ComputeGradient_FPU(
		INTERP_C,
		fDeltaZ21, fDeltaZ31,
		fDeltaX21, fDeltaX31,
		fDeltaY21, fDeltaY31,
		vZ.x, vZ.y
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


	// Bounding rectangle of this triangle in screen space
	const INT32 nMinX = ((Min3(X1, X2, X3) + 0xF) >> SHIFT) & ~(BLOCK_SIZE_X - 1);	// start in block corner
	const INT32 nMaxX = ((Max3(X1, X2, X3) + 0xF) >> SHIFT);
	const INT32 nMinY = ((Min3(Y1, Y2, Y3) + 0xF) >> SHIFT) & ~(BLOCK_SIZE_Y - 1);	// start in block corner
	const INT32 nMaxY = ((Max3(Y1, Y2, Y3) + 0xF) >> SHIFT);




	//UINT numProcessedTiles = 0;



	//const UINT stride = W * sizeof SoftPixel;	// aka 'pitch'
	SoftPixel* startPixels = context.colorBuffer + nMinY * W;	// color buffer
	ZBufElem* startZBuffer = context.depthBuffer + nMinY * W;	// depth buffer


	SPixelShaderParameters	pixelShaderArgs;
	pixelShaderArgs.globals = context.globals;



	const __m128 qf3210 = _mm_set_ps( 3.0f, 2.0f, 1.0f, 0.0f );
	//const __m128 qf4444 = _mm_set_ps1( 4.0f );
	static const __m128 qf255x4 = _mm_set_ps1( 255.0f );


	const __m128i qiOffsetDY12 = _mm_set_epi32( FDY12 * 3, FDY12 * 2, FDY12 * 1, FDY12 * 0 );
	const __m128i qiOffsetDY23 = _mm_set_epi32( FDY23 * 3, FDY23 * 2, FDY23 * 1, FDY23 * 0 );
	const __m128i qiOffsetDY31 = _mm_set_epi32( FDY31 * 3, FDY31 * 2, FDY31 * 1, FDY31 * 0 );

	const __m128i qiFDY12_4 = _mm_set1_epi32( FDY12 * SSE_REG_WIDTH );
	const __m128i qiFDY23_4 = _mm_set1_epi32( FDY23 * SSE_REG_WIDTH );
	const __m128i qiFDY31_4 = _mm_set1_epi32( FDY31 * SSE_REG_WIDTH );

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

					__m128i qiCX1 = _mm_sub_epi32( qiCY1, qiOffsetDY12 );
					__m128i qiCX2 = _mm_sub_epi32( qiCY2, qiOffsetDY23 );
					__m128i qiCX3 = _mm_sub_epi32( qiCY3, qiOffsetDY31 );

					for( UINT iX = iBlockX; iX < iBlockX + BLOCK_SIZE_X; iX += SSE_REG_WIDTH )
					{
						const __m128i qiCX1mask = _mm_cmpgt_epi32( qiCX1, _mm_setzero_si128() );
						const __m128i qiCX2mask = _mm_cmpgt_epi32( qiCX2, _mm_setzero_si128() );
						const __m128i qiCX3mask = _mm_cmpgt_epi32( qiCX3, _mm_setzero_si128() );
						const __m128i qiEdgeMask = _mm_and_si128( qiCX1mask, _mm_and_si128( qiCX2mask, qiCX3mask ) );

						F4* depth = (zbuffer + iX);
						Assert(IS_16_BYTE_ALIGNED(depth));

						__m128i* dest = (__m128i*) (pixels + iX);
						Assert(IS_16_BYTE_ALIGNED(dest));


						//#######[LOAD] load previous depth
						const __m128 qfOldDepth = _mm_load_ps( depth );

						//###[LHS]
						// start value for x and y
						const F4 fX = (F4)iX - fX1;	//<=###[LHS]
						const F4 fY = (F4)iY - fY1;	//<=###[LHS]

						// interpolate depth
						const __m128 qfvZx = _mm_set1_ps( vZ.x );
						const __m128 qfZ0 = _mm_set1_ps( fZ1 + vZ.x * fX + vZ.y * fY );
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


						//numProcessedTiles++;

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

				SoftRenderer::Dbg_BlockRasterizer_DrawPartiallyCoveredRect( context, FBlockX0 >> SHIFT, FBlockY0 >> SHIFT, BLOCK_SIZE_X, BLOCK_SIZE_Y );

			}//Partially covered block

#if SOFT_RENDER_DEBUG
			// all four corners are inside triangle and their coverage masks are (1|2|4|8) = 15
			if( a + b + c == (0xF + 0xF + 0xF) )
			{
				SoftRenderer::Dbg_BlockRasterizer_DrawFullyCoveredRect( context, FBlockX0 >> SHIFT, FBlockY0 >> SHIFT, BLOCK_SIZE_X, BLOCK_SIZE_Y );
			}
#endif // SOFT_RENDER_DEBUG


		}//for each block on X axis

		startPixels += W * BLOCK_SIZE_Y;
		startZBuffer += W * BLOCK_SIZE_Y;

	}//for each block on Y axis

	//DEVOUT( "RasterizeTriangleSSE: %u tiles\n",numProcessedTiles);
}

