#pragma once

static FORCEINLINE
INT iround( FLOAT x )
{
#if SOFT_RENDER_USE_SSE
	//return _mm_cvt_ss2si( _mm_load_ps( &x ) );
	return asmFloatToInt( x );
#else
	INT retval;
	ASM fld		x
	ASM fistp	retval
	return retval;	// use default rouding mode (round nearest)
#endif
}

// 'half spaces' rasterization
// See: http://devmaster.net/forums/topic/1145-advanced-rasterization/
// and http://www.cs.unc.edu/~olano/papers/2dh-tri/
//
template< ECullMode CULL_MODE >
static inline
void TemplateRasterizeTriangle( const XVertex& v1, const XVertex& v2, const XVertex& v3, const SoftRenderContext& context )
{
	const int W = context.W;	// viewport width
	const int H = context.H;	// viewport height


#if SOFT_RENDER_USE_HALFSPACE_RASTERIZER


	// version with floats
	// based on http://www.altdevblogaday.com/2012/04/29/software-rasterizer-part-2/
	if(1)
	{
		// back-face culling

		const FLOAT area = /*0.5f **/ ((v1.p.x - v2.p.x) * (v3.p.y - v1.p.y) - (v1.p.y - v2.p.y) * (v3.p.x - v1.p.x));

		if( CULL_MODE == Cull_Clockwise && area <= 0.0f )
		{
			return;
		}
		else if( CULL_MODE == Cull_CounterClockwise && area >= 0.0f )
		{
			return;
		}



		const float x1 = v1.p.x;
		const float x2 = v2.p.x;
		const float x3 = v3.p.x;

		const float y1 = v1.p.y;
		const float y2 = v2.p.y;
		const float y3 = v3.p.y;

		const float z1 = v1.p.z;
		const float z2 = v2.p.z;
		const float z3 = v3.p.z;

		const float w1 = v1.p.w;
		const float w2 = v2.p.w;
		const float w3 = v3.p.w;


		const float deltaX12 = x1 - x2;
		const float deltaX23 = x2 - x3;
		const float deltaX31 = x3 - x1;

		const float deltaY12 = y1 - y2;
		const float deltaY23 = y2 - y3;
		const float deltaY31 = y3 - y1;


		// edgeEqt0	= deltaX0 * (y - y0) - deltaY0 * (x- x0)
		//			= deltaX0 * y - deltaX0 * y0 - deltaY0 * x + deltaY0 * x0
		//			= (deltaY0 * x0 - deltaX0 * y0) + deltaX0 * y - deltaY0 * x
		//			= edgeConstant0 + deltaX0 * y - deltaY0 * x

		float edgeConstant1 = deltaY12 * x1 - deltaX12 * y1;
		float edgeConstant2 = deltaY23 * x2 - deltaX23 * y2;
		float edgeConstant3 = deltaY31 * x3 - deltaX31 * y3;

		// correct for fill convention
		//if (deltaY12 < 0.0 || (deltaY12 == 0.0 && deltaX12 > 0.0))
		//	++edgeConstant1;
		//if (deltaY23 < 0.0 || (deltaY23 == 0.0 && deltaX23 > 0.0))
		//	++edgeConstant2;
		//if (deltaY31 < 0.0 || (deltaY31 == 0.0 && deltaX31 > 0.0))
		//	++edgeConstant3;


		// Bounding rectangle of this triangle in screen space

		const float minx = Min3( x1, x2, x3 );
		const float maxx = Max3( x1, x2, x3 );
		const float miny = Min3( y1, y2, y3 );
		const float maxy = Max3( y1, y2, y3 );

		const int iminx = iround( minx );
		const int imaxx = iround( maxx );
		const int iminy = iround( miny );
		const int imaxy = iround( maxy );



		const float denominator = deltaX12 * deltaY23 - deltaY12 * deltaX23;
		if( denominator == 0.0 ) {
			return;	// degenerated triangle
		}
		const float invDenominator = 1.0f / denominator;


		float edgeEqtStart1 = edgeConstant1 + deltaX12 * miny - deltaY12 * minx;
		float edgeEqtStart2 = edgeConstant2 + deltaX23 * miny - deltaY23 * minx;
		float edgeEqtStart3 = edgeConstant3 + deltaX31 * miny - deltaY31 * minx;


		PS_INPUT	pixelShaderInput;
		pixelShaderInput.globals = context.globals;

		PS_OUTPUT	pixelShaderOutput;

		for( int y = iminy; y < imaxy; y++ )
		{
			float edgeEqt1 = edgeEqtStart1;
			float edgeEqt2 = edgeEqtStart2;
			float edgeEqt3 = edgeEqtStart3;

			for( int x = iminx; x < imaxx; x++ )
			{
				if( edgeEqt1 < 0.0f && edgeEqt2 < 0.0f && edgeEqt3 < 0.0f )
				{
					// inside triangle

					const UINT elemOffset = y * W + x;

					//context.pixels[ elemOffset ] = RGBA8_WHITE;

					// calculate barycentric coordinates
					const float t1 = ( deltaY23 * (x-x2)-deltaX23 * (y-y2) ) * invDenominator;
					const float t2 = ( deltaY31 * (x-x2)-deltaX31 * (y-y2) ) * invDenominator;
					const float t3 = 1.0 - t1 - t2;

					// perspective correct interpolation
					const float z = z1 * t1 + z2 * t2 + z3 * t3;

					const ZBufElem prevDepth = context.zbuffer[ elemOffset ];
					if ( z <= prevDepth )
					{
						// depth test passed
						context.zbuffer[ elemOffset ] = z;


						const float w = w1 * t1 + w2 * t2 + w3 * t3;
						const float invW = 1.0f / w;


						pixelShaderInput.normal = v1.N * t1 + v2.N * t2 + v3.N * t3;
						pixelShaderInput.normal.NormalizeFast();

						(*context.pixelShader)( pixelShaderInput, pixelShaderOutput );

						context.pixels[ elemOffset ] = pixelShaderOutput.color.ToARGB().AsUInt32();
					}
				}

				edgeEqt1 -= deltaY12;
				edgeEqt2 -= deltaY23;
				edgeEqt3 -= deltaY31;
			}//for x

			edgeEqtStart1 += deltaX12;
			edgeEqtStart2 += deltaX23;
			edgeEqtStart3 += deltaX31;
		}//for y
	}



	if(0)
	{

		//NOTE: replaced with integer version because floating-point branches are more costly(?)
		//if(0)
		//{
		//	// back-face culling

		//	const FLOAT area = /*0.5f **/ ((v1.p.x - v2.p.x) * (v3.p.y - v1.p.y) - (v1.p.y - v2.p.y) * (v3.p.x - v1.p.x));

		//	if( CULL_MODE == Cull_Clockwise && area <= 0.0f )
		//	{
		//		return;
		//	}
		//	else if( CULL_MODE == Cull_CounterClockwise && area >= 0.0f )
		//	{
		//		return;
		//	}
		//}




		const XVertex* V1 = &v1;
		const XVertex* V2 = &v2;
		const XVertex* V3 = &v3;

		// 28.4 fixed-point coordinates (4 bits of sub-pixel precision, enough for a 2048x2048 color buffer)

		const int X1 = iround( 16.0f * V1->p.x );
		const int X2 = iround( 16.0f * V2->p.x );
		const int X3 = iround( 16.0f * V3->p.x );

		const int Y1 = iround( 16.0f * V1->p.y );
		const int Y2 = iround( 16.0f * V2->p.y );
		const int Y3 = iround( 16.0f * V3->p.y );


		// 28.4 fixed-point deltas
		const int DX12 = X1 - X2;
		const int DX23 = X2 - X3;
		const int DX31 = X3 - X1;

		const int DY12 = Y1 - Y2;
		const int DY23 = Y2 - Y3;
		const int DY31 = Y3 - Y1;

		// Fixed-point deltas in 24.8
		const int FDX12 = DX12 << 4;
		const int FDX23 = DX23 << 4;
		const int FDX31 = DX31 << 4;

		const int FDY12 = DY12 << 4;
		const int FDY23 = DY23 << 4;
		const int FDY31 = DY31 << 4;


		// back-face culling

		//if(1)
		{
			// calculate signed area of triangle in screen space
			// this is actually twice the area
			const int area = DX12 * DY31 - DY12 * DX31;

			if( CULL_MODE == Cull_Clockwise && area <= 0 )
			{
				return;
			}
			else if( CULL_MODE == Cull_CounterClockwise && area >= 0 )
			{
				return;
			}
		}










		// Block size, standard 8x8 (must be power of two)
		enum { BLOCK_SIZE = 8 };
		enum { BLOCK_SIZE_LOG2 = 3 };
		const int q = 8;


		// Bounding rectangle of this triangle in screen space

		// Start in corner of 8x8 block
		//minx &= ~(q - 1);
		//miny &= ~(q - 1);

		const int minx = ((Min3( X1, X2, X3 ) + 0xF) >> 4) & ~(q - 1);
		const int maxx = ((Max3( X1, X2, X3 ) + 0xF) >> 4);
		const int miny = ((Min3( Y1, Y2, Y3 ) + 0xF) >> 4) & ~(q - 1);
		const int maxy = ((Max3( Y1, Y2, Y3 ) + 0xF) >> 4);


		//if(1)	// DBG: visualize bounding boxes
		//{
		//	//DBGOUT("minx = %d,miny = %d,maxx = %d,maxy = %d\n",minx,miny,maxx,maxy);
		//	const int minx2 = smallest( minx, W-1 );
		//	const int maxx2 = smallest( maxx, W-1 );
		//	const int miny2 = smallest( miny, H-1 );
		//	const int maxy2 = smallest( maxy, H-1 );

		//	context.renderer->DrawLine2D( minx2, miny2, maxx2, miny2, RGBA8_GREEN );
		//	context.renderer->DrawLine2D( maxx2, miny2, maxx2, maxy2, RGBA8_GREEN );
		//	context.renderer->DrawLine2D( maxx2, maxy2, minx2, maxy2, RGBA8_GREEN );
		//	context.renderer->DrawLine2D( minx2, maxy2, minx2, miny2, RGBA8_GREEN );
		//}

		//for( int y = miny; y < maxy; y++ )
		//{
		//	for( int x = minx; x < maxx; x++ )
		//	{
		//		context.pixels[ y * W + x ] = RGBA8_WHITE;
		//	}
		//}





		// Half-edge constants
		const int C1 = DY12 * X1 - DX12 * Y1;
		const int C2 = DY23 * X2 - DX23 * Y2;
		const int C3 = DY31 * X3 - DX31 * Y3;

		//// Correct for fill convention
		//if(DY12 < 0 || (DY12 == 0 && DX12 > 0)) C1++;
		//if(DY23 < 0 || (DY23 == 0 && DX23 > 0)) C2++;
		//if(DY31 < 0 || (DY31 == 0 && DX31 > 0)) C3++;



		const UINT sizeOfPixel = sizeof SoftPixel;
		const UINT pitch = sizeOfPixel * W;	// stride

		SoftPixel* pixels = (SoftPixel*) ( (BYTE*)context.pixels + miny * pitch );



		// Loop through blocks
		for( int y = miny; y < maxy; y += q )
		{
			for( int x = minx; x < maxx; x += q )
			{
				// Corners of block
				const int x0 = x << 4;
				const int x1 = (x + q - 1) << 4;
				const int y0 = y << 4;
				const int y1 = (y + q - 1) << 4;

				// Evaluate half-space functions
				bool a00 = C1 + DX12 * y0 - DY12 * x0 > 0;
				bool a10 = C1 + DX12 * y0 - DY12 * x1 > 0;
				bool a01 = C1 + DX12 * y1 - DY12 * x0 > 0;
				bool a11 = C1 + DX12 * y1 - DY12 * x1 > 0;
				int a = (a00 << 0) | (a10 << 1) | (a01 << 2) | (a11 << 3);

				bool b00 = C2 + DX23 * y0 - DY23 * x0 > 0;
				bool b10 = C2 + DX23 * y0 - DY23 * x1 > 0;
				bool b01 = C2 + DX23 * y1 - DY23 * x0 > 0;
				bool b11 = C2 + DX23 * y1 - DY23 * x1 > 0;
				int b = (b00 << 0) | (b10 << 1) | (b01 << 2) | (b11 << 3);

				bool c00 = C3 + DX31 * y0 - DY31 * x0 > 0;
				bool c10 = C3 + DX31 * y0 - DY31 * x1 > 0;
				bool c01 = C3 + DX31 * y1 - DY31 * x0 > 0;
				bool c11 = C3 + DX31 * y1 - DY31 * x1 > 0;
				int c = (c00 << 0) | (c10 << 1) | (c01 << 2) | (c11 << 3);

				// Skip block when outside an edge
				if(a == 0x0 || b == 0x0 || c == 0x0) continue;



				for(int iy = 0; iy < q; iy++)
				{
					for(int ix = x; ix < x + q; ix++)
					{
						pixels[ iy * W + ix ] = RGBA8_WHITE;
					}
				}


				//// Accept whole block when totally covered
				//if( a == 0xF && b == 0xF && c == 0xF )
				//{
				//	for(int iy = 0; iy < q; iy++)
				//	{
				//		for(int ix = x; ix < x + q; ix++)
				//		{
				//			//pixels[ix] = 0x00007F00;	// Green
				//			pixels[ iy * W + ix ] = RGBA8_WHITE;
				//		}
				//	}
				//}
				//else // Partially covered block
				//{
				//	//
				//}
			}
		}
	}//block-based

#else // use old-school scan line rasterizer



#endif





	//if(1)	// DBG: draw wire frame triangle
	//{
	//	context.renderer->DrawWireframeTriangle( v1.p.ToVec2(), v2.p.ToVec2(), v3.p.ToVec2(), RGBA8_RED );
	//}


	context.renderer->m_stats.numTrianglesRendered++;
}

//--------------------------------------------------------------//
//				End Of File.									//
//--------------------------------------------------------------//
