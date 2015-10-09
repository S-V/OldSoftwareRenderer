#include "SoftRender_PCH.h"
#pragma hdrstop
#include "SoftMesh.h"
#include "SoftRender_Internal.h"

#include <Base/Math/Vector/VectorTemplate.h>

/*
=============================================================================
	Line clipping
=============================================================================
*/
struct ClipRect
{
	INT		x0, x1, y0, y1;
};

// Cohen-Sutherland algorithm, outcodes

enum EClipCode2D
{
	CLIPCODE_EMPTY	=	0,
	CLIPCODE_BOTTOM	=	BIT(0),
	CLIPCODE_TOP	=	BIT(1),
	CLIPCODE_LEFT	=	BIT(2),
	CLIPCODE_RIGHT	=	BIT(3)
};

static inline
UINT32 GetClipCode( const ClipRect &r, const vector2d<INT32> &p )
{
	UINT32 code = CLIPCODE_EMPTY;

	if ( p.x < r.x0 )
		code |= CLIPCODE_LEFT;
	else
	if ( p.x > r.x1 )
		code |= CLIPCODE_RIGHT;

	if ( p.y < r.y0 )
		code |= CLIPCODE_TOP;
	else
	if ( p.y > r.y1 )
		code |= CLIPCODE_BOTTOM;

	return code;
}

mxSWIPED("Irrlicht's engine software renderer + SoftArt");
/*!
	Cohen Sutherland clipping
	@return: 1 if valid
*/

static int ClipLine(const ClipRect &clipping,
			vector2d<INT32> &p0,
			vector2d<INT32> &p1,
			const vector2d<INT32>& p0_in,
			const vector2d<INT32>& p1_in)
{
	UINT32 code0;
	UINT32 code1;
	UINT32 code;

	p0 = p0_in;
	p1 = p1_in;

	code0 = GetClipCode( clipping, p0 );
	code1 = GetClipCode( clipping, p1 );

	// trivial accepted
	while ( code0 | code1 )
	{
		INT32 x=0;
		INT32 y=0;

		// trivial reject
		if ( code0 & code1 )
			return 0;

		if ( code0 )
		{
			// clip first point
			code = code0;
		}
		else
		{
			// clip last point
			code = code1;
		}

		if ( (code & CLIPCODE_BOTTOM) == CLIPCODE_BOTTOM )
		{
			// clip bottom viewport
			y = clipping.y1;
			x = p0.x + ( p1.x - p0.x ) * ( y - p0.y ) / ( p1.y - p0.y );
		}
		else
		if ( (code & CLIPCODE_TOP) == CLIPCODE_TOP )
		{
			// clip to viewport
			y = clipping.y0;
			x = p0.x + ( p1.x - p0.x ) * ( y - p0.y ) / ( p1.y - p0.y );
		}
		else
		if ( (code & CLIPCODE_RIGHT) == CLIPCODE_RIGHT )
		{
			// clip right viewport
			x = clipping.x1;
			y = p0.y + ( p1.y - p0.y ) * ( x - p0.x ) / ( p1.x - p0.x );
		}
		else
		if ( (code & CLIPCODE_LEFT) == CLIPCODE_LEFT )
		{
			// clip left viewport
			x = clipping.x0;
			y = p0.y + ( p1.y - p0.y ) * ( x - p0.x ) / ( p1.x - p0.x );
		}

		if ( code == code0 )
		{
			// modify first point
			p0.x = x;
			p0.y = y;
			code0 = GetClipCode( clipping, p0 );
		}
		else
		{
			// modify second point
			p1.x = x;
			p1.y = y;
			code1 = GetClipCode( clipping, p1 );
		}
	}

	return 1;
}

void SoftRenderer::DrawWireframeTriangle( const Vec2D& vp1, const Vec2D& vp2, const Vec2D& vp3, ARGB32 color )
{
	vector2d<INT32> p1( vp1.x, vp1.y );
	vector2d<INT32> p2( vp2.x, vp2.y );
	vector2d<INT32> p3( vp3.x, vp3.y );

	UINT	viewportWidth, viewportHeight;
	GetViewportSize( viewportWidth, viewportHeight );

	ClipRect	clipRect;
	clipRect.x0 = 0;
	clipRect.y0 = 0;
	clipRect.x1 = viewportWidth-1;
	clipRect.y1 = viewportHeight-1;


	vector2d<INT32> cp1;
	vector2d<INT32> cp2;
	vector2d<INT32> cp3;

	if( 1 == ClipLine( clipRect, cp1, cp2, p1, p2 ) )
	{
		DrawLine2D( cp1.x, cp1.y, cp2.x, cp2.y, color );
	}
	if( 1 == ClipLine( clipRect, cp2, cp3, p2, p3 ) )
	{
		DrawLine2D( cp2.x, cp2.y, cp3.x, cp3.y, color );
	}
	if( 1 == ClipLine( clipRect, cp1, cp3, p1, p3 ) )
	{
		DrawLine2D( cp3.x, cp3.y, cp1.x, cp1.y, color );
	}
}

mxSWIPED("Irrlicht's engine software renderer + SoftArt");
void SoftRenderer::DrawLine2D(
	UINT iStartX, UINT iStartY,
	UINT iEndX, UINT iEndY,
	ARGB32 color )
{
	UINT	viewportWidth, viewportHeight;
	GetViewportSize( viewportWidth, viewportHeight );

	iStartX = smallest(iStartX,viewportWidth-1);
	iStartY = smallest(iStartY,viewportHeight-1);
	iEndX = smallest(iEndX,viewportWidth-1);
	iEndY = smallest(iEndY,viewportHeight-1);

	Assert(iStartX < viewportWidth);
	Assert(iStartY < viewportHeight);
	Assert(iEndX < viewportWidth);
	Assert(iEndY < viewportHeight);

	INT32 dx = iEndX - iStartX;
	INT32 dy = iEndY - iStartY;

	const UINT sizeOfPixel = sizeof ARGB32;
	const UINT pitch = sizeOfPixel * viewportWidth;

	// NOTE: we can only render lines
	// that are mostly horizontal and go from left to right

	// Divided drawing to x major DDA method and y major DDA method.

	// step sizes in bytes
	INT32 xInc = sizeOfPixel * Sign(dx);
	INT32 yInc = pitch * Sign(dy);

	dx = Abs(dx);	// NOTE: dx is positive
	dy = Abs(dy);	// NOTE: dy is positive


	// start with the first point
	ARGB32* dst = (ARGB32*) ((BYTE*)GetColorBuffer() + (iStartX * sizeOfPixel) + (iStartY * pitch));

	// if the line is mostly vertical
	if ( dy > dx )
	{
		TSwap( dx, dy );
		TSwap( xInc, yInc );
	}

	INT32 c = (UINT32)dx << 1;
	INT32 m = (UINT32)dy << 1;
	INT32 d = 0;

	// Draw line with x major DDA.

	UINT32 iStep = (UINT32)dx;

	while ( iStep-- )
	{
		*dst = color;

		dst = (ARGB32*) ( (BYTE*) dst + xInc );	// x += xInc
		d += m;

		// move to the next row
		if ( d > dx )
		{
			dst = (ARGB32*) ( (BYTE*) dst + yInc );	// y += yInc
			d -= c;
		}
	}
}

//--------------------------------------------------------------//
//				End Of File.									//
//--------------------------------------------------------------//
