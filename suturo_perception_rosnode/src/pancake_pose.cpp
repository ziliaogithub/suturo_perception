/**
 * This node subscribes to /camera/depth_registered/points,
 * looks for a table, and segments the object on the table out
 * Afterwards, the suturo_perception_pancake_pose Library will be used
 * to estimate the pose of the Pancake Mix for every cluster on the table.
 *
 * The node assumes, that the pointcloud topic is already
 * advertised by your RGBD software.
 * You can specify the PointCloud Topic with the parameter -t.
 *   Regular topics:
 *     Default Kinect: /camera/depth_registered/points
 *     Kinect on PR2: /kinect_head/depth_registered/points
 *     Gazebo with PR2: /head_mount_kinect/depth_registered/points
 *
 * REQUIREMENTS:
 *   - You need a running moveit instance, to publish the CAD model to the planning scene
 *
 *
 */
#include "ros/ros.h"
#include <ros/package.h>
#include <sensor_msgs/PointCloud.h>
#include <pcl_ros/point_cloud.h>
#include <pcl/io/pcd_io.h>
#include <pcl/point_types.h>
#include "suturo_perception.h"
#include <suturo_perception_cad_recognition/pancake_pose.h>
#include <visualization_msgs/Marker.h>
#include <ros/console.h>
#include <iostream>

// #include <suturo_manipulation_planning_scene_interface.h>
#include <moveit/move_group_interface/move_group.h>
#include <shape_tools/solid_primitive_dims.h>
#include <geometric_shapes/shape_operations.h>

#include <boost/program_options.hpp>

namespace po = boost::program_options;
using namespace boost;

suturo_perception_lib::SuturoPerception sp;
typedef pcl::PointCloud<pcl::PointXYZRGB>::Ptr rgb_pc_ptr;
pcl::PointCloud<pcl::PointXYZ>::Ptr model_cloud;
const std::string PANCAKE_MODEL_PATH = "package://suturo_perception_cad_recognition/test_data/pancake_mix.stl";

ros::Publisher vis_pub;
ros::Publisher pub_co;
bool processOneCloud = true;

shape_msgs::Mesh load_mesh_msg(std::string resource)
{
    shape_msgs::Mesh mesh_msg;
    shapes::ShapeMsg mesh;
    shapes::constructMsgFromShape( shapes::createMeshFromResource(resource), mesh);
    mesh_msg = boost::get<shape_msgs::Mesh>(mesh);
    return mesh_msg;
}
void receive_cloud(const sensor_msgs::PointCloud2ConstPtr& inputCloud)
{
  rgb_pc_ptr cloud_in (new pcl::PointCloud<pcl::PointXYZRGB>());
  pcl::fromROSMsg(*inputCloud,*cloud_in);
  ROS_INFO("Received a new point cloud: size = %lu",cloud_in->points.size());
  if(processOneCloud)
  {
    sp.setOriginalCloud(cloud_in);
    sp.setCalculateHullVolume(false);
    sp.setEcMinClusterSize(4000);
    sp.processCloudWithProjections(cloud_in);
    pcl::ModelCoefficients::Ptr table_coefficients = sp.getTableCoefficients();

    //std::vector<suturo_perception_lib::PerceivedObject> perceivedObjects;
    std::vector<suturo_perception_lib::PerceivedObject, Eigen::aligned_allocator<suturo_perception_lib::PerceivedObject> > perceivedObjects;

    perceivedObjects = sp.getPerceivedObjects();

    ROS_INFO("Received perceived Objects %lu", perceivedObjects.size());
    for (int i = 0; i < perceivedObjects.size(); i++) {
      rgb_pc_ptr object_cloud = perceivedObjects.at(i).get_pointCloud();
      // pcl::PointCloud<pcl::PointXYZ> object_cloud_xyz;
      pcl::PointCloud<pcl::PointXYZ>::Ptr object_cloud_xyz(new pcl::PointCloud<pcl::PointXYZ>);
      pcl::copyPointCloud(*object_cloud, *object_cloud_xyz);
      Eigen::Vector4f table_coefficients_eigen(
          table_coefficients->values.at(0),
          table_coefficients->values.at(1),
          table_coefficients->values.at(2),
          table_coefficients->values.at(3));
      PancakePose pp(object_cloud_xyz, model_cloud, table_coefficients_eigen);
      pp.execute();
      pcl::PointXYZ origin = pp.getOrigin();
      Eigen::Quaternionf orientation = pp.getOrientation();

      // // Publish cad model
      visualization_msgs::Marker meshMarker;
      meshMarker.header.frame_id = inputCloud->header.frame_id;
      meshMarker.header.stamp = ros::Time();
      meshMarker.ns = "pancake_pose";
      meshMarker.id = 1;
      // meshMarker.lifetime = ros::Duration(4);
      meshMarker.type = visualization_msgs::Marker::MESH_RESOURCE;
      meshMarker.action = visualization_msgs::Marker::ADD;
      meshMarker.mesh_resource = "package://suturo_perception_cad_recognition/test_data/pancake_mix.stl";
      meshMarker.pose.position.x = origin.x;
      meshMarker.pose.position.y = origin.y;
      meshMarker.pose.position.z = origin.z;
      meshMarker.pose.orientation.x = orientation.x(); 
      meshMarker.pose.orientation.y = orientation.y(); 
      meshMarker.pose.orientation.z = orientation.z(); 
      meshMarker.pose.orientation.w = orientation.w(); 
      meshMarker.scale.x = 1.0;
      meshMarker.scale.y = 1.0;
      meshMarker.scale.z = 1.0;
      meshMarker.color.r = 0;
      meshMarker.color.g = 1.0;
      meshMarker.color.b = 0;
      meshMarker.color.a = 1.0;
      vis_pub.publish(meshMarker);

      // Publish moveit CollisionObject
      moveit_msgs::CollisionObject co;
      co.header.stamp = ros::Time::now();
      co.header.frame_id = inputCloud->header.frame_id;
      co.id = "pancake_mix";
      co.operation = moveit_msgs::CollisionObject::ADD;

      co.meshes.resize(1);
      co.meshes[0] = load_mesh_msg(PANCAKE_MODEL_PATH);
      co.mesh_poses.resize(1);
      co.mesh_poses[0].position.x = origin.x;
      co.mesh_poses[0].position.y = origin.y;
      co.mesh_poses[0].position.z = origin.z;
      co.mesh_poses[0].orientation.x = orientation.x();
      co.mesh_poses[0].orientation.y = orientation.y();
      co.mesh_poses[0].orientation.z = orientation.z();
      co.mesh_poses[0].orientation.w = orientation.w();
      ros::WallDuration(1.0).sleep();
      pub_co.publish(co);
      processOneCloud = false;
    }
  }
}

int main (int argc, char** argv)
{
  ros::init(argc, argv, "pose_estimator");
  ros::NodeHandle nh;
  ros::Subscriber sub;
  std::string pointcloud_topic="";

  // "HashMap" for program parameters
  po::variables_map vm;
  try
  {
    // Declare the supported options.
    po::options_description desc("Allowed options");
    desc.add_options()
      ("help", "produce help message")
      ("topic,t", po::value<std::string>(&pointcloud_topic), "The ROS topic this node should listen to")
    ;

    po::positional_options_description p;
    po::store(po::command_line_parser(argc, argv).
    options(desc).positional(p).run(), vm); 

    if (vm.count("help")) {
      cout << "Usage: pancake_pose [-t topic]" << endl << endl;
      cout << desc << "\n";
      return 1;
    }

    // Put notify after the help check, so help is display even
    // if required parameters are not given
    po::notify(vm);

  }
  catch(std::exception& e)
  {
		cout << "Usage: pancake_pose [-t topic]" << endl << endl;
    std::cerr << "Error: " << e.what() << "\n";
    return false;
  }
  catch(...)
  {
    std::cerr << "Unknown error!" << "\n";
    return false;
  } 

  if(pointcloud_topic.empty())
      pointcloud_topic = "/camera/depth_registered/points";

  model_cloud = pcl::PointCloud<pcl::PointXYZ>::Ptr(new pcl::PointCloud<pcl::PointXYZ>);
  std::string package_path = ros::package::getPath("suturo_perception_cad_recognition");
  std::string filepath = "test_data/pancake_mix_model.pcd";
  if (pcl::io::loadPCDFile<pcl::PointXYZ> (package_path + "/" + filepath, *model_cloud) == -1)
  {
    PCL_ERROR ("Couldn't read model cloud from suturo_perception_cad_recognition\n");
    exit (-1);
  }

  vis_pub = nh.advertise<visualization_msgs::Marker>("/suturo/pancake_marker", 0);  
  pub_co = nh.advertise<moveit_msgs::CollisionObject>("collision_object", 10);
  ros::WallDuration(1.0).sleep();

  std::cout << "Subscribing to " << pointcloud_topic << std::endl;
  sub = nh.subscribe(pointcloud_topic, 1, 
      &receive_cloud);

  ros::Rate r(0.5);
  while(ros::ok()){
    if(processOneCloud)
    {
      ros::spinOnce();
      r.sleep();
    }else
    {
      std::cout << "Would you like to run the pose estimation again? [y/q]. Enter \"q\" to quit." << std::endl;
      std::string input;
      std::cin >> input;
      if(input.compare("y") == 0)
      {
        processOneCloud = true;
      }
      else
      {
        if(input.compare("q") == 0)
        {
          return 0;
        }
        else
        {
          std::cout << "Please answer with \"y\" if you want to run the pose estimation again" << std::endl;
        }
      }
    }
      
  }
  return 0;
}
// vim: tabstop=2 expandtab shiftwidth=2 softtabstop=2: 
