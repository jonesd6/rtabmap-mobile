/*
Copyright (c) 2010-2016, Mathieu Labbe - IntRoLab - Universite de Sherbrooke
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
    * Neither the name of the Universite de Sherbrooke nor the
      names of its contributors may be used to endorse or promote products
      derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include <tango-gl/conversions.h>

#include "RTABMapApp.h"

#include <rtabmap/core/Rtabmap.h>
#include <rtabmap/core/util2d.h>
#include <rtabmap/core/util3d.h>
#include <rtabmap/core/util3d_transforms.h>
#include <rtabmap/core/util3d_filtering.h>
#include <rtabmap/core/util3d_surface.h>
#include <rtabmap/core/Graph.h>
#include <rtabmap/utilite/UEventsManager.h>
#include <rtabmap/utilite/UStl.h>
#include <rtabmap/utilite/UDirectory.h>
#include <rtabmap/utilite/UFile.h>
#include <opencv2/opencv_modules.hpp>
#include <rtabmap/core/util3d_surface.h>
#include <rtabmap/utilite/UConversion.h>
#include <rtabmap/utilite/UTimer.h>
#include <rtabmap/core/ParamEvent.h>
#include <rtabmap/core/Compression.h>
#include <rtabmap/core/Optimizer.h>
#include <rtabmap/core/VWDictionary.h>
#include <rtabmap/core/Memory.h>
#include <rtabmap/core/GainCompensator.h>
#include <rtabmap/core/DBDriver.h>
#include <pcl/common/common.h>
#include <pcl/filters/extract_indices.h>
#include <pcl/io/ply_io.h>
#include <pcl/io/obj_io.h>
#include <pcl/surface/poisson.h>
#include <pcl/surface/vtk_smoothing/vtk_mesh_quadric_decimation.h>

#define LOW_RES_PIX 2
//#define DEBUG_RENDERING_PERFORMANCE;

const int g_optMeshId = -100;

static JavaVM *jvm;
static jobject RTABMapActivity = 0;

namespace {
constexpr int kTangoCoreMinimumVersion = 9377;
}  // anonymous namespace.

rtabmap::ParametersMap RTABMapApp::getRtabmapParameters()
{
	rtabmap::ParametersMap parameters;

	parameters.insert(mappingParameters_.begin(), mappingParameters_.end());

	parameters.insert(rtabmap::ParametersPair(rtabmap::Parameters::kKpMaxFeatures(), std::string("200")));
	parameters.insert(rtabmap::ParametersPair(rtabmap::Parameters::kGFTTQualityLevel(), std::string("0.0001")));
	parameters.insert(rtabmap::ParametersPair(rtabmap::Parameters::kMemImagePreDecimation(), std::string(cameraColor_&&fullResolution_?"2":"1")));
	parameters.insert(rtabmap::ParametersPair(rtabmap::Parameters::kBRIEFBytes(), std::string("64")));
	parameters.insert(rtabmap::ParametersPair(rtabmap::Parameters::kRtabmapTimeThr(), std::string("800")));
	parameters.insert(rtabmap::ParametersPair(rtabmap::Parameters::kRtabmapPublishLikelihood(), std::string("false")));
	parameters.insert(rtabmap::ParametersPair(rtabmap::Parameters::kRtabmapPublishPdf(), std::string("false")));
	parameters.insert(rtabmap::ParametersPair(rtabmap::Parameters::kRtabmapStartNewMapOnLoopClosure(), uBool2Str(appendMode_)));
	parameters.insert(rtabmap::ParametersPair(rtabmap::Parameters::kMemBinDataKept(), uBool2Str(!trajectoryMode_)));
	parameters.insert(rtabmap::ParametersPair(rtabmap::Parameters::kOptimizerIterations(), graphOptimization_?"10":"0"));
	parameters.insert(rtabmap::ParametersPair(rtabmap::Parameters::kMemIncrementalMemory(), uBool2Str(!localizationMode_)));
	parameters.insert(rtabmap::ParametersPair(rtabmap::Parameters::kRtabmapMaxRetrieved(), "1"));
	parameters.insert(rtabmap::ParametersPair(rtabmap::Parameters::kRGBDMaxLocalRetrieved(), "0"));
	parameters.insert(rtabmap::ParametersPair(rtabmap::Parameters::kMemCompressionParallelized(), std::string("false")));
	parameters.insert(rtabmap::ParametersPair(rtabmap::Parameters::kKpParallelized(), std::string("false")));
	parameters.insert(rtabmap::ParametersPair(rtabmap::Parameters::kKpMaxDepth(), std::string("10"))); // to avoid extracting features in invalid depth (as we compute transformation directly from the words)
	parameters.insert(rtabmap::ParametersPair(rtabmap::Parameters::kRGBDOptimizeFromGraphEnd(), std::string("true")));
	parameters.insert(rtabmap::ParametersPair(rtabmap::Parameters::kVisMinInliers(), std::string("25")));
	parameters.insert(rtabmap::ParametersPair(rtabmap::Parameters::kVisEstimationType(), std::string("0"))); // 0=3D-3D 1=PnP
	parameters.insert(rtabmap::ParametersPair(rtabmap::Parameters::kRGBDOptimizeMaxError(), std::string("3")));
	parameters.insert(rtabmap::ParametersPair(rtabmap::Parameters::kRGBDProximityPathMaxNeighbors(), std::string("0"))); // disable scan matching to merged nodes
	parameters.insert(rtabmap::ParametersPair(rtabmap::Parameters::kRGBDProximityBySpace(), std::string("false"))); // just keep loop closure detection
	parameters.insert(rtabmap::ParametersPair(rtabmap::Parameters::kRGBDLinearUpdate(), std::string("0.05")));
	parameters.insert(rtabmap::ParametersPair(rtabmap::Parameters::kRGBDAngularUpdate(), std::string("0.05")));
	parameters.insert(rtabmap::ParametersPair(rtabmap::Parameters::kMarkerLength(), std::string("0.0")));

	parameters.insert(rtabmap::ParametersPair(rtabmap::Parameters::kMemUseOdomGravity(), "true"));
	if(parameters.find(rtabmap::Parameters::kOptimizerStrategy()) != parameters.end())
	{
		if(parameters.at(rtabmap::Parameters::kOptimizerStrategy()).compare("2") == 0) // GTSAM
		{
			parameters.insert(rtabmap::ParametersPair(rtabmap::Parameters::kOptimizerEpsilon(), "0.00001"));
			parameters.insert(rtabmap::ParametersPair(rtabmap::Parameters::kOptimizerIterations(), graphOptimization_?"10":"0"));
			parameters.insert(rtabmap::ParametersPair(rtabmap::Parameters::kOptimizerGravitySigma(), "0.2"));
		}
		else if(parameters.at(rtabmap::Parameters::kOptimizerStrategy()).compare("1") == 0) // g2o
		{
			parameters.insert(rtabmap::ParametersPair(rtabmap::Parameters::kOptimizerEpsilon(), "0.0"));
			parameters.insert(rtabmap::ParametersPair(rtabmap::Parameters::kOptimizerIterations(), graphOptimization_?"10":"0"));
			parameters.insert(rtabmap::ParametersPair(rtabmap::Parameters::kOptimizerGravitySigma(), "0"));
		}
		else // TORO
		{
			parameters.insert(rtabmap::ParametersPair(rtabmap::Parameters::kOptimizerEpsilon(), "0.00001"));
			parameters.insert(rtabmap::ParametersPair(rtabmap::Parameters::kOptimizerIterations(), graphOptimization_?"100":"0"));
			parameters.insert(rtabmap::ParametersPair(rtabmap::Parameters::kOptimizerGravitySigma(), "0"));
		}
	}

	parameters.insert(rtabmap::ParametersPair(rtabmap::Parameters::kIcpPointToPlane(), std::string("true")));
	parameters.insert(rtabmap::ParametersPair(rtabmap::Parameters::kMemLaserScanNormalK(), std::string("0")));
	parameters.insert(rtabmap::ParametersPair(rtabmap::Parameters::kIcpIterations(), std::string("10")));
	parameters.insert(rtabmap::ParametersPair(rtabmap::Parameters::kIcpEpsilon(), std::string("0.001")));
	parameters.insert(rtabmap::ParametersPair(rtabmap::Parameters::kIcpMaxRotation(), std::string("0.17"))); // 10 degrees
	parameters.insert(rtabmap::ParametersPair(rtabmap::Parameters::kIcpMaxTranslation(), std::string("0.05")));
	parameters.insert(rtabmap::ParametersPair(rtabmap::Parameters::kIcpCorrespondenceRatio(), std::string("0.49")));
	parameters.insert(rtabmap::ParametersPair(rtabmap::Parameters::kIcpMaxCorrespondenceDistance(), std::string("0.05")));

	parameters.insert(*rtabmap::Parameters::getDefaultParameters().find(rtabmap::Parameters::kKpMaxFeatures()));
	parameters.insert(*rtabmap::Parameters::getDefaultParameters().find(rtabmap::Parameters::kMemRehearsalSimilarity()));
	parameters.insert(*rtabmap::Parameters::getDefaultParameters().find(rtabmap::Parameters::kMemMapLabelsAdded()));
	if(dataRecorderMode_)
	{
		uInsert(parameters, rtabmap::ParametersPair(rtabmap::Parameters::kKpMaxFeatures(), std::string("-1")));
		uInsert(parameters, rtabmap::ParametersPair(rtabmap::Parameters::kMemRehearsalSimilarity(), std::string("1.0"))); // deactivate rehearsal
		uInsert(parameters, rtabmap::ParametersPair(rtabmap::Parameters::kMemMapLabelsAdded(), "false")); // don't create map labels
		uInsert(parameters, rtabmap::ParametersPair(rtabmap::Parameters::kMemNotLinkedNodesKept(), std::string("true")));
	}

	return parameters;
}

RTABMapApp::RTABMapApp() :
		camera_(0),
		rtabmapThread_(0),
		rtabmap_(0),
		logHandler_(0),
		odomCloudShown_(true),
		graphOptimization_(true),
		nodesFiltering_(false),
		localizationMode_(false),
		trajectoryMode_(false),
		rawScanSaved_(false),
		smoothing_(true),
		cameraColor_(true),
		fullResolution_(false),
		appendMode_(true),
		maxCloudDepth_(0.0),
		minCloudDepth_(0.0),
		cloudDensityLevel_(1),
		meshTrianglePix_(1),
		meshAngleToleranceDeg_(15.0),
		clusterRatio_(0.1),
		maxGainRadius_(0.02f),
		renderingTextureDecimation_(4),
		backgroundColor_(0.2f),
		paused_(false),
		dataRecorderMode_(false),
		clearSceneOnNextRender_(false),
		openingDatabase_(false),
		exporting_(false),
		postProcessing_(false),
		filterPolygonsOnNextRender_(false),
		gainCompensationOnNextRender_(0),
		bilateralFilteringOnNextRender_(false),
		takeScreenshotOnNextRender_(false),
		cameraJustInitialized_(false),
		meshDecimation_(1),
		totalPoints_(0),
		totalPolygons_(0),
		lastDrawnCloudsCount_(0),
		renderingTime_(0.0f),
		lastPostRenderEventTime_(0.0),
		lastPoseEventTime_(0.0),
		visualizingMesh_(false),
		exportedMeshUpdated_(false),
		optMesh_(new pcl::TextureMesh),
		optRefId_(0),
		optRefPose_(0),
		mapToOdom_(rtabmap::Transform::getIdentity())

{
	mappingParameters_.insert(rtabmap::ParametersPair(rtabmap::Parameters::kKpDetectorStrategy(), "5")); // GFTT/FREAK
}

RTABMapApp::~RTABMapApp() {
	if(camera_)
	{
		delete camera_;
	}
	if(rtabmapThread_)
	{
		rtabmapThread_->close(false);
		delete rtabmapThread_;
	}
	if(logHandler_)
	{
		delete logHandler_;
	}
	if(optRefPose_)
	{
		delete optRefPose_;
	}
	{
		boost::mutex::scoped_lock  lock(rtabmapMutex_);
		if(rtabmapEvents_.size())
		{
			for(std::list<rtabmap::RtabmapEvent*>::iterator iter=rtabmapEvents_.begin(); iter!=rtabmapEvents_.end(); ++iter)
			{
				delete *iter;
			}
		}
		rtabmapEvents_.clear();
	}
	{
		boost::mutex::scoped_lock  lock(visLocalizationMutex_);
		if(visLocalizationEvents_.size())
		{
			for(std::list<rtabmap::RtabmapEvent*>::iterator iter=visLocalizationEvents_.begin(); iter!=visLocalizationEvents_.end(); ++iter)
			{
				delete *iter;
			}
		}
		visLocalizationEvents_.clear();
	}
}

void RTABMapApp::onCreate(JNIEnv* env, jobject caller_activity)
{
	env->GetJavaVM(&jvm);
	RTABMapActivity = env->NewGlobalRef(caller_activity);

	LOGI("RTABMapApp::onCreate()");
	createdMeshes_.clear();
	rawPoses_.clear();
	clearSceneOnNextRender_ = true;
	openingDatabase_ = false;
	exporting_ = false;
	postProcessing_=false;
	totalPoints_ = 0;
	totalPolygons_ = 0;
	lastDrawnCloudsCount_ = 0;
	renderingTime_ = 0.0f;
	lastPostRenderEventTime_ = 0.0;
	lastPoseEventTime_ = 0.0;
	bufferedStatsData_.clear();
	progressionStatus_.setJavaObjects(jvm, RTABMapActivity);
	main_scene_.setBackgroundColor(backgroundColor_, backgroundColor_, backgroundColor_);

	if(camera_)
	{
	  delete camera_;
	  camera_ = 0;
	}
	if(rtabmapThread_)
	{
		rtabmapThread_->close(false);
	    delete rtabmapThread_;
	    rtabmapThread_ = 0;
	    rtabmap_ = 0;
	}

	if(logHandler_ == 0)
	{
		logHandler_ = new LogHandler();
	}

	this->registerToEventsManager();

	camera_ = new rtabmap::CameraTango(cameraColor_, !cameraColor_ || fullResolution_?1:2, rawScanSaved_, smoothing_);
}

void RTABMapApp::setScreenRotation(int displayRotation, int cameraRotation)
{
	TangoSupportRotation rotation = tango_gl::util::GetAndroidRotationFromColorCameraToDisplay(displayRotation, cameraRotation);
	LOGI("Set orientation: display=%d camera=%d -> %d", displayRotation, cameraRotation, (int)rotation);
	main_scene_.setScreenRotation(rotation);
	camera_->setScreenRotation(rotation);
}

int RTABMapApp::openDatabase(const std::string & databasePath, bool databaseInMemory, bool optimize, const std::string & databaseSource)
{
	LOGI("Opening database %s (inMemory=%d, optimize=%d)", databasePath.c_str(), databaseInMemory?1:0, optimize?1:0);
	this->unregisterFromEventsManager(); // to ignore published init events when closing rtabmap
	status_.first = rtabmap::RtabmapEventInit::kInitializing;
	rtabmapMutex_.lock();
	if(rtabmapEvents_.size())
	{
		for(std::list<rtabmap::RtabmapEvent*>::iterator iter=rtabmapEvents_.begin(); iter!=rtabmapEvents_.end(); ++iter)
		{
			delete *iter;
		}
	}
	rtabmapEvents_.clear();
	openingDatabase_ = true;
	if(rtabmapThread_)
	{
		rtabmapThread_->close(false);
		delete rtabmapThread_;
		rtabmapThread_ = 0;
		rtabmap_ = 0;
	}

	this->registerToEventsManager();

	int status = 0;

	// Open visualization while we load (if there is an optimized mesh saved in database)
	optMesh_.reset(new pcl::TextureMesh);
	optTexture_ = cv::Mat();
	optRefId_ = 0;
	if(optRefPose_)
	{
		delete optRefPose_;
		optRefPose_ = 0;
	}
	cv::Mat cloudMat;
	std::vector<std::vector<std::vector<unsigned int> > > polygons;
#if PCL_VERSION_COMPARE(>=, 1, 8, 0)
	std::vector<std::vector<Eigen::Vector2f, Eigen::aligned_allocator<Eigen::Vector2f> > > texCoords;
#else
	std::vector<std::vector<Eigen::Vector2f> > texCoords;
#endif
	cv::Mat textures;
	if(!databaseSource.empty())
	{
		UEventsManager::post(new rtabmap::RtabmapEventInit(rtabmap::RtabmapEventInit::kInfo, "Loading optimized cloud/mesh..."));
		rtabmap::DBDriver * driver = rtabmap::DBDriver::create();
		if(driver->openConnection(databaseSource))
		{
			cloudMat = driver->loadOptimizedMesh(&polygons, &texCoords, &textures);
			if(!cloudMat.empty())
			{
				LOGI("Open: Found optimized mesh! Visualizing it.");
				optMesh_ = rtabmap::util3d::assembleTextureMesh(cloudMat, polygons, texCoords, textures, true);
				optTexture_ = textures;
				if(!optTexture_.empty())
				{
					LOGI("Open: Texture mesh: %dx%d.", optTexture_.cols, optTexture_.rows);
					status=3;
				}
				else if(optMesh_->tex_polygons.size())
				{
					LOGI("Open: Polygon mesh");
					status=2;
				}
				else if(!optMesh_->cloud.data.empty())
				{
					LOGI("Open: Point cloud");
					status=1;
				}
			}
			else
			{
				LOGI("Open: No optimized mesh found.");
			}
			delete driver;
		}
	}

	if(status > 0)
	{
		if(status==1)
		{
			UEventsManager::post(new rtabmap::RtabmapEventInit(rtabmap::RtabmapEventInit::kInfo, "Loading optimized cloud...done!"));
		}
		else if(status==2)
		{
			UEventsManager::post(new rtabmap::RtabmapEventInit(rtabmap::RtabmapEventInit::kInfo, "Loading optimized mesh...done!"));
		}
		else
		{
			UEventsManager::post(new rtabmap::RtabmapEventInit(rtabmap::RtabmapEventInit::kInfo, "Loading optimized texture mesh...done!"));
		}
		boost::mutex::scoped_lock  lockRender(renderingMutex_);
		visualizingMesh_ = true;
		exportedMeshUpdated_ = true;
	}

	UEventsManager::post(new rtabmap::RtabmapEventInit(rtabmap::RtabmapEventInit::kInfo, "Loading database..."));
	LOGI("Erasing database \"%s\"...", databasePath.c_str());
	UFile::erase(databasePath);
	if(!databaseSource.empty())
	{
		LOGI("Copying database source \"%s\" to \"%s\"...", databaseSource.c_str(), databasePath.c_str());
		UFile::copy(databaseSource, databasePath);
	}

	//Rtabmap
	mapToOdom_.setIdentity();
	rtabmap_ = new rtabmap::Rtabmap();
	rtabmap::ParametersMap parameters = getRtabmapParameters();

	parameters.insert(rtabmap::ParametersPair(rtabmap::Parameters::kDbSqlite3InMemory(), uBool2Str(databaseInMemory)));
	LOGI("Initializing database...");
	rtabmap_->init(parameters, databasePath);
	rtabmapThread_ = new rtabmap::RtabmapThread(rtabmap_);
	if(parameters.find(rtabmap::Parameters::kRtabmapDetectionRate()) != parameters.end())
	{
		rtabmapThread_->setDetectorRate(uStr2Float(parameters.at(rtabmap::Parameters::kRtabmapDetectionRate())));
	}

	// Generate all meshes
	std::map<int, rtabmap::Signature> signatures;
	std::map<int, rtabmap::Transform> poses;
	std::multimap<int, rtabmap::Link> links;
	LOGI("Loading full map from database...");
	UEventsManager::post(new rtabmap::RtabmapEventInit(rtabmap::RtabmapEventInit::kInfo, "Loading data from database..."));
	rtabmap_->get3DMap(
			signatures,
			poses,
			links,
			true,
			true);

	if(signatures.size() && poses.empty())
	{
		LOGE("Failed to optimize the graph!");
		status = -1;
	}

	{
		LOGI("Creating the meshes (%d)....", (int)poses.size());
		boost::mutex::scoped_lock  lock(meshesMutex_);
		createdMeshes_.clear();
		int i=0;
		UTimer addTime;
		for(std::map<int, rtabmap::Transform>::iterator iter=poses.begin(); iter!=poses.end() && status>=0; ++iter)
		{
			try
			{
				int id = iter->first;
				if(!iter->second.isNull())
				{
					if(uContains(signatures, id))
					{
						UTimer timer;
						rtabmap::SensorData data = signatures.at(id).sensorData();

						cv::Mat tmpA, depth;
						data.uncompressData(&tmpA, &depth);
						if(!data.imageRaw().empty() && !data.depthRaw().empty())
						{
							// Voxelize and filter depending on the previous cloud?
							pcl::PointCloud<pcl::PointXYZRGB>::Ptr cloud;
							pcl::IndicesPtr indices(new std::vector<int>);
							cloud = rtabmap::util3d::cloudRGBFromSensorData(data, meshDecimation_, maxCloudDepth_, minCloudDepth_, indices.get());
							if(cloud->size() && indices->size())
							{
								std::vector<pcl::Vertices> polygons;
								std::vector<pcl::Vertices> polygonsLowRes;
								if(main_scene_.isMeshRendering() && main_scene_.isMapRendering())
								{
									polygons = rtabmap::util3d::organizedFastMesh(cloud, meshAngleToleranceDeg_*M_PI/180.0, false, meshTrianglePix_);
									polygonsLowRes = rtabmap::util3d::organizedFastMesh(cloud, meshAngleToleranceDeg_*M_PI/180.0, false, meshTrianglePix_+LOW_RES_PIX);
								}

								if((main_scene_.isMeshRendering() && polygons.size()) || !main_scene_.isMeshRendering() || !main_scene_.isMapRendering())
								{
									std::pair<std::map<int, Mesh>::iterator, bool> inserted = createdMeshes_.insert(std::make_pair(id, Mesh()));
									UASSERT(inserted.second);
									inserted.first->second.cloud = cloud;
									inserted.first->second.indices = indices;
									inserted.first->second.polygons = polygons;
									inserted.first->second.polygonsLowRes = polygonsLowRes;
									inserted.first->second.visible = true;
									inserted.first->second.cameraModel = data.cameraModels()[0];
									inserted.first->second.gains[0] = 1.0;
									inserted.first->second.gains[1] = 1.0;
									inserted.first->second.gains[2] = 1.0;
									if(main_scene_.isMeshTexturing() && main_scene_.isMapRendering())
									{
										if(renderingTextureDecimation_>1)
										{
											cv::Size reducedSize(data.imageRaw().cols/renderingTextureDecimation_, data.imageRaw().rows/renderingTextureDecimation_);
											cv::resize(data.imageRaw(), inserted.first->second.texture, reducedSize, 0, 0, CV_INTER_LINEAR);
										}
										else
										{
											inserted.first->second.texture = data.imageRaw();
										}
									}
									LOGI("Created cloud %d (%fs)", id, timer.ticks());
								}
								else
								{
									LOGI("Cloud %d not added to created meshes", id);
								}
							}
							else
							{
								UWARN("Cloud %d is empty", id);
							}
						}
						else
						{
							UERROR("Failed to uncompress data!");
							status=-2;
						}
					}
					else
					{
						UWARN("Data for node %d not found", id);
					}
				}
				else
				{
					UWARN("Pose %d is null !?", id);
				}
				++i;
				if(addTime.elapsed() >= 4.0f)
				{
					UEventsManager::post(new rtabmap::RtabmapEventInit(rtabmap::RtabmapEventInit::kInfo, uFormat("Created clouds %d/%d", i, (int)poses.size())));
					addTime.restart();
				}
			}
			catch(const UException & e)
			{
				UERROR("Exception! msg=\"%s\"", e.what());
				status = -2;
			}
			catch (const cv::Exception & e)
			{
				UERROR("Exception! msg=\"%s\"", e.what());
				status = -2;
			}
			catch (const std::exception & e)
			{
				UERROR("Exception! msg=\"%s\"", e.what());
				status = -2;
			}
		}
		if(status < 0)
		{
			createdMeshes_.clear();
		}
		else
		{
			LOGI("Created %d meshes...", (int)createdMeshes_.size());
		}
	}



	if(optimize && status>=0)
	{
		UEventsManager::post(new rtabmap::RtabmapEventInit(rtabmap::RtabmapEventInit::kInfo, "Visual optimization..."));
		gainCompensation();

		LOGI("Polygon filtering...");
		boost::mutex::scoped_lock  lock(meshesMutex_);
		UTimer time;
		for(std::map<int, Mesh>::iterator iter = createdMeshes_.begin(); iter!=createdMeshes_.end(); ++iter)
		{
			if(iter->second.polygons.size())
			{
				// filter polygons
				iter->second.polygons = filterOrganizedPolygons(iter->second.polygons, iter->second.cloud->size());
			}
		}
	}

	UEventsManager::post(new rtabmap::RtabmapEventInit(rtabmap::RtabmapEventInit::kInfo, "Updating scene..."));
	LOGI("Open: add rtabmap event to update the scene");
	rtabmap::Statistics stats;
	stats.addStatistic(rtabmap::Statistics::kMemoryWorking_memory_size(), (float)rtabmap_->getWMSize());
	stats.addStatistic(rtabmap::Statistics::kKeypointDictionary_size(), (float)rtabmap_->getMemory()->getVWDictionary()->getVisualWords().size());
	stats.addStatistic(rtabmap::Statistics::kMemoryDatabase_memory_used(), (float)rtabmap_->getMemory()->getDatabaseMemoryUsed());
	stats.setPoses(poses);
	stats.setConstraints(links);
	rtabmapEvents_.push_back(new rtabmap::RtabmapEvent(stats));

	rtabmap_->setOptimizedPoses(poses);

	// for optimized mesh
	if(poses.size())
	{
		// just take the last as reference
		optRefId_ = poses.rbegin()->first;
		optRefPose_ = new rtabmap::Transform(poses.rbegin()->second);
	}

	if(camera_)
	{
		camera_->resetOrigin();
	}

	// Start threads
	LOGI("Start rtabmap thread");
	rtabmapThread_->registerToEventsManager();
	rtabmapThread_->start();

	UEventsManager::post(new rtabmap::RtabmapEventInit(rtabmap::RtabmapEventInit::kInitialized, ""));

	status_.first = rtabmap::RtabmapEventInit::kInitialized;
	status_.second = "";
	rtabmapMutex_.unlock();

	boost::mutex::scoped_lock  lockRender(renderingMutex_);
	if(poses.empty() || status>0)
	{
		openingDatabase_ = false;
	}

	clearSceneOnNextRender_ = status<=0;

	return status;
}

bool RTABMapApp::onTangoServiceConnected(JNIEnv* env, jobject iBinder)
{
	LOGW("onTangoServiceConnected()");
	if(camera_)
	{
		camera_->join(true);

		if (TangoService_setBinder(env, iBinder) != TANGO_SUCCESS) {
		    UERROR("TangoHandler::ConnectTango, TangoService_setBinder error");
		    return false;
		}

		camera_->setColorCamera(cameraColor_);
		if(camera_->init())
		{
			//update mesh decimation based on camera calibration
			LOGI("Cloud density level %d", cloudDensityLevel_);
			meshDecimation_ = 1;
			if(camera_)
			{
				// Google Tango Tablet 160x90
				// Phab2Pro 240x135
				// FishEye 640x480
				int width = camera_->getCameraModel().imageWidth()/(cameraColor_?8:1);
				int height = camera_->getCameraModel().imageHeight()/(cameraColor_?8:1);
				if(cloudDensityLevel_ == 3) // high
				{
					if(height >= 480 && width % 20 == 0 && height % 20 == 0)
					{
						meshDecimation_ = 20;
					}
					else if(width % 10 == 0 && height % 10 == 0)
					{
						meshDecimation_ = 10;
					}
					else if(width % 15 == 0 && height % 15 == 0)
					{
						meshDecimation_ = 15;
					}
					else
					{
						UERROR("Could not set decimation to high (size=%dx%d)", width, height);
					}
				}
				else if(cloudDensityLevel_ == 2) // medium
				{
					if(height >= 480 && width % 10 == 0 && height % 10 == 0)
					{
						meshDecimation_ = 10;
					}
					else if(width % 5 == 0 && height % 5 == 0)
					{
						meshDecimation_ = 5;
					}
					else
					{
						UERROR("Could not set decimation to medium (size=%dx%d)", width, height);
					}
				}
				else if(cloudDensityLevel_ == 1) // low
				{
					if(height >= 480 && width % 5 == 0 && height % 5 == 0)
					{
						meshDecimation_ = 5;
					}
					else if(width % 3 == 0 && width % 3 == 0)
					{
						meshDecimation_ = 3;
					}
					else if(width % 2 == 0 && width % 2 == 0)
					{
						meshDecimation_ = 2;
					}
					else
					{
						UERROR("Could not set decimation to low (size=%dx%d)", width, height);
					}
				}
			}
			LOGI("Set decimation to %d", meshDecimation_);

			LOGI("Start camera thread");
			if(!paused_)
			{
				camera_->start();
			}
			cameraJustInitialized_ = true;
			return true;
		}
		UERROR("Failed camera initialization!");
	}
	return false;
}

void RTABMapApp::onPause()
{
	LOGI("onPause()");
	if(camera_)
	{
		camera_->join(true);
		camera_->close();
	}
}


void RTABMapApp::TangoResetMotionTracking() {
  TangoService_resetMotionTracking();
}

std::vector<pcl::Vertices> RTABMapApp::filterOrganizedPolygons(
		const std::vector<pcl::Vertices> & polygons,
		int cloudSize) const
{
	std::vector<int> vertexToCluster(cloudSize, 0);
	std::map<int, std::list<int> > clusters;
	int lastClusterID = 0;

	for(unsigned int i=0; i<polygons.size(); ++i)
	{
		int clusterID = 0;
		for(unsigned int j=0;j<polygons[i].vertices.size(); ++j)
		{
			if(vertexToCluster[polygons[i].vertices[j]]>0)
			{
				clusterID = vertexToCluster[polygons[i].vertices[j]];
				break;
			}
		}
		if(clusterID>0)
		{
			clusters.at(clusterID).push_back(i);
		}
		else
		{
			clusterID = ++lastClusterID;
			std::list<int> polygons;
			polygons.push_back(i);
			clusters.insert(std::make_pair(clusterID, polygons));
		}
		for(unsigned int j=0;j<polygons[i].vertices.size(); ++j)
		{
			vertexToCluster[polygons[i].vertices[j]] = clusterID;
		}
	}

	unsigned int biggestClusterSize = 0;
	for(std::map<int, std::list<int> >::iterator iter=clusters.begin(); iter!=clusters.end(); ++iter)
	{
		LOGD("cluster %d = %d", iter->first, (int)iter->second.size());

		if(iter->second.size() > biggestClusterSize)
		{
			biggestClusterSize = iter->second.size();
		}
	}
	unsigned int minClusterSize = (unsigned int)(float(biggestClusterSize)*clusterRatio_);
	LOGI("Biggest cluster %d -> minClusterSize(ratio=%f)=%d",
			biggestClusterSize, clusterRatio_, (int)minClusterSize);

	std::vector<pcl::Vertices> filteredPolygons(polygons.size());
	int oi = 0;
	for(std::map<int, std::list<int> >::iterator iter=clusters.begin(); iter!=clusters.end(); ++iter)
	{
		if(iter->second.size() >= minClusterSize)
		{
			for(std::list<int>::iterator jter=iter->second.begin(); jter!=iter->second.end(); ++jter)
			{
				filteredPolygons[oi++] = polygons[*jter];
			}
		}
	}
	filteredPolygons.resize(oi);
	return filteredPolygons;
}


std::vector<pcl::Vertices> RTABMapApp::filterPolygons(
		const std::vector<pcl::Vertices> & polygons,
		int cloudSize) const
{
	// filter polygons
	std::vector<std::set<int> > neighbors;
	std::vector<std::set<int> > vertexToPolygons;
	rtabmap::util3d::createPolygonIndexes(
			polygons,
			cloudSize,
			neighbors,
			vertexToPolygons);
	std::list<std::list<int> > clusters = rtabmap::util3d::clusterPolygons(neighbors);

	unsigned int biggestClusterSize = 0;
	for(std::list<std::list<int> >::iterator iter=clusters.begin(); iter!=clusters.end(); ++iter)
	{
		if(iter->size() > biggestClusterSize)
		{
			biggestClusterSize = iter->size();
		}
	}
	unsigned int minClusterSize = (unsigned int)(float(biggestClusterSize)*clusterRatio_);
	LOGI("Biggest cluster = %d -> minClusterSize(ratio=%f)=%d",
			biggestClusterSize, clusterRatio_, (int)minClusterSize);

	std::vector<pcl::Vertices> filteredPolygons(polygons.size());
	int oi=0;
	for(std::list<std::list<int> >::iterator jter=clusters.begin(); jter!=clusters.end(); ++jter)
	{
		if(jter->size() >= minClusterSize)
		{
			for(std::list<int>::iterator kter=jter->begin(); kter!=jter->end(); ++kter)
			{
				filteredPolygons[oi++] = polygons.at(*kter);
			}
		}
	}
	filteredPolygons.resize(oi);
	return filteredPolygons;
}

// OpenGL thread
void RTABMapApp::InitializeGLContent()
{
	UINFO("");
	main_scene_.InitGLContent();

	float v = backgroundColor_ == 0.5f?0.4f:1.0f-backgroundColor_;
	main_scene_.setGridColor(v, v, v);
}

// OpenGL thread
void RTABMapApp::SetViewPort(int width, int height)
{
	UINFO("");
	main_scene_.SetupViewPort(width, height);
}

class PostRenderEvent : public UEvent
{
public:
	PostRenderEvent(rtabmap::RtabmapEvent * event = 0) :
		rtabmapEvent_(event)
	{
	}
	~PostRenderEvent()
	{
		if(rtabmapEvent_!=0)
		{
			delete rtabmapEvent_;
		}
	}
	virtual std::string getClassName() const {return "PostRenderEvent";}
	const rtabmap::RtabmapEvent * getRtabmapEvent() const {return rtabmapEvent_;}
private:
	rtabmap::RtabmapEvent * rtabmapEvent_;
};

// OpenGL thread
bool RTABMapApp::smoothMesh(int id, Mesh & mesh)
{
	UTimer t;
	// reconstruct depth image
	UASSERT(mesh.indices.get() && mesh.indices->size());
	cv::Mat depth = cv::Mat::zeros(mesh.cloud->height, mesh.cloud->width, CV_32FC1);
	rtabmap::Transform localTransformInv = mesh.cameraModel.localTransform().inverse();
	for(unsigned int i=0; i<mesh.indices->size(); ++i)
	{
		int index = mesh.indices->at(i);
		// FastBilateralFilter works in camera frame
		if(mesh.cloud->at(index).x > 0)
		{
			pcl::PointXYZRGB pt = rtabmap::util3d::transformPoint(mesh.cloud->at(index), localTransformInv);
			depth.at<float>(index) = pt.z;
		}
	}

	depth = rtabmap::util2d::fastBilateralFiltering(depth, 2.0f, 0.075f);
	LOGI("smoothMesh() Bilateral filtering of %d, time=%fs", id, t.ticks());

	if(!depth.empty() && mesh.indices->size())
	{
		pcl::IndicesPtr newIndices(new std::vector<int>(mesh.indices->size()));
		int oi = 0;
		for(unsigned int i=0; i<mesh.indices->size(); ++i)
		{
			int index = mesh.indices->at(i);

			 pcl::PointXYZRGB & pt = mesh.cloud->at(index);
			 pcl::PointXYZRGB newPt = rtabmap::util3d::transformPoint(mesh.cloud->at(index), localTransformInv);
			 if(depth.at<float>(index) > 0)
			 {
				 newPt.z = depth.at<float>(index);
				 newPt = rtabmap::util3d::transformPoint(newPt, mesh.cameraModel.localTransform());
				 newIndices->at(oi++) = index;
			 }
			 else
			 {
				 newPt.x = newPt.y = newPt.z = std::numeric_limits<float>::quiet_NaN();
			 }
			 pt.x = newPt.x;
			 pt.y = newPt.y;
			 pt.z = newPt.z;
		}
		newIndices->resize(oi);
		mesh.indices = newIndices;

		//reconstruct the mesh with smoothed surfaces
		std::vector<pcl::Vertices> polygons;
		if(main_scene_.isMeshRendering())
		{
			polygons = rtabmap::util3d::organizedFastMesh(mesh.cloud, meshAngleToleranceDeg_*M_PI/180.0, false, meshTrianglePix_);
		}
		LOGI("smoothMesh() Reconstructing the mesh of %d, time=%fs", id, t.ticks());
		mesh.polygons = polygons;
	}
	else
	{
		UERROR("smoothMesh() Failed to smooth surface %d", id);
		return false;
	}
	return true;
}

void RTABMapApp::gainCompensation(bool full)
{
	UTimer tGainCompensation;
	LOGI("Gain compensation...");
	boost::mutex::scoped_lock  lock(meshesMutex_);

	std::map<int, pcl::PointCloud<pcl::PointXYZRGB>::Ptr > clouds;
	std::map<int, pcl::IndicesPtr> indices;
	for(std::map<int, Mesh>::iterator iter = createdMeshes_.begin(); iter!=createdMeshes_.end(); ++iter)
	{
		clouds.insert(std::make_pair(iter->first, iter->second.cloud));
		indices.insert(std::make_pair(iter->first, iter->second.indices));
	}
	std::map<int, rtabmap::Transform> poses;
	std::multimap<int, rtabmap::Link> links;
	rtabmap_->getGraph(poses, links, true, true);
	if(full)
	{
		// full compensation
		links.clear();
		for(std::map<int, pcl::PointCloud<pcl::PointXYZRGB>::Ptr>::const_iterator iter=clouds.begin(); iter!=clouds.end(); ++iter)
		{
			int from = iter->first;
			std::map<int, pcl::PointCloud<pcl::PointXYZRGB>::Ptr>::const_iterator jter = iter;
			++jter;
			for(;jter!=clouds.end(); ++jter)
			{
				int to = jter->first;
				links.insert(std::make_pair(from, rtabmap::Link(from, to, rtabmap::Link::kUserClosure, poses.at(from).inverse()*poses.at(to))));
			}
		}
	}

	UASSERT(maxGainRadius_>0.0f);
	rtabmap::GainCompensator compensator(maxGainRadius_, 0.0f, 0.01f, 1.0f);
	if(clouds.size() > 1 && links.size())
	{
		compensator.feed(clouds, indices, links);
		LOGI("Gain compensation... compute gain: links=%d, time=%fs", (int)links.size(), tGainCompensation.ticks());
	}

	for(std::map<int, Mesh>::iterator iter = createdMeshes_.begin(); iter!=createdMeshes_.end(); ++iter)
	{
		if(!iter->second.cloud->empty())
		{
			if(clouds.size() > 1 && links.size())
			{
				compensator.getGain(iter->first, &iter->second.gains[0], &iter->second.gains[1], &iter->second.gains[2]);
				LOGI("%d mesh has gain %f,%f,%f", iter->first, iter->second.gains[0], iter->second.gains[1], iter->second.gains[2]);
			}
		}
	}
	LOGI("Gain compensation... applying gain: meshes=%d, time=%fs", (int)createdMeshes_.size(), tGainCompensation.ticks());
}

// OpenGL thread
int RTABMapApp::Render()
{
	std::list<rtabmap::RtabmapEvent*> rtabmapEvents;
	try
	{
		UASSERT(camera_!=0);

		UTimer fpsTime;
#ifdef DEBUG_RENDERING_PERFORMANCE
		UTimer time;
#endif
		boost::mutex::scoped_lock  lock(renderingMutex_);

		bool notifyDataLoaded = false;
		bool notifyCameraStarted = false;

		if(clearSceneOnNextRender_)
		{
			visualizingMesh_ = false;
		}

		// process only pose events in visualization mode
		rtabmap::Transform pose;
		{
			boost::mutex::scoped_lock lock(poseMutex_);
			if(poseEvents_.size())
			{
				pose = poseEvents_.back();
				poseEvents_.clear();
			}
		}
		if(!pose.isNull())
		{
			// update camera pose?
			if(graphOptimization_ && !mapToOdom_.isIdentity())
			{
				main_scene_.SetCameraPose(opengl_world_T_rtabmap_world*mapToOdom_*rtabmap_world_T_tango_world*pose);
			}
			else
			{
				main_scene_.SetCameraPose(opengl_world_T_tango_world*pose);
			}
			if(!camera_->isRunning() && cameraJustInitialized_)
			{
				notifyCameraStarted = true;
				cameraJustInitialized_ = false;
			}
			lastPoseEventTime_ = UTimer::now();
		}

		rtabmap::OdometryEvent odomEvent;
		{
			boost::mutex::scoped_lock  lock(odomMutex_);
			if(odomEvents_.size())
			{
				LOGI("Process odom events");
				odomEvent = odomEvents_.back();
				odomEvents_.clear();
				if(cameraJustInitialized_)
				{
					notifyCameraStarted = true;
					cameraJustInitialized_ = false;
				}
			}
		}

		if(visualizingMesh_)
		{
			if(exportedMeshUpdated_)
			{
				main_scene_.clear();
				exportedMeshUpdated_ = false;
			}
			if(!main_scene_.hasCloud(g_optMeshId))
			{
				LOGI("Adding optimized mesh to opengl (%d points, %d polygons, %d tex_coords, materials=%d texture=%dx%d)...",
						optMesh_->cloud.point_step==0?0:(int)optMesh_->cloud.data.size()/optMesh_->cloud.point_step,
						optMesh_->tex_polygons.size()!=1?0:(int)optMesh_->tex_polygons[0].size(),
						optMesh_->tex_coordinates.size()!=1?0:(int)optMesh_->tex_coordinates[0].size(),
						(int)optMesh_->tex_materials.size(),
						optTexture_.cols, optTexture_.rows);
				if(optMesh_->tex_polygons.size() && optMesh_->tex_polygons[0].size())
				{
					Mesh mesh;
					mesh.gains[0] = mesh.gains[1] = mesh.gains[2] = 1.0;
					mesh.cloud.reset(new pcl::PointCloud<pcl::PointXYZRGB>);
					mesh.normals.reset(new pcl::PointCloud<pcl::Normal>);
					pcl::fromPCLPointCloud2(optMesh_->cloud, *mesh.cloud);
					pcl::fromPCLPointCloud2(optMesh_->cloud, *mesh.normals);
					mesh.polygons = optMesh_->tex_polygons[0];
					mesh.pose.setIdentity();
					if(optMesh_->tex_coordinates.size())
					{
						mesh.texCoords = optMesh_->tex_coordinates[0];
						mesh.texture = optTexture_;
					}
					main_scene_.addMesh(g_optMeshId, mesh, opengl_world_T_rtabmap_world, true);
				}
				else
				{
					pcl::IndicesPtr indices(new std::vector<int>); // null
					pcl::PointCloud<pcl::PointXYZRGB>::Ptr cloud(new pcl::PointCloud<pcl::PointXYZRGB>);
					pcl::fromPCLPointCloud2(optMesh_->cloud, *cloud);
					main_scene_.addCloud(g_optMeshId, cloud, indices, opengl_world_T_rtabmap_world);
				}

				// clean up old messages if there are ones
				boost::mutex::scoped_lock  lock(visLocalizationMutex_);
				if(visLocalizationEvents_.size())
				{
					for(std::list<rtabmap::RtabmapEvent*>::iterator iter=visLocalizationEvents_.begin(); iter!=visLocalizationEvents_.end(); ++iter)
					{
						delete *iter;
					}
				}
				visLocalizationEvents_.clear();
			}

			std::list<rtabmap::RtabmapEvent*> visLocalizationEvents;
			visLocalizationMutex_.lock();
			visLocalizationEvents = visLocalizationEvents_;
			visLocalizationEvents_.clear();
			visLocalizationMutex_.unlock();

			if(visLocalizationEvents.size())
			{
				const rtabmap::Statistics & stats = visLocalizationEvents.back()->getStats();
				if(!stats.mapCorrection().isNull())
				{
					mapToOdom_ = stats.mapCorrection();
				}

				std::map<int, rtabmap::Transform>::const_iterator iter = stats.poses().find(optRefId_);
				if(iter != stats.poses().end() && !iter->second.isNull() && optRefPose_)
				{
					// adjust opt mesh pose
					main_scene_.setCloudPose(g_optMeshId, opengl_world_T_rtabmap_world * iter->second * (*optRefPose_).inverse());
				}
				int fastMovement = (int)uValue(stats.data(), rtabmap::Statistics::kMemoryFast_movement(), 0.0f);
				int loopClosure = (int)uValue(stats.data(), rtabmap::Statistics::kLoopAccepted_hypothesis_id(), 0.0f);
				int rejected = (int)uValue(stats.data(), rtabmap::Statistics::kLoopRejectedHypothesis(), 0.0f);
				int landmark = (int)uValue(stats.data(), rtabmap::Statistics::kLoopLandmark_detected(), 0.0f);
				if(!paused_ && loopClosure>0)
				{
					main_scene_.setBackgroundColor(0, 0.5f, 0); // green
				}
				else if(!paused_ && landmark!=0)
				{
					main_scene_.setBackgroundColor(1, 0.65f, 0); // orange
				}
				else if(!paused_ && rejected>0)
				{
					main_scene_.setBackgroundColor(0, 0.2f, 0); // dark green
				}
				else if(!paused_ && fastMovement)
				{
					main_scene_.setBackgroundColor(0.2f, 0, 0.2f); // dark magenta
				}
				else
				{
					main_scene_.setBackgroundColor(backgroundColor_, backgroundColor_, backgroundColor_);
				}
			}

			//backup state
			bool isMeshRendering = main_scene_.isMeshRendering();
			bool isTextureRendering = main_scene_.isMeshTexturing();

			main_scene_.setMeshRendering(main_scene_.hasMesh(g_optMeshId), main_scene_.hasTexture(g_optMeshId));

			fpsTime.restart();
			lastDrawnCloudsCount_ = main_scene_.Render();
			if(renderingTime_ < fpsTime.elapsed())
			{
				renderingTime_ = fpsTime.elapsed();
			}

			// revert state
			main_scene_.setMeshRendering(isMeshRendering, isTextureRendering);

			if(visLocalizationEvents.size())
			{
				// send statistics to GUI
				UEventsManager::post(new PostRenderEvent(visLocalizationEvents.back()));
				visLocalizationEvents.pop_back();

				for(std::list<rtabmap::RtabmapEvent*>::iterator iter=visLocalizationEvents.begin(); iter!=visLocalizationEvents.end(); ++iter)
				{
					delete *iter;
				}
				visLocalizationEvents.clear();
				lastPostRenderEventTime_ = UTimer::now();
			}
		}
		else
		{
			if(main_scene_.hasCloud(g_optMeshId))
			{
				main_scene_.clear();
				optMesh_.reset(new pcl::TextureMesh);
				optTexture_ = cv::Mat();
			}

			// should be before clearSceneOnNextRender_ in case database is reset
			if(!openingDatabase_)
			{
				rtabmapMutex_.lock();
				rtabmapEvents = rtabmapEvents_;
				rtabmapEvents_.clear();
				rtabmapMutex_.unlock();

				if(!clearSceneOnNextRender_ && rtabmapEvents.size())
				{
					boost::mutex::scoped_lock  lockMesh(meshesMutex_);
					if(createdMeshes_.size())
					{
						if(rtabmapEvents.front()->getStats().refImageId()>0 && rtabmapEvents.front()->getStats().refImageId() < createdMeshes_.rbegin()->first)
						{
							LOGI("Detected new database! new=%d old=%d", rtabmapEvents.front()->getStats().refImageId(), createdMeshes_.rbegin()->first);
							clearSceneOnNextRender_ = true;
						}
					}
				}
#ifdef DEBUG_RENDERING_PERFORMANCE
				if(rtabmapEvents.size())
				{
					LOGW("begin and getting rtabmap events %fs", time.ticks());
				}
#endif
			}

			if(clearSceneOnNextRender_)
			{
				LOGI("Clearing all rendering data...");
				odomMutex_.lock();
				odomEvents_.clear();
				odomMutex_.unlock();

				poseMutex_.lock();
				poseEvents_.clear();
				poseMutex_.unlock();

				main_scene_.clear();
				clearSceneOnNextRender_ = false;
				if(!openingDatabase_)
				{
					boost::mutex::scoped_lock  lock(meshesMutex_);
					LOGI("Clearing  meshes...");
					createdMeshes_.clear();
				}
				else
				{
					notifyDataLoaded = true;
				}
				rawPoses_.clear();
				totalPoints_ = 0;
				totalPolygons_ = 0;
				lastDrawnCloudsCount_ = 0;
				renderingTime_ = 0.0f;
				lastPostRenderEventTime_ = 0.0;
				lastPoseEventTime_ = 0.0;
				bufferedStatsData_.clear();
			}

			// Did we lose OpenGL context? If so, recreate the context;
			std::set<int> added = main_scene_.getAddedClouds();
			added.erase(-1);
			if(!openingDatabase_)
			{
				boost::mutex::scoped_lock  lock(meshesMutex_);
				unsigned int meshes = createdMeshes_.size();
				if(added.size() != meshes)
				{
					LOGI("added (%d) != meshes (%d)", (int)added.size(), meshes);
					boost::mutex::scoped_lock  lockRtabmap(rtabmapMutex_);
					UASSERT(rtabmap_!=0);
					for(std::map<int, Mesh>::iterator iter=createdMeshes_.begin(); iter!=createdMeshes_.end(); ++iter)
					{
						if(!main_scene_.hasCloud(iter->first) && !iter->second.pose.isNull())
						{
							LOGI("Re-add mesh %d to OpenGL context", iter->first);
							if(main_scene_.isMeshRendering() && iter->second.polygons.size() == 0)
							{
								iter->second.polygons = rtabmap::util3d::organizedFastMesh(iter->second.cloud, meshAngleToleranceDeg_*M_PI/180.0, false, meshTrianglePix_);
								iter->second.polygonsLowRes = rtabmap::util3d::organizedFastMesh(iter->second.cloud, meshAngleToleranceDeg_*M_PI/180.0, false, meshTrianglePix_+LOW_RES_PIX);
							}

							if(main_scene_.isMeshTexturing())
							{
								cv::Mat textureRaw;
								textureRaw = rtabmap::uncompressImage(rtabmap_->getMemory()->getImageCompressed(iter->first));
								if(!textureRaw.empty())
								{
									if(renderingTextureDecimation_ > 1)
									{
										cv::Size reducedSize(textureRaw.cols/renderingTextureDecimation_, textureRaw.rows/renderingTextureDecimation_);
										LOGD("resize image from %dx%d to %dx%d", textureRaw.cols, textureRaw.rows, reducedSize.width, reducedSize.height);
										cv::resize(textureRaw, iter->second.texture, reducedSize, 0, 0, CV_INTER_LINEAR);
									}
									else
									{
										iter->second.texture = textureRaw;
									}
								}
							}
							main_scene_.addMesh(iter->first, iter->second, opengl_world_T_rtabmap_world*iter->second.pose);
							main_scene_.setCloudVisible(iter->first, iter->second.visible);

							iter->second.texture = cv::Mat(); // don't keep textures in memory
						}
					}
				}
			}
			else if(notifyDataLoaded)
			{
				rtabmapMutex_.lock();
				rtabmapEvents = rtabmapEvents_;
				rtabmapEvents_.clear();
				rtabmapMutex_.unlock();
				openingDatabase_ = false;
			}

			if(rtabmapEvents.size())
			{
#ifdef DEBUG_RENDERING_PERFORMANCE
				LOGW("Process rtabmap events %fs", time.ticks());
#else
				LOGI("Process rtabmap events");
#endif

				// update buffered signatures
				std::map<int, rtabmap::SensorData> bufferedSensorData;
				if(!dataRecorderMode_)
				{
					for(std::list<rtabmap::RtabmapEvent*>::iterator iter=rtabmapEvents.begin(); iter!=rtabmapEvents.end(); ++iter)
					{
						const rtabmap::Statistics & stats =  (*iter)->getStats();

						// Don't create mesh for the last node added if rehearsal happened or if discarded (small movement)
						int smallMovement = (int)uValue(stats.data(), rtabmap::Statistics::kMemorySmall_movement(), 0.0f);
						int fastMovement = (int)uValue(stats.data(), rtabmap::Statistics::kMemoryFast_movement(), 0.0f);
						int rehearsalMerged = (int)uValue(stats.data(), rtabmap::Statistics::kMemoryRehearsal_merged(), 0.0f);
						if(!localizationMode_ && stats.getLastSignatureData().id() > 0 &&
							smallMovement == 0 && rehearsalMerged == 0 && fastMovement == 0)
						{
							int id = stats.getLastSignatureData().id();
							const rtabmap::Signature & s = stats.getLastSignatureData();

							if(!trajectoryMode_ &&
							   !s.sensorData().imageRaw().empty() &&
							   !s.sensorData().depthRaw().empty())
							{
								uInsert(bufferedSensorData, std::make_pair(id, s.sensorData()));
							}

							uInsert(rawPoses_, std::make_pair(id, s.getPose()));
						}

						int loopClosure = (int)uValue(stats.data(), rtabmap::Statistics::kLoopAccepted_hypothesis_id(), 0.0f);
						int rejected = (int)uValue(stats.data(), rtabmap::Statistics::kLoopRejectedHypothesis(), 0.0f);
						int landmark = (int)uValue(stats.data(), rtabmap::Statistics::kLoopLandmark_detected(), 0.0f);
						if(!paused_ && loopClosure>0)
						{
							main_scene_.setBackgroundColor(0, 0.5f, 0); // green
						}
						else if(!paused_ && landmark!=0)
						{
							main_scene_.setBackgroundColor(1, 0.65f, 0); // orange
						}
						else if(!paused_ && rejected>0)
						{
							main_scene_.setBackgroundColor(0, 0.2f, 0); // dark green
						}
						else if(!paused_ && rehearsalMerged>0)
						{
							main_scene_.setBackgroundColor(0, 0, 0.2f); // blue
						}
						else if(!paused_ && fastMovement)
						{
							main_scene_.setBackgroundColor(0.2f, 0, 0.2f); // dark magenta
						}
						else
						{
							main_scene_.setBackgroundColor(backgroundColor_, backgroundColor_, backgroundColor_);
						}
					}
				}

#ifdef DEBUG_RENDERING_PERFORMANCE
				LOGW("Looking fo data to load (%d) %fs", bufferedSensorData.size(), time.ticks());
#endif

				std::map<int, rtabmap::Transform> posesWithMarkers = rtabmapEvents.back()->getStats().poses();
				if(!rtabmapEvents.back()->getStats().mapCorrection().isNull())
				{
					mapToOdom_ = rtabmapEvents.back()->getStats().mapCorrection();
				}

				// Transform pose in OpenGL world
				for(std::map<int, rtabmap::Transform>::iterator iter=posesWithMarkers.begin(); iter!=posesWithMarkers.end(); ++iter)
				{
					if(!graphOptimization_)
					{
						std::map<int, rtabmap::Transform>::iterator jter = rawPoses_.find(iter->first);
						if(jter != rawPoses_.end())
						{
							iter->second = opengl_world_T_rtabmap_world*jter->second;
						}
					}
					else
					{
						iter->second = opengl_world_T_rtabmap_world*iter->second;
					}
				}

				std::map<int, rtabmap::Transform> poses(posesWithMarkers.lower_bound(0), posesWithMarkers.end());
				const std::multimap<int, rtabmap::Link> & links = rtabmapEvents.back()->getStats().constraints();
				if(poses.size())
				{
					//update graph
					main_scene_.updateGraph(poses, links);

#ifdef DEBUG_RENDERING_PERFORMANCE
					LOGW("Update graph: %fs", time.ticks());
#endif

					// update clouds
					boost::mutex::scoped_lock  lock(meshesMutex_);
					std::set<std::string> strIds;
					for(std::map<int, rtabmap::Transform>::iterator iter=poses.begin(); iter!=poses.end(); ++iter)
					{
						int id = iter->first;
						if(!iter->second.isNull())
						{
							if(main_scene_.hasCloud(id))
							{
								//just update pose
								main_scene_.setCloudPose(id, iter->second);
								main_scene_.setCloudVisible(id, true);
								std::map<int, Mesh>::iterator meshIter = createdMeshes_.find(id);
								UASSERT(meshIter!=createdMeshes_.end());
								meshIter->second.pose = opengl_world_T_rtabmap_world.inverse()*iter->second;
								meshIter->second.visible = true;
							}
							else
							{
								if(createdMeshes_.find(id) == createdMeshes_.end() &&
										bufferedSensorData.find(id) != bufferedSensorData.end())
								{
									rtabmap::SensorData data = bufferedSensorData.at(id);

									cv::Mat tmpA, depth;
									data.uncompressData(&tmpA, &depth);
#ifdef DEBUG_RENDERING_PERFORMANCE
									LOGW("Decompressing data: %fs", time.ticks());
#endif

									if(!data.imageRaw().empty() && !data.depthRaw().empty())
									{
										// Voxelize and filter depending on the previous cloud?
										pcl::PointCloud<pcl::PointXYZRGB>::Ptr cloud;
										pcl::IndicesPtr indices(new std::vector<int>);
										cloud = rtabmap::util3d::cloudRGBFromSensorData(data, meshDecimation_, maxCloudDepth_, minCloudDepth_, indices.get());
#ifdef DEBUG_RENDERING_PERFORMANCE
										LOGW("Creating node cloud %d (depth=%dx%d rgb=%dx%d, %fs)", id, data.depthRaw().cols, data.depthRaw().rows, data.imageRaw().cols, data.imageRaw().rows, time.ticks());
#endif
										if(cloud->size() && indices->size())
										{
											std::vector<pcl::Vertices> polygons;
											std::vector<pcl::Vertices> polygonsLowRes;
											if(main_scene_.isMeshRendering() && main_scene_.isMapRendering())
											{
												polygons = rtabmap::util3d::organizedFastMesh(cloud, meshAngleToleranceDeg_*M_PI/180.0, false, meshTrianglePix_);
#ifdef DEBUG_RENDERING_PERFORMANCE
												LOGW("Creating mesh, %d polygons (%fs)", (int)polygons.size(), time.ticks());
#endif
												polygonsLowRes = rtabmap::util3d::organizedFastMesh(cloud, meshAngleToleranceDeg_*M_PI/180.0, false, meshTrianglePix_+LOW_RES_PIX);
#ifdef DEBUG_RENDERING_PERFORMANCE
												LOGW("Creating mesh, %d polygons (%fs)", (int)polygons.size(), time.ticks());
#endif
											}

											if((main_scene_.isMeshRendering() && polygons.size()) || !main_scene_.isMeshRendering() || !main_scene_.isMapRendering())
											{
												std::pair<std::map<int, Mesh>::iterator, bool> inserted = createdMeshes_.insert(std::make_pair(id, Mesh()));
												UASSERT(inserted.second);
												inserted.first->second.cloud = cloud;
												inserted.first->second.indices = indices;
												inserted.first->second.polygons = polygons;
												inserted.first->second.polygonsLowRes = polygonsLowRes;
												inserted.first->second.visible = true;
												inserted.first->second.cameraModel = data.cameraModels()[0];
												inserted.first->second.gains[0] = 1.0;
												inserted.first->second.gains[1] = 1.0;
												inserted.first->second.gains[2] = 1.0;
												if(main_scene_.isMeshTexturing() && main_scene_.isMapRendering())
												{
													if(renderingTextureDecimation_ > 1)
													{
														cv::Size reducedSize(data.imageRaw().cols/renderingTextureDecimation_, data.imageRaw().rows/renderingTextureDecimation_);
														cv::resize(data.imageRaw(), inserted.first->second.texture, reducedSize, 0, 0, CV_INTER_LINEAR);
#ifdef DEBUG_RENDERING_PERFORMANCE
														LOGW("resize image from %dx%d to %dx%d (%fs)", data.imageRaw().cols, data.imageRaw().rows, reducedSize.width, reducedSize.height, time.ticks());
#endif
													}
													else
													{
														inserted.first->second.texture = data.imageRaw();
													}
												}
											}
										}
									}
								}
								if(createdMeshes_.find(id) != createdMeshes_.end())
								{
									Mesh & mesh = createdMeshes_.at(id);
									totalPoints_+=mesh.indices->size();
									totalPolygons_ += mesh.polygons.size();
									mesh.pose = opengl_world_T_rtabmap_world.inverse()*iter->second;
									main_scene_.addMesh(id, mesh, iter->second);
#ifdef DEBUG_RENDERING_PERFORMANCE
									LOGW("Adding mesh to scene: %fs", time.ticks());
#endif
									mesh.texture = cv::Mat(); // don't keep textures in memory
								}
							}
						}
					}
				}

				//filter poses?
				if(poses.size() > 2)
				{
					if(nodesFiltering_)
					{
						for(std::multimap<int, rtabmap::Link>::const_iterator iter=links.begin(); iter!=links.end(); ++iter)
						{
							if(iter->second.type() != rtabmap::Link::kNeighbor)
							{
								int oldId = iter->second.to()>iter->second.from()?iter->second.from():iter->second.to();
								poses.erase(oldId);
							}
						}
					}
				}

				if(!poses.empty())
				{
					//update cloud visibility
					boost::mutex::scoped_lock  lock(meshesMutex_);
					std::set<int> addedClouds = main_scene_.getAddedClouds();
					for(std::set<int>::const_iterator iter=addedClouds.begin();
						iter!=addedClouds.end();
						++iter)
					{
						if(*iter > 0 && poses.find(*iter) == poses.end())
						{
							main_scene_.setCloudVisible(*iter, false);
							std::map<int, Mesh>::iterator meshIter = createdMeshes_.find(*iter);
							UASSERT(meshIter!=createdMeshes_.end());
							meshIter->second.visible = false;
						}
					}
				}

				// Update markers
				std::set<int> addedMarkers = main_scene_.getAddedMarkers();
				for(std::set<int>::const_iterator iter=addedMarkers.begin();
					iter!=addedMarkers.end();
					++iter)
				{
					if(posesWithMarkers.find(*iter) == posesWithMarkers.end())
					{
						main_scene_.removeMarker(*iter);
					}
				}
				for(std::map<int, rtabmap::Transform>::const_iterator iter=posesWithMarkers.begin();
					iter!=posesWithMarkers.end() && iter->first<0;
					++iter)
				{
					int id = iter->first;
					if(main_scene_.hasMarker(id))
					{
						//just update pose
						main_scene_.setMarkerPose(id, iter->second);
					}
					else
					{
						main_scene_.addMarker(id, iter->second);
					}
				}
			}
			else
			{
				main_scene_.setCloudVisible(-1, odomCloudShown_ && !trajectoryMode_ && !paused_);

				//just process the last one
				if(!odomEvent.pose().isNull())
				{
					if(odomCloudShown_ && !trajectoryMode_)
					{
						if(!odomEvent.data().imageRaw().empty() && !odomEvent.data().depthRaw().empty())
						{
							pcl::PointCloud<pcl::PointXYZRGB>::Ptr cloud;
							pcl::IndicesPtr indices(new std::vector<int>);
							cloud = rtabmap::util3d::cloudRGBFromSensorData(odomEvent.data(), meshDecimation_, maxCloudDepth_, minCloudDepth_, indices.get());
							if(cloud->size() && indices->size())
							{
								LOGI("Created odom cloud (rgb=%dx%d depth=%dx%d cloud=%dx%d)",
										odomEvent.data().imageRaw().cols, odomEvent.data().imageRaw().rows,
										odomEvent.data().depthRaw().cols, odomEvent.data().depthRaw().rows,
									   (int)cloud->width, (int)cloud->height);
								main_scene_.addCloud(-1, cloud, indices, opengl_world_T_rtabmap_world*mapToOdom_*odomEvent.pose());
								main_scene_.setCloudVisible(-1, true);
							}
							else
							{
								UERROR("Generated cloud is empty!");
							}
						}
						else
						{
							UERROR("Odom data images are empty!");
						}
					}
				}
			}

			if(gainCompensationOnNextRender_>0)
			{
				gainCompensation(gainCompensationOnNextRender_==2);
				for(std::map<int, Mesh>::iterator iter = createdMeshes_.begin(); iter!=createdMeshes_.end(); ++iter)
				{
					main_scene_.updateGains(iter->first, iter->second.gains[0], iter->second.gains[1], iter->second.gains[2]);
				}
				gainCompensationOnNextRender_ = 0;
				notifyDataLoaded = true;
			}

			if(bilateralFilteringOnNextRender_)
			{
				LOGI("Bilateral filtering...");
				bilateralFilteringOnNextRender_ = false;
				boost::mutex::scoped_lock  lock(meshesMutex_);
				for(std::map<int, Mesh>::iterator iter = createdMeshes_.begin(); iter!=createdMeshes_.end(); ++iter)
				{
					if(iter->second.cloud->size() && iter->second.indices->size())
					{
						if(smoothMesh(iter->first, iter->second))
						{
							main_scene_.updateMesh(iter->first, iter->second);
						}
					}
				}
				notifyDataLoaded = true;
			}

			if(filterPolygonsOnNextRender_ && clusterRatio_>0.0f)
			{
				LOGI("Polygon filtering...");
				filterPolygonsOnNextRender_ = false;
				boost::mutex::scoped_lock  lock(meshesMutex_);
				UTimer time;
				for(std::map<int, Mesh>::iterator iter = createdMeshes_.begin(); iter!=createdMeshes_.end(); ++iter)
				{
					if(iter->second.polygons.size())
					{
						// filter polygons
						iter->second.polygons = filterOrganizedPolygons(iter->second.polygons, iter->second.cloud->size());
						main_scene_.updateCloudPolygons(iter->first, iter->second.polygons);
					}
				}
				notifyDataLoaded = true;
			}

			fpsTime.restart();
			lastDrawnCloudsCount_ = main_scene_.Render();
			if(renderingTime_ < fpsTime.elapsed())
			{
				renderingTime_ = fpsTime.elapsed();
			}

			if(rtabmapEvents.size())
			{
				// send statistics to GUI
				LOGI("New data added to map, rendering time: %fs", renderingTime_);
				UEventsManager::post(new PostRenderEvent(rtabmapEvents.back()));
				rtabmapEvents.pop_back();

				for(std::list<rtabmap::RtabmapEvent*>::iterator iter=rtabmapEvents.begin(); iter!=rtabmapEvents.end(); ++iter)
				{
					delete *iter;
				}
				rtabmapEvents.clear();

				lastPostRenderEventTime_ = UTimer::now();

				if(lastPoseEventTime_>0.0 && UTimer::now()-lastPoseEventTime_ > 1.0)
				{
					UERROR("TangoPoseEventNotReceived");
					UEventsManager::post(new rtabmap::CameraTangoEvent(10, "TangoPoseEventNotReceived", uNumber2Str(UTimer::now()-lastPoseEventTime_)));
				}
			}
		}

		if(takeScreenshotOnNextRender_)
		{
			takeScreenshotOnNextRender_ = false;
			int w = main_scene_.getViewPortWidth();
			int h = main_scene_.getViewPortHeight();
			cv::Mat image(h, w, CV_8UC4);
			glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, image.data);
			cv::flip(image, image, 0);
			cv::cvtColor(image, image, CV_RGBA2BGRA);
			cv::Mat roi;
			if(w>h)
			{
				int offset = (w-h)/2;
				roi = image(cv::Range::all(), cv::Range(offset,offset+h));
			}
			else
			{
				int offset = (h-w)/2;
				roi = image(cv::Range(offset,offset+w), cv::Range::all());
			}
			rtabmapMutex_.lock();
			LOGI("Saving screenshot %dx%d...", roi.cols, roi.rows);
			rtabmap_->getMemory()->savePreviewImage(roi);
			rtabmapMutex_.unlock();
			screenshotReady_.release();
		}

		if((openingDatabase_ && !visualizingMesh_) || exporting_ || postProcessing_)
		{
			// throttle rendering max 5Hz if we are doing some processing
			double renderTime = fpsTime.elapsed();
			if(0.2 - renderTime > 0.0)
			{
				uSleep((0.2 - renderTime)*1000);
			}
		}

		if(paused_ && lastPostRenderEventTime_ > 0.0)
		{
			double interval = UTimer::now() - lastPostRenderEventTime_;
			double updateInterval = 1.0;
			if(!openingDatabase_ && rtabmapThread_)
			{
				boost::mutex::scoped_lock  lock(rtabmapMutex_);
				if(rtabmapThread_ && rtabmapThread_->getDetectorRate()>0.0f)
				{
					updateInterval = 1.0f/rtabmapThread_->getDetectorRate();
				}
			}

			if(interval >= updateInterval)
			{
				if(!openingDatabase_)
				{
					// don't send event when we are opening the database (init events already sent)
					UEventsManager::post(new PostRenderEvent());
				}
				lastPostRenderEventTime_ = UTimer::now();
			}
		}

		return notifyDataLoaded||notifyCameraStarted?1:0;
	}
	catch(const UException & e)
	{
		for(std::list<rtabmap::RtabmapEvent*>::iterator iter=rtabmapEvents.begin(); iter!=rtabmapEvents.end(); ++iter)
		{
			delete *iter;
		}
		rtabmapEvents.clear();
		UERROR("Exception! msg=\"%s\"", e.what());
		return -2;
	}
	catch(const cv::Exception & e)
	{
		for(std::list<rtabmap::RtabmapEvent*>::iterator iter=rtabmapEvents.begin(); iter!=rtabmapEvents.end(); ++iter)
		{
			delete *iter;
		}
		rtabmapEvents.clear();
		UERROR("Exception! msg=\"%s\"", e.what());
		return -1;
	}
	catch(const std::exception & e)
	{
		for(std::list<rtabmap::RtabmapEvent*>::iterator iter=rtabmapEvents.begin(); iter!=rtabmapEvents.end(); ++iter)
		{
			delete *iter;
		}
		rtabmapEvents.clear();
		UERROR("Exception! msg=\"%s\"", e.what());
		return -2;
	}
}

void RTABMapApp::SetCameraType(
    tango_gl::GestureCamera::CameraType camera_type) {
  main_scene_.SetCameraType(camera_type);
}

void RTABMapApp::OnTouchEvent(int touch_count,
                                      tango_gl::GestureCamera::TouchEvent event,
                                      float x0, float y0, float x1, float y1) {
  main_scene_.OnTouchEvent(touch_count, event, x0, y0, x1, y1);
}

void RTABMapApp::setPausedMapping(bool paused)
{
	{
		boost::mutex::scoped_lock  lock(renderingMutex_);
		if(!localizationMode_)
		{
			visualizingMesh_ = false;
		}
		main_scene_.setBackgroundColor(backgroundColor_, backgroundColor_, backgroundColor_);
	}
	paused_ = paused;
	if(camera_)
	{
		if(paused_)
		{
			LOGW("Pause!");
			camera_->kill();
		}
		else
		{
			LOGW("Resume!");
			UEventsManager::post(new rtabmap::RtabmapEventCmd(rtabmap::RtabmapEventCmd::kCmdTriggerNewMap));
			camera_->start();
		}
	}
}
void RTABMapApp::setOnlineBlending(bool enabled)
{
	main_scene_.setBlending(enabled);
}
void RTABMapApp::setMapCloudShown(bool shown)
{
	main_scene_.setMapRendering(shown);
}
void RTABMapApp::setOdomCloudShown(bool shown)
{
	odomCloudShown_ = shown;
	main_scene_.setTraceVisible(shown);
}
void RTABMapApp::setMeshRendering(bool enabled, bool withTexture)
{
	main_scene_.setMeshRendering(enabled, withTexture);
}
void RTABMapApp::setPointSize(float value)
{
	main_scene_.setPointSize(value);
}
void RTABMapApp::setFOV(float angle)
{
	main_scene_.setFOV(angle);
}
void RTABMapApp::setOrthoCropFactor(float value)
{
	main_scene_.setOrthoCropFactor(value);
}
void RTABMapApp::setGridRotation(float value)
{
	main_scene_.setGridRotation(value);
}
void RTABMapApp::setFirstPanoPosition()
{
	main_scene_.setFirstPanoPosition();
}
void RTABMapApp::rotatePanoPosition()
{
	main_scene_.rotatePanoPosition();
}
void RTABMapApp::setLighting(bool enabled)
{
	main_scene_.setLighting(enabled);
}
void RTABMapApp::setBackfaceCulling(bool enabled)
{
	main_scene_.setBackfaceCulling(enabled);
}
void RTABMapApp::setWireframe(bool enabled)
{
	main_scene_.setWireframe(enabled);
}

void RTABMapApp::setLocalizationMode(bool enabled)
{
	localizationMode_ = enabled;
	this->post(new rtabmap::ParamEvent(rtabmap::Parameters::kMemIncrementalMemory(), uBool2Str(!localizationMode_)));
}
void RTABMapApp::setTrajectoryMode(bool enabled)
{
	trajectoryMode_ = enabled;
	this->post(new rtabmap::ParamEvent(rtabmap::Parameters::kMemBinDataKept(), uBool2Str(!trajectoryMode_)));
}

void RTABMapApp::setGraphOptimization(bool enabled)
{
	graphOptimization_ = enabled;
	UASSERT(camera_ != 0 && rtabmap_!=0 && rtabmap_->getMemory()!=0);
	if(!camera_->isRunning() && rtabmap_->getMemory()->getLastWorkingSignature()!=0)
	{
		std::map<int, rtabmap::Transform> poses;
		std::multimap<int, rtabmap::Link> links;
		rtabmap_->getGraph(poses, links, true, true);
		if(poses.size())
		{
			boost::mutex::scoped_lock  lock(rtabmapMutex_);
			rtabmap::Statistics stats = rtabmap_->getStatistics();
			stats.setPoses(poses);
			stats.setConstraints(links);

			LOGI("Send rtabmap event to update graph...");
			rtabmapEvents_.push_back(new rtabmap::RtabmapEvent(stats));

			rtabmap_->setOptimizedPoses(poses);
		}
	}
}
void RTABMapApp::setNodesFiltering(bool enabled)
{
	nodesFiltering_ = enabled;
	setGraphOptimization(graphOptimization_); // this will resend the graph if paused
}
void RTABMapApp::setGraphVisible(bool visible)
{
	main_scene_.setGraphVisible(visible);
	main_scene_.setTraceVisible(visible);
}
void RTABMapApp::setGridVisible(bool visible)
{
	main_scene_.setGridVisible(visible);
}

void RTABMapApp::setRawScanSaved(bool enabled)
{
	if(rawScanSaved_ != enabled)
	{
		rawScanSaved_ = enabled;
		if(camera_)
		{
			camera_->setRawScanPublished(rawScanSaved_);
		}
	}
}

void RTABMapApp::setCameraColor(bool enabled)
{
	if(cameraColor_ != enabled)
	{
		cameraColor_ = enabled;
	}
}

void RTABMapApp::setFullResolution(bool enabled)
{
	if(fullResolution_ != enabled)
	{
		fullResolution_ = enabled;
		if(camera_)
		{
			camera_->setDecimation(fullResolution_?1:2);
		}
		rtabmap::ParametersMap parameters;
		parameters.insert(rtabmap::ParametersPair(rtabmap::Parameters::kMemImagePreDecimation(), std::string(fullResolution_?"2":"1")));
		this->post(new rtabmap::ParamEvent(parameters));
	}
}

void RTABMapApp::setSmoothing(bool enabled)
{
	if(smoothing_ != enabled)
	{
		smoothing_ = enabled;
		if(camera_)
		{
			camera_->setSmoothing(smoothing_);
		}
	}
}

void RTABMapApp::setAppendMode(bool enabled)
{
	if(appendMode_ != enabled)
	{
		appendMode_ = enabled;
		rtabmap::ParametersMap parameters;
		parameters.insert(rtabmap::ParametersPair(rtabmap::Parameters::kRtabmapStartNewMapOnLoopClosure(), uBool2Str(appendMode_)));
		this->post(new rtabmap::ParamEvent(parameters));
	}
}

void RTABMapApp::setDataRecorderMode(bool enabled)
{
	if(dataRecorderMode_ != enabled)
	{
		dataRecorderMode_ = enabled; // parameters will be set when resuming (we assume we are paused)
	}
}

void RTABMapApp::setMaxCloudDepth(float value)
{
	maxCloudDepth_ = value;
}

void RTABMapApp::setMinCloudDepth(float value)
{
	minCloudDepth_ = value;
}

void RTABMapApp::setCloudDensityLevel(int value)
{
	cloudDensityLevel_ = value;
}

void RTABMapApp::setMeshAngleTolerance(float value)
{
	meshAngleToleranceDeg_ = value;
}

void RTABMapApp::setMeshTriangleSize(int value)
{
	meshTrianglePix_ = value;
}

void RTABMapApp::setClusterRatio(float value)
{
	clusterRatio_ = value;
}

void RTABMapApp::setMaxGainRadius(float value)
{
	maxGainRadius_ = value;
}

void RTABMapApp::setRenderingTextureDecimation(int value)
{
	UASSERT(value>=1);
	renderingTextureDecimation_ = value;
}

void RTABMapApp::setBackgroundColor(float gray)
{
	backgroundColor_ = gray;
	float v = backgroundColor_ == 0.5f?0.4f:1.0f-backgroundColor_;
	main_scene_.setGridColor(v, v, v);
}

int RTABMapApp::setMappingParameter(const std::string & key, const std::string & value)
{
	std::string compatibleKey = key;

	// Backward compatibility
	std::map<std::string, std::pair<bool, std::string> >::const_iterator iter=rtabmap::Parameters::getRemovedParameters().find(key);
	if(iter != rtabmap::Parameters::getRemovedParameters().end())
	{
		if(iter->second.first)
		{
			// can be migrated
			compatibleKey = iter->second.second;
			LOGW("Parameter name changed: \"%s\" -> \"%s\". Please update the code accordingly. Value \"%s\" is still set to the new parameter name.",
					iter->first.c_str(), iter->second.second.c_str(), value.c_str());
		}
		else
		{
			if(iter->second.second.empty())
			{
				UERROR("Parameter \"%s\" doesn't exist anymore!",
						iter->first.c_str());
			}
			else
			{
				UERROR("Parameter \"%s\" doesn't exist anymore! You may look at this similar parameter: \"%s\"",
						iter->first.c_str(), iter->second.second.c_str());
			}
		}
	}

	if(rtabmap::Parameters::getDefaultParameters().find(compatibleKey) != rtabmap::Parameters::getDefaultParameters().end())
	{
		LOGI(uFormat("Setting param \"%s\"  to \"%s\"", compatibleKey.c_str(), value.c_str()).c_str());
		if(compatibleKey.compare(rtabmap::Parameters::kKpDetectorStrategy()) == 0 &&
		   mappingParameters_.at(rtabmap::Parameters::kKpDetectorStrategy()).compare(value) != 0)
		{
			// Changing feature type should reset mapping!
			resetMapping();
		}
		uInsert(mappingParameters_, rtabmap::ParametersPair(compatibleKey, value));
		UEventsManager::post(new rtabmap::ParamEvent(this->getRtabmapParameters()));
		return 0;
	}
	else
	{
		UERROR(uFormat("Key \"%s\" doesn't exist!", compatibleKey.c_str()).c_str());
		return -1;
	}
}

void RTABMapApp::setGPS(const rtabmap::GPS & gps)
{
	if(camera_)
	{
		camera_->setGPS(gps);
	}
}

void RTABMapApp::addEnvSensor(int type, float value)
{
	if(camera_)
	{
		camera_->addEnvSensor(type, value);
	}
}

void RTABMapApp::resetMapping()
{
	LOGW("Reset!");
	status_.first = rtabmap::RtabmapEventInit::kInitializing;
	status_.second = "";

	mapToOdom_.setIdentity();
	clearSceneOnNextRender_ = true;

	if(camera_)
	{
		camera_->resetOrigin();
	}

	UEventsManager::post(new rtabmap::RtabmapEventCmd(rtabmap::RtabmapEventCmd::kCmdResetMemory));
}

void RTABMapApp::save(const std::string & databasePath)
{
	LOGI("Saving database to %s", databasePath.c_str());
	rtabmapThread_->join(true);

	LOGI("Taking screenshot...");
	takeScreenshotOnNextRender_ = true;
	if(!screenshotReady_.acquire(1, 2000))
	{
		UERROR("Failed to take a screenshot after 2 sec!");
	}

	// save mapping parameters in the database
	bool appendModeBackup = appendMode_;
	if(appendMode_)
	{
		appendMode_ = false;
	}

	bool dataRecorderModeBackup = dataRecorderMode_;
	if(dataRecorderMode_)
	{
		dataRecorderMode_ = false;
	}

	bool localizationModeBackup = localizationMode_;
	if(localizationMode_)
	{
		localizationMode_ = false;
	}

	if(appendModeBackup || dataRecorderModeBackup || localizationModeBackup)
	{
		rtabmap::ParametersMap parameters = getRtabmapParameters();
		rtabmap_->parseParameters(parameters);
		appendMode_ = appendModeBackup;
		dataRecorderMode_ = dataRecorderModeBackup;
		localizationMode_ = localizationModeBackup;
	}

	std::map<int, rtabmap::Transform> poses = rtabmap_->getLocalOptimizedPoses();
	rtabmap_->close(true, databasePath);
	rtabmap_->init(getRtabmapParameters(), dataRecorderMode_?"":databasePath);
	rtabmap_->setOptimizedPoses(poses);
	if(dataRecorderMode_)
	{
		clearSceneOnNextRender_ = true;
	}
	rtabmapThread_->start();

}

void RTABMapApp::cancelProcessing()
{
	UWARN("Processing canceled!");
	progressionStatus_.setCanceled(true);
}

bool RTABMapApp::exportMesh(
		float cloudVoxelSize,
		bool regenerateCloud,
		bool meshing,
		int textureSize,
		int textureCount,
		int normalK,
		bool optimized,
		float optimizedVoxelSize,
		int optimizedDepth,
		int optimizedMaxPolygons,
		float optimizedColorRadius,
		bool optimizedCleanWhitePolygons,
		int optimizedMinClusterSize,
		float optimizedMaxTextureDistance,
		int optimizedMinTextureClusterSize,
		bool blockRendering)
{
	// make sure createdMeshes_ is not modified while exporting! We don't
	// lock the meshesMutex_ because we want to continue rendering.

	std::map<int, rtabmap::Transform> poses = rtabmap_->getLocalOptimizedPoses();
	if(poses.empty())
	{
		// look if we just triggered new map without localizing afterward (pause/resume in append Mode)
		std::multimap<int, rtabmap::Link> links;
		rtabmap_->getGraph(
				poses,
				links,
				true,
				false);
		if(poses.empty())
		{
			UERROR("Empty optimized poses!");
			return false;
		}
		rtabmap_->setOptimizedPoses(poses);
	}

	if(blockRendering)
	{
		renderingMutex_.lock();
		main_scene_.clear();
	}

	exporting_ = true;

	bool success = false;

	try
	{
		int totalSteps = 0;
		totalSteps+=poses.size(); // assemble
		if(meshing)
		{
			if(optimized)
			{
				totalSteps += poses.size(); // meshing
				if(textureSize > 0)
				{
					totalSteps += 1; // gain
					totalSteps += 1; // blending

					if(optimizedMaxPolygons > 0)
					{
						totalSteps += 1; // decimation
					}
				}

				totalSteps += 1; // texture/coloring

				if(textureSize > 0)
				{
					totalSteps+=poses.size()+1; // texture cameras + apply polygons
				}
			}
			if(textureSize>0)
			{
				totalSteps += poses.size()+1; // uncompress and merge textures
			}
		}
		totalSteps += 1; // save file

		progressionStatus_.reset(totalSteps);

		//Assemble the meshes
		if(meshing) // Mesh or  Texture Mesh
		{
			pcl::PolygonMesh::Ptr polygonMesh(new pcl::PolygonMesh);
			pcl::TextureMesh::Ptr textureMesh(new pcl::TextureMesh);
			std::vector<std::map<int, pcl::PointXY> > vertexToPixels;
			cv::Mat globalTextures;
			int totalPolygons = 0;
			{
				if(optimized)
				{
					std::map<int, rtabmap::Transform> cameraPoses;
					std::map<int, rtabmap::CameraModel> cameraModels;
					std::map<int, cv::Mat> cameraDepths;

					UTimer timer;
					LOGI("Assemble clouds (%d)...", (int)poses.size());
#ifndef DISABLE_LOG
					int cloudCount=0;
#endif
					pcl::PointCloud<pcl::PointXYZRGBNormal>::Ptr mergedClouds(new pcl::PointCloud<pcl::PointXYZRGBNormal>);
					for(std::map<int, rtabmap::Transform>::iterator iter=poses.begin();
						iter!= poses.end();
						++iter)
					{
						std::map<int, Mesh>::iterator jter = createdMeshes_.find(iter->first);
						pcl::PointCloud<pcl::PointXYZRGB>::Ptr cloud(new pcl::PointCloud<pcl::PointXYZRGB>);
						pcl::IndicesPtr indices(new std::vector<int>);
						rtabmap::CameraModel model;
						cv::Mat depth;
						float gains[3];
						gains[0] = gains[1] = gains[2] = 1.0f;
						if(jter != createdMeshes_.end())
						{
							cloud = jter->second.cloud;
							indices = jter->second.indices;
							model = jter->second.cameraModel;
							gains[0] = jter->second.gains[0];
							gains[1] = jter->second.gains[1];
							gains[2] = jter->second.gains[2];

							rtabmap::SensorData data = rtabmap_->getMemory()->getNodeData(iter->first, false);
							data.uncompressData(0, &depth);
						}
						else
						{
							rtabmap::SensorData data = rtabmap_->getMemory()->getNodeData(iter->first, true);
							if(!data.imageRaw().empty() && !data.depthRaw().empty() && data.cameraModels().size() == 1)
							{
								cloud = rtabmap::util3d::cloudRGBFromSensorData(data, meshDecimation_, maxCloudDepth_, minCloudDepth_, indices.get());
								model = data.cameraModels()[0];
								depth = data.depthRaw();
							}
						}
						if(cloud->size() && indices->size() && model.isValidForProjection())
						{
							pcl::PointCloud<pcl::PointXYZRGB>::Ptr transformedCloud(new pcl::PointCloud<pcl::PointXYZRGB>);
							if(optimizedVoxelSize > 0.0f)
							{
								transformedCloud = rtabmap::util3d::voxelize(cloud, indices, optimizedVoxelSize);
								transformedCloud = rtabmap::util3d::transformPointCloud(transformedCloud, iter->second);
							}
							else
							{
								// it looks like that using only transformPointCloud with indices
								// flushes the colors, so we should extract points before... maybe a too old PCL version
								pcl::copyPointCloud(*cloud, *indices, *transformedCloud);
								transformedCloud = rtabmap::util3d::transformPointCloud(transformedCloud, iter->second);
							}

							Eigen::Vector3f viewpoint( iter->second.x(),  iter->second.y(),  iter->second.z());
							pcl::PointCloud<pcl::Normal>::Ptr normals = rtabmap::util3d::computeNormals(transformedCloud, normalK, 0.0f, viewpoint);

							pcl::PointCloud<pcl::PointXYZRGBNormal>::Ptr cloudWithNormals(new pcl::PointCloud<pcl::PointXYZRGBNormal>);
							pcl::concatenateFields(*transformedCloud, *normals, *cloudWithNormals);

							if(textureSize == 0 && (gains[0] != 1.0 || gains[1] != 1.0 || gains[2] != 1.0))
							{
								for(unsigned int i=0; i<cloudWithNormals->size(); ++i)
								{
									pcl::PointXYZRGBNormal & pt = cloudWithNormals->at(i);
									pt.r = uchar(std::max(0.0, std::min(255.0, double(pt.r) * gains[0])));
									pt.g = uchar(std::max(0.0, std::min(255.0, double(pt.g) * gains[1])));
									pt.b = uchar(std::max(0.0, std::min(255.0, double(pt.b) * gains[2])));
								}
							}


							if(mergedClouds->size() == 0)
							{
								*mergedClouds = *cloudWithNormals;
							}
							else
							{
								*mergedClouds += *cloudWithNormals;
							}

							cameraPoses.insert(std::make_pair(iter->first, iter->second));
							cameraModels.insert(std::make_pair(iter->first, model));
							if(!depth.empty())
							{
								cameraDepths.insert(std::make_pair(iter->first, depth));
							}

							LOGI("Assembled %d points (%d/%d total=%d)", (int)cloudWithNormals->size(), ++cloudCount, (int)poses.size(), (int)mergedClouds->size());
						}
						else
						{
							UERROR("Cloud %d not found or empty", iter->first);
						}

						if(progressionStatus_.isCanceled())
						{
							if(blockRendering)
							{
								renderingMutex_.unlock();
							}
							exporting_ = false;
							return false;
						}
						progressionStatus_.increment();
					}
					LOGI("Assembled clouds (%d)... done! %fs (total points=%d)", (int)cameraPoses.size(), timer.ticks(), (int)mergedClouds->size());

					if(mergedClouds->size()>=3)
					{
						if(optimizedDepth == 0)
						{
							Eigen::Vector4f min,max;
							pcl::getMinMax3D(*mergedClouds, min, max);
							float mapLength = uMax3(max[0]-min[0], max[1]-min[1], max[2]-min[2]);
							optimizedDepth = 12;
							for(int i=6; i<12; ++i)
							{
								if(mapLength/float(1<<i) < 0.03f)
								{
									optimizedDepth = i;
									break;
								}
							}
							LOGI("optimizedDepth=%d (map length=%f)", optimizedDepth, mapLength);
						}

						// Mesh reconstruction
						LOGI("Mesh reconstruction...");
						pcl::PolygonMesh::Ptr mesh(new pcl::PolygonMesh);
						pcl::Poisson<pcl::PointXYZRGBNormal> poisson;
						poisson.setDepth(optimizedDepth);
						poisson.setInputCloud(mergedClouds);
						poisson.reconstruct(*mesh);
						LOGI("Mesh reconstruction... done! %fs (%d polygons)", timer.ticks(), (int)mesh->polygons.size());

						if(progressionStatus_.isCanceled())
						{
							if(blockRendering)
							{
								renderingMutex_.unlock();
							}
							exporting_ = false;
							return false;
						}

						progressionStatus_.increment(poses.size());

						if(mesh->polygons.size())
						{
							totalPolygons=(int)mesh->polygons.size();

							if(optimizedMaxPolygons > 0 && optimizedMaxPolygons < (int)mesh->polygons.size())
							{
#ifndef DISABLE_VTK
								unsigned int count = mesh->polygons.size();
								float factor = 1.0f-float(optimizedMaxPolygons)/float(count);
								LOGI("Mesh decimation (max polygons %d/%d -> factor=%f)...", optimizedMaxPolygons, (int)count, factor);

								progressionStatus_.setMax(progressionStatus_.getMax() + optimizedMaxPolygons/10000);

								pcl::PolygonMesh::Ptr output(new pcl::PolygonMesh);
								pcl::MeshQuadricDecimationVTK mqd;
								mqd.setTargetReductionFactor(factor);
								mqd.setInputMesh(mesh);
								mqd.process (*output);
								mesh = output;

								//mesh = rtabmap::util3d::meshDecimation(mesh, decimationFactor);
								// use direct instantiation above to this fix some linker errors on android like:
								// pcl::MeshQuadricDecimationVTK::performProcessing(pcl::PolygonMesh&): error: undefined reference to 'vtkQuadricDecimation::New()'
								// pcl::VTKUtils::mesh2vtk(pcl::PolygonMesh const&, vtkSmartPointer<vtkPolyData>&): error: undefined reference to 'vtkFloatArray::New()'

								LOGI("Mesh decimated (factor=%f) from %d to %d polygons (%fs)", factor, count, (int)mesh->polygons.size(), timer.ticks());
								if(count < mesh->polygons.size())
								{
									UWARN("Decimated mesh has more polygons than before!");
								}
#else
								UWARN("RTAB-Map is not built with PCL-VTK module so mesh decimation cannot be used!");
#endif
							}

							if(progressionStatus_.isCanceled())
							{
								if(blockRendering)
								{
									renderingMutex_.unlock();
								}
								exporting_ = false;
								return false;
							}

							progressionStatus_.increment();

							rtabmap::util3d::denseMeshPostProcessing<pcl::PointXYZRGBNormal>(
									mesh,
									0.0f,
									0,
									mergedClouds,
									optimizedColorRadius,
									textureSize == 0,
									optimizedCleanWhitePolygons,
									optimizedMinClusterSize);

							if(textureSize>0)
							{
								LOGI("Texturing... cameraPoses=%d, cameraDepths=%d", (int)cameraPoses.size(), (int)cameraDepths.size());
								textureMesh = rtabmap::util3d::createTextureMesh(
										mesh,
										cameraPoses,
										cameraModels,
										cameraDepths,
										optimizedMaxTextureDistance,
										0.0f,
										0.0f,
										optimizedMinTextureClusterSize,
										std::vector<float>(),
										&progressionStatus_,
										&vertexToPixels);
								LOGI("Texturing... done! %fs", timer.ticks());

								if(progressionStatus_.isCanceled())
								{
									if(blockRendering)
									{
										renderingMutex_.unlock();
									}
									exporting_ = false;
									return false;
								}

								// Remove occluded polygons (polygons with no texture)
								if(textureMesh->tex_coordinates.size() && optimizedCleanWhitePolygons)
								{
									LOGI("Cleanup mesh...");
									rtabmap::util3d::cleanTextureMesh(*textureMesh, 0);
									LOGI("Cleanup mesh... done! %fs", timer.ticks());
								}

								totalPolygons = 0;
								for(unsigned int t=0; t<textureMesh->tex_polygons.size(); ++t)
								{
									totalPolygons+=textureMesh->tex_polygons[t].size();
								}
							}
							else
							{
								totalPolygons = (int)mesh->polygons.size();
								polygonMesh = mesh;
							}
						}
					}
					else
					{
						UERROR("Merged cloud too small (%d points) to create polygons!", (int)mergedClouds->size());
					}
				}
				else // organized meshes
				{
					pcl::PointCloud<pcl::PointXYZRGBNormal>::Ptr mergedClouds(new pcl::PointCloud<pcl::PointXYZRGBNormal>);

					if(textureSize > 0)
					{
						textureMesh->tex_materials.resize(poses.size());
						textureMesh->tex_polygons.resize(poses.size());
						textureMesh->tex_coordinates.resize(poses.size());
					}

					int polygonsStep = 0;
					int oi = 0;
					for(std::map<int, rtabmap::Transform>::iterator iter=poses.begin();
						iter!= poses.end();
						++iter)
					{
						LOGI("Assembling cloud %d (total=%d)...", iter->first, (int)poses.size());

						std::map<int, Mesh>::iterator jter = createdMeshes_.find(iter->first);
						pcl::PointCloud<pcl::PointXYZRGB>::Ptr cloud(new pcl::PointCloud<pcl::PointXYZRGB>);
						std::vector<pcl::Vertices> polygons;
						float gains[3] = {1.0f};
						if(jter != createdMeshes_.end())
						{
							cloud = jter->second.cloud;
							polygons= jter->second.polygons;
							if(cloud->size() && polygons.size() == 0)
							{
								polygons = rtabmap::util3d::organizedFastMesh(cloud, meshAngleToleranceDeg_*M_PI/180.0, false, meshTrianglePix_);
							}
							gains[0] = jter->second.gains[0];
							gains[1] = jter->second.gains[1];
							gains[2] = jter->second.gains[2];
						}
						else
						{
							rtabmap::SensorData data = rtabmap_->getMemory()->getNodeData(iter->first, true);
							if(!data.imageRaw().empty() && !data.depthRaw().empty() && data.cameraModels().size() == 1)
							{
								cloud = rtabmap::util3d::cloudRGBFromSensorData(data, meshDecimation_, maxCloudDepth_, minCloudDepth_);
								polygons = rtabmap::util3d::organizedFastMesh(cloud, meshAngleToleranceDeg_*M_PI/180.0, false, meshTrianglePix_);
							}
						}

						if(cloud->size() && polygons.size())
						{
							// Convert organized to dense cloud
							pcl::PointCloud<pcl::PointXYZRGB>::Ptr outputCloud(new pcl::PointCloud<pcl::PointXYZRGB>);
							std::vector<pcl::Vertices> outputPolygons;
							std::vector<int> denseToOrganizedIndices = rtabmap::util3d::filterNaNPointsFromMesh(*cloud, polygons, *outputCloud, outputPolygons);

							pcl::PointCloud<pcl::Normal>::Ptr normals = rtabmap::util3d::computeNormals(outputCloud, normalK);

							pcl::PointCloud<pcl::PointXYZRGBNormal>::Ptr cloudWithNormals(new pcl::PointCloud<pcl::PointXYZRGBNormal>);
							pcl::concatenateFields(*outputCloud, *normals, *cloudWithNormals);

							UASSERT(outputPolygons.size());

							totalPolygons+=outputPolygons.size();

							if(textureSize == 0)
							{
								// colored mesh
								cloudWithNormals = rtabmap::util3d::transformPointCloud(cloudWithNormals, iter->second);

								if(gains[0] != 1.0f || gains[1] != 1.0f || gains[2] != 1.0f)
								{
									for(unsigned int i=0; i<cloudWithNormals->size(); ++i)
									{
										pcl::PointXYZRGBNormal & pt = cloudWithNormals->at(i);
										pt.r = uchar(std::max(0.0, std::min(255.0, double(pt.r) * gains[0])));
										pt.g = uchar(std::max(0.0, std::min(255.0, double(pt.g) * gains[1])));
										pt.b = uchar(std::max(0.0, std::min(255.0, double(pt.b) * gains[2])));
									}
								}

								if(mergedClouds->size() == 0)
								{
									*mergedClouds = *cloudWithNormals;
									polygonMesh->polygons = outputPolygons;
								}
								else
								{
									rtabmap::util3d::appendMesh(*mergedClouds, polygonMesh->polygons, *cloudWithNormals, outputPolygons);
								}
							}
							else
							{
								// texture mesh
								unsigned int polygonSize = outputPolygons.front().vertices.size();
								textureMesh->tex_polygons[oi].resize(outputPolygons.size());
								textureMesh->tex_coordinates[oi].resize(outputPolygons.size() * polygonSize);
								for(unsigned int j=0; j<outputPolygons.size(); ++j)
								{
									pcl::Vertices vertices = outputPolygons[j];
									UASSERT(polygonSize == vertices.vertices.size());
									for(unsigned int k=0; k<vertices.vertices.size(); ++k)
									{
										//uv
										UASSERT(vertices.vertices[k] < denseToOrganizedIndices.size());
										int originalVertex = denseToOrganizedIndices[vertices.vertices[k]];
										textureMesh->tex_coordinates[oi][j*vertices.vertices.size()+k] = Eigen::Vector2f(
												float(originalVertex % cloud->width) / float(cloud->width),   // u
												float(cloud->height - originalVertex / cloud->width) / float(cloud->height)); // v

										vertices.vertices[k] += polygonsStep;
									}
									textureMesh->tex_polygons[oi][j] = vertices;

								}
								polygonsStep += outputCloud->size();

								pcl::PointCloud<pcl::PointXYZRGBNormal>::Ptr transformedCloud = rtabmap::util3d::transformPointCloud(cloudWithNormals, iter->second);
								if(mergedClouds->size() == 0)
								{
									*mergedClouds = *transformedCloud;
								}
								else
								{
									*mergedClouds += *transformedCloud;
								}

								textureMesh->tex_materials[oi].tex_illum = 1;
								textureMesh->tex_materials[oi].tex_name = uFormat("material_%d", iter->first);
								textureMesh->tex_materials[oi].tex_file = uNumber2Str(iter->first);
								++oi;
							}
						}
						else
						{
							UERROR("Mesh not found for mesh %d", iter->first);
						}

						if(progressionStatus_.isCanceled())
						{
							if(blockRendering)
							{
								renderingMutex_.unlock();
							}
							exporting_ = false;
							return false;
						}
						progressionStatus_.increment();
					}
					if(textureSize == 0)
					{
						if(mergedClouds->size())
						{
							pcl::toPCLPointCloud2(*mergedClouds, polygonMesh->cloud);
						}
						else
						{
							polygonMesh->polygons.clear();
						}
					}
					else
					{
						textureMesh->tex_materials.resize(oi);
						textureMesh->tex_polygons.resize(oi);

						if(mergedClouds->size())
						{
							pcl::toPCLPointCloud2(*mergedClouds, textureMesh->cloud);
						}
					}
				}

				// end optimized or organized

				if(textureSize>0 && totalPolygons && textureMesh->tex_materials.size())
				{
					LOGI("Merging %d textures...", (int)textureMesh->tex_materials.size());
					globalTextures = rtabmap::util3d::mergeTextures(
							*textureMesh,
							std::map<int, cv::Mat>(),
							std::map<int, std::vector<rtabmap::CameraModel> >(),
							rtabmap_->getMemory(),
							0,
							textureSize,
							textureCount,
							vertexToPixels,
							true, 10.0f, true ,true, 0, 0, 0, false,
							&progressionStatus_);
				}
				if(progressionStatus_.isCanceled())
				{
					if(blockRendering)
					{
						renderingMutex_.unlock();
					}
					exporting_ = false;
					return false;
				}

				progressionStatus_.increment();
			}
			if(totalPolygons)
			{
				if(textureSize == 0)
				{
					UASSERT((int)polygonMesh->polygons.size() == totalPolygons);
					if(polygonMesh->polygons.size())
					{
						// save in database
						pcl::PointCloud<pcl::PointXYZRGBNormal>::Ptr cloud(new pcl::PointCloud<pcl::PointXYZRGBNormal>);
						pcl::fromPCLPointCloud2(polygonMesh->cloud, *cloud);
						cv::Mat cloudMat = rtabmap::compressData2(rtabmap::util3d::laserScanFromPointCloud(*cloud, rtabmap::Transform(), false)); // for database
						std::vector<std::vector<std::vector<unsigned int> > > polygons(1);
						polygons[0].resize(polygonMesh->polygons.size());
						for(unsigned int p=0; p<polygonMesh->polygons.size(); ++p)
						{
							polygons[0][p] = polygonMesh->polygons[p].vertices;
						}
						boost::mutex::scoped_lock  lock(rtabmapMutex_);

						rtabmap_->getMemory()->saveOptimizedMesh(cloudMat, polygons);
						success = true;
					}
				}
				else if(textureMesh->tex_materials.size())
				{
					pcl::PointCloud<pcl::PointNormal>::Ptr cloud(new pcl::PointCloud<pcl::PointNormal>);
					pcl::fromPCLPointCloud2(textureMesh->cloud, *cloud);
					cv::Mat cloudMat = rtabmap::compressData2(rtabmap::util3d::laserScanFromPointCloud(*cloud, rtabmap::Transform(), false)); // for database

					// save in database
					std::vector<std::vector<std::vector<unsigned int> > > polygons(textureMesh->tex_polygons.size());
					for(unsigned int t=0; t<textureMesh->tex_polygons.size(); ++t)
					{
						polygons[t].resize(textureMesh->tex_polygons[t].size());
						for(unsigned int p=0; p<textureMesh->tex_polygons[t].size(); ++p)
						{
							polygons[t][p] = textureMesh->tex_polygons[t][p].vertices;
						}
					}
					boost::mutex::scoped_lock  lock(rtabmapMutex_);
					rtabmap_->getMemory()->saveOptimizedMesh(cloudMat, polygons, textureMesh->tex_coordinates, globalTextures);
					success = true;
				}
				else
				{
					UERROR("Failed exporting texture mesh! There are no textures!");
				}
			}
			else
			{
				UERROR("Failed exporting mesh! There are no polygons!");
			}
		}
		else // Point cloud
		{
			pcl::PointCloud<pcl::PointXYZRGB>::Ptr mergedClouds(new pcl::PointCloud<pcl::PointXYZRGB>);
			for(std::map<int, rtabmap::Transform>::iterator iter=poses.begin();
				iter!= poses.end();
				++iter)
			{
				std::map<int, Mesh>::iterator jter=createdMeshes_.find(iter->first);
				pcl::PointCloud<pcl::PointXYZRGB>::Ptr cloud(new pcl::PointCloud<pcl::PointXYZRGB>);
				pcl::IndicesPtr indices(new std::vector<int>);
				float gains[3];
				gains[0] = gains[1] = gains[2] = 1.0f;
				if(regenerateCloud)
				{
					if(jter != createdMeshes_.end())
					{
						gains[0] = jter->second.gains[0];
						gains[1] = jter->second.gains[1];
						gains[2] = jter->second.gains[2];
					}
					rtabmap::SensorData data = rtabmap_->getMemory()->getNodeData(iter->first, true);
					if(!data.imageRaw().empty() && !data.depthRaw().empty())
					{
						// full resolution
						cloud = rtabmap::util3d::cloudRGBFromSensorData(data, 1, maxCloudDepth_, minCloudDepth_, indices.get());
					}
				}
				else
				{
					if(jter != createdMeshes_.end())
					{
						cloud = jter->second.cloud;
						indices = jter->second.indices;
						gains[0] = jter->second.gains[0];
						gains[1] = jter->second.gains[1];
						gains[2] = jter->second.gains[2];
					}
					else
					{
						rtabmap::SensorData data = rtabmap_->getMemory()->getNodeData(iter->first, true);
						if(!data.imageRaw().empty() && !data.depthRaw().empty())
						{
							cloud = rtabmap::util3d::cloudRGBFromSensorData(data, meshDecimation_, maxCloudDepth_, minCloudDepth_, indices.get());
						}
					}
				}
				if(cloud->size() && indices->size())
				{
					// Convert organized to dense cloud
					pcl::PointCloud<pcl::PointXYZRGB>::Ptr transformedCloud(new pcl::PointCloud<pcl::PointXYZRGB>);
					if(cloudVoxelSize > 0.0f)
					{
						transformedCloud = rtabmap::util3d::voxelize(cloud, indices, cloudVoxelSize);
						transformedCloud = rtabmap::util3d::transformPointCloud(transformedCloud, iter->second);
					}
					else
					{
						// it looks like that using only transformPointCloud with indices
						// flushes the colors, so we should extract points before... maybe a too old PCL version
						pcl::copyPointCloud(*cloud, *indices, *transformedCloud);
						transformedCloud = rtabmap::util3d::transformPointCloud(transformedCloud, iter->second);
					}

					if(gains[0] != 1.0f || gains[1] != 1.0f || gains[2] != 1.0f)
					{
						//LOGD("cloud %d, gain=%f", iter->first, gain);
						for(unsigned int i=0; i<transformedCloud->size(); ++i)
						{
							pcl::PointXYZRGB & pt = transformedCloud->at(i);
							//LOGI("color %d = %d %d %d", i, (int)pt.r, (int)pt.g, (int)pt.b);
							pt.r = uchar(std::max(0.0, std::min(255.0, double(pt.r) * gains[0])));
							pt.g = uchar(std::max(0.0, std::min(255.0, double(pt.g) * gains[1])));
							pt.b = uchar(std::max(0.0, std::min(255.0, double(pt.b) * gains[2])));

						}
					}

					if(mergedClouds->size() == 0)
					{
						*mergedClouds = *transformedCloud;
					}
					else
					{
						*mergedClouds += *transformedCloud;
					}
				}

				if(progressionStatus_.isCanceled())
				{
					if(blockRendering)
					{
						renderingMutex_.unlock();
					}
					exporting_ = false;
					return false;
				}
				progressionStatus_.increment();
			}

			if(mergedClouds->size())
			{
				if(cloudVoxelSize > 0.0f)
				{
					mergedClouds = rtabmap::util3d::voxelize(mergedClouds, cloudVoxelSize);
				}

				// save in database
				{
					cv::Mat cloudMat = rtabmap::compressData2(rtabmap::util3d::laserScanFromPointCloud(*mergedClouds)); // for database
					boost::mutex::scoped_lock  lock(rtabmapMutex_);
					rtabmap_->getMemory()->saveOptimizedMesh(cloudMat);
					success = true;
				}
			}
			else
			{
				UERROR("Merged cloud is empty!");
			}
		}

		progressionStatus_.finish();

		if(blockRendering)
		{
			renderingMutex_.unlock();
		}
	}
	catch (std::exception & e)
	{
		UERROR("Out of memory! %s", e.what());

		if(blockRendering)
		{
			renderingMutex_.unlock();
		}

		success = false;
	}
	exporting_ = false;

	optRefId_ = 0;
	if(optRefPose_)
	{
		delete optRefPose_;
		optRefPose_ = 0;
	}
	if(success && poses.size())
	{
		// for optimized mesh
		// just take the last as reference
		optRefId_ = poses.rbegin()->first;
		optRefPose_ = new rtabmap::Transform(poses.rbegin()->second);
	}

	return success;
}

bool RTABMapApp::postExportation(bool visualize)
{
	LOGI("postExportation(visualize=%d)", visualize?1:0);
	optMesh_.reset(new pcl::TextureMesh);
	optTexture_ = cv::Mat();
	exportedMeshUpdated_ = false;

	if(visualize)
	{
		visualizingMesh_ = false;
		cv::Mat cloudMat;
		std::vector<std::vector<std::vector<unsigned int> > > polygons;
#if PCL_VERSION_COMPARE(>=, 1, 8, 0)
		std::vector<std::vector<Eigen::Vector2f, Eigen::aligned_allocator<Eigen::Vector2f> > > texCoords;
#else
		std::vector<std::vector<Eigen::Vector2f> > texCoords;
#endif
		cv::Mat textures;
		if(rtabmap_ && rtabmap_->getMemory())
		{
			cloudMat = rtabmap_->getMemory()->loadOptimizedMesh(&polygons, &texCoords, &textures);
			if(!cloudMat.empty())
			{
				LOGI("postExportation: Found optimized mesh! Visualizing it.");
				optMesh_ = rtabmap::util3d::assembleTextureMesh(cloudMat, polygons, texCoords, textures, true);
				optTexture_ = textures;

				boost::mutex::scoped_lock  lock(renderingMutex_);
				visualizingMesh_ = true;
				exportedMeshUpdated_ = true;
			}
			else
			{
				LOGI("postExportation: No optimized mesh found.");
			}
		}
	}
	else if(visualizingMesh_)
	{
		rtabmapMutex_.lock();
		if(!rtabmap_->getLocalOptimizedPoses().empty())
		{
			rtabmap::Statistics stats;
			stats.setPoses(rtabmap_->getLocalOptimizedPoses());
			rtabmapEvents_.push_back(new rtabmap::RtabmapEvent(stats));
		}
		rtabmapMutex_.unlock();

		visualizingMesh_ = false;
	}

	return visualizingMesh_;
}

bool RTABMapApp::writeExportedMesh(const std::string & directory, const std::string & name)
{
	LOGI("writeExportedMesh: dir=%s name=%s", directory.c_str(), name.c_str());
	exporting_ = true;

	bool success = false;

	pcl::PolygonMesh::Ptr polygonMesh(new pcl::PolygonMesh);
	pcl::TextureMesh::Ptr textureMesh(new pcl::TextureMesh);
	cv::Mat cloudMat;
	std::vector<std::vector<std::vector<unsigned int> > > polygons;
#if PCL_VERSION_COMPARE(>=, 1, 8, 0)
	std::vector<std::vector<Eigen::Vector2f, Eigen::aligned_allocator<Eigen::Vector2f> > > texCoords;
#else
	std::vector<std::vector<Eigen::Vector2f> > texCoords;
#endif
	cv::Mat textures;
	if(rtabmap_ && rtabmap_->getMemory())
	{
		cloudMat = rtabmap_->getMemory()->loadOptimizedMesh(&polygons, &texCoords, &textures);
		if(!cloudMat.empty())
		{
			LOGI("writeExportedMesh: Found optimized mesh!");
			if(textures.empty())
			{
				polygonMesh = rtabmap::util3d::assemblePolygonMesh(cloudMat, polygons.size() == 1?polygons[0]:std::vector<std::vector<unsigned int> >());
			}
			else
			{
				textureMesh = rtabmap::util3d::assembleTextureMesh(cloudMat, polygons, texCoords, textures, false);
			}
		}
		else
		{
			LOGI("writeExportedMesh: No optimized mesh found.");
		}
	}

	if(polygonMesh->cloud.data.size())
	{
		// Point cloud PLY
		std::string filePath = directory + UDirectory::separator() + name + ".ply";
		LOGI("Saving ply (%d vertices, %d polygons) to %s.", (int)polygonMesh->cloud.data.size()/polygonMesh->cloud.point_step, (int)polygonMesh->polygons.size(), filePath.c_str());
		success = pcl::io::savePLYFileBinary(filePath, *polygonMesh) == 0;
		if(success)
		{
			LOGI("Saved ply to %s!", filePath.c_str());
		}
		else
		{
			UERROR("Failed saving ply to %s!", filePath.c_str());
		}
	}
	else if(textureMesh->cloud.data.size())
	{
		// TextureMesh OBJ
		LOGD("Saving texture(s) (%d)", textures.empty()?0:textures.cols/textures.rows);
		UASSERT(textures.empty() || textures.cols % textures.rows == 0);
		UASSERT((int)textureMesh->tex_materials.size() == textures.cols/textures.rows);
		for(unsigned int i=0; i<textureMesh->tex_materials.size(); ++i)
		{
			std::string baseNameNum = name;
			if(textureMesh->tex_materials.size()>1)
			{
				baseNameNum+=uNumber2Str(i);
			}
			std::string fullPath = directory+UDirectory::separator()+baseNameNum+".jpg";
			textureMesh->tex_materials[i].tex_file = baseNameNum+".jpg";
			LOGI("Saving texture to %s.", fullPath.c_str());
			success = cv::imwrite(fullPath, textures(cv::Range::all(), cv::Range(i*textures.rows, (i+1)*textures.rows)));
			if(!success)
			{
				LOGI("Failed saving %s!", fullPath.c_str());
			}
			else
			{
				LOGI("Saved %s.", fullPath.c_str());
			}
		}

		if(success)
		{
			// With Sketchfab, the OBJ models are rotated 90 degrees on x axis, so rotate -90 to have model in right position
			pcl::PointCloud<pcl::PointNormal>::Ptr cloud(new pcl::PointCloud<pcl::PointNormal>);
			pcl::fromPCLPointCloud2(textureMesh->cloud, *cloud);
			cloud = rtabmap::util3d::transformPointCloud(cloud, rtabmap::Transform(1,0,0,0, 0,0,1,0, 0,-1,0,0));
			pcl::toPCLPointCloud2(*cloud, textureMesh->cloud);
			std::string filePath = directory + UDirectory::separator() + name + ".obj";
			int totalPolygons = 0;
			for(unsigned int i=0;i<textureMesh->tex_polygons.size(); ++i)
			{
				totalPolygons += textureMesh->tex_polygons[i].size();
			}
			LOGI("Saving obj (%d vertices, %d polygons) to %s.", (int)textureMesh->cloud.data.size()/textureMesh->cloud.point_step, totalPolygons, filePath.c_str());
			success = pcl::io::saveOBJFile(filePath, *textureMesh) == 0;

			if(success)
			{
				LOGI("Saved obj to %s!", filePath.c_str());
			}
			else
			{
				UERROR("Failed saving obj to %s!", filePath.c_str());
			}
		}
	}
	exporting_ = false;
	return success;
}

int RTABMapApp::postProcessing(int approach)
{
	postProcessing_ = true;
	LOGI("postProcessing(%d)", approach);
	int returnedValue = 0;
	if(rtabmap_)
	{
		std::map<int, rtabmap::Transform> poses;
		std::multimap<int, rtabmap::Link> links;

		// detect more loop closures
		if(approach == -1 || approach == 2)
		{
			if(approach == -1)
			{
				progressionStatus_.reset(6);
			}
			returnedValue = rtabmap_->detectMoreLoopClosures(1.0f, M_PI/6.0f, approach == -1?5:1, true, true, approach==-1?&progressionStatus_:0);
			if(approach == -1 && progressionStatus_.isCanceled())
			{
				postProcessing_ = false;
				return -1;
			}
		}

		// graph optimization
		if(returnedValue >=0)
		{
			if (approach == 1)
			{
				if(rtabmap::Optimizer::isAvailable(rtabmap::Optimizer::kTypeG2O))
				{
					std::map<int, rtabmap::Signature> signatures;
					rtabmap_->getGraph(poses, links, true, true, &signatures);

					rtabmap::ParametersMap param;
					param.insert(rtabmap::ParametersPair(rtabmap::Parameters::kOptimizerIterations(), "30"));
					param.insert(rtabmap::ParametersPair(rtabmap::Parameters::kOptimizerEpsilon(), "0"));
					rtabmap::Optimizer * sba = rtabmap::Optimizer::create(rtabmap::Optimizer::kTypeG2O, param);
					poses = sba->optimizeBA(poses.rbegin()->first, poses, links, signatures);
					delete sba;
				}
				else
				{
					UERROR("g2o not available!");
				}
			}
			else if(approach!=4 && approach!=5 && approach != 7)
			{
				// simple graph optmimization
				rtabmap_->getGraph(poses, links, true, true);
			}
		}

		if(poses.size())
		{
			boost::mutex::scoped_lock  lock(rtabmapMutex_);
			rtabmap::Statistics stats = rtabmap_->getStatistics();
			stats.setPoses(poses);
			stats.setConstraints(links);

			LOGI("PostProcessing, sending rtabmap event to update graph...");
			rtabmapEvents_.push_back(new rtabmap::RtabmapEvent(stats));

			rtabmap_->setOptimizedPoses(poses);
		}
		else if(approach!=4 && approach!=5 && approach != 7)
		{
			returnedValue = -1;
		}

		if(returnedValue >=0)
		{
			boost::mutex::scoped_lock  lock(renderingMutex_);
			// filter polygons
			if(approach == -1 || approach == 4)
			{
				filterPolygonsOnNextRender_ = true;
			}

			// gain compensation
			if(approach == -1 || approach == 5 || approach == 6)
			{
				gainCompensationOnNextRender_ = approach == 6 ? 2 : 1; // 2 = full, 1 = fast
			}

			// bilateral filtering
			if(approach == 7)
			{
				bilateralFilteringOnNextRender_ = true;
			}
		}
	}

	postProcessing_ = false;
	return returnedValue;
}

bool RTABMapApp::handleEvent(UEvent * event)
{
	if(camera_ && camera_->isRunning())
	{
		// called from events manager thread, so protect the data
		if(event->getClassName().compare("OdometryEvent") == 0)
		{
			LOGI("Received OdometryEvent!");
			if(odomMutex_.try_lock())
			{
				odomEvents_.clear();
				if(camera_->isRunning())
				{
					odomEvents_.push_back(*((rtabmap::OdometryEvent*)(event)));
				}
				odomMutex_.unlock();
			}
		}
		if(status_.first == rtabmap::RtabmapEventInit::kInitialized &&
		   event->getClassName().compare("RtabmapEvent") == 0)
		{
			LOGI("Received RtabmapEvent event!");
			if(camera_->isRunning())
			{
				if(visualizingMesh_)
				{
					boost::mutex::scoped_lock lock(visLocalizationMutex_);
					visLocalizationEvents_.push_back((rtabmap::RtabmapEvent*)event);
				}
				else
				{
					boost::mutex::scoped_lock lock(rtabmapMutex_);
					rtabmapEvents_.push_back((rtabmap::RtabmapEvent*)event);
				}
				return true;
			}
		}
	}

	if(event->getClassName().compare("PoseEvent") == 0)
	{
		if(poseMutex_.try_lock())
		{
			poseEvents_.clear();
			poseEvents_.push_back(((rtabmap::PoseEvent*)event)->pose());
			poseMutex_.unlock();
		}
	}

	if(event->getClassName().compare("CameraTangoEvent") == 0)
	{
		rtabmap::CameraTangoEvent * tangoEvent = (rtabmap::CameraTangoEvent*)event;

		// Call JAVA callback with tango event msg
		bool success = false;
		if(jvm && RTABMapActivity)
		{
			JNIEnv *env = 0;
			jint rs = jvm->AttachCurrentThread(&env, NULL);
			if(rs == JNI_OK && env)
			{
				jclass clazz = env->GetObjectClass(RTABMapActivity);
				if(clazz)
				{
					jmethodID methodID = env->GetMethodID(clazz, "tangoEventCallback", "(ILjava/lang/String;Ljava/lang/String;)V" );
					if(methodID)
					{
						env->CallVoidMethod(RTABMapActivity, methodID,
								tangoEvent->type(),
								env->NewStringUTF(tangoEvent->key().c_str()),
								env->NewStringUTF(tangoEvent->value().c_str()));
						success = true;
					}
				}
			}
			jvm->DetachCurrentThread();
		}
		if(!success)
		{
			UERROR("Failed to call RTABMapActivity::tangoEventCallback");
		}
	}

	if(event->getClassName().compare("RtabmapEventInit") == 0)
	{
		LOGI("Received RtabmapEventInit!");
		status_.first = ((rtabmap::RtabmapEventInit*)event)->getStatus();
		status_.second = ((rtabmap::RtabmapEventInit*)event)->getInfo();

		// Call JAVA callback with init msg
		bool success = false;
		if(jvm && RTABMapActivity)
		{
			JNIEnv *env = 0;
			jint rs = jvm->AttachCurrentThread(&env, NULL);
			if(rs == JNI_OK && env)
			{
				jclass clazz = env->GetObjectClass(RTABMapActivity);
				if(clazz)
				{
					jmethodID methodID = env->GetMethodID(clazz, "rtabmapInitEventCallback", "(ILjava/lang/String;)V" );
					if(methodID)
					{
						env->CallVoidMethod(RTABMapActivity, methodID,
								status_.first,
								env->NewStringUTF(status_.second.c_str()));
						success = true;
					}
				}
			}
			jvm->DetachCurrentThread();
		}
		if(!success)
		{
			UERROR("Failed to call RTABMapActivity::rtabmapInitEventsCallback");
		}
	}

	if(event->getClassName().compare("PostRenderEvent") == 0)
	{
		LOGI("Received PostRenderEvent!");

		int loopClosureId = 0;
		int featuresExtracted = 0;
		if(((PostRenderEvent*)event)->getRtabmapEvent())
		{
			const rtabmap::Statistics & stats = ((PostRenderEvent*)event)->getRtabmapEvent()->getStats();
			loopClosureId = stats.loopClosureId()>0?stats.loopClosureId():stats.proximityDetectionId()>0?stats.proximityDetectionId():0;
			featuresExtracted = stats.getLastSignatureData().getWords().size();

			uInsert(bufferedStatsData_, std::make_pair<std::string, float>(rtabmap::Statistics::kMemoryWorking_memory_size(), uValue(stats.data(), rtabmap::Statistics::kMemoryWorking_memory_size(), 0.0f)));
			uInsert(bufferedStatsData_, std::make_pair<std::string, float>(rtabmap::Statistics::kMemoryShort_time_memory_size(), uValue(stats.data(), rtabmap::Statistics::kMemoryShort_time_memory_size(), 0.0f)));
			uInsert(bufferedStatsData_, std::make_pair<std::string, float>(rtabmap::Statistics::kKeypointDictionary_size(), uValue(stats.data(), rtabmap::Statistics::kKeypointDictionary_size(), 0.0f)));
			uInsert(bufferedStatsData_, std::make_pair<std::string, float>(rtabmap::Statistics::kTimingTotal(), uValue(stats.data(), rtabmap::Statistics::kTimingTotal(), 0.0f)));
			uInsert(bufferedStatsData_, std::make_pair<std::string, float>(rtabmap::Statistics::kLoopHighest_hypothesis_id(), uValue(stats.data(), rtabmap::Statistics::kLoopHighest_hypothesis_id(), 0.0f)));
			uInsert(bufferedStatsData_, std::make_pair<std::string, float>(rtabmap::Statistics::kMemoryDatabase_memory_used(), uValue(stats.data(), rtabmap::Statistics::kMemoryDatabase_memory_used(), 0.0f)));
			uInsert(bufferedStatsData_, std::make_pair<std::string, float>(rtabmap::Statistics::kLoopVisual_inliers(), uValue(stats.data(), rtabmap::Statistics::kLoopVisual_inliers(), 0.0f)));
			uInsert(bufferedStatsData_, std::make_pair<std::string, float>(rtabmap::Statistics::kLoopVisual_matches(), uValue(stats.data(), rtabmap::Statistics::kLoopVisual_matches(), 0.0f)));
			uInsert(bufferedStatsData_, std::make_pair<std::string, float>(rtabmap::Statistics::kLoopRejectedHypothesis(), uValue(stats.data(), rtabmap::Statistics::kLoopRejectedHypothesis(), 0.0f)));
			uInsert(bufferedStatsData_, std::make_pair<std::string, float>(rtabmap::Statistics::kLoopOptimization_max_error(), uValue(stats.data(), rtabmap::Statistics::kLoopOptimization_max_error(), 0.0f)));
			uInsert(bufferedStatsData_, std::make_pair<std::string, float>(rtabmap::Statistics::kLoopOptimization_max_error_ratio(), uValue(stats.data(), rtabmap::Statistics::kLoopOptimization_max_error_ratio(), 0.0f)));
			uInsert(bufferedStatsData_, std::make_pair<std::string, float>(rtabmap::Statistics::kMemoryRehearsal_sim(), uValue(stats.data(), rtabmap::Statistics::kMemoryRehearsal_sim(), 0.0f)));
			uInsert(bufferedStatsData_, std::make_pair<std::string, float>(rtabmap::Statistics::kLoopHighest_hypothesis_value(), uValue(stats.data(), rtabmap::Statistics::kLoopHighest_hypothesis_value(), 0.0f)));
			uInsert(bufferedStatsData_, std::make_pair<std::string, float>(rtabmap::Statistics::kMemoryDistance_travelled(), uValue(stats.data(), rtabmap::Statistics::kMemoryDistance_travelled(), 0.0f)));
			uInsert(bufferedStatsData_, std::make_pair<std::string, float>(rtabmap::Statistics::kMemoryFast_movement(), uValue(stats.data(), rtabmap::Statistics::kMemoryFast_movement(), 0.0f)));
			uInsert(bufferedStatsData_, std::make_pair<std::string, float>(rtabmap::Statistics::kLoopLandmark_detected(), uValue(stats.data(), rtabmap::Statistics::kLoopLandmark_detected(), 0.0f)));
		}
		// else use last data

		int nodes = (int)uValue(bufferedStatsData_, rtabmap::Statistics::kMemoryWorking_memory_size(), 0.0f) +
				uValue(bufferedStatsData_, rtabmap::Statistics::kMemoryShort_time_memory_size(), 0.0f);
		int words = (int)uValue(bufferedStatsData_, rtabmap::Statistics::kKeypointDictionary_size(), 0.0f);
		float updateTime = uValue(bufferedStatsData_, rtabmap::Statistics::kTimingTotal(), 0.0f);
		int highestHypId = (int)uValue(bufferedStatsData_, rtabmap::Statistics::kLoopHighest_hypothesis_id(), 0.0f);
		int databaseMemoryUsed = (int)uValue(bufferedStatsData_, rtabmap::Statistics::kMemoryDatabase_memory_used(), 0.0f);
		int inliers = (int)uValue(bufferedStatsData_, rtabmap::Statistics::kLoopVisual_inliers(), 0.0f);
		int matches = (int)uValue(bufferedStatsData_, rtabmap::Statistics::kLoopVisual_matches(), 0.0f);
		int rejected = (int)uValue(bufferedStatsData_, rtabmap::Statistics::kLoopRejectedHypothesis(), 0.0f);
		float optimizationMaxError = uValue(bufferedStatsData_, rtabmap::Statistics::kLoopOptimization_max_error(), 0.0f);
		float optimizationMaxErrorRatio = uValue(bufferedStatsData_, rtabmap::Statistics::kLoopOptimization_max_error_ratio(), 0.0f);
		float rehearsalValue = uValue(bufferedStatsData_, rtabmap::Statistics::kMemoryRehearsal_sim(), 0.0f);
		float hypothesis = uValue(bufferedStatsData_, rtabmap::Statistics::kLoopHighest_hypothesis_value(), 0.0f);
		float distanceTravelled = uValue(bufferedStatsData_, rtabmap::Statistics::kMemoryDistance_travelled(), 0.0f);
		int fastMovement = (int)uValue(bufferedStatsData_, rtabmap::Statistics::kMemoryFast_movement(), 0.0f);
		int landmarkDetected = (int)uValue(bufferedStatsData_, rtabmap::Statistics::kLoopLandmark_detected(), 0.0f);
		rtabmap::Transform currentPose = main_scene_.GetCameraPose();
                glm::quat targetRotation = main_scene_.getPanoRotation();
                Eigen::Quaternionf cameraRotation = currentPose.getQuaternionf();
		float x=0.0f,y=0.0f,z=0.0f,roll=0.0f,pitch=0.0f,yaw=0.0f, qw=0.0f, qx=0.0f, qy=0.0f, qz=0.0f;
		float target_x=0.0f, target_y=0.0f,target_z=0.0f, target_qw=0.0f, target_qx=0.0f, target_qy=0.0f, target_qz=0.0f;
		if(!currentPose.isNull())
		{
			currentPose.getTranslationAndEulerAngles(x,y,z,roll,pitch,yaw);
                        qw = cameraRotation.w();
                        qx = cameraRotation.x();
                        qy = cameraRotation.y();
                        qz = cameraRotation.z();
                        target_qw = targetRotation[0];
                        target_qx = targetRotation[1];
                        target_qy = targetRotation[2];
                        target_qz = targetRotation[3];
                         
                }

		// Call JAVA callback with some stats
		UINFO("Send statistics to GUI");
		bool success = false;
		if(jvm && RTABMapActivity)
		{
			JNIEnv *env = 0;
			jint rs = jvm->AttachCurrentThread(&env, NULL);
			if(rs == JNI_OK && env)
			{
				jclass clazz = env->GetObjectClass(RTABMapActivity);
				if(clazz)
				{
					jmethodID methodID = env->GetMethodID(clazz, "updateStatsCallback", "(IIIIFIIIIIIFIFIFFFFIIFFFFFFFFFFFFFFFFF)V" );
					if(methodID)
					{
						env->CallVoidMethod(RTABMapActivity, methodID,
								nodes,
								words,
								totalPoints_,
								totalPolygons_,
								updateTime,
								loopClosureId,
								highestHypId,
								databaseMemoryUsed,
								inliers,
								matches,
								featuresExtracted,
								hypothesis,
								lastDrawnCloudsCount_,
								renderingTime_>0.0f?1.0f/renderingTime_:0.0f,
								rejected,
								rehearsalValue,
								optimizationMaxError,
								optimizationMaxErrorRatio,
								distanceTravelled,
								fastMovement,
								landmarkDetected,
								x,
								y,
								z,
								qw,
								qx,
								qy,
                                                                qz,
                                                                roll,
                                                                pitch,
                                                                yaw,
                                                                target_x,
								target_y,
								target_z,
								target_qw,
								target_qx,
								target_qy,
                                                                target_qz);
						success = true;
					}
				}
			}
			jvm->DetachCurrentThread();
		}
		if(!success)
		{
			UERROR("Failed to call RTABMapActivity::updateStatsCallback");
		}
		renderingTime_ = 0.0f;
	}
	return false;
}

