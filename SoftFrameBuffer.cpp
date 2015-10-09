#include "SoftRender_PCH.h"
#pragma hdrstop
#include "SoftFrameBuffer.h"

SoftFrameBuffer::SoftFrameBuffer()
{
	m_colorBuffer = nil;
	m_depthBuffer = nil;
	m_viewportWidth = 0;
	m_viewportHeight = 0;
}

SoftFrameBuffer::~SoftFrameBuffer()
{
	Shutdown();
}

bool SoftFrameBuffer::Initialize( UINT width, UINT height, SoftPixel* pixels )
{
	CHK_VRET_FALSE_IF_NOT(width > 1);
	CHK_VRET_FALSE_IF_NOT(height > 1);
	CHK_VRET_FALSE_IF_NIL(pixels);

	Shutdown();

	DEVOUT("FrameBuffer::Initialize: %ux%u, color: %u KiB, depth: color: %u KiB\n",
		width,
		height,
		(width*height*sizeof m_colorBuffer[0])/mxKIBIBYTE,
		(width*height*sizeof m_depthBuffer[0])/mxKIBIBYTE
		);

	m_viewportWidth = width;
	m_viewportHeight = height;
	m_colorBuffer = pixels;
	m_depthBuffer = (ZBufElem*) mxAlloc( m_viewportWidth * m_viewportHeight * sizeof m_depthBuffer[0] );

	return true;
}

void SoftFrameBuffer::Shutdown()
{
	m_viewportWidth = 0;
	m_viewportHeight = 0;
	m_colorBuffer = nil;
	if( m_depthBuffer != nil )
	{
		mxFree( m_depthBuffer );
		m_depthBuffer = nil;
	}
}

void SoftFrameBuffer::ClearDepthOnly()
{
	mxPROFILE_SCOPE("Clear depth buffer");

	// Clear depth buffer.

#if 0
	for( UINT y = 0; y < m_viewportHeight; y++ )
	{
		for( UINT x = 0; x < m_viewportWidth; x++ )
		{
			m_depthBuffer[ y * m_viewportWidth + x ] = SOFT_MAX_DEPTH;
		}
	}
#else

	mxSTATIC_ASSERT( SOFT_RENDER_USES_FLOATING_POINT_DEPTH_BUFFER );

	_flint	u;
	u.f = SOFT_MAX_DEPTH;

#if 0
	const __m128i qiMaxDepth = _mm_set1_epi32( u.i );

	for( UINT y = 0; y < m_viewportHeight; y++ )
	{
		for( UINT x = 0; x < m_viewportWidth; x+=4 )
		{
			__m128i* dest = (__m128i*) &m_depthBuffer[ y * m_viewportWidth + x ];

			_mm_store_si128( dest, qiMaxDepth );
		}
	}
#else
	MemSet( m_depthBuffer, u.i, m_viewportHeight * m_viewportWidth * sizeof m_depthBuffer[0] );
#endif


#endif
}
