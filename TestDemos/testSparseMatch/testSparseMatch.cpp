#include "CorrespondenceDefs.hpp"
#include "Census.hpp"
#include "fast.hpp"
#include "Matcher.hpp"
#include <iostream>
#include <opencv2/core/core.hpp>
#include <opencv2/highgui/highgui.hpp>
#include <opencv2/features2d/features2d.hpp>

using namespace correspondence;
using namespace cv;

int main(int argc, char** argv)
{
  if( argc != 3 ) 
  {
    std::cout<<std::endl<<"Usage: "<<argv[0]<<"[path to image1] [path to image2]"<<std::endl;
    return -1;
  }

  Mat matL = imread(argv[1], CV_LOAD_IMAGE_GRAYSCALE );
  if( !matL.data ) 
  {
    std::cout<< "Error reading image " << argv[1] << std::endl;
    return -1;
  }
  Mat matR = imread(argv[2], CV_LOAD_IMAGE_GRAYSCALE );

  if( !matR.data ) 
  {
    std::cout<< "Error reading image " << argv[2] << std::endl;
    return -1;
  }
  pt offset;
  offset.x = 0;
  offset.y = 0;
  Image imageL(matL.rows, matL.cols, 1, offset, (byte*)matL.data);
  Image imageR(matR.rows, matR.cols, 1, offset, (byte*)matR.data);

  CensusCfg cfg(SPARSE_8, imageL.rows, imageL.cols, imageL.stride, imageL.pxStep);

  //Do Census Transform
  double t = (double)cv::getTickCount();
  Image censusL(matL.rows, matL.cols, 1/*size of descriptor in bytes*/, offset);
  Image censusR(matR.rows, matR.cols, 1/*size of descriptor in bytes*/, offset);
  t = (double)cv::getTickCount();
  censusTransformSSE(imageL, cfg, censusL);
  censusTransformSSE(imageR, cfg, censusR);
  t = ((double)getTickCount() - t)/getTickFrequency();
  std::cout<<"SSE Census Transform "<<t/1.0<<std::endl;

  //Do Feature Detection
  FeatureList kpsL, kpsR;
  fast10_detect_both(imageL.data, imageL.cols, imageL.rows, imageL.stride, 15, kpsL);
  fast10_detect_both(imageR.data, imageR.cols, imageR.rows, imageR.stride, 15, kpsR);

  //Do Stereo Matching
  MatchingParams params(STEREO, SPARSECW_16, static_cast<int>(imageL.cols * .1), 1, censusL.stride, censusL.pxStep);
  params.filterDist = 11;
  Matcher census(cfg, params, imageL.rows, imageL.cols);

  std::vector<Match> matches;
  t = (double)cv::getTickCount();
  
  census.matchSparse(censusL, censusR, kpsL, kpsR, matches);
  t = ((double)getTickCount() - t)/getTickFrequency();
  std::cout<<"Matching Time "<<t/1.0<<std::endl;
  
  std::cout<<"Number of matches: "<<matches.size()<<std::endl;

  std::vector<cv::DMatch> dmatches;
  std::vector<KeyPoint> cvKpsL, cvKpsR;

  for(size_t i = 0; i < matches.size(); ++i)
  {
    DMatch _match;
    _match.queryIdx = matches[i].feature1Idx;
    _match.trainIdx = matches[i].feature2Idx;
    dmatches.push_back(_match);
  }

  for(size_t i = 0; i < kpsL.allFeatures.size(); ++i)
  {
    KeyPoint kp;
    kp.pt.x = static_cast<float>(kpsL.allFeatures[i].x);
    kp.pt.y = static_cast<float>(kpsL.allFeatures[i].y);
    cvKpsL.push_back(kp);
  }

  for(size_t i = 0; i < kpsR.allFeatures.size(); ++i)
  {
    KeyPoint kp;
    kp.pt.x = static_cast<float>(kpsR.allFeatures[i].x);
    kp.pt.y = static_cast<float>(kpsR.allFeatures[i].y);
    cvKpsR.push_back(kp);
  }

  if(!matches.empty())
  {
    namedWindow("Matches", CV_WINDOW_KEEPRATIO);
    // Draw matches
    Mat imgMatch;
    std::vector<char> mask;
    drawMatches(matL, cvKpsL, matR, cvKpsR, dmatches, imgMatch, cv::Scalar::all(-1), cv::Scalar::all(-1), mask, 2);
    imshow("Matches", imgMatch);
    waitKey(0);
  }

  return 0;
}
