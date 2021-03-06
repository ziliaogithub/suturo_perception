#include "ros/ros.h"
#include <ros/package.h>
#include "suturo_perception_rosnode.h"


int main (int argc, char** argv)
{
  ros::init(argc, argv, "suturo_perception");
  ros::NodeHandle nh;
  std::string recognitionDir = "";
  if(argc > 0 && strcmp(argv[1], "_") != 0)
  {
    recognitionDir = argv[1];
    ROS_INFO ("Dir for 2D recognition data set by parameter: %s", recognitionDir.c_str());
  }
  else 
  {
    std::string package_path = ros::package::getPath("suturo_perception_rosnode");
    stringstream ss;
    ss << package_path << "/data/milestone3_2d_db.yml";
    recognitionDir = ss.str();
    ROS_INFO ("Dir for 2D recognition data set to milestone 3 database: %s", recognitionDir.c_str());
  }

  // get parameters
  std::string pointTopic;
  std::string colorTopic;
  std::string frameId;

  // ros strangeness strikes again. don't try to && these!
  if(ros::param::get("/suturo_perception/point_topic", pointTopic)) ROS_INFO("Using parameters from Parameter Server");
  else { pointTopic = "/camera/depth_registered/points"; ROS_INFO("Using default parameters");}
  if(ros::param::get("/suturo_perception/frame_id", frameId));
  else frameId = "camera_rgb_optical_frame";
  if(ros::param::get("/suturo_perception/color_topic", colorTopic)) ROS_INFO("Using parameters from Parameter Server");
  else { colorTopic = "/camera/rgb/image_color"; ROS_INFO("Using default parameters");}
  
  // get recognition dir
  ROS_INFO("PointCloud topic is: %s", pointTopic.c_str());
  ROS_INFO("FrameID          is: %s", frameId.c_str());
  ROS_INFO("ColorTopic topic is: %s", colorTopic.c_str());
  

  SuturoPerceptionROSNode spr(nh, pointTopic, colorTopic, frameId, recognitionDir);

  ROS_INFO("                    _____ ");
  ROS_INFO("                   |     | ");
  ROS_INFO("                   | | | | ");
  ROS_INFO("                   |_____| ");
  ROS_INFO("             ____ ___|_|___ ____ ");
  ROS_INFO("            ()___)         ()___) ");
  ROS_INFO("            // /|           |\\ \\\\ ");
  ROS_INFO("           // / |           | \\ \\\\ ");
  ROS_INFO("          (___) |___________| (___) ");
  ROS_INFO("          (___)   (_______)   (___) ");
  ROS_INFO("          (___)     (___)     (___) ");
  ROS_INFO("          (___)      |_|      (___) ");
  ROS_INFO("          (___)  ___/___\\___   | | ");
  ROS_INFO("           | |  |           |  | | ");
  ROS_INFO("           | |  |___________| /___\\ ");
  ROS_INFO("          /___\\  |||     ||| //   \\\\ ");
  ROS_INFO("         //   \\\\ |||     ||| \\\\   // ");
  ROS_INFO("         \\\\   // |||     |||  \\\\ // ");
  ROS_INFO("          \\\\ // ()__)   (__() ");
  ROS_INFO("                ///       \\\\\\ ");
  ROS_INFO("               ///         \\\\\\ ");
  ROS_INFO("             _///___     ___\\\\\\_ ");
  ROS_INFO("            |_______|   |_______| ");

  ROS_INFO("   ____  __  __ ______  __  __   ___   ____                            ");
  ROS_INFO("  / __/ / / / //_  __/ / / / /  / _ \\ / __ \\                           ");
  ROS_INFO(" _\\ \\  / /_/ /  / /   / /_/ /  / , _// /_/ /                           ");
  ROS_INFO("/___/_ \\____/_ /_/__  \\____/  /_/|_| \\____/______   ____  ____    _  __");
  ROS_INFO("  / _ \\  / __/  / _ \\ / ___/  / __/  / _ \\/_  __/  /  _/ / __ \\  / |/ /");
  ROS_INFO(" / ___/ / _/   / , _// /__   / _/   / ___/ / /    _/ /  / /_/ / /    / ");
  ROS_INFO("/_/    /___/  /_/|_| \\___/  /___/  /_/    /_/    /___/  \\____/ /_/|_/  ");
                                                                       
  // ROS_INFO("           suturo_perception READY");
  ros::MultiThreadedSpinner spinner(2);
  spinner.spin();
  return (0);
}
// vim: tabstop=2 expandtab shiftwidth=2 softtabstop=2: 
