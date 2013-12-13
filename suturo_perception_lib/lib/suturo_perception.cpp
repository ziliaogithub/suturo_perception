// TODO: Load 2d objrec Database file via parameter

#include "suturo_perception.h"
using namespace suturo_perception_lib;

// Comparator function for PerceivedObject's. PerceivedObjects will be compared by their volume
bool ReceivedObjectGreaterThan(const PerceivedObject& p1, 
    const PerceivedObject& p2)
{
  return p1.c_volume > p2.c_volume;
}

/*
 * Constructor
 */
SuturoPerception::SuturoPerception()
{
  // initialize logger
  logger = Logger("perception_lib");
  // Set default parameters
  zAxisFilterMin = 0.0;
  zAxisFilterMax = 1.5;
  downsampleLeafSize = 0.01;
  planeMaxIterations = 1000;
  planeDistanceThreshold = 0.01;
  ecClusterTolerance = 0.02; // 2cm
  ecMinClusterSize = 8000;
  ecMaxClusterSize = 200000;  
  prismZMin = 0.02;
  prismZMax = 0.50; // cutoff 50 cm above plane
  ecObjClusterTolerance = 0.03; // 3cm
  ecObjMinClusterSize = 100;
  ecObjMaxClusterSize = 25000;
  debug = true;
  writer_pcd = false;
}


pcl::PointCloud<pcl::PointXYZRGB>::Ptr SuturoPerception::getObjectsOnPlaneCloud()
{
  return objects_on_plane_cloud_;
}
pcl::PointCloud<pcl::PointXYZRGB>::Ptr SuturoPerception::getPlaneCloud()
{
  return plane_cloud_;
}

/**
 * Use EuclideanClusterExtraction on object_clusters to identify seperate objects in the given pointcloud.
 * Create a ConvexHull for every object_cluster and extract everything that's above it (in a given range,
 * see SuturoPerception::prismZMax and SuturoPerception::prismZMin.
 * In the future, this method will also extract 2d images from every object cluster.
 */
void SuturoPerception::clusterFromProjection(pcl::PointCloud<pcl::PointXYZRGB>::Ptr object_clusters, pcl::PointCloud<pcl::PointXYZRGB>::Ptr original_cloud, std::vector<int> *removed_indices_filtered, std::vector<pcl::PointCloud<pcl::PointXYZRGB>::Ptr> &extracted_objects, std::vector<cv::Mat> &extracted_images, std::vector<ROI> &perceived_cluster_rois_)
{

  if(object_clusters->points.size() == 0)
  {
    logger.logError("clusterFromProjection: object_clusters is empty. Skipping ...");
    return;
  }

  if(original_cloud->points.size() == 0)
  {
    logger.logError("clusterFromProjection: original_cloud is empty. Skipping ...");
    return;
  }

  mutex.lock();
  // extracted_images = tmpPerceivedObjects; // TODO use temporary list for images
  extracted_images.clear();
  mutex.unlock();

  pcl::PCDWriter writer;
  boost::posix_time::ptime s = boost::posix_time::microsec_clock::local_time();

  // Identify clusters in the input cloud
  pcl::search::KdTree<pcl::PointXYZRGB>::Ptr tree (new pcl::search::KdTree<pcl::PointXYZRGB>);
  tree->setInputCloud (object_clusters);

  std::vector<pcl::PointIndices> cluster_indices;
  pcl::EuclideanClusterExtraction<pcl::PointXYZRGB> ec;
  ec.setClusterTolerance (ecObjClusterTolerance);
  ec.setMinClusterSize (ecObjMinClusterSize);
  ec.setMaxClusterSize (ecObjMaxClusterSize);
  ec.setSearchMethod (tree);
  ec.setInputCloud (object_clusters);
  ec.extract(cluster_indices);
  logger.logInfo((boost::format("Found %s clusters.") % cluster_indices.size()).str());

  boost::posix_time::ptime e = boost::posix_time::microsec_clock::local_time();
  logger.logTime(s, e, "filtering out objects above the plane");

  int i=0;
  // Iterate over the found clusters and extract single pointclouds
  for (std::vector<pcl::PointIndices>::const_iterator it = cluster_indices.begin (); it != cluster_indices.end (); ++it)
  {
    // Gather all points for a cluster into a single pointcloud
    boost::posix_time::ptime s1 = boost::posix_time::microsec_clock::local_time();
    pcl::PointCloud<pcl::PointXYZRGB>::Ptr cloud_cluster (new pcl::PointCloud<pcl::PointXYZRGB>);
    for (std::vector<int>::const_iterator pit = it->indices.begin (); pit != it->indices.end (); pit++)
      cloud_cluster->points.push_back (object_clusters->points[*pit]); //*

    cloud_cluster->width = cloud_cluster->points.size ();
    cloud_cluster->height = 1;
    cloud_cluster->is_dense = true;
    logger.logInfo((boost::format("Cloud Cluster Size is %s") % cloud_cluster->points.size ()).str());
    std::ostringstream fn;
    fn << "2dcluster_" << i << ".pcd";
    if(writer_pcd) writer.write(fn.str(), *cloud_cluster, false);

  
    // Extract every point above the 2d cluster.
    // These points will belong to a single object on the table
    pcl::PointIndices::Ptr object_indices (new pcl::PointIndices); // The extracted indices of a single object above the plane
    pcl::PointCloud<pcl::PointXYZRGB>::Ptr object_points (new pcl::PointCloud<pcl::PointXYZRGB>());
    PointCloudOperations::extractAllPointsAbovePointCloud(original_cloud, cloud_cluster, object_points, object_indices, 2,
        prismZMin, prismZMax);
    extracted_objects.push_back(object_points);

    // logger.logError("After extract");

    boost::posix_time::ptime e1 = boost::posix_time::microsec_clock::local_time();
    logger.logTime(s1, e1, "Extracted Object Points");

    std::ostringstream cl_file;
    cl_file << "2d_Z_cluster_" << i << ".pcd";
    if(writer_pcd) writer.write(cl_file.str(), *object_points, false);
    // cout << "2d_Z_cluster_" << i << " has " << object_points->size() << " points" << endl;

    boost::posix_time::ptime s2 = boost::posix_time::microsec_clock::local_time();

    // RGB Values for points
    int r,g,b;
    cv::Mat img(cv::Size(original_cloud->width,original_cloud->height),CV_8UC3, cv::Scalar(0,0,0)); // Create an image with the size of the original cloud

    // Compute the ROI (region of interest, with the segmented image)
    int min_column = original_cloud->width;
    int max_column = 0;
    int min_row = original_cloud->height;
    int max_row = 0;

    // fill in color of extracted object points
    for (std::vector<int>::const_iterator pit = object_indices->indices.begin(); pit != object_indices->indices.end(); pit++)
    {
      int index = removed_indices_filtered->at(*pit);
      int row = index / original_cloud->width;
      int column = index % original_cloud->width;

      // Calculate the dimensions of the image
      if(column > max_column) max_column = column;
      if(row > max_row) max_row = row;
      if(row < min_row) min_row = row;
      if(column < min_column) min_column = column;

      img.at<cv::Vec3b>( row, column)[0] = original_cloud->points[index].b;
      img.at<cv::Vec3b>( row, column)[1] = original_cloud->points[index].g;
      img.at<cv::Vec3b>( row, column)[2] = original_cloud->points[index].r;
    }

    int roi_topleft_x = min_column;
    int roi_topleft_y = min_row;
    int roi_width = max_column - min_column;
    int roi_height = max_row - min_row;

    ROI roi;
    roi.origin.x = roi_topleft_x;
    roi.origin.y = roi_topleft_y;
    roi.width = roi_width;
    roi.height = roi_height;

    cv::Rect region_of_interest = cv::Rect(roi_topleft_x, roi_topleft_y, roi_width, roi_height);
    cv::Mat image_roi = img(region_of_interest);

    extracted_images.push_back(image_roi);
    perceived_cluster_rois_.push_back(roi);
    i++;
  }

  if(writer_pcd) writer.write ("cluster_from_projection_clusters.pcd", *object_clusters, false);

}

/*
 * Process a single point cloud.
 * This will include
 *   - Centroid calculation
 *   - Volume calculation
 *
 * The result is a list of PerceivedObject's, which will be put into the buffer perceivedObjects.
 */
void SuturoPerception::processCloudWithProjections(pcl::PointCloud<pcl::PointXYZRGB>::Ptr cloud_in)
{

	boost::posix_time::ptime start = boost::posix_time::microsec_clock::local_time();

  pcl::PointCloud<pcl::PointXYZRGB>::Ptr cloud (new pcl::PointCloud<pcl::PointXYZRGB>), 
                                      cloud_filtered (new pcl::PointCloud<pcl::PointXYZRGB>), 
                                      cloud_projected (new pcl::PointCloud<pcl::PointXYZRGB>),
                                      objects_cloud_projected (new pcl::PointCloud<pcl::PointXYZRGB>),
                                      cloud_plane (new pcl::PointCloud<pcl::PointXYZRGB>);
  pcl::PCDReader reader;
  pcl::PCDWriter writer;


  boost::posix_time::ptime s = boost::posix_time::microsec_clock::local_time();

  // TODO Remove NaNs

  // Build a filter to filter on the Z Axis
  pcl::PassThrough<pcl::PointXYZRGB> pass(true);
  PointCloudOperations::filterZAxis(cloud_in, cloud_filtered, pass, zAxisFilterMin, zAxisFilterMax);
  logger.logInfo((boost::format("PointCloud: %s data points") % cloud_filtered->points.size()).str());

  std::vector<int> removed_indices_filtered;
  removed_indices_filtered = *pass.getIndices();
  boost::posix_time::ptime e = boost::posix_time::microsec_clock::local_time();
  logger.logTime(s, e, "z-filter");


  //voxelizing cloud
  pcl::PointCloud<pcl::PointXYZRGB>::Ptr cloud_downsampled (new pcl::PointCloud<pcl::PointXYZRGB>());
  PointCloudOperations::downsample(cloud_filtered, cloud_downsampled, downsampleLeafSize);
  cloud_filtered = cloud_downsampled; // Use the downsampled cloud now

  // Find the biggest table plane in the scene
  pcl::ModelCoefficients::Ptr coefficients (new pcl::ModelCoefficients);
  pcl::PointIndices::Ptr inliers (new pcl::PointIndices);
  PointCloudOperations::fitPlanarModel(cloud_filtered, inliers, coefficients, planeMaxIterations, planeDistanceThreshold);
  logger.logInfo((boost::format("Table inlier count: %s") % inliers->indices.size ()).str());
  // Table segmentation done
  
  // Extract the plane as a PointCloud from the calculated inliers
  PointCloudOperations::extractInliersFromPointCloud(cloud_filtered, inliers, cloud_plane, false);

  // Take the biggest cluster in the extracted plane. This will be
  // most likely our desired table pointcloud
  pcl::PointCloud<pcl::PointXYZRGB>::Ptr plane_cluster (new pcl::PointCloud<pcl::PointXYZRGB>);
  pcl::PointIndices::Ptr new_inliers (new pcl::PointIndices);
  PointCloudOperations::extractBiggestCluster(cloud_plane, plane_cluster, inliers, new_inliers,
    ecObjClusterTolerance, ecMinClusterSize, ecMaxClusterSize);

  // NOTE: We need to transform the inliers from table_cluster_indices to inliers
  inliers = new_inliers;
  
  if(inliers->indices.size () == 0)
  {
    logger.logError("Second Table Inlier Set is empty. Exiting....");
    return;
  }
  plane_cloud_ = plane_cluster; // save the reference to the segmented and clustered table plane

  // Extract all objects above
  // the table plane
  pcl::PointIndices::Ptr object_indices (new pcl::PointIndices);
  pcl::PointCloud<pcl::PointXYZRGB>::Ptr object_clusters (new pcl::PointCloud<pcl::PointXYZRGB>());
  PointCloudOperations::extractAllPointsAbovePointCloud(cloud_filtered, plane_cluster,
      object_clusters, object_indices, 2, prismZMin, prismZMax);
  objects_on_plane_cloud_ = object_clusters;

  // Project the pointcloud above the table onto the table to get a 2d representation of the objects
  // This will cause every point of an object to be at the base of the object
  PointCloudOperations::projectToPlaneCoefficients(cloud_filtered, object_indices, coefficients, objects_cloud_projected);
  if(writer_pcd) writer.write ("objects_cloud_projected.pcd", *objects_cloud_projected, false);

  // Take the projected points, cluster them and extract everything that's above it
  // By doing this, we should get every object on the table and a 2d image of it.
  std::vector<pcl::PointCloud<pcl::PointXYZRGB>::Ptr> extractedObjects;
  perceived_cluster_rois_.clear();
  clusterFromProjection(objects_cloud_projected, cloud_in, &removed_indices_filtered, extractedObjects, perceived_cluster_images_, perceived_cluster_rois_);
  logger.logInfo((boost::format(" - extractedObjects Vector size %s") % extractedObjects.size()).str());
  logger.logInfo((boost::format(" - extractedImages  Vector size %s") % perceived_cluster_images_.size()).str());
  logger.logInfo((boost::format(" - extractedROIs  Vector size %s") % perceived_cluster_rois_.size()).str());
    
  // temporary list of perceived objects
  std::vector<PerceivedObject> tmpPerceivedObjects;

  // shape detector to detect shape
  // int to represent the shape
  suturo_perception_shape_detection::RandomSampleConsensus rsc;
  int ptShape;

  // initialize list of histogram images
  perceived_cluster_histograms_.clear();

  // hack for collision_objects
  collision_objects = extractedObjects;
  int i=0;
  // Iterate over the extracted clusters and write them as a PerceivedObjects to the result list
  for (std::vector<pcl::PointCloud<pcl::PointXYZRGB>::Ptr>::iterator it = extractedObjects.begin(); 
      it != extractedObjects.end(); ++it)
  {  
    logger.logInfo((boost::format("Transform cluster %s into a message. \
                    Cluster has %s points") % i % (*it)->points.size()).str());

    if((*it)->points.size()==0)
    {
      logger.logError("Cluster cloud is empty. Skipping ...");
      i++;
      continue;
    }

    // Calculate the volume of each cluster
    // Create a convex hull around the cluster and calculate the total volume
    pcl::PointCloud<pcl::PointXYZRGB>::Ptr hull_points (new pcl::PointCloud<pcl::PointXYZRGB> ());
    pcl::PointCloud<pcl::PointXYZRGB>::Ptr obj_points_from_hull (new pcl::PointCloud<pcl::PointXYZRGB> ());

    pcl::ConvexHull<pcl::PointXYZRGB> hull;
    hull.setInputCloud(*it);
    hull.setDimension(3);
    hull.setComputeAreaVolume(true); // This creates alot of output, but it's necessary for getTotalVolume() ....
    hull.reconstruct (*hull_points);

    // Get average color of the object
    uint32_t averageColor = ca.getAverageColor(*it);
    uint32_t averageColorHSV = ca.convertRGBToHSV(averageColor);

    // Get hue histogram of the object
    boost::shared_ptr<std::vector<int> > histogram = ca.getHistogramHue(*it);
    uint8_t histogram_quality = ca.getHistogramQuality();

    // generate image of histogram
    perceived_cluster_histograms_.push_back(ca.histogramToImage(histogram));
    

    // Detect the shape of the object
    rsc.detectShape(*it);
    ptShape = rsc.getShape();

    // Centroid calulcation
    Eigen::Vector4f centroid;
    pcl::compute3DCentroid (*hull_points, centroid);  

    logger.logInfo((boost::format("Centroid: %s, %s, %s") % centroid[0] % centroid[1] % centroid[2]).str());

    // Add the detected cluster to the list of perceived objects
    PerceivedObject percObj;
    percObj.c_id= objectID;
    objectID++;
    Point ptCentroid;
    ptCentroid.x=centroid[0];
    ptCentroid.y=centroid[1];
    ptCentroid.z=centroid[2];
    percObj.c_centroid = ptCentroid;
    percObj.c_shape = ptShape;
    percObj.c_volume = hull.getTotalVolume();
    percObj.c_color_average_r = (averageColor >> 16) & 0x0000ff;
    percObj.c_color_average_g = (averageColor >> 8)  & 0x0000ff;
    percObj.c_color_average_b = (averageColor)       & 0x0000ff;
    percObj.c_color_average_h = (averageColorHSV >> 16) & 0x0000ff;
    percObj.c_color_average_s = (averageColorHSV >> 8)  & 0x0000ff;
    percObj.c_color_average_v = (averageColorHSV)       & 0x0000ff;
    percObj.c_hue_histogram = *histogram;
    percObj.c_hue_histogram_quality = histogram_quality;
    percObj.c_roi = perceived_cluster_rois_[i];

    tmpPerceivedObjects.push_back(percObj);
    i++;
  }

  // Lock the buffer access to assign the recently perceived objects
  mutex.lock();
  perceivedObjects = tmpPerceivedObjects;
  mutex.unlock();

  boost::posix_time::ptime end = boost::posix_time::microsec_clock::local_time();
  logger.logTime(start, end, "TOTAL");
}


std::vector<PerceivedObject> SuturoPerception::getPerceivedObjects()
{
  return perceivedObjects;
}

std::vector<cv::Mat> SuturoPerception::getPerceivedClusterImages()
{
  return perceived_cluster_images_;
}

std::vector<cv::Mat> SuturoPerception::getPerceivedClusterHistograms()
{
  return perceived_cluster_histograms_;
}

std::vector<ROI> SuturoPerception::getPerceivedClusterROIs()
{
  return perceived_cluster_rois_;
}

void 
SuturoPerception::detectShape(pcl::PointCloud<pcl::PointXYZRGB>::Ptr cloudIn)
{   
    suturo_perception_shape_detection::RandomSampleConsensus rsc;
    rsc.detectShape(cloudIn);
}
// vim: tabstop=2 expandtab shiftwidth=2 softtabstop=2: 
