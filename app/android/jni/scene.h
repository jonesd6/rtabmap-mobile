/*
 * Copyright 2014 Google Inc. All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef TANGO_POINT_CLOUD_SCENE_H_
#define TANGO_POINT_CLOUD_SCENE_H_

#include <jni.h>
#include <memory>
#include <set>

#include <tango_client_api.h>  // NOLINT
#include <tango-gl/axis.h>
#include <tango-gl/camera.h>
#include <tango-gl/color.h>
#include <tango-gl/gesture_camera.h>
#include <tango-gl/grid.h>
#include <tango-gl/frustum.h>
#include <tango-gl/trace.h>
#include <tango-gl/transform.h>
#include <tango-gl/util.h>
#include <tango-gl/circle.h>

#include <rtabmap/core/Transform.h>
#include <rtabmap/core/Link.h>

#include <point_cloud_drawable.h>
#include <graph_drawable.h>
#include <bounding_box_drawable.h>

#include <pcl/point_cloud.h>
#include <pcl/point_types.h>

// Scene provides OpenGL drawable objects and renders them for visualization.
class Scene {
 public:
  // Constructor and destructor.
  Scene();
  ~Scene();

  // Allocate OpenGL resources for rendering.
  void InitGLContent();

  // Release non-OpenGL allocated resources.
  void DeleteResources();

  // Setup GL view port.
  void SetupViewPort(int w, int h);
  int getViewPortWidth() const {return screenWidth_;}
  int getViewPortHeight() const {return screenHeight_;}

  void setScreenRotation(TangoSupportRotation colorCameraToDisplayRotation) {color_camera_to_display_rotation_ = colorCameraToDisplayRotation;}

  void clear(); // removed all point clouds

  // Render loop.
  // @param: cur_pose_transformation, latest pose's transformation.
  // @param: point_cloud_transformation, pose transformation at point cloud
  //         frame's timestamp.
  // @param: point_cloud_vertices, point cloud's vertices of the current point
  //         frame.
  int Render();

  // Set render camera's viewing angle, first person, third person or top down.
  //
  // @param: camera_type, camera type includes first person, third person and
  //         top down
  void SetCameraType(tango_gl::GestureCamera::CameraType camera_type);

  void SetCameraPose(const rtabmap::Transform & pose);
  void SetTargetPose(const rtabmap::Transform & target_pose);
  rtabmap::Transform GetCameraPose() const {return currentPose_!=0?*currentPose_:rtabmap::Transform();}
  rtabmap::Transform GetTargetPose() const {return targetPose_!=0?*targetPose_:rtabmap::Transform();}
  rtabmap::Transform GetOpenGLCameraPose(float * fov = 0) const;

  // Touch event passed from android activity. This function only support two
  // touches.
  //
  // @param: touch_count, total count for touches.
  // @param: event, touch event of current touch.
  // @param: x0, normalized touch location for touch 0 on x axis.
  // @param: y0, normalized touch location for touch 0 on y axis.
  // @param: x1, normalized touch location for touch 1 on x axis.
  // @param: y1, normalized touch location for touch 1 on y axis.
  void OnTouchEvent(int touch_count, tango_gl::GestureCamera::TouchEvent event,
                    float x0, float y0, float x1, float y1);

  void updateGraph(
  		const std::map<int, rtabmap::Transform> & poses,
  		const std::multimap<int, rtabmap::Link> & links);

  void setGraphVisible(bool visible);
  void setGridVisible(bool visible);
  void setTraceVisible(bool visible);

  void addMarker(int id, const rtabmap::Transform & pose);
  void setMarkerPose(int id, const rtabmap::Transform & pose);
  bool hasMarker(int id) const;
  void removeMarker(int id);
  std::set<int> getAddedMarkers() const;

  void addCloud(
  		  int id,
  		  const pcl::PointCloud<pcl::PointXYZRGB>::Ptr & cloud,
		  const pcl::IndicesPtr & indices,
  		  const rtabmap::Transform & pose);
  void addMesh(
  		int id,
  		const Mesh & mesh,
  		const rtabmap::Transform & pose,
		bool createWireframe = false);

  void setCloudPose(int id, const rtabmap::Transform & pose);
  void setCloudVisible(int id, bool visible);
  bool hasCloud(int id) const;
  bool hasMesh(int id) const;
  bool hasTexture(int id) const;
  std::set<int> getAddedClouds() const;
  void updateCloudPolygons(int id, const std::vector<pcl::Vertices> & polygons);
  void updateMesh(int id, const Mesh & mesh);
  void updateGains(int id, float gainR, float gainG, float gainB);

  void setBlending(bool enabled) {blending_ = enabled;}
  void setMapRendering(bool enabled) {mapRendering_ = enabled;}
  void setMeshRendering(bool enabled, bool withTexture) {meshRendering_ = enabled; meshRenderingTexture_ = withTexture;}
  void setPointSize(float size) {pointSize_ = size;}
  void setFOV(float angle);
  void setOrthoCropFactor(float value);
  void setFirstPanoPosition();
  void rotatePanoPosition();
  glm::quat getPanoRotation();
  void setGridRotation(float angleDeg);
  void setLighting(bool enabled) {lighting_ = enabled;}
  void setBackfaceCulling(bool enabled) {backfaceCulling_ = enabled;}
  void setWireframe(bool enabled) {wireFrame_ = enabled;}
  void setBackgroundColor(float r, float g, float b) {r_=r; g_=g; b_=b;} // 0.0f <> 1.0f
  void setGridColor(float r, float g, float b);

  bool isBlending() const {return blending_;}
  bool isMapRendering() const {return mapRendering_;}
  bool isMeshRendering() const {return meshRendering_;}
  bool isMeshTexturing() const {return meshRendering_ && meshRenderingTexture_;}
  float getPointSize() const {return pointSize_;}
  bool isLighting() const {return lighting_;}
  bool isBackfaceCulling() const {return backfaceCulling_;}

 private:
  // Camera object that allows user to use touch input to interact with.
  tango_gl::GestureCamera* gesture_camera_;

  // Device axis (in device frame of reference).
  tango_gl::Axis* axis_;

  // Device frustum.
  tango_gl::Frustum* frustum_;

  // Ground grid.
  tango_gl::Grid* grid_;

  // Target lock.
  tango_gl::Circle* target_lock_;

  // Bounding box
  BoundingBoxDrawable * box_;

  // Trace of pose data.
  tango_gl::Trace* trace_;
  GraphDrawable * graph_;
  bool graphVisible_;
  bool gridVisible_;
  bool traceVisible_;

  std::map<int, tango_gl::Axis*> markers_;

  TangoSupportRotation color_camera_to_display_rotation_;

  std::map<int, PointCloudDrawable*> pointClouds_;

  rtabmap::Transform * currentPose_;
  rtabmap::Transform * targetPose_;

  // Shader to display point cloud.
  GLuint graph_shader_program_;

  bool blending_;
  bool mapRendering_;
  bool meshRendering_;
  bool meshRenderingTexture_;
  float pointSize_;
  bool boundingBoxRendering_;
  bool lighting_;
  bool backfaceCulling_;
  bool wireFrame_;
  float r_;
  float g_;
  float b_;
  GLuint fboId_;
  GLuint depthTexture_;
  GLsizei screenWidth_;
  GLsizei screenHeight_;
  bool doubleTapOn_;
  cv::Point2f doubleTapPos_;
};

#endif  // TANGO_POINT_CLOUD_SCENE_H_
