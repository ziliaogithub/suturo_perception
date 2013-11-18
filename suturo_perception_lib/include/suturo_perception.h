#ifndef SUTURO_PERCEPTION_H
#define SUTURO_PERCEPTION_H

#include <iostream>

#include <pcl/io/pcd_io.h>
#include <pcl/point_types.h>
#include <boost/signals2/mutex.hpp>
#include "perceived_object.h"

namespace suturo_perception_lib
{
  class SuturoPerception
  {
    public:
    void process_cloud(pcl::PointCloud<pcl::PointXYZRGB>::Ptr cloud_in);
    std::vector<PerceivedObject> getPerceivedObjects();
    
    private:
    // ID counter for the perceived objects
    int objectID;

    // Buffer for the last perceived objects
    std::vector<PerceivedObject> perceivedObjects;
    
    // Mutex for buffer locking
    boost::signals2::mutex mutex;
  };
}

#endif
