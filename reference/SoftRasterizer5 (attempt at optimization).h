#pragma once

#include <Base/Util/Rectangle.h>

static FORCEINLINE
INT iround( FLOAT x )
{
#if SOFT_RENDER_USE_SSE
	//return _mm_cvt_ss2si( _mm_load_ps( &x ) );
	return asmFloatToInt( x );
#else
	INT retval;
	ASM fld		x
	ASM fistp	retval
	return retval;	// use default rouding mode (round nearest)
#endif
}

#if SOFT_RENDER_DEBUG
	inline
	void DbgDrawRect( const SoftRenderContext& context, SoftPixel color, int topLeftX, int topLeftY, int width, int height )
	{
		const int W = context.W;	// viewport width
		const int H = context.H;	// viewport height

		TRectArea<int>	screen(0,0,W-1,H-1);
		TRectArea<int>	rect(topLeftX,topLeftY,width,height);
		rect.Clip(screen);

		context.renderer->DrawLine2D( rect.TopLeftX, rect.TopLeftY, rect.TopLeftX + rect.Width, rect.TopLeftY, color );
		context.renderer->DrawLine2D( rect.TopLeftX + rect.Width, rect.TopLeftY, rect.TopLeftX + rect.Width, rect.TopLeftY + rect.Height, color );
		context.renderer->DrawLine2D( rect.TopLeftX + rect.Width, rect.TopLeftY + rect.Height, rect.TopLeftX, rect.TopLeftY + rect.Height, color );
		context.renderer->DrawLine2D( rect.TopLeftX, rect.TopLeftY + rect.Height, rect.TopLeftX, rect.TopLeftY, color );
	}
#endif // SOFT_RENDER_DEBUG


// 'half spaces' rasterization
// See: http://devmaster.net/forums/topic/1145-advanced-rasterization/
// and http://www.cs.unc.edu/~olano/papers/2dh-tri/
//
template< ECullMode CULL_MODE >
static inline
void TemplateRasterizeTriangle( const XVertex& v1, const XVertex& v2, const XVertex& v3, const SoftRenderContext& context )
{
	mxPROFILE_SCOPE("Rasterize Triangle");

	const int W = context.W;	// viewport width
	const int H = context.H;	// viewport height


#if SOFT_RENDER_USE_HALFSPACE_RASTERIZER

	const float fX1 = v1.P.x;
	const float fX2 = v2.P.x;
	const float fX3 = v3.P.x;

	const float fY1 = v1.P.y;
	const float fY2 = v2.P.y;
	const float fY3 = v3.P.y;

	//const float fZ1 = v1.P.z;
	//const float fZ2 = v2.P.z;
	//const float fZ3 = v3.P.z;

	//const float fW1 = v1.P.w;
	//const float fW2 = v2.P.w;
	//const float fW3 = v3.P.w;





	/* excerpt from "A Parallel Algorithm for Polygon Rasterization" by Juan Pineda
	(Computer Graphics, Volume 22, Number 4, August 1988).

	Typically in  3D graphics rendering, polygon vertices are in floating
	point  format  after  3D  transformation  and  projection.  Some
	implementations  round  the  X  and  Y floating  point  ordinates to
	integer  values, so that  simple  integer  line  algorithms can be used
	compute the  triangle  edges.  This rounding can leave  gaps on the
	order of 1/2 pixel wide between adjacent polygons that do not share
	common vertices.  Gaps also occur as a result of the  finite precision
	used in specifying the endpoints, but these gaps are much narrower. 
	Some implementations attempt to eUminate these gaps by growing the 
	edges of triangles to insure  overlap, but these  solutions cause other
	artifacts. 
	In order to minimize these artifacts, it is desirable to render triangle 
	edges as close as possible to the real line between two vertices.  This
	is  conveniently  done  with  this  algorithm  by  performing  the 
	interpolator  setup computations in floating point, and converting to 
	fixed point at the end: 
	*/
	// 28.4 fixed-point coordinates (4 bits of sub-pixel precision, enough for a 2048x2048 color buffer)

	const INT32 X1 = iround( 16.0f * fX1 );
	const INT32 X2 = iround( 16.0f * fX2 );
	const INT32 X3 = iround( 16.0f * fX3 );

	const INT32 Y1 = iround( 16.0f * fY1 );
	const INT32 Y2 = iround( 16.0f * fY2 );
	const INT32 Y3 = iround( 16.0f * fY3 );

	//const INT32 Z1 = iround( 16.0f * fZ1 );
	//const INT32 Z2 = iround( 16.0f * fZ2 );
	//const INT32 Z3 = iround( 16.0f * fZ3 );

	//const INT32 W1 = iround( 16.0f * fW1 );
	//const INT32 W2 = iround( 16.0f * fW2 );
	//const INT32 W3 = iround( 16.0f * fW3 );

	// deltas
	const INT32 DeltaX12 = X1 - X2;
	const INT32 DeltaX23 = X2 - X3;
	const INT32 DeltaX31 = X3 - X1;

	const INT32 DeltaY12 = Y1 - Y2;
	const INT32 DeltaY23 = Y2 - Y3;
	const INT32 DeltaY31 = Y3 - Y1;




	// back-face culling

	// calculate signed area of triangle in screen space
	// this is actually twice the area
	const int nArea = DeltaX12 * DeltaY31 - DeltaY12 * DeltaX31;
	(void)nArea;

	if( CULL_MODE == Cull_CW && nArea <= 0 )
	{
		return;
	}
	else if( CULL_MODE == Cull_CCW && nArea >= 0 )
	{
		return;
	}



	// 24.8 Fixed-point deltas
	const INT32 FDX12 = DeltaX12 << 4;
	const INT32 FDX23 = DeltaX23 << 4;
	const INT32 FDX31 = DeltaX31 << 4;

	const INT32 FDY12 = DeltaY12 << 4;
	const INT32 FDY23 = DeltaY23 << 4;
	const INT32 FDY31 = DeltaY31 << 4;





	// Compute interpolation data
	const float fDeltaX12 = fX1 - fX2;
	const float fDeltaX23 = fX2 - fX3;
	//const float fDeltaX31 = fX3 - fX1;

	const float fDeltaY12 = fY1 - fY2;
	const float fDeltaY23 = fY2 - fY3;
	//const float fDeltaY31 = fY3 - fY1;

	//INTERP_C = FLTDX21 * FLTDY31 - FLTDX31 * FLTDY21;

	const float denominator = fDeltaX12 * fDeltaY23 - fDeltaY12 * fDeltaX23;
	if( denominator == 0.0 ) {
		return;	// degenerate triangle
	}
	const float invDenominator = 1.0f / denominator;
	(void)invDenominator;




	// Block size, standard 8x8 (must be power of two)
	enum { BLOCK_SIZE = 8 };
	//enum { BLOCK_SIZE_LOG2 = 3 };


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

	INT32 nMinX = Clamp( (Min3(X1, X2, X3) + 0xF) >> 4, 0, W );
	INT32 nMaxX = Clamp( (Max3(X1, X2, X3) + 0xF) >> 4, 0, W );
	INT32 nMinY = Clamp( (Min3(Y1, Y2, Y3) + 0xF) >> 4, 0, H );
	INT32 nMaxY = Clamp( (Max3(Y1, Y2, Y3) + 0xF) >> 4, 0, H );

	//const INT32 iArea = (imaxx - iminx) * (imaxy - iminy);
	//if( iArea == 0 ) {
	//	return;
	//}


	// Start in corner of 8x8 block
	nMinX &= ~(BLOCK_SIZE - 1);
	nMinY &= ~(BLOCK_SIZE - 1);




	//const UINT stride = W * sizeof SoftPixel;	// aka 'pitch'
	SoftPixel* startPixels = context.pixels + nMinY * W;
	ZBufElem* startZBuffer = context.zbuffer + nMinY * W;



	PS_INPUT	pixelShaderInput;
	pixelShaderInput.globals = context.globals;

	PS_OUTPUT	pixelShaderOutput;



	//// for calculating barycentric coordinates
	//const float tmpDY23inv = fDeltaY23 * invDenominator;
	//const float tmpDX23inv = fDeltaX23 * invDenominator;
	//const float tmpDY31inv = fDeltaY31 * invDenominator;
	//const float tmpDX31inv = fDeltaX31 * invDenominator;


	//const float fStepX = 1.0f / (nMaxX - nMinX);
	//const float fStepY = 1.0f / (nMaxY - nMinY);

	// for calculating barycentric coordinates
	//float fTmpX = (float)nMinX - fX3;
	//float fTmpY = (float)nMinY - fY3;

	for( UINT iBlockY = nMinY; iBlockY < nMaxY; iBlockY += BLOCK_SIZE )
	{
		for( UINT iBlockX = nMinX; iBlockX < nMaxX; iBlockX += BLOCK_SIZE )
		{
			// Corners of block in 28.4 fixed-point (4 bits of sub-pixel accuracy)
			const UINT FBlockX0 = (iBlockX << 4);
			const UINT FBlockX1 = (iBlockX + (BLOCK_SIZE - 1)) << 4;
			const UINT FBlockY0 = (iBlockY << 4);
			const UINT FBlockY1 = (iBlockY + (BLOCK_SIZE - 1)) << 4;

			// Evaluate half-space functions in the 4 corners of the block

			//OPTIMIZED: extract sign bit instead of converting to bool
		#if 0
			const bool a00 = (C1 + DeltaX12 * FBlockY0 - DeltaY12 * FBlockX0) < 0;
			const bool a10 = (C1 + DeltaX12 * FBlockY0 - DeltaY12 * FBlockX1) < 0;
			const bool a01 = (C1 + DeltaX12 * FBlockY1 - DeltaY12 * FBlockX0) < 0;
			const bool a11 = (C1 + DeltaX12 * FBlockY1 - DeltaY12 * FBlockX1) < 0;
			const UINT a = (a00 << 0) | (a10 << 1) | (a01 << 2) | (a11 << 3);	// compose mask for all four corners

			const bool b00 = (C2 + DeltaX23 * FBlockY0 - DeltaY23 * FBlockX0) < 0;
			const bool b10 = (C2 + DeltaX23 * FBlockY0 - DeltaY23 * FBlockX1) < 0;
			const bool b01 = (C2 + DeltaX23 * FBlockY1 - DeltaY23 * FBlockX0) < 0;
			const bool b11 = (C2 + DeltaX23 * FBlockY1 - DeltaY23 * FBlockX1) < 0;
			const UINT b = (b00 << 0) | (b10 << 1) | (b01 << 2) | (b11 << 3);	// compose mask for all four corners

			const bool c00 = Sign(C3 + DeltaX31 * FBlockY0 - DeltaY31 * FBlockX0);
			const bool c10 = Sign(C3 + DeltaX31 * FBlockY0 - DeltaY31 * FBlockX1);
			const bool c01 = Sign(C3 + DeltaX31 * FBlockY1 - DeltaY31 * FBlockX0);
			const bool c11 = Sign(C3 + DeltaX31 * FBlockY1 - DeltaY31 * FBlockX1);
			const UINT c = (c00 << 0) | (c10 << 1) | (c01 << 2) | (c11 << 3);	// compose mask for all four corners
		#else
			const UINT a00 = INT32_SIGN_BIT_SET( C1 + DeltaX12 * FBlockY0 - DeltaY12 * FBlockX0 );
			const UINT a10 = INT32_SIGN_BIT_SET( C1 + DeltaX12 * FBlockY0 - DeltaY12 * FBlockX1 );
			const UINT a01 = INT32_SIGN_BIT_SET( C1 + DeltaX12 * FBlockY1 - DeltaY12 * FBlockX0 );
			const UINT a11 = INT32_SIGN_BIT_SET( C1 + DeltaX12 * FBlockY1 - DeltaY12 * FBlockX1 );
			const UINT a = (a00 << 0) | (a10 << 1) | (a01 << 2) | (a11 << 3);	// compose mask for all four corners

			const UINT b00 = INT32_SIGN_BIT_SET( C2 + DeltaX23 * FBlockY0 - DeltaY23 * FBlockX0 );
			const UINT b10 = INT32_SIGN_BIT_SET( C2 + DeltaX23 * FBlockY0 - DeltaY23 * FBlockX1 );
			const UINT b01 = INT32_SIGN_BIT_SET( C2 + DeltaX23 * FBlockY1 - DeltaY23 * FBlockX0 );
			const UINT b11 = INT32_SIGN_BIT_SET( C2 + DeltaX23 * FBlockY1 - DeltaY23 * FBlockX1 );
			const UINT b = (b00 << 0) | (b10 << 1) | (b01 << 2) | (b11 << 3);	// compose mask for all four corners

			const UINT c00 = INT32_SIGN_BIT_SET( C3 + DeltaX31 * FBlockY0 - DeltaY31 * FBlockX0 );
			const UINT c10 = INT32_SIGN_BIT_SET( C3 + DeltaX31 * FBlockY0 - DeltaY31 * FBlockX1 );
			const UINT c01 = INT32_SIGN_BIT_SET( C3 + DeltaX31 * FBlockY1 - DeltaY31 * FBlockX0 );
			const UINT c11 = INT32_SIGN_BIT_SET( C3 + DeltaX31 * FBlockY1 - DeltaY31 * FBlockX1 );
			const UINT c = (c00 << 0) | (c10 << 1) | (c01 << 2) | (c11 << 3);	// compose mask for all four corners
		#endif


			// Skip block when outside an edge => outside the triangle

			//OPTIMIZED: replaced logical 'or' with bit-and
		#if 0
			if( a == 0 || b == 0 || c == 0 ) {
				continue;
			}
		#else
			//const UINT s = ((a & b) & c);
			//if( s == 0 ) {
			if( a*b*c == 0 ) {
			//if( !(a&b&c) ) {
			//if( !( a & b & c ) ) {
				continue;
			}
		#endif


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
				//DbgDrawRect( context, RGBA8_GREEN, FBlockX0 >> 4, FBlockY0 >> 4, BLOCK_SIZE, BLOCK_SIZE );

				for( UINT iY = iBlockY; iY < iBlockY + BLOCK_SIZE; iY++ )
				{
					for( UINT iX = iBlockX; iX < iBlockX + BLOCK_SIZE; iX++ )
					{
						pixels[ iX ] = RGBA8_WHITE;
					}
					pixels += W;
					zbuffer += W;
				}
			}
			else // Partially covered block
			{
				//DbgDrawRect( context, RGBA8_GREEN, FBlockX0 >> 4, FBlockY0 >> 4, BLOCK_SIZE, BLOCK_SIZE );

				// in 28.4
				INT32 CY1 = C1 + DeltaX12 * FBlockY0 - DeltaY12 * FBlockX0;
				INT32 CY2 = C2 + DeltaX23 * FBlockY0 - DeltaY23 * FBlockX0;
				INT32 CY3 = C3 + DeltaX31 * FBlockY0 - DeltaY31 * FBlockX0;

				for( UINT iY = iBlockY; iY < iBlockY + BLOCK_SIZE; iY++ )
				{
					INT32 CX1 = CY1;
					INT32 CX2 = CY2;
					INT32 CX3 = CY3;

					for( UINT iX = iBlockX; iX < iBlockX + BLOCK_SIZE; iX++ )
					{
						// less holes between polygons if we use less or equal
						if( CX1 <= 0 && CX2 <= 0 && CX3 <= 0 )
							//if( CX1 < 0 && CX2 < 0 && CX3 < 0 )
						{
							// inside triangle
							//context.pixels[ iY * W + iX ] = RGBA8_WHITE;
							pixels[ iX ] = RGBA8_RED;

						}//for x
						CX1 -= FDY12;
						CX2 -= FDY23;
						CX3 -= FDY31;
					}//for y

					CY1 += FDX12;
					CY2 += FDX23;
					CY3 += FDX31;

					pixels += W;
					zbuffer += W;
				}
			}//Partially covered block

		}//for each block on X axis

		startPixels += W * BLOCK_SIZE;
		startZBuffer += W * BLOCK_SIZE;

	}//for each block on Y axis

#else // use old-school scan line rasterizer



#endif

	//if(1)	// DBG: draw wire frame triangle
	//{
	//	context.renderer->DrawWireframeTriangle( v1.P.ToVec2(), v2.P.ToVec2(), v3.P.ToVec2(), RGBA8_RED );
	//}

	context.renderer->m_stats.numTrianglesRendered++;
}

//--------------------------------------------------------------//
//				End Of File.									//
//--------------------------------------------------------------//
