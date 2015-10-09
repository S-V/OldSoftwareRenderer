
// Perform back-face culling.
// it should be done in screen space, after projection.
template< ECullMode CULL_MODE >
static inline
bool CullTriangle( const XVertex& v1, const XVertex& v2, const XVertex& v3 )
{
	// In order to know whether the triangle is front- or back-facing,
	// you take v0-v2 and v0-v2, make a cross product. This gives you the face normal.
	// If this vector is towards you ( dot(normal, viewVector) < 0), draw.
	// But here we operate on projected vertices
	// and use 2D cross product - (U.x*V.y-U.y*V.x)
	// culling performed in screen space
	// by checking the clockwise/counterclockwise order of a polygon's vertices.


	// back-face culling

	const FLOAT fDeltaX12 = v1.P.x - v2.P.x;
	const FLOAT fDeltaY31 = v3.P.y - v1.P.y;
	const FLOAT fDeltaY12 = v1.P.y - v2.P.y;
	const FLOAT fDeltaX31 = v3.P.x - v1.P.x;

	// calculate signed area of triangle in screen space
	// this is actually twice the area
	const FLOAT fArea = fDeltaX12 * fDeltaY31 - fDeltaY12 * fDeltaX31;
	(void)fArea;

	//OPTIMIZE: floating-point branches are more costly than integer ones,
	// extract sign bit?

	if( CULL_MODE == Cull_CW && fArea <= 0.0f )
	{
		return true;
	}
	if( CULL_MODE == Cull_CCW && fArea >= 0.0f )
	{
		return true;
	}

	return false;
}

template< ECullMode CULL_MODE, EFillMode FILL_MODE >
static inline
//bool CullTriangle2( const XVertex *& v1, const XVertex *& v2, const XVertex *& v3 )
bool CullTriangle2( XVertex & v1, XVertex & v2, XVertex & v3 )
{
	// In order to know whether the triangle is front- or back-facing,
	// you take v0-v2 and v0-v2, make a cross product. This gives you the face normal.
	// If this vector is towards you ( dot(normal, viewVector) < 0), draw.
	// But here we operate on projected vertices
	// and use 2D cross product - (U.x*V.y-U.y*V.x)
	// culling performed in screen space
	// by checking the clockwise/counterclockwise order of a polygon's vertices.


	// back-face culling

	const FLOAT fDeltaX12 = v1.P.x - v2.P.x;
	const FLOAT fDeltaY31 = v3.P.y - v1.P.y;
	const FLOAT fDeltaY12 = v1.P.y - v2.P.y;
	const FLOAT fDeltaX31 = v3.P.x - v1.P.x;

	// calculate signed area of triangle in screen space
	// this is actually twice the area
	const FLOAT fArea = fDeltaX12 * fDeltaY31 - fDeltaY12 * fDeltaX31;
	(void)fArea;

	//OPTIMIZE: floating-point branches are more costly than integer ones,
	// extract sign bit?

	if( CULL_MODE == Cull_CW && fArea <= 0.0f )
	{
		return true;
	}
	if( CULL_MODE == Cull_CCW && fArea >= 0.0f )
	{
		return true;
	}

	if( FILL_MODE == Fill_Solid && fArea > 0 )
	{
		TSwap( v1, v2 );
	}

	return false;
}
