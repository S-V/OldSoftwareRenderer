#include "SoftRender_PCH.h"
#pragma hdrstop
#include "SoftRender.h"
#include "SoftRender_Internal.h"

SoftTexture2D::SoftTexture2D()
{
	this->Clear();
}

SoftTexture2D::~SoftTexture2D()
{
	this->Clear();
}

void SoftTexture2D::Setup_Checkerboard()
{
	m_data.SetNum( SIZE_X*SIZE_Y );

	ARGB32* data = m_data.ToPtr();

	// Frequency: the lower the number, the higher the frequency

	const UINT freq = 2;
	const UINT fAnd = 1 << freq;

	for( UINT y = 0; y < SIZE_Y; y++ )
	{
		const UINT yIndex = y * SIZE_X;

		for( UINT x = 0; x < SIZE_X; x++ )
		{
			data[ yIndex + x ] = ((y&fAnd) == (x&fAnd)) ? ARGB8_BLACK : ARGB8_WHITE;
		}
	}
}

void SoftTexture2D::Clear()
{
	m_data.EmptyOrClear();
}

ARGB32 SoftTexture2D::Sample_Point_Wrap( F4 u, F4 v ) const
{
	//F4 fU = clampf(u,0,1);
	//F4 fV = clampf(v,0,1);

	F4 fWidth = SIZE_X;
	F4 fHeight = SIZE_Y;

	//
	const int iU = iround( u * fWidth ) & (SIZE_X-1);	// [0..1] -> [0..H]
	const int iV = iround( v * fHeight ) & (SIZE_Y-1);	// [0..1] -> [0..H]

	return m_data.ToPtr()[ iV * SIZE_X + iU ];
/*
	// 28.4 fixed-point coordinates
	const INT32 FU = iround( 16.0f * u );
	const INT32 FV = iround( 16.0f * v );

	enum { FIX_POINT_PRE = 9 };

	const UINT offset =
		(( ( FU & t->textureYMask ) >> FIX_POINT_PRE ) << t->pitchlog2)
		| (( FV & t->textureXMask ) >> FIX_POINT_PRE)
		;

	//*(span++) += textureBuffer[((iv>>10)&0xffffffC0) + (iu>>16)];

*/

#if 0
	float4 res;
	__m128i tU, tV;

	__m128 conv = _mm_rcp_ps( f255 );

	// Formula for computing U and V:
	// tX = (int)( min(iX/iW, 1.0f) * (tex_width - 1) );
	{
		__m128 t2 = _mm_mul_ps( MSR_Wrap(u), _mm_set_ps1((float)(tex->clip_rect.w-1)) );
		__m128 t3 = _mm_mul_ps( MSR_Wrap(v), _mm_set_ps1((float)(tex->clip_rect.h-1)) );

		tU = _mm_cvtps_epi32(t2);
		tV = _mm_cvtps_epi32(t3);
	}

	// tSample = tV * pitch + tU * bytesPerPixel
	__m128i tIdx = _mm_add_epi32( mul_epi32(tU, _mm_set1_epi32(tex->format->BytesPerPixel)), mul_epi32(tV, _mm_set1_epi32(tex->pitch)));

	// Since SSE doesn't support arbitrary indexing out of an array, we have to extract the indexes,
	// grab the sample, and recreate an SSE register with the new samples.
	Uint8 *ptr = (Uint8*&)tex->pixels;
	Uint8 *sample3 = &ptr[_mm_extract_epi32(tIdx, 3)];
	Uint8 *sample2 = &ptr[_mm_extract_epi32(tIdx, 2)];
	Uint8 *sample1 = &ptr[_mm_extract_epi32(tIdx, 1)];
	Uint8 *sample0 = &ptr[_mm_extract_epi32(tIdx, 0)];

	__m128i tSample = _mm_set_epi32( *(Uint32*)sample3, *(Uint32*)sample2, *(Uint32*)sample1, *(Uint32*)sample0 );

	// Finally, grab each of the channels out by shifting and masking.
	res.r = _mm_cvtepi32_ps(_mm_srl_epi32( _mm_and_si128( tSample, _mm_set1_epi32(tex->format->Rmask) ), _mm_set_epi32(0, 0, 0, tex->format->Rshift) ) );
	res.g = _mm_cvtepi32_ps(_mm_srl_epi32( _mm_and_si128( tSample, _mm_set1_epi32(tex->format->Gmask) ), _mm_set_epi32(0, 0, 0, tex->format->Gshift) ) );
	res.b = _mm_cvtepi32_ps(_mm_srl_epi32( _mm_and_si128( tSample, _mm_set1_epi32(tex->format->Bmask) ), _mm_set_epi32(0, 0, 0, tex->format->Bshift) ) );

	*res.r = _mm_mul_ps( *res.r, conv );
	*res.g = _mm_mul_ps( *res.g, conv );
	*res.b = _mm_mul_ps( *res.b, conv );

	return res;
#endif
}

//--------------------------------------------------------------//
//				End Of File.									//
//--------------------------------------------------------------//
