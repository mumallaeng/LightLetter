/*
 * Function skeletons below mirror OpenCV 5.0.0 (Apache-2.0, (c) OpenCV
 * contributors). Original source is pasted as // comments and is replaced by
 * a C reimplementation step by step. See THIRD_PARTY_NOTICES.md.
 */

#include "aruco_detect.h"
#include "aruco_dict.h"

void adaptive_threshold(const uint8_t *gray, uint8_t *bin, int w, int h, int win, int c)
{
    /* TODO */
}

// ---- OpenCV 5.0.0 thresh.cpp : adaptiveThreshold (line 1901) ----
// void cv::adaptiveThreshold( InputArray _src, OutputArray _dst, double maxValue,
//                             int method, int type, int blockSize, double delta )
// {
//     CV_INSTRUMENT_REGION();
//
//     Mat src = _src.getMat();
//     CV_Assert( src.type() == CV_8UC1 );
//     CV_Assert( blockSize % 2 == 1 && blockSize > 1 );
//     Size size = src.size();
//
//     _dst.create( size, src.type() );
//     Mat dst = _dst.getMat();
//
//     if( maxValue < 0 )
//     {
//         dst = Scalar(0);
//         return;
//     }
//
//     CALL_HAL(adaptiveThreshold, cv_hal_adaptiveThreshold, src.data, src.step, dst.data, dst.step, src.cols, src.rows,
//              maxValue, method, type, blockSize, delta);
//
//     Mat mean;
//
//     if( src.data != dst.data )
//         mean = dst;
//
//     if (method == ADAPTIVE_THRESH_MEAN_C)
//         boxFilter( src, mean, src.type(), Size(blockSize, blockSize),
//                    Point(-1,-1), true, BORDER_REPLICATE|BORDER_ISOLATED );
//     else if (method == ADAPTIVE_THRESH_GAUSSIAN_C)
//     {
//         Mat srcfloat,meanfloat;
//         src.convertTo(srcfloat,CV_32F);
//         meanfloat=srcfloat;
//         GaussianBlur(srcfloat, meanfloat, Size(blockSize, blockSize), 0, 0, BORDER_REPLICATE|BORDER_ISOLATED);
//         meanfloat.convertTo(mean, src.type());
//     }
//     else
//         CV_Error( cv::Error::StsBadFlag, "Unknown/unsupported adaptive threshold method" );
//
//     int i, j;
//     uchar imaxval = saturate_cast<uchar>(maxValue);
//     int idelta = type == THRESH_BINARY ? cvCeil(delta) : cvFloor(delta);
//     uchar tab[768];
//
//     if( type == cv::THRESH_BINARY )
//         for( i = 0; i < 768; i++ )
//             tab[i] = (uchar)(i - 255 > -idelta ? imaxval : 0);
//     else if( type == cv::THRESH_BINARY_INV )
//         for( i = 0; i < 768; i++ )
//             tab[i] = (uchar)(i - 255 <= -idelta ? imaxval : 0);
//     else
//         CV_Error( cv::Error::StsBadFlag, "Unknown/unsupported threshold type" );
//
//     if( src.isContinuous() && mean.isContinuous() && dst.isContinuous() )
//     {
//         size.width *= size.height;
//         size.height = 1;
//     }
//
//     for( i = 0; i < size.height; i++ )
//     {
//         const uchar* sdata = src.ptr(i);
//         const uchar* mdata = mean.ptr(i);
//         uchar* ddata = dst.ptr(i);
//
//         for( j = 0; j < size.width; j++ )
//             ddata[j] = tab[sdata[j] - mdata[j] + 255];
//     }
// }

static void threshold_frame(const uint8_t *gray, uint8_t *bin, int w, int h)
{
    /* TODO */
}

// ---- OpenCV 5.0.0 aruco_detector.cpp : _threshold (line 119) ----
// static void _threshold(InputArray _in, OutputArray _out, int winSize, double constant) {
//
//     CV_Assert(winSize >= 3);
//     if(winSize % 2 == 0) winSize++; // win size must be odd
//     adaptiveThreshold(_in, _out, 255, ADAPTIVE_THRESH_MEAN_C, THRESH_BINARY_INV, winSize, constant);
// }

static int find_marker_contours(const uint8_t *bin, int w, int h, aruco_quad_t *cand, int max_cand)
{
    /* TODO */
    return 0;
}

// ---- OpenCV 5.0.0 aruco_detector.cpp : _findMarkerContours (own: connected components) (line 131) ----
// static void _findMarkerContours(const Mat &in, vector<vector<Point2f> > &candidates,
//                                 vector<vector<Point> > &contoursOut, double minPerimeterRate,
//                                 double maxPerimeterRate, double accuracyRate,
//                                 double minCornerDistanceRate, int minSize) {
//
//     CV_Assert(minPerimeterRate > 0 && maxPerimeterRate > 0 && accuracyRate > 0 &&
//               minCornerDistanceRate >= 0);
//
//     // calculate maximum and minimum sizes in pixels
//     unsigned int minPerimeterPixels =
//         (unsigned int)(minPerimeterRate * max(in.cols, in.rows));
//     unsigned int maxPerimeterPixels =
//         (unsigned int)(maxPerimeterRate * max(in.cols, in.rows));
//
//     // for aruco3 functionality
//     if (minSize != 0) {
//         minPerimeterPixels = 4*minSize;
//     }
//
//     vector<vector<Point> > contours;
//     findContours(in, contours, RETR_LIST, CHAIN_APPROX_NONE);
//     // now filter list of contours
//     for(unsigned int i = 0; i < contours.size(); i++) {
//         // check perimeter
//         if(contours[i].size() < minPerimeterPixels || contours[i].size() > maxPerimeterPixels)
//             continue;
//
//         // check is square and is convex
//         vector<Point> approxCurve;
//         approxPolyDP(contours[i], approxCurve, double(contours[i].size()) * accuracyRate, true);
//         if(approxCurve.size() != 4 || !isContourConvex(approxCurve)) continue;
//
//         // check min distance between corners
//         double minDistSq = max(in.cols, in.rows) * max(in.cols, in.rows);
//         for(int j = 0; j < 4; j++) {
//             double d = (double)(approxCurve[j].x - approxCurve[(j + 1) % 4].x) *
//                            (double)(approxCurve[j].x - approxCurve[(j + 1) % 4].x) +
//                        (double)(approxCurve[j].y - approxCurve[(j + 1) % 4].y) *
//                            (double)(approxCurve[j].y - approxCurve[(j + 1) % 4].y);
//             minDistSq = min(minDistSq, d);
//         }
//         double minCornerDistancePixels = double(contours[i].size()) * minCornerDistanceRate;
//         if(minDistSq < minCornerDistancePixels * minCornerDistancePixels) continue;
//
//         // if it passes all the test, add to candidates vector
//         vector<Point2f> currentCandidate;
//         currentCandidate.resize(4);
//         for(int j = 0; j < 4; j++) {
//             currentCandidate[j] = Point2f((float)approxCurve[j].x, (float)approxCurve[j].y);
//         }
//         candidates.push_back(currentCandidate);
//         contoursOut.push_back(contours[i]);
//     }
// }

static void reorder_candidates_corners(aruco_quad_t *cand, int n)
{
    /* TODO */
}

// ---- OpenCV 5.0.0 aruco_detector.cpp : _reorderCandidatesCorners (line 190) ----
// static void _reorderCandidatesCorners(vector<vector<Point2f> > &candidates) {
//
//     for(unsigned int i = 0; i < candidates.size(); i++) {
//         double dx1 = candidates[i][1].x - candidates[i][0].x;
//         double dy1 = candidates[i][1].y - candidates[i][0].y;
//         double dx2 = candidates[i][2].x - candidates[i][0].x;
//         double dy2 = candidates[i][2].y - candidates[i][0].y;
//         double crossProduct = (dx1 * dy2) - (dy1 * dx2);
//
//         if(crossProduct < 0.0) { // not clockwise direction
//             swap(candidates[i][1], candidates[i][3]);
//         }
//     }
// }

static int detect_candidates(const uint8_t *gray, aruco_quad_t *cand, int max_cand)
{
    /* TODO */
    return 0;
}

// ---- OpenCV 5.0.0 aruco_detector.cpp : detectCandidates (line 921) ----
//     void detectCandidates(const Mat& grey, vector<vector<Point2f> >& candidates, vector<vector<Point> >& contours) {
//         /// 1. DETECT FIRST SET OF CANDIDATES
//         _detectInitialCandidates(grey, candidates, contours, detectorParams);
//         /// 2. SORT CORNERS
//         _reorderCandidatesCorners(candidates);
//     }

static int extract_bits(const uint8_t *gray, int w, int h, const aruco_quad_t *q, uint8_t bits[16])
{
    /* TODO */
    return 0;
}

// ---- OpenCV 5.0.0 aruco_detector.cpp : _extractCellPixelRatio (line 316) ----
// static Mat _extractCellPixelRatio(InputArray _image, const vector<Point2f>& corners, int markerSize,
//                                    int markerBorderBits, int cellSize, double cellMarginRate, double minStdDevOtsu) {
//     CV_Assert(_image.getMat().channels() == 1);
//     CV_Assert(corners.size() == 4ull);
//     CV_Assert(markerBorderBits > 0 && cellSize > 0 && cellMarginRate >= 0 && cellMarginRate <= 0.5);
//     CV_Assert(minStdDevOtsu >= 0);
//
//     // number of bits in the marker
//     int markerSizeWithBorders = markerSize + 2 * markerBorderBits;
//     int cellMarginPixels = int(cellMarginRate * cellSize);
//
//     Mat resultImg; // marker image after removing perspective
//     int resultImgSize = markerSizeWithBorders * cellSize;
//     Mat resultImgCorners(4, 1, CV_32FC2);
//     resultImgCorners.ptr<Point2f>(0)[0] = Point2f(0, 0);
//     resultImgCorners.ptr<Point2f>(0)[1] = Point2f((float)resultImgSize - 1, 0);
//     resultImgCorners.ptr<Point2f>(0)[2] =
//         Point2f((float)resultImgSize - 1, (float)resultImgSize - 1);
//     resultImgCorners.ptr<Point2f>(0)[3] = Point2f(0, (float)resultImgSize - 1);
//
//     // remove perspective
//     Mat transformation = getPerspectiveTransform(corners, resultImgCorners);
//     warpPerspective(_image, resultImg, transformation, Size(resultImgSize, resultImgSize),
//                     INTER_NEAREST);
//
//     // output image containing the ratio of white pixels in each cell
//     Mat cellPixelRatio(markerSizeWithBorders, markerSizeWithBorders, CV_32FC1, Scalar::all(0));
//
//     // check if standard deviation is enough to apply Otsu
//     // if not enough, it probably means all pixels are the same color (black or white)
//     Mat mean, stddev;
//     // Remove some border just to avoid border noise from perspective transformation
//     Mat innerRegion = resultImg.colRange(cellSize / 2, resultImg.cols - cellSize / 2)
//                           .rowRange(cellSize / 2, resultImg.rows - cellSize / 2);
//     meanStdDev(innerRegion, mean, stddev);
//     if(stddev.ptr< double >(0)[0] < minStdDevOtsu) {
//         // all black or all white, depending on mean value
//         if(mean.ptr< double >(0)[0] > 127){
//             cellPixelRatio.setTo(1);
//         } else {
//             cellPixelRatio.setTo(0);
//         }
//
//         return cellPixelRatio;
//     }
//
//     // now extract code, first threshold using Otsu
//     threshold(resultImg, resultImg, 125, 255, THRESH_BINARY | THRESH_OTSU);
//
//     // for each cell
//     for(int y = 0; y < markerSizeWithBorders; y++) {
//         for(int x = 0; x < markerSizeWithBorders; x++) {
//             int Xstart = x * (cellSize) + cellMarginPixels;
//             int Ystart = y * (cellSize) + cellMarginPixels;
//             Mat square = resultImg(Rect(Xstart, Ystart, cellSize - 2 * cellMarginPixels,
//                                         cellSize - 2 * cellMarginPixels));
//             // count white pixels on each cell to assign its value
//             size_t nZ = (size_t) countNonZero(square);
//
//             // define the cell pixel ratio as the ratio of the white pixels. For inverted markers, the ratio will be inverted.
//             cellPixelRatio.at<float>(y, x) = (nZ / (float)square.total());
//         }
//     }
//
//     return cellPixelRatio;
// }

static int dictionary_identify(const uint8_t bits[16], int *id, int *rotation)
{
    /* TODO */
    return 0;
}

// ---- OpenCV 5.0.0 aruco_dictionary.cpp : Dictionary::identify (line 121) ----
// bool Dictionary::identify(const Mat &onlyBits, CV_OUT int &idx, CV_OUT int &rotation, double maxCorrectionRate) const {
//     CV_Assert(onlyBits.rows == markerSize && onlyBits.cols == markerSize);
//
//     Mat candidateBitRatio;
//     onlyBits.convertTo(candidateBitRatio, CV_32F);
//     const float validBitIdThreshold = DEFAULT_VALID_BIT_ID_THRESHOLD;
//     return identify(candidateBitRatio, idx, rotation, maxCorrectionRate, validBitIdThreshold);
// }

static int get_distance_to_id(const uint8_t bits[16], int id)
{
    /* TODO */
    return 0;
}

// ---- OpenCV 5.0.0 aruco_dictionary.cpp : Dictionary::getDistanceToId (line 131) ----
// int Dictionary::getDistanceToId(InputArray bits, int id, bool allRotations) const {
//
//     CV_Assert(id >= 0 && id < bytesList.rows);
//
//     unsigned int nRotations = 4;
//     if(!allRotations) nRotations = 1;
//
//     Mat candidateBytes = getByteListFromBits(bits.getMat());
//     int currentMinDistance = int(bits.total() * bits.total());
//     for(unsigned int r = 0; r < nRotations; r++) {
//         int currentHamming = cv::hal::normHamming(
//                 bytesList.ptr(id) + r*candidateBytes.cols,
//                 candidateBytes.ptr(),
//                 candidateBytes.cols);
//
//         if(currentHamming < currentMinDistance) {
//             currentMinDistance = currentHamming;
//         }
//     }
//     return currentMinDistance;
// }

static int identify_one_candidate(const uint8_t *gray, int w, int h, aruco_quad_t *q, int *id)
{
    /* TODO */
    return 0;
}

// ---- OpenCV 5.0.0 aruco_detector.cpp : _identifyOneCandidate (line 466) ----
// static uint8_t _identifyOneCandidate(const Dictionary& dictionary, const Mat& _image,
//                                      const vector<Point2f>& _corners, int& idx,
//                                      const DetectorParameters& params, int& rotation,
//                                      float &markerConfidence, bool confidenceNeeded,
//                                      const float scale = 1.f) {
//     CV_DbgAssert(params.markerBorderBits > 0);
//     uint8_t typ=1;
//     // get bits
//     // scale corners to the correct size to search on the corresponding image pyramid
//     vector<Point2f> scaled_corners(4);
//     for (int i = 0; i < 4; ++i) {
//         scaled_corners[i].x = _corners[i].x * scale;
//         scaled_corners[i].y = _corners[i].y * scale;
//     }
//
//     Mat cellPixelRatio =
//         _extractCellPixelRatio(_image, scaled_corners, dictionary.markerSize, params.markerBorderBits,
//                                params.perspectiveRemovePixelPerCell,
//                                params.perspectiveRemoveIgnoredMarginPerCell, params.minOtsuStdDev);
//
//     // analyze border bits
//     int maximumErrorsInBorder =
//     int(dictionary.markerSize * dictionary.markerSize * params.maxErroneousBitsInBorderRate);
//     int borderErrors =
//         _getBorderErrors(cellPixelRatio, dictionary.markerSize, params.markerBorderBits, params.validBitIdThreshold);
//
//     // check if it is a white marker
//     if(params.detectInvertedMarker){
//         Mat invCellPixelRatio = 1.f - cellPixelRatio;
//         int invBError = _getBorderErrors(invCellPixelRatio, dictionary.markerSize, params.markerBorderBits, params.validBitIdThreshold);
//         // white marker
//         if(invBError<borderErrors){
//             borderErrors = invBError;
//             invCellPixelRatio.copyTo(cellPixelRatio);
//             typ=2;
//         }
//     }
//     if(borderErrors > maximumErrorsInBorder) return 0; // border is wrong
//
//     // take only inner bits
//     Mat onlyCellPixelRatio =
//         cellPixelRatio.rowRange(params.markerBorderBits,
//                                 cellPixelRatio.rows - params.markerBorderBits)
//             .colRange(params.markerBorderBits, cellPixelRatio.cols - params.markerBorderBits);
//
//     // try to identify the marker
//     if(!dictionary.identify(onlyCellPixelRatio, idx, rotation, params.errorCorrectionRate, params.validBitIdThreshold))
//         return 0;
//
//     // compute the candidate's confidence
//     if(confidenceNeeded) {
//         Mat groundTruthbits = dictionary.getMarkerBits(idx, rotation);
//         markerConfidence = _getMarkerConfidence(groundTruthbits, cellPixelRatio, dictionary.markerSize, params.markerBorderBits);
//     }
//
//     return typ;
// }

int detect_markers(const uint8_t *rgb, int w, int h, int stride, aruco_marker_t *out, int max_out)
{
    /* TODO */
    return 0;
}

// ---- OpenCV 5.0.0 aruco_detector.cpp : ArucoDetectorImpl::detectMarkers (line 719) ----
//     void detectMarkers(InputArray _image, OutputArrayOfArrays _corners, OutputArray _ids,
//             OutputArrayOfArrays _rejectedImgPoints, OutputArray _dictIndices, OutputArray _markersConfidence, DictionaryMode dictMode) {
//         CV_Assert(!_image.empty());
//
//         CV_Assert(detectorParams.markerBorderBits > 0);
//         // check that the parameters are set correctly if Aruco3 is used
//         CV_Assert(!(detectorParams.useAruco3Detection == true &&
//                     detectorParams.minSideLengthCanonicalImg == 0 &&
//                     detectorParams.minMarkerLengthRatioOriginalImg == 0.0));
//
//         Mat grey;
//         _convertToGrey(_image, grey);
//
//         // Aruco3 functionality is the extension of Aruco.
//         // The description can be found in:
//         // [1] Speeded up detection of squared fiducial markers, 2018, FJ Romera-Ramirez et al.
//         // if Aruco3 functionality if not wanted
//         // change some parameters to be sure to turn it off
//         if (!detectorParams.useAruco3Detection) {
//             detectorParams.minMarkerLengthRatioOriginalImg = 0.0;
//             detectorParams.minSideLengthCanonicalImg = 0;
//         }
//         else {
//             // always turn on corner refinement in case of Aruco3, due to upsampling
//             detectorParams.cornerRefinementMethod = (int)CORNER_REFINE_SUBPIX;
//             // only CORNER_REFINE_SUBPIX implement correctly for useAruco3Detection
//             // Todo: update other CORNER_REFINE methods
//         }
//
//         /// Step 0: equation (2) from paper [1]
//         const float fxfy = (!detectorParams.useAruco3Detection ? 1.f : detectorParams.minSideLengthCanonicalImg /
//                 (detectorParams.minSideLengthCanonicalImg + std::max(grey.cols, grey.rows)*
//                  detectorParams.minMarkerLengthRatioOriginalImg));
//
//         /// Step 1: create image pyramid. Section 3.4. in [1]
//         vector<Mat> grey_pyramid;
//         int closest_pyr_image_idx = 0, num_levels = 0;
//         //// Step 1.1: resize image with equation (1) from paper [1]
//         if (detectorParams.useAruco3Detection) {
//             const float scale_pyr = 2.f;
//             const float img_area = static_cast<float>(grey.rows*grey.cols);
//             const float min_area_marker = static_cast<float>(detectorParams.minSideLengthCanonicalImg*
//                     detectorParams.minSideLengthCanonicalImg);
//             // find max level
//             num_levels = static_cast<int>(log2(img_area / min_area_marker)/scale_pyr);
//             // the closest pyramid image to the downsampled segmentation image
//             // will later be used as start index for corner upsampling
//             const float scale_img_area = img_area * fxfy * fxfy;
//             closest_pyr_image_idx = cvRound(log2(img_area / scale_img_area)/scale_pyr);
//         }
//         buildPyramid(grey, grey_pyramid, num_levels);
//
//         // resize to segmentation image
//         // in this reduces size the contours will be detected
//         if (fxfy != 1.f)
//             resize(grey, grey, Size(cvRound(fxfy * grey.cols), cvRound(fxfy * grey.rows)));
//
//         /// STEP 2: Detect marker candidates
//         vector<vector<Point2f> > candidates;
//         vector<vector<Point> > contours;
//         vector<int> ids;
//         vector<float> markersConfidence;
//
//         /// STEP 2.a Detect marker candidates :: using AprilTag
//         if(detectorParams.cornerRefinementMethod == (int)CORNER_REFINE_APRILTAG){
//             _apriltag(grey, detectorParams, candidates, contours);
//         }
//         /// STEP 2.b Detect marker candidates :: traditional way
//         else {
//             detectCandidates(grey, candidates, contours);
//         }
//
//         /// STEP 2.c FILTER OUT NEAR CANDIDATE PAIRS
//         vector<int> dictIndices;
//         vector<vector<Point2f>> rejectedImgPoints;
//         if (DictionaryMode::Single == dictMode) {
//             Dictionary& dictionary = dictionaries.at(0);
//             auto selectedCandidates = filterTooCloseCandidates(grey.size(), candidates, contours, dictionary.markerSize);
//             candidates.clear();
//             contours.clear();
//
//             /// STEP 2: Check candidate codification (identify markers)
//             identifyCandidates(grey, grey_pyramid, selectedCandidates, candidates, contours,
//                     ids, dictionary, rejectedImgPoints, markersConfidence, _markersConfidence.needed());
//
//             /// STEP 3: Corner refinement :: use corner subpix
//             if (detectorParams.cornerRefinementMethod == (int)CORNER_REFINE_SUBPIX) {
//                 performCornerSubpixRefinement(grey, grey_pyramid, closest_pyr_image_idx, candidates, dictionary);
//             }
//         } else if (DictionaryMode::Multi == dictMode) {
//             map<int, vector<MarkerCandidateTree>> candidatesPerDictionarySize;
//             for (const Dictionary& dictionary : dictionaries) {
//                 candidatesPerDictionarySize.emplace(dictionary.markerSize, vector<MarkerCandidateTree>());
//             }
//
//             // create candidate trees for each dictionary size
//             for (auto& candidatesTreeEntry : candidatesPerDictionarySize) {
//                 // copy candidates
//                 vector<vector<Point2f>> candidatesCopy = candidates;
//                 vector<vector<Point> > contoursCopy = contours;
//                 candidatesTreeEntry.second = filterTooCloseCandidates(grey.size(), candidatesCopy, contoursCopy, candidatesTreeEntry.first);
//             }
//             candidates.clear();
//             contours.clear();
//
//             /// STEP 2: Check candidate codification (identify markers)
//             int dictIndex = 0;
//             for (const Dictionary&  currentDictionary : dictionaries) {
//                 // temporary variable to store the current candidates
//                 vector<vector<Point2f>> currentCandidates;
//                 identifyCandidates(grey, grey_pyramid, candidatesPerDictionarySize.at(currentDictionary.markerSize), currentCandidates, contours,
//                         ids, currentDictionary, rejectedImgPoints, markersConfidence, _markersConfidence.needed());
//                 if (_dictIndices.needed()) {
//                     dictIndices.insert(dictIndices.end(), currentCandidates.size(), dictIndex);
//                 }
//
//                 /// STEP 3: Corner refinement :: use corner subpix
//                 if (detectorParams.cornerRefinementMethod == (int)CORNER_REFINE_SUBPIX) {
//                     performCornerSubpixRefinement(grey, grey_pyramid, closest_pyr_image_idx, currentCandidates, currentDictionary);
//                 }
//                 candidates.insert(candidates.end(), currentCandidates.begin(), currentCandidates.end());
//                 dictIndex++;
//             }
//
//             // Clean up rejectedImgPoints by comparing to itself and all candidates
//             const float epsilon = 0.000001f;
//             auto compareCandidates = [epsilon](vector<Point2f> a, vector<Point2f> b) {
//                 for (int i = 0; i < 4; i++) {
//                     if (std::abs(a[i].x - b[i].x) > epsilon || std::abs(a[i].y - b[i].y) > epsilon) {
//                         return false;
//                     }
//                 }
//                 return true;
//             };
//             std::sort(rejectedImgPoints.begin(), rejectedImgPoints.end(), [](const vector<Point2f>& a, const vector<Point2f>&b){
//                     float avgX = (a[0].x + a[1].x + a[2].x + a[3].x)*.25f;
//                     float avgY = (a[0].y + a[1].y + a[2].y + a[3].y)*.25f;
//                     float aDist = avgX*avgX + avgY*avgY;
//                     avgX = (b[0].x + b[1].x + b[2].x + b[3].x)*.25f;
//                     avgY = (b[0].y + b[1].y + b[2].y + b[3].y)*.25f;
//                     float bDist = avgX*avgX + avgY*avgY;
//                     return aDist < bDist;
//                 });
//             auto last = std::unique(rejectedImgPoints.begin(), rejectedImgPoints.end(), compareCandidates);
//             rejectedImgPoints.erase(last, rejectedImgPoints.end());
//
//             for (auto it = rejectedImgPoints.begin(); it != rejectedImgPoints.end();) {
//                 bool erased = false;
//                 for (const auto& candidate : candidates) {
//                     if (compareCandidates(candidate, *it)) {
//                         it = rejectedImgPoints.erase(it);
//                         erased = true;
//                         break;
//                     }
//                 }
//                 if (!erased) {
//                     it++;
//                 }
//             }
//         }
//
//         /// STEP 3, Optional : Corner refinement :: use contour container
//         if (detectorParams.cornerRefinementMethod == (int)CORNER_REFINE_CONTOUR){
//
//             if (!ids.empty()) {
//
//                 // do corner refinement using the contours for each detected markers
//                 parallel_for_(Range(0, (int)candidates.size()), [&](const Range& range) {
//                         for (int i = range.start; i < range.end; i++) {
//                         _refineCandidateLines(contours[i], candidates[i]);
//                         }
//                         });
//             }
//         }
//
//         if (detectorParams.cornerRefinementMethod != (int)CORNER_REFINE_SUBPIX && fxfy != 1.f) {
//             // only CORNER_REFINE_SUBPIX implement correctly for useAruco3Detection
//             // Todo: update other CORNER_REFINE methods
//
//             // scale to original size, this however will lead to inaccurate detections!
//             for (auto &vecPoints : candidates)
//                 for (auto &point : vecPoints)
//                     point *= 1.f/fxfy;
//         }
//
//         // copy to output arrays
//         _copyVector2Output(candidates, _corners);
//         Mat(ids).copyTo(_ids);
//         if(_rejectedImgPoints.needed()) {
//             _copyVector2Output(rejectedImgPoints, _rejectedImgPoints);
//         }
//         if (_dictIndices.needed()) {
//             Mat(dictIndices).copyTo(_dictIndices);
//         }
//         if (_markersConfidence.needed()) {
//             Mat(markersConfidence).copyTo(_markersConfidence);
//         }
//     }
