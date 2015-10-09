#pragma once

#include <SoftRender/SoftRender.h>

struct SoftFrameBuffer
{
	SoftPixel *	m_colorBuffer;	// <= memory not owned, managed by the user
	ZBufElem *	m_depthBuffer;	// <= owned by the framebuffer

	UINT		m_viewportWidth;
	UINT		m_viewportHeight;

public:
	SoftFrameBuffer();
	~SoftFrameBuffer();

	bool Initialize( UINT width, UINT height, SoftPixel* pixels );
	void Shutdown();

	void ClearDepthOnly();

	//void SetPixel( vec4_carg colorRGBA );
};

//--------------------------------------------------------------//
//				End Of File.									//
//--------------------------------------------------------------//
