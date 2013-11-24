#include "ros/ros.h"
#include <boost/signals2/mutex.hpp>
#include <boost/thread/thread.hpp>
#include <boost/date_time.hpp>
#include <dynamic_reconfigure/server.h>
#include <suturo_perception_rosnode/SuturoPerceptionConfig.h>
#include <visualization_msgs/Marker.h>

#include "suturo_perception.h"
#include "perceived_object.h"
#include "point.h"
#include "suturo_perception_msgs/GetClusters.h"


class SuturoPerceptionROSNode
{
public:
  /*
   * Constructor
   */
  SuturoPerceptionROSNode(ros::NodeHandle& n, std::string pt, std::string fi) : 
    nh(n), 
    pointTopic(pt), 
    frameId(fi)
  {
    clusterService = nh.advertiseService("GetClusters", 
      &SuturoPerceptionROSNode::getClusters, this);
    vis_pub = nh.advertise<visualization_msgs::Marker>("visualization_marker", 0);
    objectID = 0;
    maxMarkerId = 0;

    // Initialize dynamic reconfigure
    reconfCb = boost::bind(&SuturoPerceptionROSNode::reconfigureCallback, this, _1, _2);
    reconfSrv.setCallback(reconfCb);
  }

   /*
    * Receive callback for the /camera/depth_registered/points subscription
    */
   void receive_cloud(const sensor_msgs::PointCloud2ConstPtr& inputCloud)
   {
    // process only one cloud
    ROS_INFO("Receiving cloud");
    if(processing)
    {
      ROS_INFO("processing...");
      pcl::PointCloud<pcl::PointXYZRGB>::Ptr cloud_in (new pcl::PointCloud<pcl::PointXYZRGB>());
      pcl::fromROSMsg(*inputCloud,*cloud_in);

      ROS_INFO("Received a new point cloud: size = %lu",cloud_in->points.size());
      sp.processCloud(cloud_in);
      processing = false;
      //sp.writeCloudToDisk(objects);
      ROS_INFO("Cloud processed. Lock buffer and return the results");      
    }
  }

  /*
   * Implementation of the GetClusters Service.
   *
   * This method will subscribe to the /camera/depth_registered/points topic, 
   * wait for the processing of a single point cloud, and return the result from
   * the calulations as a list of PerceivedObjects.
   */
  bool getClusters(suturo_perception_msgs::GetClusters::Request &req,
    suturo_perception_msgs::GetClusters::Response &res)
  {
    ros::Subscriber sub;
    processing = true;

    // signal failed call, if request string does not match
    if (req.s.compare("get") != 0)
    {
      return false;
    }

    // Subscribe to the depth information topic
    sub = nh.subscribe(pointTopic, 1, 
      &SuturoPerceptionROSNode::receive_cloud, this);

    ROS_INFO("Waiting for processed cloud");
    ros::Rate r(20); // 20 hz
    // cancel service call, if no cloud is received after 5s
    boost::posix_time::ptime cancelTime = boost::posix_time::second_clock::local_time() + boost::posix_time::seconds(10);
    while(processing)
    {
      if(boost::posix_time::second_clock::local_time() >= cancelTime) processing = false;
      ros::spinOnce();
      r.sleep();
    }

    mutex.lock();
    perceivedObjects = sp.getPerceivedObjects();
    res.perceivedObjs = *convertPerceivedObjects(&perceivedObjects);
    mutex.unlock();

    ROS_INFO("Shutting down subscriber");
    sub.shutdown(); // shutdown subscriber, to mitigate funky behavior
    publishVisualizationMarkers(res.perceivedObjs);
    
    ROS_INFO("Service call finished. return");
    return true;
  }

  /*
   * Callback for the dynamic reconfigure service
   */
  void reconfigureCallback(suturo_perception_rosnode::SuturoPerceptionConfig &config, uint32_t level)
  {
    ROS_INFO("Reconfigure request : \n"
              "zAxisFilterMin: %f \n"
              "zAxisFilterMax: %f \n"
              "downsampleLeafSize: %f \n"
              "planeMaxIterations: %i \n"
              "planeDistanceThreshold: %f \n"
              "ecClusterTolerance: %f \n"
              "ecMinClusterSize: %i \n"
              "ecMaxClusterSize: %i \n"
              "prismZMin: %f \n"
              "prismZMax: %f \n"
              "ecObjClusterTolerance: %f \n"
              "ecObjMinClusterSize: %i \n"
              "ecObjMaxClusterSize: %i \n",
              config.zAxisFilterMin, config.zAxisFilterMax, config.downsampleLeafSize,
              config.planeMaxIterations, config.planeDistanceThreshold, config.ecClusterTolerance,
              config.ecMinClusterSize, config.ecMaxClusterSize, config.prismZMin, config.prismZMax,
              config.ecObjClusterTolerance, config.ecObjMinClusterSize, config.ecObjMaxClusterSize);
    /*while(processing) // wait until current processing run is completed 
    { 
      boost::this_thread::sleep(boost::posix_time::milliseconds(1));
    }*/
    sp.setZAxisFilterMin(config.zAxisFilterMin);
    sp.setZAxisFilterMax(config.zAxisFilterMax);
    sp.setDownsampleLeafSize(config.downsampleLeafSize);
    sp.setPlaneMaxIterations(config.planeMaxIterations);
    sp.setPlaneDistanceThreshold(config.planeDistanceThreshold);
    sp.setEcClusterTolerance(config.ecClusterTolerance);
    sp.setEcMinClusterSize(config.ecMinClusterSize);
    sp.setEcMaxClusterSize(config.ecMaxClusterSize);
    sp.setPrismZMin(config.prismZMin);
    sp.setPrismZMax(config.prismZMax);
    sp.setEcObjClusterTolerance(config.ecObjClusterTolerance);
    sp.setEcObjMinClusterSize(config.ecObjMinClusterSize);
    sp.setEcObjMaxClusterSize(config.ecObjMaxClusterSize);
    ROS_INFO("Reconfigure successful");
  }

private:
  bool processing; // processing flag
  suturo_perception_lib::SuturoPerception sp;
  std::vector<suturo_perception_lib::PerceivedObject> perceivedObjects;
  ros::NodeHandle nh;
  boost::signals2::mutex mutex;
  // ID counter for the perceived objects
  int objectID;
  ros::ServiceServer clusterService;
  ros::Publisher vis_pub;
  int maxMarkerId;
  std::string pointTopic;
  std::string frameId;
  // dynamic reconfigure
  dynamic_reconfigure::Server<suturo_perception_rosnode::SuturoPerceptionConfig> reconfSrv;
  dynamic_reconfigure::Server<suturo_perception_rosnode::SuturoPerceptionConfig>::CallbackType reconfCb;
  
  /*
   * Convert suturo_perception_lib::PerceivedObject list to suturo_perception_msgs:PerceivedObject list
   */
  std::vector<suturo_perception_msgs::PerceivedObject> *convertPerceivedObjects(std::vector<suturo_perception_lib::PerceivedObject> *objects)
  {
    std::vector<suturo_perception_msgs::PerceivedObject> *result = new std::vector<suturo_perception_msgs::PerceivedObject>();
    for (std::vector<suturo_perception_lib::PerceivedObject>::iterator it = objects->begin(); it != objects->end(); ++it)
    {
      suturo_perception_msgs::PerceivedObject *msgObj = new suturo_perception_msgs::PerceivedObject();
      msgObj->c_id = it->c_id;
      msgObj->c_shape = it->c_shape;
      msgObj->c_volume = it->c_volume;
      msgObj->c_centroid.x = it->c_centroid.x;
      msgObj->c_centroid.y = it->c_centroid.y;
      msgObj->c_centroid.z = it->c_centroid.z;
      msgObj->frame_id = frameId;
      msgObj->c_color_average_r = it->c_color_average_r;
      msgObj->c_color_average_g = it->c_color_average_g;
      msgObj->c_color_average_b = it->c_color_average_b;
      // these are not set for now
      msgObj->recognition_label_2d = "";
      result->push_back(*msgObj);
    }
    return result;
  }

  void publishVisualizationMarkers(std::vector<suturo_perception_msgs::PerceivedObject> objs)
  {
    ROS_INFO("Publishing centroid visualization markers");
    int markerId = 0;
    std::vector<suturo_perception_msgs::PerceivedObject>::iterator it = objs.begin();
    for (std::vector<suturo_perception_msgs::PerceivedObject>::iterator it = objs.begin(); 
         it != objs.end (); ++it)
    {
      ROS_DEBUG("Publishing centroid marker");
      visualization_msgs::Marker centroidMarker;
      centroidMarker.header.frame_id = frameId;
      centroidMarker.header.stamp = ros::Time();
      centroidMarker.ns = "suturo_perception";
      centroidMarker.id = markerId;
      centroidMarker.type = visualization_msgs::Marker::SPHERE;
      centroidMarker.action = visualization_msgs::Marker::ADD;
      centroidMarker.pose.position.x = it->c_centroid.x;
      centroidMarker.pose.position.y = it->c_centroid.y;
      centroidMarker.pose.position.z = it->c_centroid.z;
      centroidMarker.pose.orientation.x = 0.0;
      centroidMarker.pose.orientation.y = 0.0;
      centroidMarker.pose.orientation.z = 0.0;
      centroidMarker.pose.orientation.w = 0.0;
      centroidMarker.scale.x = 0.1;
      centroidMarker.scale.y = 0.1;
      centroidMarker.scale.z = 0.1;
      centroidMarker.color.a = 1.0;
      centroidMarker.color.r = 0.0;
      centroidMarker.color.g = 1.0;
      centroidMarker.color.b = 0.0;
      markerId++;
      maxMarkerId++;
      vis_pub.publish(centroidMarker);

      ROS_DEBUG("Publishing text marker");
      visualization_msgs::Marker textMarker;
      textMarker.header.frame_id = frameId;
      textMarker.header.stamp = ros::Time();
      textMarker.ns = "suturo_perception";
      textMarker.id = markerId + 1000;
      textMarker.type = visualization_msgs::Marker::TEXT_VIEW_FACING;
      textMarker.action = visualization_msgs::Marker::ADD;
      textMarker.pose.position.x = it->c_centroid.x - 0.0;
      textMarker.pose.position.y = it->c_centroid.y - 0.1;
      textMarker.pose.position.z = it->c_centroid.z - 0.4;
      textMarker.scale.x = 0.025;
      textMarker.scale.y = 0.025;
      textMarker.scale.z = 0.025;
      textMarker.color.a = 1.0;
      textMarker.color.r = 1.0;
      textMarker.color.g = 0.0;
      textMarker.color.b = 0.0;
      std::stringstream ss;
      ss << "Volume: " << it->c_volume << "\n" 
         << "rgb: " << (int)it->c_color_average_r << "." << 
            (int)it->c_color_average_g << "." << (int)it->c_color_average_b << "\n"
         << "Shape: ";
      switch(it->c_shape)
      {
        case 0: ss << "None"; break;
        case 1: ss << "Box"; break;
        case 2: ss << "Cylinder"; break;
        case 3: ss << "Sphere"; break;
        default: ss << "None"; break;
      }
      textMarker.text = ss.str();
      vis_pub.publish(textMarker);
    }
    // remove markers that have not been updated
    for(int i = markerId; i <= maxMarkerId; ++i)
    {
      visualization_msgs::Marker marker;
      marker.header.frame_id = frameId;
      marker.header.stamp = ros::Time();
      marker.ns = "suturo_perception";
      marker.id = i;
      marker.action = visualization_msgs::Marker::DELETE;
      vis_pub.publish(marker);
      // also remove text marker
      marker.id = i + 1001;
      vis_pub.publish(marker);
    }
    maxMarkerId = markerId;
  }
};


int main (int argc, char** argv)
{
  ros::init(argc, argv, "suturo_perception");
  ros::NodeHandle nh;

  // get parameters
  std::string pointTopic;
  std::string frameId;

  // ros strangeness strikes again. don't try to && these!
  if(ros::param::get("/suturo_perception/point_topic", pointTopic)) ROS_INFO("Using parameters from Parameter Server");
  else { pointTopic = "/camera/depth_registered/points"; ROS_INFO("Using default parameters");}
  if(ros::param::get("/suturo_perception/frame_id", frameId));
  else frameId = "camera_rgb_optical_frame";
  
  SuturoPerceptionROSNode spr(nh, pointTopic, frameId);

  ROS_INFO("suturo_perception READY");
  ros::MultiThreadedSpinner spinner(2);
  spinner.spin();
  return (0);
}
