/*
 * Copyright (c) 2014-2018, the neonavigation authors
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of the copyright holder nor the names of its 
 *       contributors may be used to endorse or promote products derived from 
 *       this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <ros/ros.h>
#include <geometry_msgs/PolygonStamped.h>
#include <nav_msgs/OccupancyGrid.h>
#include <sensor_msgs/PointCloud.h>

#include <string>
#include <vector>

#include <costmap_cspace/node_handle_float.h>
#include <costmap_cspace/CSpace3D.h>
#include <costmap_cspace/CSpace3DUpdate.h>

#include <costmap_3d.h>

class Costmap3DOFNode
{
protected:
  ros::NodeHandle_f nh_;
  ros::NodeHandle_f nhp_;
  ros::Subscriber sub_map_;
  ros::Subscriber sub_map_overlay_;
  ros::Publisher pub_costmap_;
  ros::Publisher pub_costmap_update_;
  ros::Publisher pub_footprint_;
  ros::Publisher pub_debug_;
  ros::Timer timer_footprint_;

  costmap_cspace::Costmap3d::Ptr costmap_;

  void cbMap(
      const nav_msgs::OccupancyGrid::ConstPtr &msg,
      const costmap_cspace::Costmap3dLayerBase::Ptr map)
  {
    if (map->getAngularGrid() <= 0)
    {
      ROS_ERROR("ang_resolution is not set.");
      std::runtime_error("ang_resolution is not set.");
    }
    ROS_INFO("2D costmap received");

    map->setBaseMap(*msg);
    ROS_DEBUG("C-Space costmap generated");

    auto map_msg = map->getMap();
    pub_costmap_.publish<costmap_cspace::CSpace3D>(*map_msg);
    publishDebug(*map_msg);
  }
  void cbMapOverlay(
      const nav_msgs::OccupancyGrid::ConstPtr &msg,
      const costmap_cspace::Costmap3dLayerBase::Ptr map)
  {
    ROS_DEBUG("Overlay 2D costmap received");

    auto map_msg = map->getMap();
    if (map_msg->header.frame_id != msg->header.frame_id)
    {
      ROS_ERROR("map and map_overlay must have same frame_id");
      return;
    }
    if (map_msg->info.width < 1 ||
        map_msg->info.height < 1)
      return;

    map->processMapOverlay(*msg);
    ROS_DEBUG("C-Space costmap updated");
  }
  bool cbUpdate(
      const costmap_cspace::CSpace3DMsg::Ptr map,
      const costmap_cspace::CSpace3DUpdate::Ptr update)
  {
    publishDebug(*map);
    pub_costmap_update_.publish(*update);
    return true;
  }
  void publishDebug(const costmap_cspace::CSpace3D &map)
  {
    sensor_msgs::PointCloud pc;
    pc.header = map.header;
    pc.header.stamp = ros::Time::now();
    for (size_t yaw = 0; yaw < map.info.angle; yaw++)
    {
      for (unsigned int i = 0; i < map.info.width * map.info.height; i++)
      {
        int gx = i % map.info.width;
        int gy = i / map.info.width;
        if (map.data[i + yaw * map.info.width * map.info.height] < 100)
          continue;
        geometry_msgs::Point32 p;
        p.x = gx * map.info.linear_resolution + map.info.origin.position.x;
        p.y = gy * map.info.linear_resolution + map.info.origin.position.y;
        p.z = yaw * 0.1;
        pc.points.push_back(p);
      }
    }
    pub_debug_.publish(pc);
  }
  void cbPublishFootprint(const ros::TimerEvent &event, const geometry_msgs::PolygonStamped msg)
  {
    auto footprint = msg;
    footprint.header.stamp = ros::Time::now();
    pub_footprint_.publish(footprint);
  }

public:
  Costmap3DOFNode()
    : nh_()
    , nhp_("~")
  {
    int ang_resolution;
    float linear_expand;
    float linear_spread;
    costmap_cspace::Costmap3dLayerFootprint::map_overlay_mode overlay_mode;
    nhp_.param("ang_resolution", ang_resolution, 16);
    nhp_.param("linear_expand", linear_expand, 0.2f);
    nhp_.param("linear_spread", linear_spread, 0.5f);
    std::string overlay_mode_str;
    nhp_.param("overlay_mode", overlay_mode_str, std::string("max"));
    if (overlay_mode_str.compare("overwrite") == 0)
    {
      overlay_mode = costmap_cspace::Costmap3dLayerFootprint::map_overlay_mode::OVERWRITE;
    }
    else if (overlay_mode_str.compare("max") == 0)
    {
      overlay_mode = costmap_cspace::Costmap3dLayerFootprint::map_overlay_mode::MAX;
    }
    else
    {
      ROS_ERROR("Unknown overlay_mode \"%s\"", overlay_mode_str.c_str());
      throw std::runtime_error("Unknown overlay_mode.");
    }
    ROS_INFO("costmap_3d: %s mode", overlay_mode_str.c_str());

    XmlRpc::XmlRpcValue footprint_xml;
    if (!nhp_.hasParam("footprint"))
    {
      ROS_FATAL("Footprint doesn't specified");
      throw std::runtime_error("Footprint doesn't specified.");
    }
    nhp_.getParam("footprint", footprint_xml);
    costmap_cspace::Polygon footprint;
    try
    {
      footprint = costmap_cspace::Polygon(footprint_xml);
    }
    catch (const std::exception &e)
    {
      ROS_FATAL("Invalid footprint");
      throw e;
    }

    costmap_.reset(new costmap_cspace::Costmap3d(
        ang_resolution, linear_expand, linear_spread, footprint));
    auto root_layer = costmap_->addRootLayer<costmap_cspace::Costmap3dLayerFootprint>();
    sub_map_ = nh_.subscribe<nav_msgs::OccupancyGrid>(
        "map", 1,
        boost::bind(&Costmap3DOFNode::cbMap, this, _1, root_layer));

    auto layer = costmap_->addLayer<costmap_cspace::Costmap3dLayerFootprint>(overlay_mode);
    sub_map_overlay_ = nh_.subscribe<nav_msgs::OccupancyGrid>(
        "map_overlay", 1,
        boost::bind(&Costmap3DOFNode::cbMapOverlay, this, _1, layer));

    auto end_layer = costmap_->addLayer<costmap_cspace::Costmap3dLayerOutput>();
    end_layer->setHandler(boost::bind(&Costmap3DOFNode::cbUpdate, this, _1, _2));

    pub_costmap_ = nhp_.advertise<costmap_cspace::CSpace3D>("costmap", 1, true);
    pub_costmap_update_ = nhp_.advertise<costmap_cspace::CSpace3DUpdate>("costmap_update", 1, true);
    pub_footprint_ = nhp_.advertise<geometry_msgs::PolygonStamped>("footprint", 2, true);
    pub_debug_ = nhp_.advertise<sensor_msgs::PointCloud>("debug", 1, true);

    const geometry_msgs::PolygonStamped footprint_msg = footprint.toMsg();
    timer_footprint_ = nh_.createTimer(
        ros::Duration(1.0),
        boost::bind(&Costmap3DOFNode::cbPublishFootprint, this, _1, footprint_msg));
  }
};

int main(int argc, char *argv[])
{
  ros::init(argc, argv, "costmap_3d");

  Costmap3DOFNode cm;
  ros::spin();

  return 0;
}