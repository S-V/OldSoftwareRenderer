#include "SoftRender_PCH.h"
#pragma hdrstop

#include <Base/JobSystem/ThreadPool.h>

#include "SoftRender.h"
#include "SoftFrameBuffer.h"
#include "SoftRender_Internal.h"
#include "TriangleClipping.inl"
#include "SoftImmediateRenderer.h"
#include "SoftTileRenderer.h"
#include "SoftThreads.h"

namespace SoftRenderer
{
	// current states
	struct Globals
	{
		TPtr< ATriangleRenderer >	m_currentRenderer;

		SoftFrameBuffer		m_frameBuffer;

		ThreadPool		m_threadPool;

		//srTileRenderer		m_tileRenderer;
		//srImmediateRenderer	m_immediateRenderer;
	};
	static TPtr< Globals >	gPtr;



	static Settings	settings;

	static bool	bFrameStarted;

	Stats	stats;
	bool	bDbg_DrawBlockBounds = false;
	bool	bDbg_EnableThreading = true;

}//namespace SoftRenderer


namespace SoftRenderer
{

bool Initialize( const InitArgs& initArgs )
{
	CHK_VRET_FALSE_IF_NOT(initArgs.isOk());

	DEVOUT("SoftRenderer::Initialize:\n");
	DEVOUT("\tsizeof(XVertex) = %u bytes:\n", sizeof XVertex);
	DEVOUT("\tsizeof(XTriangle) = %u bytes:\n", sizeof XTriangle);

	gPtr.ConstructInPlace();

	gPtr->m_frameBuffer.Initialize( initArgs.width, initArgs.height, initArgs.rgba );


	ThreadPool::CInfo	cInfo;
	gPtr->m_threadPool.Initialize( cInfo );
	//Async::CInfo	cInfo;
	//Async::Initialize( cInfo );



#if SOFT_RENDER_USE_IMMEDIATE_RASTERIZATION
	mxSTATIC_IN_PLACE_CTOR_X( gPtr->m_currentRenderer, srImmediateRenderer );
#else
	mxSTATIC_IN_PLACE_CTOR_X( gPtr->m_currentRenderer, srTileRenderer, initArgs.width, initArgs.height );
#endif

	ModifySettings( initArgs.settings );

	stats.Reset();

	bFrameStarted = false;

	return true;
}

void ModifySettings( const Settings& newSettings )
{
	mxUNDONE;
	//if( newSettings.bImmediateRasterization )
	//{
	//	gPtr->m_currentRenderer = &gPtr->m_immediateRenderer;
	//}
	//else
	//{
	//	gPtr->m_currentRenderer = &gPtr->m_tileRenderer;
	//}
	settings = newSettings;
}

void Shutdown()
{
	gPtr->m_currentRenderer->Flush();

	gPtr->m_threadPool.Shutdown();
	//Async::Shutdown();

	gPtr->m_frameBuffer.Shutdown();

	gPtr->m_currentRenderer.Destruct();

	bFrameStarted = false;

	stats.Reset();

	gPtr.Destruct();
}

void BeginFrame()
{
	Assert(!bFrameStarted);

	gPtr->m_currentRenderer->Flush();

	bFrameStarted = true;

	stats.Reset();

	gPtr->m_frameBuffer.ClearDepthOnly();
}

void EndFrame()
{
	Assert(bFrameStarted);
	
	gPtr->m_currentRenderer->Flush();

	bFrameStarted = false;
}

void SetWorldMatrix( const float4x4& newWorldMatrix )
{
	gPtr->m_currentRenderer->SetWorldMatrix( newWorldMatrix );
}

void SetViewMatrix( const float4x4& newViewMatrix )
{
	gPtr->m_currentRenderer->SetViewMatrix( newViewMatrix );
}

void SetProjectionMatrix( const float4x4& newProjectionMatrix )
{
	gPtr->m_currentRenderer->SetProjectionMatrix( newProjectionMatrix );
}

void SetCullMode( ECullMode newCullMode )
{
	gPtr->m_currentRenderer->SetCullMode( newCullMode );
}

void SetFillMode( EFillMode newFillMode )
{
	gPtr->m_currentRenderer->SetFillMode( newFillMode );
}

void SetVertexShader( F_VertexShader* newVertexShader )
{
	gPtr->m_currentRenderer->SetVertexShader( newVertexShader );
}

void SetPixelShader( F_PixelShader* newPixelShader )
{
	gPtr->m_currentRenderer->SetPixelShader( newPixelShader );
}

void SetTexture( SoftTexture2D* newTexture2D )
{
	gPtr->m_currentRenderer->SetTexture( newTexture2D );
}

void DrawTriangles( const SVertex* vertices, UINT numVertices, const SIndex* indices, UINT numIndices )
{
	gPtr->m_currentRenderer->DrawTriangles( gPtr->m_frameBuffer, vertices, numVertices, indices, numIndices );
}

const Settings& CurrentSettings()
{
	return settings;
}

void GetViewportSize( UINT &W, UINT &H )
{
	W = gPtr->m_frameBuffer.m_viewportWidth;
	H = gPtr->m_frameBuffer.m_viewportHeight;
}

SoftPixel* GetColorBuffer()
{
	return gPtr->m_frameBuffer.m_colorBuffer;
}

ThreadPool& GetThreadPool()
{
	return gPtr->m_threadPool;
}

}//namespace SoftRenderer

SoftRenderer::Settings::Settings()
{
	mode = CpuMode_Use_FPU;
}

SoftRenderer::InitArgs::InitArgs()
{
	width = 0;
	height = 0;
	rgba = nil;
}

bool SoftRenderer::InitArgs::isOk() const
{
	CHK_VRET_FALSE_IF_NOT(width > 0);
	CHK_VRET_FALSE_IF_NOT(height > 0);
	CHK_VRET_FALSE_IF_NIL(rgba);
	return true;
}

void SoftRenderer::Stats::Reset()
{
	numTrianglesRendered = 0;
	numVertices = 0;
	numIndices = 0;
}

const char* ECullMode_To_Chars( ECullMode cullMode )
{
	switch( cullMode )
	{
	case Cull_None :	return "None";
	case Cull_CW :		return "CW";
	case Cull_CCW :		return "CCW";
	default:	Unreachable;
	}
	return "?";
}

const char* ECpuMode_To_Chars( ECpuMode cpuMode )
{
	switch( cpuMode )
	{
	case CpuMode_Use_FPU :	return "FPU";
	case CpuMode_Use_SSE :	return "SSE";
	//case CpuMode_Use_AVX :	return "AVX";
	default:	Unreachable;
	}
	return "?";
}
