#include "stdafx.h"
#pragma hdrstop

#include <Base/Util/LogUtil.h>
#include <Base/Util/FPSTracker.h>
#include <Base/Util/Sorting.h>

#include <Core/Util/Timer.h>

#include <Driver/MyAppWindow.h>
#include <Driver/BitmapWindow.h>

#include <SoftRender/SoftRender.h>
#include <SoftRender/SoftMesh.h>
#include <SoftRender/SoftMath.h>
#pragma comment( lib, "SoftRender.lib" )


enum
{
	//VTX_WORLD_POS_INDEX = 0

	IREG_TEX_COORD_U = 0,
	IREG_TEX_COORD_V = 1,
};

void DefaultVertexShader( const VS_INPUT& inputs, VS_OUTPUT &outputs )
{
	const ShaderGlobals * globals = inputs.globals;

	const SVertex &	vertex_in = *inputs.vertex;

	XVertex * vertex = outputs.vertex_out;

	const float4 posLocal = XMVectorSet( vertex_in.position.x, vertex_in.position.y, vertex_in.position.z, 1.0f );

	vertex->P.q = XMVector4Transform( posLocal, globals->WVP );
	//vertex->P = as_matrix4(globals->WVP) * vertex_in.position;

	//vertex->N.q = XMVector4Transform( vertex_in.normal.q, globals->worldMatrix );

	//vertex->UV = vertex_in.texCoord;

	//vertex->v[ VTX_WORLD_POS_INDEX ].q = XMVector4Transform( vertex_in.position.q, globals->worldMatrix );

	vertex->vars[ IREG_TEX_COORD_U ] = vertex_in.texCoord.x;
	vertex->vars[ IREG_TEX_COORD_V ] = vertex_in.texCoord.y;
}
void DefaultPixelShader( SPixelShaderParameters & args )
{
	SoftTexture2D *	texture = args.globals->texture;

	//*(args.pixel) = SINGLE_FLOAT_TO_RGBA32( args.depth );

	*(args.pixel) = texture->Sample_Point_Wrap( args.vars[0],args.vars[1] );

	//F4 U = inputs.vars[ IREG_TEX_COORD_U ];
	//F4 V = inputs.vars[ IREG_TEX_COORD_U ];

	////outputs.color.q = inputs.normal.q;
	////outputs.color.q = XMVectorReplicate( inputs.depth );
	//// 
	//outputs.color.R = inputs.vars[ IREG_TEX_COORD_U ];
	//outputs.color.G = inputs.vars[ IREG_TEX_COORD_V ];
	//outputs.color.B = 1.0f; 
	//outputs.color.A = 1.0f;

//	outputs.color.q = inputs.v[ VTX_WORLD_POS_INDEX ].q;

	//outputs.color.q = inputs.v[ VTX_WORLD_POS_INDEX ].q;
	//if( XMVectorGetX(XMVector3LengthEst(outputs.color.q)) < 1 ) {
	//	outputs.color *= 0.3;
	//}

	//if( XMVectorGetX(XMVector3LengthEst(inputs.v[ VTX_WORLD_POS_INDEX ].q)) < 0.1f ) {
	//	outputs.color.SetAll(1.0f);
	//}
}


void DrawMesh( const SoftMesh& mesh )
{
	const UINT numVertices = mesh.NumVertices();
	const UINT numIndices = mesh.NumIndices();

	const SVertex* vertices = mesh.GetVerticesArray();
	const SIndex* indices = mesh.GetIndicesArray();

	SoftRenderer::DrawTriangles( vertices, numVertices, indices, numIndices );

	SoftRenderer::stats.numVertices += numVertices;
	SoftRenderer::stats.numIndices += numIndices;
}

bool LoadModelFromBin( const char* name, SoftMesh & mesh )
{
	FileReader file( name );
	CHK_VRET_FALSE_IF_NOT(file.IsOpen());

	/*
	1. int32 - число вертексов.
	2. int32 - число индексов.
	3. Данные вертексов подряд.
	4. Данные индексов подряд.

	Формат вертекса:

	posx - float
	posy - float
	posz - float
	normalx - float
	normaly - float
	normalz - float
	tu - float
	tv - float
	*/
	const UINT32 numVertices = ReadUInt32( file );
	const UINT32 numIndices = ReadUInt32( file );

	mesh.m_vb.SetNum(numVertices);
	mesh.m_ib.SetNum(numIndices);

	for( UINT iVertex = 0; iVertex < numVertices; iVertex++ )
	{
		Vec3D	position;
		file >> position;

		Vec3D	normal;
		file >> normal;

		Vec2D	texCoords;
		file >> texCoords;

		mesh.m_vb[ iVertex ].SetPosition( position );
		mesh.m_vb[ iVertex ].SetNormal( normal );
		mesh.m_vb[ iVertex ].texCoord = texCoords;
	}

	mesh.RecalculateAABB();

	for( UINT i = 0; i < numIndices; i++ )
	{
		const UINT16 index = ReadUInt16( file );
		mesh.m_ib[ i ] = index;
	}

	return true;
}

bool LoadTeapotModel( SoftMesh & mesh )
{
	return LoadModelFromBin( "Teapot.bin", mesh );
}




static const char* APP_SETTINGS = "_session";


struct SoftModel
{
	SoftMesh *	m_mesh;
	float4x4	m_xform;
	//F4			m_depth;

public:
	SoftModel()
	{
		m_mesh = nil;
		m_xform = XMMatrixIdentity();
	}
};

enum { MAX_MODELS = 16 };



	struct QueueItem
	{
		SoftModel *	model;
		F4			depth;

		//bool operator < ( const QueueItem& b ) const
		//{
		//	return this->depth < b.depth;
		//}
		// for InsertionSort
		static bool Compare( const QueueItem& a, const QueueItem& b )
		{
			return a.depth > b.depth;
		}
	};




class MyApp : public AClientViewport
{
	SCamera			m_camera;

	SoftMesh		m_cubeMesh;
	SoftMesh		m_teapotMesh;
	SoftMesh		m_venusMesh;

	SoftTexture2D	m_testTexture;


	TStaticList< SoftModel, MAX_MODELS >	m_models;


	FLOAT	m_angle;
	bool	m_animateScene;

	int		m_backFaceCulling;	// ECullMode
	int		m_cpuMode;

	bool	m_solidFillMode;
	bool	m_showStats;
	bool	m_showHelp;

	UINT	m_visibleModels;

	TPtr<BitmapWindow> m_screen;

	FPSTracker<>	m_fpsCounter;

public:
	MyApp()
	{
		m_angle = 0.0f;
		m_animateScene = false;
		m_backFaceCulling = Cull_CCW;
		m_cpuMode = CpuMode_Use_SSE;
		m_solidFillMode = true;
		m_showStats = true;
		m_showHelp = true;
		m_visibleModels = 0;
	}
	~MyApp()
	{
	}

	enum ETestModels
	{
		TestModel_Venus1 = 0,
		TestModel_Venus2,
		TestModel_Teapot1,
		TestModel_Cube1,
		TestModel_LargeTeapot,
		TestModel_Cube2,

		TestModel_Count
	};
	bool Initialize( MyAppWindow& window, BitmapWindow& screen )
	{
		window.SetClient(this);
		m_screen = &screen;

		{
			FileReader	sessionFile(APP_SETTINGS,FileRead_NoErrors);
			if( sessionFile.IsOpen() )
			{
				ArchivePODReader	archive( sessionFile );
				archive && m_camera;
			}
			else
			{
				this->ResetCamera();
			}
		}

		//CreateScene
		{
			m_cubeMesh.SetupCube();
			//m_teapotMesh.SetupTeapot();
			LoadTeapotModel(m_teapotMesh);
			LoadModelFromBin("venus.bin",m_venusMesh);

			m_testTexture.Setup_Checkerboard();

			m_models.SetNum(TestModel_Count);

			m_models[TestModel_Venus1].m_mesh = &m_venusMesh;
			m_models[TestModel_Venus2].m_mesh = &m_venusMesh;
			m_models[TestModel_Teapot1].m_mesh = &m_teapotMesh;
			m_models[TestModel_Cube1].m_mesh = &m_cubeMesh;
			m_models[TestModel_LargeTeapot].m_mesh = &m_teapotMesh;
			m_models[TestModel_Cube2].m_mesh = &m_cubeMesh;
		}


		SoftRenderer::InitArgs	initArgs;
		initArgs.width = screen.GetWidth();
		initArgs.height = screen.GetHeight();
		initArgs.rgba = (SoftPixel*)screen.GetPixels();

		SoftRenderer::Initialize( initArgs );

		return true;
	}

	void Shutdown()
	{
		SoftRenderer::Shutdown();

		{
			FileWriter	sessionFile(APP_SETTINGS,FileWrite_NoErrors);
			if( sessionFile.IsOpen() )
			{
				ArchivePODWriter	archive( sessionFile );
				archive && m_camera;
			}
		}
		m_screen = nil;
	}

	bool IsValid() const
	{
		return true;
	}

	void ResetCamera()
	{
		m_camera.GetView().SetDefaults();
		m_camera.ResetView();
		m_camera.GetView().origin.Set(0,0,-10);
		m_camera.GetView().nearZ = 0.1f;
		m_camera.GetView().farZ = 50.f;
		//m_camera.Update(0);
	}

public:	//=-- AClientViewport

	virtual void OnKeyPressed( EKeyCode key ) override
	{
		AClientViewport::OnKeyPressed( key );
		m_camera.OnKeyPressed( key );

		if( key == EKeyCode::Key_B )
		{
			m_backFaceCulling++;
			if( m_backFaceCulling >= Cull_MAX ) {
				m_backFaceCulling = Cull_None;
			}
		}


		if( key == EKeyCode::Key_V )
		{
			m_solidFillMode ^= 1;
		}
		if( key == EKeyCode::Key_R )
		{
			this->ResetCamera();
		}
		if( key == EKeyCode::Key_F1 )
		{
			m_showHelp ^= 1;
		}
		if( key == EKeyCode::Key_F2 )
		{
			m_showStats ^= 1;
		}
		if( key == EKeyCode::Key_P )
		{
			m_animateScene ^= 1;
		}
		if( key == EKeyCode::Key_I )
		{
			SoftRenderer::bDbg_DrawBlockBounds ^= 1;
		}
		if( key == EKeyCode::Key_T )
		{
			SoftRenderer::bDbg_EnableThreading ^= 1;
		}

		if( key == EKeyCode::Key_U )
		{
			m_cpuMode++;
			if( m_cpuMode >= CpuMode_MAX ) {
				m_cpuMode = CpuMode_Use_FPU;
			}
		}

	}

	virtual void OnKeyReleased( EKeyCode key ) override
	{
		AClientViewport::OnKeyReleased( key );
		m_camera.OnKeyReleased( key );
	}

	virtual void OnMouseMove( const SMouseMoveEvent& args ) override
	{
		AClientViewport::OnMouseMove( args );
		m_camera.OnMouseMove( args.mouseDeltaX, args.mouseDeltaY );
	}

	virtual void Draw() override
	{
		mxPROFILE_SCOPE("Draw:");

		const F4 aspectRatio = (F4)m_screen->GetWidth() / m_screen->GetHeight();
		m_camera.SetAspectRatio( aspectRatio );

		const rxView& view = m_camera.GetView();

		ViewFrustum	viewFrustum;
		viewFrustum.ExtractFrustumPlanes( as_matrix4(view.CalcViewProjMatrix()) );

		{
			SoftRenderer::Settings	settings;
			settings.mode = (ECpuMode)m_cpuMode;
			SoftRenderer::ModifySettings(settings);
		}

		SoftRenderer::SetViewMatrix( view.CreateViewMatrix() );
		SoftRenderer::SetProjectionMatrix( view.CreateProjectionMatrix() );

		SoftRenderer::BeginFrame();
		{
			SoftRenderer::SetVertexShader( &DefaultVertexShader );
			SoftRenderer::SetPixelShader( &DefaultPixelShader );


			SoftRenderer::SetFillMode( m_solidFillMode ? Fill_Solid : Fill_Wireframe );
			SoftRenderer::SetCullMode( (ECullMode)m_backFaceCulling );


			//const float4x4 rotationMatrix = m_animateScene ? XMMatrixRotationY( DEG2RAD(m_angle)*100.0f ) : XMMatrixIdentity();
			//const float4x4 rotationMatrix = XMMatrixRotationY( DEG2RAD(m_angle)*100.0f );

			SoftRenderer::SetTexture(&m_testTexture);



#if 0
			SoftRenderer::SetWorldMatrix( XMMatrixScaling(1.5,1.5,1.5) * XMMatrixRotationY( DEG2RAD(m_angle)*100.0f ) * XMMatrixTranslation(0,0,0) );
			DrawMesh( m_teapotMesh, m_renderer );

			SoftRenderer::SetWorldMatrix( XMMatrixRotationY( DEG2RAD(m_angle)*-100.0f ) * XMMatrixRotationZ( DEG2RAD(m_angle)*100.0f ) * XMMatrixTranslation(1,1,1) );
			DrawMesh( m_cubeMesh, m_renderer );

			SoftRenderer::SetWorldMatrix( XMMatrixTranslation(2,1,6) *  XMMatrixRotationY( DEG2RAD(m_angle)*-40.0f ) );
			DrawMesh( m_teapotMesh, m_renderer );

			//DrawMesh( m_cubeMesh, m_renderer );

			SoftRenderer::SetWorldMatrix( XMMatrixScaling(5.4,5.4,5.4) * XMMatrixRotationY( DEG2RAD(m_angle)*-50.0f ) *XMMatrixTranslation(10,-5,14) );
			DrawMesh( m_teapotMesh, m_renderer );
#endif



			m_models[TestModel_Venus1].m_xform = XMMatrixScaling(0.5,0.5,0.5) * XMMatrixRotationY( DEG2RAD(m_angle)*80.0f ) * XMMatrixTranslation(2,-7,15);
			m_models[TestModel_Venus2].m_xform = XMMatrixScaling(0.2,0.2,0.2) * XMMatrixRotationY(-DEG2RAD(m_angle)*90.0f ) * XMMatrixTranslation(-1,1,-4);

			m_models[TestModel_Teapot1].m_xform = XMMatrixScaling(1.5,1.5,1.5) * XMMatrixRotationY( DEG2RAD(m_angle)*100.0f ) * XMMatrixTranslation(0,0,0);
			m_models[TestModel_Cube1].m_xform = XMMatrixRotationY( DEG2RAD(m_angle)*-100.0f ) * XMMatrixRotationZ( DEG2RAD(m_angle)*100.0f ) * XMMatrixTranslation(1,1,1);
			m_models[TestModel_LargeTeapot].m_xform = XMMatrixTranslation(2,1,6) *  XMMatrixRotationY( DEG2RAD(m_angle)*-40.0f );
			m_models[TestModel_Cube2].m_xform = XMMatrixScaling(5.4,5.4,5.4) * XMMatrixRotationY( DEG2RAD(m_angle)*-50.0f ) *XMMatrixTranslation(10,-5,14);


			TStaticList< QueueItem, MAX_MODELS >	drawList;

			for( UINT iModel = 0; iModel < m_models.Num(); iModel++ )
			{
				SoftModel * model = &m_models[ iModel ];

				const AABB worldAabb = model->m_mesh->m_aabb.Trasform( as_matrix4(model->m_xform) );
				if( viewFrustum.IntersectsAABB( worldAabb ) )
				{
					QueueItem & newItem = drawList.Add();
					newItem.model = model;
					newItem.depth = worldAabb.ShortestDistanceSquared( view.origin );
				}
			}

			if( drawList.Num() > 1 )
			{
				InsertionSort< QueueItem, QueueItem >( drawList.ToPtr(), 0, drawList.Num()-1 );
			}

			for( UINT iModel = 0; iModel < drawList.Num(); iModel++ )
			{
				SoftModel * model = drawList[ iModel ].model;

				//DBGOUT("Draw[%u]: depth = %3.f\n",iModel,drawList[ iModel ].depth);

				SoftRenderer::SetWorldMatrix( model->m_xform );

				DrawMesh( *model->m_mesh );
			}


			m_visibleModels = drawList.Num();

			//SoftRenderer::DrawLine2D(0,0,500,500,ARGB8_GREEN);
		}
		SoftRenderer::EndFrame();


		const SoftRenderer::Settings& realSettings = SoftRenderer::CurrentSettings();

		const F4 fps = m_fpsCounter.CalculateFPS();

		char text[1024];
		int y = 0;
		if( m_showStats && m_screen.IsValid() )
		{
			mxSPRINTF_ANSI( text, "FPS = %.3f", fps );
			m_screen->DrawText(10,y+=15,text,FColor::RED.ToFloatPtr());

			const rxView& view = m_camera.GetView();

			mxSPRINTF_ANSI( text, "pos: %.3f, %.3f, %.3f", view.origin.x, view.origin.y, view.origin.z );
			m_screen->DrawText(10,y+=15,text,FColor::RED.ToFloatPtr());

			mxSPRINTF_ANSI( text, "look: %.3f, %.3f, %.3f", view.look.x, view.look.y, view.look.z );
			m_screen->DrawText(10,y+=15,text,FColor::RED.ToFloatPtr());

			mxSPRINTF_ANSI( text, "Models: %u", m_visibleModels );
			m_screen->DrawText(10,y+=15,text,FColor::BLUE.ToFloatPtr());

			mxSPRINTF_ANSI( text, "Triangles: %u", SoftRenderer::stats.numTrianglesRendered );
			m_screen->DrawText(10,y+=15,text,FColor::BLUE.ToFloatPtr());

			mxSPRINTF_ANSI( text, "Vertices: %u", SoftRenderer::stats.numVertices );
			m_screen->DrawText(10,y+=15,text,FColor::BLUE.ToFloatPtr());

			mxSPRINTF_ANSI( text, "Indices: %u", SoftRenderer::stats.numIndices );
			m_screen->DrawText(10,y+=15,text,FColor::BLUE.ToFloatPtr());
		}
		if( m_showHelp && m_screen.IsValid() )
		{
			mxSPRINTF_ANSI( text, "P - toggle animation", fps );
			m_screen->DrawText(10,y+=15,text,FColor::GREEN.ToFloatPtr());

			mxSPRINTF_ANSI( text, "B - back face culling (%s)", ECullMode_To_Chars((ECullMode)m_backFaceCulling) );
			m_screen->DrawText(10,y+=15,text,FColor::GREEN.ToFloatPtr());

			mxSPRINTF_ANSI( text, "V - polygon filling mode (%s)", (m_solidFillMode ? "Solid" : "Wireframe") );
			m_screen->DrawText(10,y+=15,text,FColor::GREEN.ToFloatPtr());

			mxSPRINTF_ANSI( text, "U - instruction set used (%s, real: %s)", ECpuMode_To_Chars((ECpuMode)m_cpuMode), ECpuMode_To_Chars(realSettings.mode) );
			m_screen->DrawText(10,y+=15,text,FColor::GREEN.ToFloatPtr());

			mxSPRINTF_ANSI( text, "F1 - toggle help", fps );
			m_screen->DrawText(10,y+=15,text,FColor::GREEN.ToFloatPtr());

			mxSPRINTF_ANSI( text, "F2 - toggle stats", fps );
			m_screen->DrawText(10,y+=15,text,FColor::GREEN.ToFloatPtr());

			mxSPRINTF_ANSI( text, "R - reset camera", fps );
			m_screen->DrawText(10,y+=15,text,FColor::GREEN.ToFloatPtr());

			mxSPRINTF_ANSI( text, "T - threading (%s)", SoftRenderer::bDbg_EnableThreading ? "enabled" : "disabled" );
			m_screen->DrawText(10,y+=15,text,FColor::GREEN.ToFloatPtr());

			mxSPRINTF_ANSI( text, "Using %s", SOFT_RENDER_USE_HALFSPACE_RASTERIZER ? "halfspaces" : "scanlines" );
			m_screen->DrawText(10,y+=15,text,FColor::GREEN.ToFloatPtr());
		}
	}
	virtual void Tick( FLOAT deltaSeconds )
	{
		m_camera.Update( deltaSeconds );

		const F4 fps = m_fpsCounter.UpdateAndCalculateFPS( deltaSeconds );
		(void)fps;

		if( m_animateScene ) {
			m_angle += deltaSeconds;
		}
		m_angle = clampf( m_angle, 0.0f, 360.0f );
	}
};



mxAPPLICATION_ENTRY_POINT;

int mxAppMain()
{
	SetupBaseUtil	setupBase;

	FileLogUtil		fileLog;
	SetupCoreUtil	setupCore;

	WindowsDriver	driver;

	//vector2d<int>	resolution(1920,1080);
	//vector2d<int>	resolution(1500,1024);	<= had Access Violation (unaligned load)
	//vector2d<int>	resolution(1280,1024);
	//vector2d<int>	resolution(1280,720);
	vector2d<int>	resolution(1024,768);
	//vector2d<int>	resolution(800,600);
	//vector2d<int>	resolution(640,480);
	//vector2d<int>	resolution(512,512);
	//vector2d<int>	resolution(512,256);
	//vector2d<int>	resolution(320,240);
	//resolution*=1;
	//vector2d<int>	topLeftCorner(10,10);
	vector2d<int>	topLeftCorner(0,0);


	MyAppWindow	window( resolution.x, resolution.y );
	window.bringToFront();
	window.setTopLeft( topLeftCorner.x, topLeftCorner.y );


	BitmapWindow	screen;
	screen.Create( resolution.x, resolution.y, window.GetWindowHandle() );



	MyApp		app;

	if( !app.Initialize( window, screen ) ) {
		return -1;
	}


	window.CaptureMouseInput(false);


	GameTimer		timer;

	while( window.isOpen() )
	{
		const F4 deltaSeconds = timer.TickFrame();

		app.Tick( deltaSeconds );

		screen.ClearWith(0);
		//screen.SetPixel(100,100,-1);

		window.Draw();
		screen.Show();

		driver.ProcessWinMessages();

		mxPROFILE_INCREMENT_FRAME_COUNTER;

		//YieldProcessor();
	}

	app.Shutdown();

	screen.Clear();

	return 0;
}

