/*
 * Function skeletons below mirror OpenCV 5.0.0 (Apache-2.0, (c) OpenCV
 * contributors). Original source is pasted as // comments and is replaced by
 * a C reimplementation step by step. See THIRD_PARTY_NOTICES.md.
 */

#include "aruco_geom.h"

static int find_homography_4(const double src[4][2], const double dst[4][2], double H[9])
{
    (void)src; (void)dst; (void)H;
    return -1;
}

// ---- OpenCV 5.0.0 geometry.cpp : findHomography / getPerspectiveTransform (line 769) ----
// cv::Mat getPerspectiveTransform(const Point2f src[], const Point2f dst[], int solveMethod)
// {
//     CV_INSTRUMENT_REGION();
//
//     // try c22 = 1
//     Mat M(3, 3, CV_64F), X8(8, 1, CV_64F, M.ptr());
//     double a[8][8], b[8];
//     Mat A(8, 8, CV_64F, a), B(8, 1, CV_64F, b);
//
//     for( int i = 0; i < 4; ++i )
//     {
//         a[i][0] = a[i+4][3] = src[i].x;
//         a[i][1] = a[i+4][4] = src[i].y;
//         a[i][2] = a[i+4][5] = 1;
//         a[i][3] = a[i][4] = a[i][5] =
//         a[i+4][0] = a[i+4][1] = a[i+4][2] = 0;
//         a[i][6] = -src[i].x*dst[i].x;
//         a[i][7] = -src[i].y*dst[i].x;
//         a[i+4][6] = -src[i].x*dst[i].y;
//         a[i+4][7] = -src[i].y*dst[i].y;
//         b[i] = dst[i].x;
//         b[i+4] = dst[i].y;
//     }
//
//     if (solve(A, B, X8, solveMethod) && norm(A * X8, B) < 1e-8)
//     {
//         M.ptr<double>()[8] = 1.;
//
//         return M;
//     }
//
//     // c00^2 + c01^2 + c02^2 + c10^2 + c11^2 + c12^2 + c20^2 + c21^2 + c22^2 = 1
//     hconcat(A, -B, A);
//
//     Mat AtA;
//     mulTransposed(A, AtA, true);
//
//     Mat D, U;
//     SVDecomp(AtA, D, U, noArray());
//
//     Mat X9(9, 1, CV_64F, M.ptr());
//     U.col(8).copyTo(X9);
//
//     return M;
// }

// ---- OpenCV 5.0.0 geometry.cpp : getPerspectiveTransform (public) (line 898) ----
// cv::Mat getPerspectiveTransform(InputArray _src, InputArray _dst, int solveMethod)
// {
//     Mat src = _src.getMat(), dst = _dst.getMat();
//     CV_Assert(src.checkVector(2, CV_32F) == 4 && dst.checkVector(2, CV_32F) == 4);
//     return getPerspectiveTransform((const Point2f*)src.data, (const Point2f*)dst.data, solveMethod);
// }

int find_homography(const double (*src)[2], const double (*dst)[2], int n, double H[9])
{
    (void)src; (void)dst; (void)n; (void)H;
    return -1;
}

// ---- OpenCV 5.0.0 fundam.cpp : findHomography (runKernel) (line 124) ----
//     int runKernel( InputArray _m1, InputArray _m2, OutputArray _model ) const CV_OVERRIDE
//     {
//         Mat m1 = _m1.getMat(), m2 = _m2.getMat();
//         int i, count = m1.checkVector(2);
//         const Point2f* M = m1.ptr<Point2f>();
//         const Point2f* m = m2.ptr<Point2f>();
//
//         double LtL[9][9], W[9][1], V[9][9];
//         Mat _LtL( 9, 9, CV_64F, &LtL[0][0] );
//         Mat matW( 9, 1, CV_64F, W );
//         Mat matV( 9, 9, CV_64F, V );
//         Mat _H0( 3, 3, CV_64F, V[8] );
//         Mat _Htemp( 3, 3, CV_64F, V[7] );
//         Point2d cM(0,0), cm(0,0), sM(0,0), sm(0,0);
//
//         for( i = 0; i < count; i++ )
//         {
//             cm.x += m[i].x; cm.y += m[i].y;
//             cM.x += M[i].x; cM.y += M[i].y;
//         }
//
//         cm.x /= count;
//         cm.y /= count;
//         cM.x /= count;
//         cM.y /= count;
//
//         for( i = 0; i < count; i++ )
//         {
//             sm.x += fabs(m[i].x - cm.x);
//             sm.y += fabs(m[i].y - cm.y);
//             sM.x += fabs(M[i].x - cM.x);
//             sM.y += fabs(M[i].y - cM.y);
//         }
//
//         if( fabs(sm.x) < DBL_EPSILON || fabs(sm.y) < DBL_EPSILON ||
//             fabs(sM.x) < DBL_EPSILON || fabs(sM.y) < DBL_EPSILON )
//             return 0;
//         sm.x = count/sm.x; sm.y = count/sm.y;
//         sM.x = count/sM.x; sM.y = count/sM.y;
//
//         double invHnorm[9] = { 1./sm.x, 0, cm.x, 0, 1./sm.y, cm.y, 0, 0, 1 };
//         double Hnorm2[9] = { sM.x, 0, -cM.x*sM.x, 0, sM.y, -cM.y*sM.y, 0, 0, 1 };
//         Mat _invHnorm( 3, 3, CV_64FC1, invHnorm );
//         Mat _Hnorm2( 3, 3, CV_64FC1, Hnorm2 );
//
//         _LtL.setTo(Scalar::all(0));
//         for( i = 0; i < count; i++ )
//         {
//             double x = (m[i].x - cm.x)*sm.x, y = (m[i].y - cm.y)*sm.y;
//             double X = (M[i].x - cM.x)*sM.x, Y = (M[i].y - cM.y)*sM.y;
//             double Lx[] = { X, Y, 1, 0, 0, 0, -x*X, -x*Y, -x };
//             double Ly[] = { 0, 0, 0, X, Y, 1, -y*X, -y*Y, -y };
//             int j, k;
//             for( j = 0; j < 9; j++ )
//                 for( k = j; k < 9; k++ )
//                     LtL[j][k] += Lx[j]*Lx[k] + Ly[j]*Ly[k];
//         }
//         completeSymm( _LtL );
//
//         eigen( _LtL, matW, matV );
//         _Htemp = _invHnorm*_H0;
//         _H0 = _Htemp*_Hnorm2;
//         _H0.convertTo(_model, _H0.type(), scaleFor(_H0.at<double>(2,2)));
//         return 1;
//     }

void perspective_transform(const double H[9], const double src[2], double dst[2])
{
    (void)H; (void)src; (void)dst;
}

// ---- OpenCV 5.0.0 matmul.dispatch.cpp : perspectiveTransform (line 544) ----
// void perspectiveTransform(InputArray _src, OutputArray _dst, InputArray _mtx)
// {
//     CV_INSTRUMENT_REGION();
//
//     Mat src = _src.getMat(), m = _mtx.getMat();
//     int depth = src.depth(), scn = src.channels(), dcn = m.rows-1;
//     CV_Assert( scn + 1 == m.cols );
//     CV_Assert( depth == CV_32F || depth == CV_64F );
//
//     _dst.createSameSize( src, CV_MAKETYPE(depth, dcn) );
//     Mat dst = _dst.getMat();
//
//     const int mtype = CV_64F;
//     AutoBuffer<double> _mbuf;
//     double* mbuf = m.ptr<double>();
//
//     if( !m.isContinuous() || m.type() != mtype )
//     {
//         _mbuf.allocate((dcn+1)*(scn+1));
//         mbuf = _mbuf.data();
//         Mat tmp(dcn+1, scn+1, mtype, mbuf);
//         m.convertTo(tmp, mtype);
//         m = tmp;
//     }
//
//     TransformFunc func = getPerspectiveTransform(depth);
//     CV_Assert( func != 0 );
//
//     const Mat* arrays[] = {&src, &dst, 0};
//     uchar* ptrs[2] = {};
//     NAryMatIterator it(arrays, ptrs);
//     size_t i, total = it.size;
//
//     for( i = 0; i < it.nplanes; i++, ++it )
//         func( ptrs[0], ptrs[1], (uchar*)mbuf, (int)total, scn, dcn );
// }

void undistort_points(const double *src, double *dst, int n)
{
    (void)src; (void)dst; (void)n;
}

// ---- OpenCV 5.0.0 pinhole.cpp : undistortPointsInternal (line 106) ----
// static void undistortPointsInternal( const Mat& _src, Mat& _dst, const Mat& _cameraMatrix,
//                    const Mat& _distCoeffs, const Mat& matR, const Mat& matP, TermCriteria criteria)
// {
//     CV_Assert(criteria.isValid());
//     double A[3][3], RR[3][3], k[14]={0,0,0,0,0,0,0,0,0,0,0,0,0,0};
//     Mat matA(3, 3, CV_64F, A), _Dk;
//     Mat _RR(3, 3, CV_64F, RR);
//     cv::Matx33d invMatTilt = cv::Matx33d::eye();
//     cv::Matx33d matTilt = cv::Matx33d::eye();
//     bool haveDistCoeffs = !_distCoeffs.empty();
//
//     CV_Assert( (_src.rows == 1 || _src.cols == 1) &&
//         (_dst.rows == 1 || _dst.cols == 1) &&
//         _src.cols + _src.rows - 1 == _dst.rows + _dst.cols - 1 &&
//         (_src.type() == CV_32FC2 || _src.type() == CV_64FC2) &&
//         (_dst.type() == CV_32FC2 || _dst.type() == CV_64FC2));
//
//     CV_Assert( _cameraMatrix.rows == 3 && _cameraMatrix.cols == 3 && _cameraMatrix.channels() == 1 );
//     _cameraMatrix.convertTo(matA, CV_64F);
//
//     if( haveDistCoeffs )
//     {
//         CV_Assert(
//             (_distCoeffs.rows == 1 || _distCoeffs.cols == 1) &&
//             (_distCoeffs.rows*_distCoeffs.cols == 4 ||
//              _distCoeffs.rows*_distCoeffs.cols == 5 ||
//              _distCoeffs.rows*_distCoeffs.cols == 8 ||
//              _distCoeffs.rows*_distCoeffs.cols == 12 ||
//              _distCoeffs.rows*_distCoeffs.cols == 14));
//
//         _Dk = Mat( _distCoeffs.rows, _distCoeffs.cols,
//             CV_MAKETYPE(CV_64F,_distCoeffs.channels()), k);
//         _distCoeffs.convertTo(_Dk, CV_64F);
//         CV_Assert(_Dk.ptr<double>() == k);
//         if (k[12] != 0 || k[13] != 0)
//         {
//             computeTiltProjectionMatrix<double>(k[12], k[13], NULL, NULL, NULL, &invMatTilt);
//             computeTiltProjectionMatrix<double>(k[12], k[13], &matTilt, NULL, NULL);
//         }
//     }
//
//     if( !matR.empty() )
//     {
//         CV_Assert( matR.rows == 3 && matR.cols == 3 && matR.channels() == 1 );
//         matR.convertTo(_RR, CV_64F);
//         CV_Assert(_RR.ptr<double>() == &RR[0][0]);
//     }
//     else
//         setIdentity(_RR);
//
//     if( !matP.empty() )
//     {
//         double PP[3][3];
//         Mat _PP(3, 3, CV_64F, PP);
//         CV_Assert( matP.rows == 3 && (matP.cols == 3 || matP.cols == 4));
//         matP.colRange(0, 3).convertTo(_PP, CV_64F);
//         CV_Assert(_PP.ptr<double>() == &PP[0][0]);
//         _RR = _PP*_RR;
//     }
//
//     const Point2f* srcf = (const Point2f*)_src.data;
//     const Point2d* srcd = (const Point2d*)_src.data;
//     Point2f* dstf = (Point2f*)_dst.data;
//     Point2d* dstd = (Point2d*)_dst.data;
//     int stype = _src.type();
//     int dtype = _dst.type();
//     int sstep = _src.rows == 1 ? 1 : (int)(_src.step/_src.elemSize());
//     int dstep = _dst.rows == 1 ? 1 : (int)(_dst.step/_dst.elemSize());
//
//     double fx = A[0][0];
//     double fy = A[1][1];
//     double ifx = 1./fx;
//     double ify = 1./fy;
//     double cx = A[0][2];
//     double cy = A[1][2];
//
//     int n = _src.rows + _src.cols - 1;
//     for( int i = 0; i < n; i++ )
//     {
//         double x, y, x0 = 0, y0 = 0, u, v;
//         if( stype == CV_32FC2 )
//         {
//             x = srcf[i*sstep].x;
//             y = srcf[i*sstep].y;
//         }
//         else
//         {
//             x = srcd[i*sstep].x;
//             y = srcd[i*sstep].y;
//         }
//         // [u, v]^T = [fx * x''' + cx, fy * y''' + cy]^T =>
//         // [x''', y''']^T = [(u - cx) / fx, (v - cy) / fy]^T
//         u = x; v = y;
//         x = (x - cx)*ifx;
//         y = (y - cy)*ify;
//
//         if( haveDistCoeffs ) {
//             // compensate tilt distortion
//             // s * [x''', y''', 1]^T = matTilt * [x'', y'', 1]^T =>
//             // s * matTilt^{-1} * [x''', y''', 1]^T = [x'', y'', 1]^T =>
//             // (invMatTilt := matTilt^{-1}, vecUntilt := invMatTilt * [x''', y''', 1]^T)
//             // s * vecUntilt = [x'', y'', 1]^T =>
//             // s * vecUntilt_1 = x'', s * vecUntilt_2 = y'', s * vecUntilt_3 = 1 =>
//             // invProj := s = 1 / vecUntilt_3, x'' = invProj * vecUntilt_1, y'' = invProj * vecUntilt_2
//             cv::Vec3d vecUntilt = invMatTilt * cv::Vec3d(x, y, 1);
//             double invProj = vecUntilt(2) ? 1./vecUntilt(2) : 1;
//             x0 = x = invProj * vecUntilt(0);
//             y0 = y = invProj * vecUntilt(1);
//
//             double error = std::numeric_limits<double>::max();
//             double prevError = std::numeric_limits<double>::max();
//             // compensate distortion iteratively using fixed-point iteration
//
//             // parameter for damped fixed-point iteration
//             double alpha = 1.;
//
//             for( int j = 0; ; j++ )
//             {
//                 if ((criteria.type & TermCriteria::COUNT) && j >= criteria.maxCount)
//                     break;
//                 if ((criteria.type & TermCriteria::EPS) && error < criteria.epsilon)
//                     break;
//                 // r^2 = x'^2 + y'^2
//                 double r2 = x*x + y*y;
//                 // icdist := (1 + k4 * r^2 + k5 * r^4 + k6 * r^6) / (1 + k1 * r^2 + k2 * r^4 + k3 * r^6)
//                 double icdist = (1 + ((k[7]*r2 + k[6])*r2 + k[5])*r2)/(1 + ((k[4]*r2 + k[1])*r2 + k[0])*r2);
//                 if (icdist < 0)  // test: undistortPoints.regression_14583
//                 {
//                     x = (u - cx)*ifx;
//                     y = (v - cy)*ify;
//                     break;
//                 }
//                 // deltaX := 2 * p1 * x' * y' + p2 * (r^2 + 2 * x'^2) + s1 * r^2 + s2 * r^4
//                 // deltaY := p1 * (r^2 + 2 * y'^2) + 2 * p2 * x' * y' + s3 * r^2 + s4 * r^4
//                 double deltaX = 2*k[2]*x*y + k[3]*(r2 + 2*x*x)+ k[8]*r2+k[9]*r2*r2;
//                 double deltaY = k[2]*(r2 + 2*y*y) + 2*k[3]*x*y+ k[10]*r2+k[11]*r2*r2;
//                 // [x'', y'']^T = [x' / icdist + deltaX, y' / icdist + deltaY]^T =>
//                 // [x', y']^T = [(x'' - deltaX) * icdist, (y'' - deltaY) * icdist]^T =>
//                 // x' = f1(x') := (x'' - deltaX) * icdist, y' = f2(y') := (y'' - deltaY) * icdist
//                 // Damped fixed-point iteration:
//                 //   f1(x') = (x'' - deltaX) * icdist, f2(y') = (y'' - deltaY) * icdist
//                 //   new_x' = (1 - alpha) * x' + alpha * f1(x'), new_y' = (1 - alpha) * y' + alpha * f2(y')
//                 double new_x = (1. - alpha)*x + alpha*(x0 - deltaX)*icdist;
//                 double new_y = (1. - alpha)*y + alpha*(y0 - deltaY)*icdist;
//
//                 if(criteria.type & TermCriteria::EPS)
//                 {
//                     double r4, r6, a1, a2, a3, cdist, icdist2;
//                     double xd, yd, xd0, yd0;
//                     Vec3d vecTilt;
//
//                     // r^2 = x'^2 + y'^2
//                     r2 = new_x*new_x + new_y*new_y;
//                     r4 = r2*r2;
//                     r6 = r4*r2;
//                     a1 = 2*new_x*new_y;
//                     a2 = r2 + 2*new_x*new_x;
//                     a3 = r2 + 2*new_y*new_y;
//                     // cdist := 1 + k1 * r^2 + k2 * r^4 + k3 * r^6
//                     cdist = 1 + k[0]*r2 + k[1]*r4 + k[4]*r6;
//                     // icdist2 := 1 / (1 + k4 * r^2 + k5 * r^4 + k6 * r^6)
//                     icdist2 = 1./(1 + k[5]*r2 + k[6]*r4 + k[7]*r6);
//                     // x'' = x' * cdist * icdist2 + 2 * p1 * x' * y' + p2 * (r^2 + 2 * x'^2) + s1 * r^2 + s2 * r^4
//                     // y'' = y' * cdist * icdist2 + p1 * (r^2 + 2 * y'^2) + 2 * p2 * x' * y' + s3 * r^2 + s4 * r^4
//                     xd0 = new_x*cdist*icdist2 + k[2]*a1 + k[3]*a2 + k[8]*r2+k[9]*r4;
//                     yd0 = new_y*cdist*icdist2 + k[2]*a3 + k[3]*a1 + k[10]*r2+k[11]*r4;
//
//                     // s * [x''', y''', 1]^T = matTilt * [x'', y'', 1]^T =>
//                     // (vecTilt := matTilt * [x'', y'', 1]^T)
//                     // s * [x''', y''', 1]^T = vecTilt =>
//                     // s * x''' = vecTilt_1, s * y''' = vecTilt_2, s = vecTilt_3 =>
//                     // invProj := 1 / s = 1 / vecTilt_3, x''' = invProj * vecTilt_1, y''' = invProj * vecTilt_2
//                     vecTilt = matTilt*cv::Vec3d(xd0, yd0, 1);
//                     invProj = vecTilt(2) ? 1./vecTilt(2) : 1;
//                     xd = invProj * vecTilt(0);
//                     yd = invProj * vecTilt(1);
//
//                     // [u, v]^T = [fx * x''' + cx, fy * y''' + cy]^T
//                     double x_proj = xd*fx + cx;
//                     double y_proj = yd*fy + cy;
//
//                     error = sqrt( std::pow(x_proj - u, 2) + std::pow(y_proj - v, 2) );
//                 }
//                 if (error > prevError) {
//                     alpha *= .5;
//                 } else {
//                     x = new_x;
//                     y = new_y;
//                 }
//                 prevError = error;
//             }
//         }
//
//         if( !matR.empty() || !matP.empty() )
//         {
//             double xx = RR[0][0]*x + RR[0][1]*y + RR[0][2];
//             double yy = RR[1][0]*x + RR[1][1]*y + RR[1][2];
//             double ww = 1./(RR[2][0]*x + RR[2][1]*y + RR[2][2]);
//             x = xx*ww;
//             y = yy*ww;
//         }
//
//         if( dtype == CV_32FC2 )
//         {
//             dstf[i*dstep].x = (float)x;
//             dstf[i*dstep].y = (float)y;
//         }
//         else
//         {
//             dstd[i*dstep].x = x;
//             dstd[i*dstep].y = y;
//         }
//     }
// }

void distort_point(const double src[2], double dst[2])
{
    (void)src; (void)dst;
}

// ---- OpenCV 5.0.0 imgwarp.cpp : warpPerspective (remap side; own: normalized -> k1,k2 -> pixel) (line 3013) ----
// void cv::warpPerspective( InputArray _src, OutputArray _dst, InputArray _M0,
//                           Size dsize, int flags, int borderType, const Scalar& borderValue,
//                           AlgorithmHint hint )
// {
//     CV_INSTRUMENT_REGION();
//
//     if (hint == cv::ALGO_HINT_DEFAULT)
//         hint = cv::getDefaultAlgorithmHint();
//
//     CV_Assert( _src.total() > 0 );
//
//     int interpolation = flags & INTER_MAX;
//
//     CV_OCL_RUN(_src.dims() <= 2 && _dst.isUMat() &&
//                _src.cols() <= SHRT_MAX && _src.rows() <= SHRT_MAX,
//                ocl_warpTransform_cols4(_src, _dst, _M0, dsize, flags, borderType, borderValue,
//                                        OCL_OP_PERSPECTIVE))
//
//     CV_OCL_RUN(_src.dims() <= 2 && _dst.isUMat(),
//                ocl_warpTransform(_src, _dst, _M0, dsize, flags, borderType, borderValue,
//                               OCL_OP_PERSPECTIVE))
//
//     Mat src = _src.getMat(), M0 = _M0.getMat();
//     _dst.create( dsize.empty() ? src.size() : dsize, src.type() );
//     Mat dst = _dst.getMat();
//
//     if( dst.data == src.data )
//         src = src.clone();
//
//     double M[9];
//     Mat matM(3, 3, CV_64F, M);
//
//     if( interpolation == INTER_AREA )
//         interpolation = INTER_LINEAR;
//
//     CV_Assert( (M0.type() == CV_32F || M0.type() == CV_64F) && M0.rows == 3 && M0.cols == 3 );
//     M0.convertTo(matM, matM.type());
//
//     if( !(flags & WARP_INVERSE_MAP) )
//         invert(matM, matM);
//
//     if (genericWarp(src, matM, Mat(), Mat(), dst, interpolation, borderType, borderValue, false)) {
//         return;
//     }
//
//     hal::warpPerspective(src.type(), src.data, src.step, src.cols, src.rows, dst.data, dst.step, dst.cols, dst.rows,
//                         matM.ptr<double>(), interpolation, borderType, borderValue.val, hint);
// }

// ---- OpenCV 5.0.0 fundam.cpp : cv::findHomography (line 449) ----
// Mat findHomography( InputArray _points1, InputArray _points2,
//                            OutputArray _mask, int method, double ransacReprojThreshold )
// {
//     return findHomography(_points1, _points2, method, ransacReprojThreshold, _mask);
// }
