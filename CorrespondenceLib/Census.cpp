#include <xmmintrin.h>
#include <algorithm>
#include "Census.hpp"

using namespace correspondence;

void censusTransform(const Image& im, Image& output, eSamplingWindow type, byte** pResult)
{
}

void censusTransformSSE(const Image& im, const CensusCfg& cfg, Image& rResult)
{
  //Copy pattern to local var
  std::vector<int> offsetsLUT;
  offsetsLUT.insert(offsetsLUT.end(), cfg.pattern.begin(), cfg.pattern.end());
  //Convert offset terms from bytes to __m128i
  /*for(int i = 0; i < offsetsLUT.size(); ++i)
  {
    offsetsLUT[i] /= 16;
  }*/

  int edgeSize = static_cast<int>(cfg.windowSize * .5);
  int pxSize = cfg.pattern.size() / 8;

  __m128i bitconst = {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1};
  __m128i bitmask;

  for(int i = edgeSize; i < im.rows - edgeSize; ++i)
  {
    //Set to beginning of the row
    //struct 'Image' ensures that beginning of row is always aligned
    __m128i* resultPtr = (__m128i*)(rResult.at(i, 0) + 16);
    __m128i* objectPx = (__m128i*)im.at(i, 0);
    
    int j = 0;
    for(j; j < im.stride; j += 16)//Will always divide evenly
    {
      bitmask = bitconst;
      int bitCount = 0;
      for(int k = 0; k < static_cast<int>(offsetsLUT.size()); ++k)
      {
        //Load the next pixel of each center pixel's sampling window
        __m128i samplePx = _mm_loadu_si128((__m128i*)(im.at(i, 0) + offsetsLUT[k]));
        //do comparison
        *resultPtr = _mm_or_si128(*resultPtr, _mm_and_si128(_mm_cmpgt_epi8(samplePx, *objectPx), bitmask));//only load one bit for each comparison
        bitmask = _mm_add_epi8(bitmask, bitmask);//multiply bitmask by 2, to target the next bit of each descriptor
        ++bitCount;
        if(bitCount == 7)
        {
          ++resultPtr;
          bitmask = bitconst;
          bitCount = 0;
        }
      }
      ++objectPx;
      //Store the result into rResult
      storeSSE16(resultPtr, resultPtr - 1, rResult.at(i,j));
    }
  }
}

void censusTransformScalar(const Image& im, const CensusCfg& cfg, Image& rResult)
{
  int edgeSize = static_cast<int>(cfg.windowSize * .5);
  int pxSize = cfg.pattern.size() / 8;

  byte* resultPtr;

  for(int i = edgeSize; i < im.rows - edgeSize; ++i)
  {
    //Set resultPtr to beginning of the row
    resultPtr = rResult.at(i, edgeSize * pxSize);
    for(int j = edgeSize; j < im.cols - edgeSize; ++j)
    {
      //Now we have chosen a pixel to examine
      int bitCount = 0;
      censusTransformSinglePx(im.at(i, j), cfg.pattern, &resultPtr);
    }
  }
}

void storeSSE16(const __m128i* vect1, const __m128i* vect2, byte* dst)
{
  __m128i* dstMMX = (__m128i*)dst;
  *dstMMX = _mm_unpacklo_epi8(*vect1, *vect2);
  *++dstMMX = _mm_unpackhi_epi8(*vect1, *vect2);
}
