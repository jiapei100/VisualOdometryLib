#include "Matcher.hpp"

using namespace correspondence;

Matcher::Matcher()
  : hammingTfm(0, 0, 0, pt())
{
};

Matcher::Matcher(const CensusCfg& _cfg, const MatchingParams& _params, int _rows, int _cols)
  : cfg(_cfg),
    params(_params),
    hammingTfm(_rows, _cols, 1, pt())
{
}

Matcher::~Matcher()
{
}

void Matcher::matchDense(const Image& censusIm1, const Image& censusIm2,
                         std::vector<correspondence::Match>& rMatches)
{
  //Implement the moving window technique for computing dense disparity maps, as described in 'Fast Census Transform-based Stereo Algorithm using SSE2'
}

void Matcher::matchSparse(const Image& censusIm1, const Image& censusIm2, const FeatureList& kps1, 
                 FeatureList& kps2, std::vector<Match>& rMatches)
{
  //1. Loop through each feature from the first image
  //2. For each feature, usePrepRegion to get a list of possible matches, (Features) from the second image
  //3. Compare each possible match to the left-hand feature, using calcSHD
  //4. Keep track of the right-hand Feature with the smallest SHD
  //5. At the end, if the best feature is within epsilon (normalize for descriptor length) then add it to rMatches

  rMatches.clear();
  rMatches.reserve(kps1.nonmaxFeatures.size());

  for(size_t i = 0; i < kps1.nonmaxFeatures.size(); ++i)//For each left-hand Feature
  {
    //If kp1 is too close to edge of img
    if(kps1.nonmaxFeatures[i].y < params.edgeSize || kps1.nonmaxFeatures[i].y > cfg.imgRows - params.edgeSize ||
      kps1.nonmaxFeatures[i].x < params.edgeSize || kps1.nonmaxFeatures[i].x > cfg.imgCols - params.edgeSize)
      continue;
    std::vector<KpRow> potMatches;
    getPotentialMatches(kps1.nonmaxFeatures[i], kps2, potMatches);
    
    //Call matchFeature, to get the best match from the pool of potential matches
    if(!potMatches.empty())
    {
      Match match = matchFeature(censusIm1, kps1.nonmaxFeatures[i], censusIm2, potMatches);
      //If the match is high-enough quality, add it to rMatches
      if(match.dist < params.filterDist)
        rMatches.push_back(match);
    }
  }
}

void Matcher::getPotentialMatches(const Feature& kp1, FeatureList& kps2, std::vector<KpRow>& rPotMatches)
{
  int firstRow, lastRow, firstCol, lastCol, epipolar;
  //1. Given a matching mode and correlationWindowType, determine the image region that encloses each of the required pixels
  //2. If Stereo, choose an epipolar region, and provide room for the size of the correlation window of each contained Feature
  //3. If Flow, choose a region surrounding the left-hand Feature and capture potential matches within it.  Then, capture all of the pixels required for an SHD of each potential match
  if(params.mode == STEREO)
  {
    epipolar = static_cast<int>(params.epipolarRange * .5);
    firstRow = kp1.y - epipolar;
    lastRow = kp1.y + epipolar;
    firstCol = kp1.x - params.maxDisparity;
    lastCol = kp1.x;

    if(firstRow < params.edgeSize - 1)
      firstRow = params.edgeSize - 1;
    if(lastRow > cfg.imgRows + params.edgeSize)
      lastRow = cfg.imgRows + params.edgeSize;
    if(firstCol < params.edgeSize - 1)
      firstCol = params.edgeSize - 1;
    if(lastCol > cfg.imgCols + params.edgeSize)
      lastCol = cfg.imgCols + params.edgeSize;
  }
//  else
  //  getPotentialFlow(kp1, kps2, cfg, params, rPotMatches);

  rPotMatches.clear();

  int numRows = lastRow - firstRow + 1;
  rPotMatches.reserve(numRows);

  for(int i = 0; i < numRows; ++i)
  {
    KpRow row;
    std::vector<Feature>::iterator iter = kps2.allFeatures.begin() + kps2.rowIdxs[kp1.y];
    while(iter->x < firstCol)
      ++iter;
    if(iter->x < lastCol)
      row.begin = iter;
    else//No valid potential matches on this row
      continue;
    while(iter->x < lastCol)
      ++iter;
    row.end = iter;
    rPotMatches.push_back(row);
  }
}

uint32_t Matcher::calcHammingDist(const uint16_t _1, const uint16_t _2)
{
  //XOR the desc
  uint32_t newBitStr = _1 ^ _2;

  //From Bit Twiddling Hacks
  uint32_t result; // store the total here
  static const int S[] = {1, 2, 4, 8, 16}; // Magic Binary Numbers
  static const int B[] = {0x55555555, 0x33333333, 0x0F0F0F0F, 0x00FF00FF, 0x0000FFFF};

  result = newBitStr - ((newBitStr >> 1) & B[0]);
  result = ((result >> S[1]) & B[1]) + (result & B[1]);
  result = ((result >> S[2]) + result) & B[2];
  result = ((result >> S[3]) + result) & B[3];
  result = ((result >> S[4]) + result) & B[4];

  return result;
}

uint32_t Matcher::calcHammingDistSSE(__m128i _1, __m128i _2)
{
  const __m128i mask_lo = {0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 
                           0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f};
  const __m128i mask_popcnt = {0, 1, 1, 2, 1, 2, 2, 3, 1, 2, 2, 3, 2, 3, 3, 4};
  //Use this one if PSHUFB
  //Use PSHUFB to calculate the popcount, with a 4-bit LUT
  __m128i v = _mm_xor_si128(_1, _2);
  __m128i lo = _mm_and_si128(v, mask_lo);
  __m128i hi = _mm_and_si128(_mm_srli_epi16(v, 4), mask_lo);
  
  lo = _mm_shuffle_epi8(mask_popcnt, lo);
  hi = _mm_shuffle_epi8(mask_popcnt, hi);
  v = _mm_add_epi8(lo, hi);

 const __m128i zeroes = _mm_set1_epi8(0);
 const __m128i ones = _mm_set1_epi16(1);

 __m128i total = _mm_set1_epi32(0);
 
  //Horizontal Add
  lo = _mm_unpacklo_epi8(v, zeroes);
  hi = _mm_unpackhi_epi8(v, zeroes);

  total = _mm_add_epi32(total, _mm_madd_epi16(lo, ones));//sums adjacent u16 values and unpacks to u32
  total = _mm_add_epi32(total, _mm_madd_epi16(hi, ones));
  
  //Shift the remaining entries to the least-significant entry and sum
  total = _mm_add_epi32(total, _mm_srli_si128(total, 8));
  total = _mm_add_epi32(total, _mm_srli_si128(total, 4));
  
  uint32_t popcnt = _mm_cvtsi128_si32(total);//Extract the least-significant int
  
  return popcnt;
}

namespace
{
  uint32_t calcSHD_1B(const Image& census1, const Image& census2,
                 const Feature& kp1, const Feature& kp2, const MatchingParams& params)
  {
    int step = 4;
    int totalDist = 0;

    uint8_t* pxL = census1.at(kp1.y, kp1.x);
    uint8_t* pxR = census2.at(kp2.y, kp2.x);

    for(int i = 0; i < params.pattern.size(); i += step)
    {
      uint32_t l = *(pxL + params.pattern[i]) << 24 | *(pxL + params.pattern[i + 1]) << 16 |
        *(pxL + params.pattern[i + 2]) << 8 | *(pxL + params.pattern[i + 3]);
      uint32_t r = *(pxR + params.pattern[i])  << 24 | *(pxR + params.pattern[i + 1]) << 16 |
        *(pxR + params.pattern[i + 2]) << 8 | *(pxR + params.pattern[i + 3]);

      totalDist += _mm_popcnt_u32(l^r);
    }

    return totalDist;
  }

  uint32_t calcSHD_2B(const Image& census1, const Image& census2,
                 const Feature& kp1, const Feature& kp2, const MatchingParams& params)
  {
    int step = 2;
    int totalDist = 0;

    uint8_t* pxL = census1.at(kp1.y, kp1.x);
    uint8_t* pxR = census2.at(kp2.y, kp2.x);

    for(int i = 0; i < params.pattern.size(); ++i/*i += step*/)
    {
      /*
      uint32_t l = *(pxL + params.pattern[i]) << 24 | *(pxL + params.pattern[i] + 1) << 16 |
        *(pxL + params.pattern[i + 1]) << 8 | *(pxL + params.pattern[i + 1] + 1);
      uint32_t r = *(pxR + params.pattern[i]) << 24 | *(pxR + params.pattern[i] + 1) << 16 |
        *(pxR + params.pattern[i + 1]) << 8 | *(pxR + params.pattern[i + 1] + 1);
        */
      uint32_t l = *(pxL + params.pattern[i]) << 8 | *(pxL + params.pattern[i] + 1);
      uint32_t r = *(pxR + params.pattern[i]) << 8 | *(pxR + params.pattern[i] + 1);
      totalDist += _mm_popcnt_u32(l^r);
    }

    return totalDist;
  }

};

uint32_t Matcher::calcSHD(const Image& census1, const Image& census2,
                 const Feature& kp1, const Feature& kp2)
{
  //1. For each pixel in the correlationWindow of each image, calculate the Hamming Distance and add it to the total
  //2. When finished, return the total
  
  uint32_t totalDist;

  //TRICKY only supporting pxStep of either 2 or 4 right now
  if(census1.pxStep == 2)
    totalDist = calcSHD_2B(census1, census2, kp1, kp2, params);
  else if(census2.pxStep == 1)
    totalDist = calcSHD_1B(census1, census2, kp1, kp2, params);

  return totalDist;
}

correspondence::Match Matcher::matchFeature(const Image& census1, const Feature& kp1, const Image& census2, 
                                   const std::vector<KpRow>& potMatches)
{
  //1. For each possible matching Feature from the right-hand window, compute the SHD
  //2. Compare the latest SHD to the minimum, if the new one is less, update it and the Feature that it is associated with
  Match bestMatch;
  bestMatch.dist = 3000;//Higher than any possible distance, so it will be given a valid value immediately
  bestMatch.feature1Idx = kp1.idx;
  
  for(size_t i = 0; i < potMatches.size(); ++i)
  {
    for(std::vector<Feature>::iterator iter = potMatches[i].begin; iter != potMatches[i].end; ++iter)
    {
      //Use FAST score to reject obviously bad matches out-of-hand
      if((kp1.score < 0 && iter->score > 0) || (kp1.score > 0 && iter->score < 0))
        continue;
      int dist = static_cast<int>(calcSHD(census1, census2, kp1, *iter));
      if(bestMatch.dist > dist)
      {
        bestMatch.dist = dist;
        bestMatch.feature2Idx = iter->idx;
      }
    }
  }
  
  return bestMatch;
}