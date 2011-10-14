#include "Config.h"
#include "ConfigFile.h"

std::string Config::_DataDirectory = "data_in";
std::string Config::_GenDirectory = "data_gen";
std::string Config::_ResultDirectory = "data_out";
std::string	Config::_PathKinectXmlFile = "SamplesConfig.xml";
int			Config::_DataInRatioFrame = 1;
bool		Config::_SaveImageInitialPairs = false;

int			Config::_FeatureType = 0;
int			Config::_FeatureDepthMin = 0;
int			Config::_FeatureDepthMax = 6000;
bool		Config::_FeatureDisplay = false;

float		Config::_MatchingDistanceRatioNN = 0.5;
float		Config::_MatchingMaxDistanceKeypoint = 3.0;
bool		Config::_MatchingAllowInvalid = false;
int			Config::_MatchingNbIterations = 20;
int			Config::_MatchingMinNbInlier = 10;
float		Config::_MatchingMinRatioInlier = 0.3;
float		Config::_MatchingMaxDistanceInlier = 0.05;
bool		Config::_MatchingRunICP = false;

float		Config::_MapNodeDistance = 0.1;
float		Config::_MapNodeAngle = 5.0;

float		Config::_LoopClosureDistance = 6.0;
float		Config::_LoopClosureAngle = 300.0;
int			Config::_LoopClosureWindowSize = 5;

bool		Config::_PcdGenerateInitial = true;
bool		Config::_PcdGenerateOptimized = true;
int			Config::_PcdRatioKeepSubsample = 80;
int			Config::_PcdMaxNbPoints = 2E6;
int			Config::_PcdRatioFrame = 1;

void Config::LoadConfig(std::string filename)
{
	ConfigFile config(filename);

	Config::_DataDirectory = config.read<string>("DirDataIn", "data_in");
	Config::_GenDirectory = config.read<string>("DirDataGen", "data_gen");
	Config::_ResultDirectory = config.read<string>("DirDataOut", "data_out");
	Config::_PathKinectXmlFile = config.read<string>("PathKinectXmlFile", "SamplesConfig.xml");
	Config::_DataInRatioFrame = config.read<int>("DataInRatioFrame", 0);
	Config::_SaveImageInitialPairs = config.read<bool>("SaveImageInitialPairs", false);

	Config::_FeatureType = config.read<int>("FeatureType", 0);
	Config::_FeatureDepthMin = config.read<int>("FeatureDepthMin", 0);
	Config::_FeatureDepthMax = config.read<int>("FeatureDepthMax", 6000);
	Config::_FeatureDisplay = config.read<bool>("FeatureDisplay", false);

	Config::_MatchingDistanceRatioNN = config.read<float>("MatchingDistanceRatioNN", 0.5);
	Config::_MatchingMaxDistanceKeypoint = config.read<float>("MatchingMaxDistanceKeypoint", 3.0);
	Config::_MatchingAllowInvalid = config.read<bool>("MatchingAllowInvalid", false);
	Config::_MatchingNbIterations = config.read<int>("MatchingNbIterations", 20);
	Config::_MatchingMinNbInlier = config.read<int>("MatchingMinNbInlier", 10);
	Config::_MatchingMinRatioInlier = config.read<float>("MatchingMinRatioInlier", 0.3);
	Config::_MatchingMaxDistanceInlier = config.read<float>("MatchingMaxDistanceInlier", 0.05);
	Config::_MatchingRunICP = config.read<bool>("MatchingRunICP",false);

	Config::_MapNodeDistance = config.read<float>("MapNodeDistance", 0.1);
	Config::_MapNodeAngle = config.read<float>("MapNodeAngle", 5.0);

	Config::_LoopClosureDistance = config.read<float>("LoopClosureDistance", 6.0);
	Config::_LoopClosureAngle = config.read<float>("LoopClosureAngle", 300.0);
	Config::_LoopClosureWindowSize = config.read<int>("LoopClosureWindowSize", 5);

	Config::_PcdGenerateInitial = config.read<bool>("PcdGenerateInitial", true);
	Config::_PcdGenerateOptimized = config.read<bool>("PcdGenerateOptimized", true);
	Config::_PcdRatioKeepSubsample = config.read<int>("PcdRatioKeepSubsample", 80);
	Config::_PcdMaxNbPoints = config.read<int>("PcdMaxNbPoints", 2E6);
	Config::_PcdRatioFrame = config.read<int>("PcdRatioFrame", 1);
}
