#pragma once

#include "SoftRender_Internal.h"

#include "BackFaceCulling.inl"

namespace SoftRenderer
{

// Cohen–Sutherland clipping algorithm
enum ClipCode
{
	CLIP_NEG_X = 0,
	CLIP_POS_X = 1,
	CLIP_NEG_Y = 2,
	CLIP_POS_Y = 3,
	CLIP_NEG_Z = 4,
	CLIP_POS_Z = 5,
};

// projection matrix transforms vertices from world space to normalized clip space
// where only points that fulfill the following are inside the view frustum:
// -|w| <= x <= |w|
// -|w| <= y <= |w|
// -|w| <= z <= |w|
// tests each axis against W
static inline
UINT CalcClipMask( XVertex & v )
{
#if 0
	UINT cmask = 0;
	if( v.p.w - v.p.x < 0.0f ) cmask |= BIT( CLIP_POS_X );
	if( v.p.x + v.p.w < 0.0f ) cmask |= BIT( CLIP_NEG_X );
	if( v.p.w - v.p.y < 0.0f ) cmask |= BIT( CLIP_POS_Y );
	if( v.p.y + v.p.w < 0.0f ) cmask |= BIT( CLIP_NEG_Y );
	if( v.p.w - v.p.z < 0.0f ) cmask |= BIT( CLIP_POS_Z );
	if( v.p.z + v.p.w < 0.0f ) cmask |= BIT( CLIP_NEG_Z );
	return cmask;
#else
	return
		(( v.P.x < -v.P.w ) << CLIP_NEG_X)|
		(( v.P.x >  v.P.w ) << CLIP_POS_X)|
		(( v.P.y < -v.P.w ) << CLIP_NEG_Y)|
		(( v.P.y >  v.P.w ) << CLIP_POS_Y)|
		(( v.P.z < -v.P.w ) << CLIP_NEG_Z)|
		(( v.P.z >  v.P.z ) << CLIP_POS_Z);
#endif
}



#define DIFFERENT_SIGNS( x, y )\
	((x <= 0 && y > 0) || (x > 0 && y <= 0))


template< ECullMode CULL_MODE, EFillMode FILL_MODE >
static inline
void Template_ClipTriangle( F_RenderSingleTriangle* drawTriangleFunction, XVertex* v, const SoftRenderContext& context )
{
	mxPROFILE_SCOPE("Process Single Triangle");

	// calculate outcodes of all three vertices
	const UINT cmask = CalcClipMask( v[0] ) | CalcClipMask( v[1] ) | CalcClipMask( v[2] );

	IF_LIKELY( cmask == 0 )
	{
		// the whole triangle lies inside the view frustum, no intersection of any kind
		// the triangle is trivially accepted
		ProjectVertex( &v[0], context );
		ProjectVertex( &v[1], context );
		ProjectVertex( &v[2], context );

		if( !CullTriangle2< CULL_MODE, FILL_MODE >( v[0], v[1], v[2] ) )
		{
			(*drawTriangleFunction)( v[0], v[1], v[2], context );
		}
	}
	else
	{
		// the triangle intersects one or more planes;
		// clip this triangle against the planes and render the resulting (convex) polygon.

		// NOTE: must mirror ClipCode enum, but plane normals inverted,
		// because we leave the portion of triangle in front of planes.
		//
		static const SIMD_VEC4 planes[NUM_CLIP_PLANES] =
		{
			{ 1.0f,  0.0f,  0.0f,  1.0f },	// PLANE_NEG_X, CLIP_NEG_X
			{-1.0f,  0.0f,  0.0f,  1.0f },	// PLANE_POS_X, CLIP_POS_X

			{ 0.0f,  1.0f,  0.0f,  1.0f },	// PLANE_NEG_Y, CLIP_NEG_Y
			{ 0.0f, -1.0f,  0.0f,  1.0f },	// PLANE_POS_Y, CLIP_POS_Y

			{ 0.0f,  0.0f,  1.0f,  1.0f },	// PLANE_NEG_Z, CLIP_NEG_Z
			//{ 0.0f,  0.0f,  -1.0f,  1.0f },	// PLANE_POS_Z, CLIP_POS_Z
		};

		// temporary storage for ping-ponging
		UINT	list1[ CLIP_BUFFER_SIZE ];
		UINT	list2[ CLIP_BUFFER_SIZE ];
		UINT *	inlist = list1;		// indices of vertices entering the frustum
		UINT *	outlist = list2;	// indices of vertices leaving the frustum
		int		n = 3;
		int		nverts = 3;

		// Start out with the original vertices.
		inlist[0] = 0;
		inlist[1] = 1;
		inlist[2] = 2;

		for( UINT iPlane = 0; iPlane < NUM_CLIP_PLANES; iPlane++ )
		{
			if( cmask & (1 << iPlane) )
			{
				// the triangle is clipped against this plane

				UINT iPrevVtx = inlist[0];
				FLOAT fPrevDp = Dot_Product( planes[iPlane], v[iPrevVtx] );
	
				UINT outcount = 0;

				inlist[n] = inlist[0];	// prevent rotation of vertices

				for( UINT i = 1; i <= n; i++ )
				{
					UINT iThisVtx = inlist[i];
					FLOAT fThisDp = Dot_Product( planes[iPlane], v[iThisVtx] );

					if( fPrevDp >= 0.0f ) {
						// previous vertex is in front of the clipping plane
						outlist[outcount++] = iPrevVtx;
					}

					if( DIFFERENT_SIGNS( fThisDp, fPrevDp ) )
					{
						// this line is clipped by the plane, get the point of intersection (with interpolated vertex attributes)
						if( fThisDp < 0.0f )
						{
							// this point is behind plane, previous point is in front of plane
							// Avoid division by zero as we know dp != dpPrev from DIFFERENT_SIGNS, above.
							const FLOAT fraction = fThisDp / ( fThisDp - fPrevDp );
							InterpolateVertex( fraction, v[iThisVtx], v[iPrevVtx], v[nverts] );
						}
						else
						{
							// Coming back in.
							const FLOAT fraction = fPrevDp / ( fPrevDp - fThisDp );
							InterpolateVertex( fraction, v[iPrevVtx], v[iThisVtx], v[nverts] );
						}
						outlist[outcount++] = nverts;
						nverts++;
					}

					iPrevVtx = iThisVtx;
					fPrevDp = fThisDp;
				}

				// Result of clipping produced no valid polygon
				if( outcount < 3 ) {
					return;
				}

				TSwap( inlist, outlist );

				n = outcount;
			}//if clipped by this plane
		}//for each plane

		ProjectVertex( &v[inlist[0]], context );
		ProjectVertex( &v[inlist[1]], context );

		for( UINT i = 2; i < n; i++ )
		{
			ProjectVertex( &v[inlist[i]], context );

			if( CullTriangle2< CULL_MODE, FILL_MODE >( v[inlist[0]], v[inlist[i-1]], v[inlist[i]] ) )
			{
				continue;
			}

			(*drawTriangleFunction)( v[inlist[0]], v[inlist[i-1]], v[inlist[i]], context );
		}
	}//if clipped by frustum planes
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

		Template_ClipTriangle< CULL_MODE, FILL_MODE >( drawTriangleFunction, v, context );
	}//for each triangle
}

}//namespace SoftRenderer

//--------------------------------------------------------------//
//				End Of File.									//
//--------------------------------------------------------------//
