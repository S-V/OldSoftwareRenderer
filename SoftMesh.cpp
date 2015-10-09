#include "SoftRender_PCH.h"
#pragma hdrstop
#include "SoftMesh.h"

//#include <Utility_Graphics/models/teapot.h>

SoftMesh::SoftMesh()
{
	m_aabb.Clear();
}

void SoftMesh::SetVertices( const SVertex* p, UINT numPoints )
{
	m_vb.SetNum(numPoints);
	m_vb.CopyFromArray(p, m_vb.GetDataSize());
	m_aabb.Clear();
	this->RecalculateAABB();
}

void SoftMesh::SetIndices( const SIndex* p, UINT numIndices )
{
	m_ib.SetNum(numIndices);
	m_ib.CopyFromArray(p, m_ib.GetDataSize());
}

void SoftMesh::RecalculateAABB()
{
	m_aabb.Clear();
	for( UINT iPoint = 0; iPoint < m_vb.Num(); iPoint++ )
	{
		m_aabb.AddPoint( m_vb[ iPoint ].GetPosition() );
	}
}

void SoftMesh::SetupCube()
{
	enum { NUM_VERTICES = 8 };

	static const Vec3D positions[ NUM_VERTICES ] =
	{
		Vec3D( -1.0f, -1.0f, -1.0f ),
		Vec3D(  1.0f, -1.0f, -1.0f ),
		Vec3D( -1.0f,  1.0f, -1.0f ),
		Vec3D(  1.0f,  1.0f, -1.0f ),
		Vec3D( -1.0f, -1.0f,  1.0f ),
		Vec3D(  1.0f, -1.0f,  1.0f ),
		Vec3D( -1.0f,  1.0f,  1.0f ),
		Vec3D(  1.0f,  1.0f,  1.0f )
	};

	enum { NUM_CUBE_SIDES = 6 };
	enum { NUM_TRIANGLES = NUM_CUBE_SIDES*2 };
	enum { NUM_INDICES = NUM_TRIANGLES*3 };

	static const SIndex indexData[ NUM_INDICES ] =
	{
		1, 0, 2,
		2, 3, 1,		// -Z
		1, 3, 7,
		7, 5, 1,		// +X
		1, 5, 4, 
		4, 0, 1,		// -Y
		0, 4, 6,
		6, 2, 0,		// -X
		2, 6, 7,
		7, 3, 2,		// +Y
		4, 5, 7,
		7, 6, 4			// +Z
	};

	m_vb.SetNum(NUM_VERTICES);
	for( UINT iVertex = 0; iVertex < NUM_VERTICES; iVertex++ )
	{
		m_vb[ iVertex ].SetPosition( positions[ iVertex ] );
		//m_vb[ iVertex ].uv = 0.0f;
	}

	this->RecalculateAABB();

	m_ib.SetNum(NUM_INDICES);
	m_ib.CopyFromArray( indexData );
}

//void SoftMesh::SetupTeapot()
//{
//	m_vb.SetNum(num_teapot_vertices);
//	for( UINT iVertex = 0; iVertex < m_vb.Num(); iVertex++ )
//	{
//		SVertex & v = m_vb[ iVertex ];
//		v.position.Set( *(Vec3D*)teapot_vertices, 1.0f );
//		v.normal.Set( *(Vec3D*)teapot_normals, 0.0f );
//	}
//
//	m_ib.SetNum(num_teapot_indices);
//	for( UINT i = 0; i < m_ib.Num(); i++ )
//	{
//		m_ib[ i ] = teapot_indices[ i ];
//	}
//}

//--------------------------------------------------------------//
//				End Of File.									//
//--------------------------------------------------------------//
