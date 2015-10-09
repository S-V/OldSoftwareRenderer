#pragma once


#if SOFT_RENDER_USE_SSE
	#include <smmintrin.h>
#endif // SOFT_RENDER_USE_SSE



enum { SSE_REG_WIDTH = 4 };
enum { AVX_REG_WIDTH = 8 };





static FORCEINLINE
INT iround( FLOAT x )
{
#if 0//SOFT_RENDER_USE_SSE
	return _mm_cvt_ss2si( _mm_load_ps( &x ) );
#else
	INT retval;
	ASM fld		x
	ASM fistp	retval
	return retval;	// use default rouding mode (round nearest)
#endif
}


static inline
ARGB32 SINGLE_FLOAT_TO_RGBA32( FLOAT x ) {
#if 0
	F4 tmp = x * 255.f;
	tmp = minf( tmp, 255.f );
	U4 i = iround( tmp );
	//return (i<<24)|(i<<16)|(i<<8)|i;
	return (i<<16)|(i<<8)|i;	// Alpha=0
#else
	int i = iround( x * 255.f );
	return (i<<16)|(i<<8)|i;	// Alpha=0
#endif
}




#define INTERPOLATE_SIMPLE( V, A, B, T )\
	(V) = (A) + ((B) - (A)) * (T)


#if SOFT_RENDER_USE_SSE
	#define INTERPOLATE_VEC4( V, A, B, T )\
		(V).q = XMVectorLerp( (A).q, (B).q, (T) )

#else
	#define INTERPOLATE_VEC4( V, A, B, T )\
		INTERPOLATE_SIMPLE( V, A, B, T )
#endif




// calculates gradient for interpolation via the plane equation.
// See:
// https://github.com/jbush001/VectorProc/wiki/Software-Rendering-Pipeline
// and
// http://devmaster.net/forums/topic/1145-advanced-rasterization/page__st__40
// page 3.
// 
static inline
void ComputeGradient_FPU(
						 F4 C, 
						 F4 di21, F4 di31,	// deltas of interpolated parameter
						 F4 dx21, F4 dx31,
						 F4 dy21, F4 dy31,
						 F4 & dx, F4 & dy
						 )
{
	// A * x + B * y + C * z + D = 0
	// z = -A / C * x - B / C * y - D
	// z = z0 + vZ/dx * (x - x0) + vZ/dy * (y - y0)
	// 
	// A = (z3 - z1) * (y2 - y1) - (z2 - z1) * (y3 - y1)
	// B = (x3 - x1) * (z2 - z1) - (x2 - x1) * (z3 - z1)
	// C = (x2 - x1) * (y3 - y1) - (x3 - x1) * (y2 - y1)
	//
	const F4 A = di31 * dy21 - di21 * dy31;
	const F4 B = dx31 * di21 - dx21 * di31;

	//Let's say we want to interpolate some component z linearly across the polygon (note that z stands for any interpolant). We can visualize this as a plane going through the x, y and z positions of the triangle, in 3D space. Now, the equation of a 3D plane is generally:
	//A * x + B * y + C * z + D = 0

	//From this we can derive that:
	//z = -A / C * x - B / C * y - D

	//Note that for every step in the x-direction, z increments by -A / C, and likewise it increments by -B / C for every step in the y-direction. So these are the gradients we're looking for to perform linear interpolation. In the plane equation (A, B, C) is the normal vector of the plane. It can easily be computed with a cross product.
	//Now that we have the gradients, let's call them vZ/dx (which is -A / C) and vZ/dy (which is -B / C), we can easily compute z everywhere on the triangle. We know the z value in all three vertex positions. Let's call the one of the first vertex z0, and it's position coordinates (x0, y0). Then a generic z value of a point (x, y) can be computed as:
	//z = z0 + vZ/dx * (x - x0) + vZ/dy * (y - y0)

	//Once you've computed the z value for the center of the starting pixel this way, you can easily add vZ/dx to get the z value for the next pixel, or vZ/dy for the pixel below (with the y-axis going down).
	dx = -A/C;
	dy = -B/C;
}
