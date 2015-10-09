Fixed point data can be handled very efficiently with SSE, linear interpolation can be done using pmaddubsw and pmaddwd (_mm_maddubs_epi16 & _mm_madd_epi16). using the 2 madd instructions you can bilinear interpolation between 4 values in 2 instructions, and you can use 8bit color data directly. Doing the same in SIMD floats the equivalent interpolation would be 9 instructions, you would still need to convert your data to floats in the first place, and you are going to use alot more registers if you start off by converting everything to floats. The result of the fixed point bilinear interpolation is in 4 32bit ints, so can easily be converted to 4 floats afterwards.

 Look at the pipeline for the pixels and try and figure at what point you need float precision if at all.
 
 
 It depends on what you need to interpolate, if you are bilinear filtering a texel, you do not require perspective correct on the actual color's, only the coordinates, so it makes perfect sense to use the madd instructions for instance


 lerpTableX - look up table of weight and apposing weight as unsigned bytes
 lerpTableY - look up table of weight and apposing weight as unsigned words

 texel - contains the the four texels to interpolate between in RRRRGGGGBBBBAAAA
 lerpX - contains the horizontal interpolation 
 lerpY - contains the vertical interpolation

 __m128 bilinear( __m128i &texel, int lerpX, int lerpY ) {
 __m128i output = _mm_maddubs_epi16( texel, lerpTableX[lerpX] );
 output = _mm_madd_epi16( output, lerpTableY[lerpY] );
 return _mm_cvtepi32_ps( output );
 }
 //texel is created by reading 4 memory locations and interleaving the results
 //lerp simd values can be calculated as well using gpr, movd and a shuffle

 This is just an alternative which for bilinear filtering is significantly faster than interpolating with floats and uses less registers.


 I agree, using floats is absolutely fine as well.




v 0. Pasted by arabesc as cpp at 2009-05-23 23:32:28 MSK and set expiration to never.
Paste will expire never.
#include &lt;cassert&gt;
#include &lt;cstdio&gt;
#include &lt;cmath&gt;
#include &lt;algorithm&gt;
#include &lt;limits&gt;
#include &lt;time.h&gt;
#include &lt;emmintrin.h&gt;
#define RES_X 64
#define RES_Y 64
__declspec(align(16)) int frameBuffer[RES_Y][RES_X] = { 0 };
int Present()
{
    int count = 0;
    for( size_t i = 0; i &lt; RES_Y; ++i )
    {
        for( size_t j = 0; j &lt; RES_X; ++j )
        {
            printf( "%c", frameBuffer[i][j] + '0' );
            count += frameBuffer[i][j] != 0 ? 1 : 0;
        }
        printf( "\n" );
    }
    return count;
};
template &lt;int BlockPower, int Shift&gt;
inline void triangle(float x1, float y1, float x2, float y2, float x3, float y3, int color)
{
    assert(BlockPower &gt; 1);
    const int BlockSize = 1 &lt;&lt; BlockPower;
    const __m128i sse_zero   = _mm_set_epi32(0, 0, 0, 0);
    const __m128i sse_max    = _mm_set_epi32(-1, -1, -1, -1);
    const __m128i sse_color  = _mm_set_epi32(color, color, color, color);
    // (32-Shift).(Shift) fixed-point coordinates
    int x[3] = { int(x1*(1 &lt;&lt; Shift)), int(x2*(1 &lt;&lt; Shift)), int(x3*(1 &lt;&lt; Shift)) };
    int y[3] = { int(y1*(1 &lt;&lt; Shift)), int(y2*(1 &lt;&lt; Shift)), int(y3*(1 &lt;&lt; Shift)) };
    __declspec(align(16)) int dx[4] = { x[0] - x[1], x[1] - x[2], x[2] - x[0], 0 };
    __declspec(align(16)) int dy[4] = { y[0] - y[1], y[1] - y[2], y[2] - y[0], 0 };
    // eliminate subpixel-sized triangles
    if (!(dx[0] | dx[1] | dx[2] | dy[0] | dy[1] | dy[2]))
    return;
    // bounding rectangle
    int minx = std::min( std::numeric_limits&lt;int&gt;::max(), std::min(x[0], std::min(x[1], x[2])));
    int maxx = std::max(-std::numeric_limits&lt;int&gt;::max(), std::max(x[0], std::max(x[1], x[2])));
    int miny = std::min( std::numeric_limits&lt;int&gt;::max(), std::min(y[0], std::min(y[1], y[2])));
    int maxy = std::max(-std::numeric_limits&lt;int&gt;::max(), std::max(y[0], std::max(y[1], y[2])));
    const int complement = (1 &lt;&lt; Shift) - 1;
    minx = (minx + complement) &gt;&gt; Shift;
    maxx = (maxx + complement) &gt;&gt; Shift;
    miny = (miny + complement) &gt;&gt; Shift;
    maxy = (maxy + complement) &gt;&gt; Shift;
    // start in corner of (BlockSize)x(BlockSize) block
    minx &= ~(BlockSize - 1);
    miny &= ~(BlockSize - 1);
    int* color_buffer = frameBuffer[miny];
    // fixed-point deltas
    const __m128i fdx = _mm_slli_epi32(_mm_load_si128((const __m128i*)dx), Shift);
    const __m128i fdy = _mm_slli_epi32(_mm_load_si128((const __m128i*)dy), Shift);
    // half-edge constants
    int c[3] = { x[0]*dy[0] - y[0]*dx[0], x[1]*dy[1] - y[1]*dx[1], x[2]*dy[2] - y[2]*dx[2] };
    // correct for fill convention
    for (int i = 0; i != 3; ++i)
    if (dy[i] &lt; 0  ||  (0 == dy[i]  &&  dx[i] &gt; 0))
    ++c[i];
    __m128i fdy_0 = _mm_shuffle_epi32(fdy, _MM_SHUFFLE(0, 0, 0, 3));
    __m128i fdy_1 = _mm_shuffle_epi32(fdy, _MM_SHUFFLE(1, 1, 1, 3));
    __m128i fdy_2 = _mm_shuffle_epi32(fdy, _MM_SHUFFLE(2, 2, 2, 3));
    fdy_0 = _mm_add_epi32(fdy_0, _mm_shuffle_epi32(fdy_0, _MM_SHUFFLE(1, 1, 0, 0)));
    fdy_0 = _mm_add_epi32(fdy_0, _mm_shuffle_epi32(fdy_0, _MM_SHUFFLE(1, 0, 0, 0)));
    fdy_1 = _mm_add_epi32(fdy_1, _mm_shuffle_epi32(fdy_1, _MM_SHUFFLE(1, 1, 0, 0)));
    fdy_1 = _mm_add_epi32(fdy_1, _mm_shuffle_epi32(fdy_1, _MM_SHUFFLE(1, 0, 0, 0)));
    fdy_2 = _mm_add_epi32(fdy_2, _mm_shuffle_epi32(fdy_2, _MM_SHUFFLE(1, 1, 0, 0)));
    fdy_2 = _mm_add_epi32(fdy_2, _mm_shuffle_epi32(fdy_2, _MM_SHUFFLE(1, 0, 0, 0)));
    const __m128i fdy_0x4 = _mm_slli_epi32(_mm_shuffle_epi32(fdy, _MM_SHUFFLE(0, 0, 0, 0)), 2);
    const __m128i fdy_1x4 = _mm_slli_epi32(_mm_shuffle_epi32(fdy, _MM_SHUFFLE(1, 1, 1, 1)), 2);
    const __m128i fdy_2x4 = _mm_slli_epi32(_mm_shuffle_epi32(fdy, _MM_SHUFFLE(2, 2, 2, 2)), 2);
    // loop through blocks
    for (int y = miny; y &lt; maxy; y += BlockSize)
    {
        for (int x = minx; x &lt; maxx; x += BlockSize)
        {
            // corners of block
            const int x0 = x &lt;&lt; Shift;
            const int x1 = (x  +  BlockSize - 1) &lt;&lt; Shift;
            const int y0 = y &lt;&lt; Shift;
            const int y1 = (y  +  BlockSize - 1) &lt;&lt; Shift;
            // evaluate half-space functions
            __declspec(align(16)) int c00[4] =
            { c[0] + dx[0]*y0 - dy[0]*x0, c[1] + dx[1]*y0 - dy[1]*x0, c[2] + dx[2]*y0 - dy[2]*x0, 1 };
            __declspec(align(16)) int c01[4] =
            { c[0] + dx[0]*y0 - dy[0]*x1, c[1] + dx[1]*y0 - dy[1]*x1, c[2] + dx[2]*y0 - dy[2]*x1, 1 };
            __declspec(align(16)) int c10[4] =
            { c[0] + dx[0]*y1 - dy[0]*x0, c[1] + dx[1]*y1 - dy[1]*x0, c[2] + dx[2]*y1 - dy[2]*x0, 1 };
            __declspec(align(16)) int c11[4] =
            { c[0] + dx[0]*y1 - dy[0]*x1, c[1] + dx[1]*y1 - dy[1]*x1, c[2] + dx[2]*y1 - dy[2]*x1, 1 };
            const int hsf[3] =
            {
                ((c00[0] &gt; 0) &lt;&lt; 0) | ((c01[0] &gt; 0) &lt;&lt; 1) | ((c10[0] &gt; 0) &lt;&lt; 2) | ((c11[0] &gt; 0) &lt;&lt; 3),
                ((c00[1] &gt; 0) &lt;&lt; 0) | ((c01[1] &gt; 0) &lt;&lt; 1) | ((c10[1] &gt; 0) &lt;&lt; 2) | ((c11[1] &gt; 0) &lt;&lt; 3),
                ((c00[2] &gt; 0) &lt;&lt; 0) | ((c01[2] &gt; 0) &lt;&lt; 1) | ((c10[2] &gt; 0) &lt;&lt; 2) | ((c11[2] &gt; 0) &lt;&lt; 3)
            };
            // skip block when outside an edge
            if (!(hsf[0] & hsf[1] & hsf[2]))
            continue;
            __m128i sse_c00 = _mm_load_si128((const __m128i*)c00);
            __m128i* buffer = reinterpret_cast&lt;__m128i*&gt;(color_buffer + x);
            for (int iy = 0; iy != BlockSize; ++iy)
            {
                __m128i sse_cx_0 = _mm_shuffle_epi32(sse_c00, _MM_SHUFFLE(0, 0, 0, 0));
                __m128i sse_cx_1 = _mm_shuffle_epi32(sse_c00, _MM_SHUFFLE(1, 1, 1, 1));
                __m128i sse_cx_2 = _mm_shuffle_epi32(sse_c00, _MM_SHUFFLE(2, 2, 2, 2));
                sse_cx_0 = _mm_sub_epi32(sse_cx_0, fdy_0);
                sse_cx_1 = _mm_sub_epi32(sse_cx_1, fdy_1);
                sse_cx_2 = _mm_sub_epi32(sse_cx_2, fdy_2);
                for (int ix = 0; ix != BlockSize/4; ++ix)
                {
                    __m128i *data  = buffer + ix;
                    __m128i pixels = _mm_load_si128(data);
                    __m128i r = _mm_and_si128(_mm_cmpgt_epi32(sse_cx_0, sse_zero),
                    _mm_and_si128(_mm_cmpgt_epi32(sse_cx_1, sse_zero),
                    _mm_cmpgt_epi32(sse_cx_2, sse_zero)));
                    pixels = _mm_or_si128(_mm_and_si128(r, sse_color), _mm_andnot_si128(r, pixels));
                    _mm_store_si128(data, pixels);
                    sse_cx_0 = _mm_sub_epi32(sse_cx_0, fdy_0x4);
                    sse_cx_1 = _mm_sub_epi32(sse_cx_1, fdy_1x4);
                    sse_cx_2 = _mm_sub_epi32(sse_cx_2, fdy_2x4);
                }
                sse_c00 = _mm_add_epi32(sse_c00, fdx);
                buffer += RES_X/4;
            }
        }
        color_buffer += BlockSize*RES_X;
    }
}
int main( int argc, char* argv[] )
{
    const size_t count = 50000;
    float pts[7][2];
    for( size_t i = 0; i &lt; 7; ++i )
    {
        pts[i][1] = sin( atan( 1.0f ) * i * 8.0f / 7.0f ) * 30.0f + 30.0f;
        pts[i][0] = cos( atan( 1.0f ) * i * 8.0f / 7.0f ) * 30.0f + 30.0f;
    }
    float cl1 = (float)clock();
    for( size_t j = 0; j &lt; count; ++j )
    {
        for( size_t i = 0; i &lt; 7; ++i )
        {
            size_t n = ( i + 1 ) % 7;
            triangle&lt;3, 4&gt;( pts[n][0], pts[n][1], pts[i][0], pts[i][1], 30, 30, int( i + 1 ) );
        }
    }
    float cl2 = (float)clock();
    int pixels = Present();
    float time = ( cl2 - cl1 ) / float( CLOCKS_PER_SEC );
    float mtps = ( count * pixels ) / 1000.0f / 1000.0f / time;
    printf( "time %f mpixel rate %f \n", time, mtps );
}
