#include <Eigen/Geometry>
#include "pcl/common/transformation_from_correspondences.h"

// Open CV
#include "cv.h"
#include "highgui.h"

extern "C" {
#include "sift.h"
#include "imgfeatures.h"
#include "kdtree.h"
#include "utils.h"	// stack_imgs
#include "xform.h"
}

#include "CameraDevice.h"
#include "Config.h"
#include "CommonTypes.h"
#include "FrameData.h"
#include "Matching.h"
#include "Timer.h"

#include <iostream>
#include <vector>
#include <fstream>

using namespace std;

/* the maximum number of keypoint NN candidates to check during BBF search */
#define KDTREE_BBF_MAX_NN_CHKS		200
/* threshold on squared ratio of distances between NN and 2nd NN */
#define NN_SQ_DIST_RATIO_THR		0.49
// relative difference of depth for a valid match (higher -> more tolerant)
#define MATCH_RELATIVE_DEPTH		0.1


// -----------------------------------------------------------------------------------------------------
//  evaluateTransform
// -----------------------------------------------------------------------------------------------------
void evaluateTransform(
		const Eigen::Matrix4f& transformation,
        const std::vector<Eigen::Vector3f> &matchesOrig,
        const std::vector<Eigen::Vector3f> &matchesDest,
        double maxError,        
        std::vector<int> &inliers,
        double &meanError,
        float &ratio)
{
	inliers.clear();
	meanError = 0.0;
	ratio = 0.0;

	// for every matching point
	for (unsigned int id = 0; id < matchesOrig.size(); id++)
	{
		// vectors with homogeneous coordinates
		Eigen::Vector4f orig(matchesOrig[id][0], matchesOrig[id][1], matchesOrig[id][2], 1.0);
		Eigen::Vector4f dest(matchesDest[id][0], matchesDest[id][1], matchesDest[id][2], 1.0);

		// project the original point and compute the difference vector wrt the match
		Eigen::Vector4f vectorDiff = (transformation * orig) - dest;

		// compute the error
		double error = vectorDiff.squaredNorm();

		// check and ignore outlier
		if (error > maxError)
			continue;

		// keep the inlier
		inliers.push_back(id);
		meanError += sqrt(error);
	}

	if (inliers.size()>0)
		meanError /= inliers.size();
	else
		meanError = -1.0;

	ratio = (float)inliers.size()/matchesOrig.size();
}

// -----------------------------------------------------------------------------------------------------
//  drawInliers
// -----------------------------------------------------------------------------------------------------
void drawInliers(
		FrameData &frameData1,
		FrameData &frameData2,
		const vector<int> &indexMatches,
		const vector<int> &indexBestInliers,
		const vector<int> &initialPairs,
		bool forLoopClosure)
{
	CvPoint pt1, pt2;
	CvFont font;
	double hScale=0.5;
	double vScale=0.5;
	int    lineWidth=1;
	// define a font to write some text
	cvInitFont(&font, CV_FONT_HERSHEY_SIMPLEX, hScale,vScale, 0, lineWidth);

	IplImage* imgStackedInliers = NULL;

	// stack the 2 images
	imgStackedInliers = stack_imgs( frameData1.getImage(), frameData2.getImage() );

	// draw red lines for outliers
	// all the initial matches are drawn here - the inliers will be overwritten with green
	for (int i=0; i<indexMatches.size(); i++)
	{
		int idMatch = indexMatches[i];
		const struct feature* feat1 = frameData1.getFeature(idMatch);
		const struct feature* feat2 = frameData1.getFeatureMatch(idMatch);

		// draw a line through the 2 points in the stacked image
		pt1 = cvPoint( cvRound( feat1->x ), cvRound( feat1->y ) );
		pt2 = cvPoint( cvRound( feat2->x ), cvRound( feat2->y ) );
		pt2.y += frameData1.getImage()->height;
		// draw a green line
		cvLine( imgStackedInliers, pt1, pt2, CV_RGB(255,0,0), 1, 8, 0 );
	}
	// draw green lines for the best inliers
	for (int i=0; i<indexBestInliers.size(); i++)
	{
		int idInlier = indexBestInliers[i];
		int idMatch = indexMatches[idInlier];
		const struct feature* feat1 = frameData1.getFeature(idMatch);
		const struct feature* feat2 = frameData1.getFeatureMatch(idMatch);

		// draw a line through the 2 points in the stacked image
		pt1 = cvPoint( cvRound( feat1->x ), cvRound( feat1->y ) );
		pt2 = cvPoint( cvRound( feat2->x ), cvRound( feat2->y ) );
		pt2.y += frameData1.getImage()->height;
		// draw a green line
		cvLine( imgStackedInliers, pt1, pt2, CV_RGB(0,255,0), 1, 8, 0 );
	}
	// draw lines for the 3 initial points
	for (int i=0; i<initialPairs.size(); i++)
	{
		int idMatch = initialPairs[i];
		const struct feature* feat1 = frameData1.getFeature(idMatch);
		const struct feature* feat2 = frameData1.getFeatureMatch(idMatch);

		// draw a line through the 2 points in the stacked image
		pt1 = cvPoint( cvRound( feat1->x ), cvRound( feat1->y ) );
		pt2 = cvPoint( cvRound( feat2->x ), cvRound( feat2->y ) );
		pt2.y += frameData1.getImage()->height;
		// draw a green line
		cvLine( imgStackedInliers, pt1, pt2, CV_RGB(0,0,140), 2, 8, 0 );
	}

	// save stacked image
	char buf[256];
	sprintf(buf,"inliers:%d/%d (%d%%)", indexBestInliers.size(), indexMatches.size(), indexBestInliers.size()*100/indexMatches.size());
	cvPutText(imgStackedInliers, buf, cvPoint(5, 950), &font, cvScalar(255,255,0));
	if (forLoopClosure)
		sprintf(buf, "%s/loopc_%d_%d_inliers.bmp", Config::_ResultDirectory.c_str(), frameData1.getFrameID(), frameData2.getFrameID());
	else
		sprintf(buf, "%s/sift_%d_%d_inliers.bmp", Config::_ResultDirectory.c_str(), frameData1.getFrameID(), frameData2.getFrameID());
	cvSaveImage(buf, imgStackedInliers);
	cvReleaseImage(&imgStackedInliers);
}

// -----------------------------------------------------------------------------------------------------
//  findTransformRANSAC
// -----------------------------------------------------------------------------------------------------
bool findTransformRANSAC(
		FrameData &frameData1,
		FrameData &frameData2,
		vector<int> &indexMatches,
		vector<Eigen::Vector3f>	&matchesOrig,
		vector<Eigen::Vector3f>	&matchesDest,
		Transformation &resultTransform,
		bool forLoopClosure)
{
	bool validTransformation = false;
	// find transform pairs
	pcl::TransformationFromCorrespondences tfc;
	Eigen::Matrix4f bestTransformationMat;
	std::vector<int> indexBestInliers;
	vector<int>	initialPairs;	// to track the 3 first points
	int nbValidMatches = indexMatches.size();
	double bestError = 1E10;	// large value
	float bestRatio = 0;

	if (nbValidMatches < 3)
		return false;

	for (int iteration=0; iteration<Config::_MatchingNbIterations; iteration++)
	{
		//printf("\nIteration %d ... \t", iteration+1);
		tfc.reset();
		// pickup 3 points from matches
		for (int i=0; i<3; i++)
		{
			int id_match = rand() % nbValidMatches;
			tfc.add(matchesOrig[id_match], matchesDest[id_match]);
		}

		/* alternative method for choosing the 3 initial pairs
		{
			int id_match;
			initialPairs.clear();
			if (indexArea1.size()==0 || indexArea2.size()==0 || indexArea3.size()==0)
			{
				printf("Data not dispatched. %d %d %d", indexArea1.size(), indexArea2.size(), indexArea3.size());
				break;
			}
			// select 1 random point from area1
			id_match = indexArea1[rand() % indexArea1.size()];
			tfc.add(matchesOrig[id_match], matchesDest[id_match]);
			initialPairs.push_back(indexMatches[id_match]);
			// select 1 random point from area2
			id_match = indexArea2[rand() % indexArea2.size()];
			tfc.add(matchesOrig[id_match], matchesDest[id_match]);
			initialPairs.push_back(indexMatches[id_match]);
			// select 1 random point from area3
			id_match = indexArea3[rand() % indexArea3.size()];
			tfc.add(matchesOrig[id_match], matchesDest[id_match]);
			initialPairs.push_back(indexMatches[id_match]);
		}*/

		// compute transformation from matches
		Eigen::Matrix4f transformation = tfc.getTransformation().matrix();

		// compute error and keep only inliers
		std::vector<int> indexInliers;
		double maxInlierDistance = Config::_MatchingMaxDistanceInlier;
		double meanError;
		float ratio;

		evaluateTransform(transformation,
			matchesOrig,
			matchesDest,
			maxInlierDistance * maxInlierDistance,
			indexInliers,
			meanError,
			ratio);

		//printf("Found %d inliers (%d%%)\tMean error:%f\n", indexInliers.size(), indexInliers.size()*100/nbValidMatches, meanError);

		if (meanError<0 || meanError >= maxInlierDistance)
			continue;	// skip these 3 points and go for a new iteration

		if (meanError < bestError)
		{
			if (ratio > bestRatio)
				bestRatio = ratio;

			if (indexInliers.size()<Config::_MatchingMinNbInlier || ratio<Config::_MatchingMinRatioInlier)
				continue;	// not enough inliers found

			//printf("\t => Best candidate transformation! ", indexInliers.size(), meanError);
			bestTransformationMat = transformation;
			bestError = meanError;
			indexBestInliers = indexInliers;
		}

		// ----------------------------------------------------
		// recompute a new transformation with all the inliers
		// ----------------------------------------------------
		//printf("\nRecomputing transfo... \t");
		tfc.reset();
		for (int idInlier = 0; idInlier < indexInliers.size(); idInlier++) {
			int idMatch  = indexInliers[idInlier];
			tfc.add(matchesOrig[idMatch], matchesDest[idMatch]);
		}
		// compute transformation from inliers
		transformation = tfc.getTransformation().matrix();

		evaluateTransform(transformation,
				matchesOrig,
				matchesDest,
				maxInlierDistance * maxInlierDistance,
				indexInliers,
				meanError,
				ratio);

		if (meanError<0 || meanError >= maxInlierDistance)
			continue;	// skip these 3 points and go for a new iteration

		if (meanError < bestError)
		{
			if (ratio > bestRatio)
				bestRatio = ratio;

			if (indexInliers.size()<Config::_MatchingMinNbInlier || ratio<Config::_MatchingMinRatioInlier)
				continue;	// not enough inliers found

			//printf("\t => Best transformation! ", indexInliers.size(), meanError);
			bestTransformationMat = transformation;
			bestError = meanError;
			indexBestInliers = indexInliers;
		}
	}

	if (indexBestInliers.size()>0)
	{
		// RANSAC success
		std::cout << "Best Transformation --->\t" << indexBestInliers.size() << "/" << nbValidMatches;
		std::cout << " inliers (" << indexBestInliers.size()*100/nbValidMatches <<  "%)";
		std::cout << "\terror="<< bestError << std::endl;
		//std::cout << bestTransformationMat << std::endl;

		char buf[256];
		sprintf(buf, "%s/stats.log", Config::_ResultDirectory.c_str());
		std::ofstream fileStats(buf, ios_base::app);
		fileStats << frameData1.getFrameID() << "-" << frameData2.getFrameID();
		fileStats << "\t" << indexBestInliers.size() << "\t" << nbValidMatches;
		fileStats << "\t" << indexBestInliers.size()*100/nbValidMatches << "\n";

		validTransformation = true;
		resultTransform._matrix = bestTransformationMat;
		resultTransform._error = bestError;
		resultTransform._ratioInliers = float(indexBestInliers.size())/nbValidMatches;

		drawInliers(frameData1, frameData2, indexMatches, indexBestInliers, initialPairs, forLoopClosure);
	}
	else
	{
		// no valid transformation found
		validTransformation = false;
		resultTransform._ratioInliers = bestRatio;
	}

	return validTransformation;
}

// -----------------------------------------------------------------------------------------------------
//  kdSearchFeatureMatches
// -----------------------------------------------------------------------------------------------------
void kdSearchFeatureMatches(
		FrameData &frameData1,
		FrameData &frameData2,
		vector<int> &indexMatches,
		vector<Eigen::Vector3f>	&matchesOrig,
		vector<Eigen::Vector3f>	&matchesDest,
		bool forLoopClosure)
{
	Timer tm;
	struct feature** neighbourFeatures = NULL;	// SIFT feature
	struct feature* feat;
	struct kd_node* kdRoot;
	CvPoint pt1, pt2;
	double d0, d1;
	int k, i, nbInitialMatches = 0, nbValidMatches = 0;
	char buf[256];
	float constant = 0.001 / CameraDevice::_FocalLength;    // TODO - redefine this properly
	IplImage* imgStacked = NULL;

	CvFont font;
	double hScale=0.5;
	double vScale=0.5;
	int    lineWidth=1;
	// define a font to write some text
	cvInitFont(&font, CV_FONT_HERSHEY_SIMPLEX, hScale,vScale, 0, lineWidth);

	//vector<int>	indexArea1, indexArea2, indexArea3;

	indexMatches.clear();
	matchesOrig.clear();
	matchesDest.clear();

	if (Config::_MatchingSaveImageInitialPairs)
	{
		// stack the 2 images
		imgStacked = stack_imgs(frameData1.getImage(), frameData2.getImage());
	}

	tm.start();
	printf("Frames %03d-%03d:\t Searching for matches... ", frameData1.getFrameID(), frameData2.getFrameID());
	fflush(stdout);

	// try match the new features found in the 2d frame...
	kdRoot = kdtree_build(frameData2.getFeatures(), frameData2.getNbFeatures());
	fflush(stdout);
	for (i=0; i < frameData1.getNbFeatures(); i++)
	{
		// ... looking in the 1st frame
		feat = frameData1.getFeatures() + i;
		// search for 2 nearest neighbours
		fflush(stdout);
		k = kdtree_bbf_knn(kdRoot, feat, 2, &neighbourFeatures, KDTREE_BBF_MAX_NN_CHKS);
		if (k == 2)
		{
			// the neighbours are ordered in increasing descriptor distance
			d0 = descr_dist_sq( feat, neighbourFeatures[0] );
			d1 = descr_dist_sq( feat, neighbourFeatures[1] );
			// check if the 2 are close enough
			if (d0 < d1 * NN_SQ_DIST_RATIO_THR)
			{
				const struct feature* feat1 = frameData1.getFeature(i);
				const struct feature* feat2 = neighbourFeatures[0];
	            
				// draw a line through the 2 points in the stacked image
				pt1 = cvPoint( cvRound( feat1->x ), cvRound( feat1->y ) );
				pt2 = cvPoint( cvRound( feat2->x ), cvRound( feat2->y ) );
				pt2.y += frameData1.getImage()->height;
				nbInitialMatches++;
				
				// read depth info
				const TDepthPixel depth1 = frameData1.getFeatureDepth(feat1);
				const TDepthPixel depth2 = frameData2.getFeatureDepth(feat2);				
				
				// check if depth values are close enough (values in mm)
				if (depth1>0 &&
					depth2>0 &&
					abs(depth1-depth2)/float(depth1) < MATCH_RELATIVE_DEPTH)	// read: relative diff
				{
					// draw a green line
					if (imgStacked != NULL)
						cvLine( imgStacked, pt1, pt2, CV_RGB(0,255,0), 1, 8, 0 );
					// this is a valid match
					nbValidMatches++;
					// fwd link the previous features to the new features according to the match
					frameData1.setFeatureMatch(i, neighbourFeatures[0]);
					indexMatches.push_back(i);
			
					// convert pixels to metric
					float z1 = (feat1->x - NBPIXELS_X_HALF) * depth1 * constant;
					float y1 = (NBPIXELS_Y_HALF - feat1->y) * depth1 * constant;
					float x1 = depth1 * 0.001 ; // given depth values are in mm
					
					float z2 = (feat2->x - NBPIXELS_X_HALF) * depth2 * constant;
					float y2 = (NBPIXELS_Y_HALF - feat2->y) * depth2 * constant;
					float x2 = depth2 * 0.001 ; // given depth values are in mm

					Eigen::Vector3f orig(x1,y1,z1);
					Eigen::Vector3f dest(x2,y2,z2);
					
					matchesOrig.push_back(orig);
					matchesDest.push_back(dest);
					
					/* alternative method
					if (feat1->x < 210)
						indexArea1.push_back(nbValidMatches-1);
					else if (feat1->x > 430)
						indexArea3.push_back(nbValidMatches-1);
					else
						indexArea2.push_back(nbValidMatches-1);*/
				}
				else
				{
					// ignore the pairs without depth any info, but show the remaining outliers
					if (depth1>0 || depth2>0)
					{
						if (imgStacked != NULL)
						{
							// draw a red line
							cvLine(imgStacked, pt1, pt2, CV_RGB(255,0,0), 1, 8, 0);
							//cvPutText(imgStacked, bufDiff, cvPoint((pt1.x+pt2.x)/2 -20,(pt1.y+pt2.y)/2), &font, cvScalar(255,255,0));
							sprintf(buf,"_%u", depth1);
							cvPutText(imgStacked, buf, cvPoint(pt1.x, pt1.y), &font, cvScalar(255,255,0));
							sprintf(buf,"_%u", depth2);
							cvPutText(imgStacked, buf, cvPoint(pt2.x, pt2.y), &font, cvScalar(255,255,0));
						}
					}
				}
			}
		}
		if (neighbourFeatures != NULL)
			free(neighbourFeatures);
	}
	fflush(stdout);
	
	// free memory
	kdtree_release(kdRoot);
	
	int ratio=0;
	if (nbInitialMatches!=0)
		ratio = nbValidMatches*100/nbInitialMatches;
	sprintf(buf,"Matches:%d/%d (%d%%)", nbValidMatches, nbInitialMatches, ratio);

	if (imgStacked != NULL)
	{
		cvPutText(imgStacked, buf, cvPoint(5, 950), &font, cvScalar(255,255,0));
		if (forLoopClosure)
			sprintf(buf, "%s/loopc_%d_%d.bmp", Config::_ResultDirectory.c_str(), frameData1.getFrameID(), frameData2.getFrameID());
		else
			sprintf(buf, "%s/sift_%d_%d.bmp", Config::_ResultDirectory.c_str(), frameData1.getFrameID(), frameData2.getFrameID());

		// save stacked image
		cvSaveImage(buf, imgStacked);
		cvReleaseImage(&imgStacked);
	}

	tm.stop();
	printf("\tMatches: %d/%d (%d%%).\t(%dms)\n", nbValidMatches, nbInitialMatches, ratio, tm.duration() );
	fflush(stdout);
}

// -----------------------------------------------------------------------------------------------------
//  computeTransformation
// -----------------------------------------------------------------------------------------------------
bool computeTransformation(
		int frameID1,
		int frameID2,
		FrameData &frameData1,
		FrameData &frameData2,
		Transformation &resultingTransform,
		bool forLoopClosure)
{
	Timer tm;

	vector<int> indexMatches;
	vector<Eigen::Vector3f>	matchesOrig;
	vector<Eigen::Vector3f>	matchesDest;
	bool validTransform = false;

	int nbValidMatches = indexMatches.size();

	resultingTransform._matrix = Eigen::Matrix4f::Identity();
	resultingTransform._error = 1.0;
	resultingTransform._idOrig = frameID1;
	resultingTransform._idDest = frameID2;
	resultingTransform._ratioInliers = 0;

	// ---------------------------------------------------------------------------
	// feature extraction
	// ---------------------------------------------------------------------------
	tm.start();
	printf("Frames %03d-%03d:\t Extracting SIFT features... ", frameID1, frameID2);
	fflush(stdout);

	// load data Frame1
	if (! frameData1.isLoaded(frameID1))
	{
		if (!frameData1.loadImage(frameID1))
			return false;
		if (!frameData1.loadDepthData())
			return false;

		frameData1.computeFeatures();
		frameData1.drawFeatures();
	}

	// load data Frame2
	if (! frameData2.isLoaded(frameID2))
	{
		if (!frameData2.loadImage(frameID2))
			return false;
		if (!frameData2.loadDepthData())
			return false;

		frameData2.computeFeatures();
		frameData2.drawFeatures();
	}

	tm.stop();
	printf("\t%d + %d features.\t(%dms)\n", frameData1.getNbFeatures(), frameData2.getNbFeatures(), tm.duration());
	fflush(stdout);

	if (frameData1.getNbFeatures()==0)
		return false;
	if (frameData2.getNbFeatures()==0)
		return false;

	// ---------------------------------------------------------------------------
	// feature matching through kd-tree search
	// ---------------------------------------------------------------------------
	kdSearchFeatureMatches(
		frameData1,
		frameData2,
		indexMatches,
		matchesOrig,
		matchesDest,
		forLoopClosure);

	// ---------------------------------------------------------------------------
	//  find transformation through RANSAC iterations 
	// ---------------------------------------------------------------------------
	if (indexMatches.size()>3)
	{
		validTransform = findTransformRANSAC(
				frameData1,
				frameData2,
				indexMatches,
				matchesOrig,
				matchesDest,
				resultingTransform,
				forLoopClosure);
	}

	return validTransform;
}

// -----------------------------------------------------------------------------------------------------
//  checkLoopClosure
// -----------------------------------------------------------------------------------------------------
bool checkLoopClosure(
		int frameID1,
		int frameID2,
		FrameData &frameData1,
		FrameData &frameData2,
		Transformation &resultingTransform)
{
	return computeTransformation(
			 frameID1,
			 frameID2,
			 frameData1,
			 frameData2,
			 resultingTransform,
			 true);
}
