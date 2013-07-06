#include <cob_object_recording/object_recording.h>
#include <boost/filesystem.hpp>
#include <fstream>

//ObjectRecording::ObjectRecording()
//{
//	it_ = 0;
//	sync_input_ = 0;
//}

ObjectRecording::ObjectRecording(ros::NodeHandle nh)
: node_handle_(nh)
{
//	color_camera_matrix_ = (cv::Mat_<double>(3, 4) << 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0);
//	pointcloud_width_ = 640;
//	pointcloud_height_ = 480;

	// subscribers
	input_marker_detection_sub_.subscribe(node_handle_, "input_marker_detections", 1);
	it_ = new image_transport::ImageTransport(node_handle_);
	color_image_sub_.subscribe(*it_, "input_color_image", 1);
	//input_pointcloud_sub_ = node_handle_.subscribe("input_pointcloud_segments", 10, &ObjectRecording::inputCallback, this);
	input_pointcloud_sub_.subscribe(node_handle_, "input_pointcloud", 1);
//	input_color_camera_info_sub_ = node_handle_.subscribe("input_color_camera_info", 1, &ObjectRecording::calibrationCallback, this);

	// input synchronization
	sync_input_ = new message_filters::Synchronizer< message_filters::sync_policies::ApproximateTime<cob_object_detection_msgs::DetectionArray, sensor_msgs::PointCloud2, sensor_msgs::Image> >(10);
	sync_input_->connectInput(input_marker_detection_sub_, input_pointcloud_sub_, color_image_sub_);

	service_server_start_recording_ = node_handle_.advertiseService("start_recording", &ObjectRecording::startRecording, this);
	service_server_stop_recording_ = node_handle_.advertiseService("stop_recording", &ObjectRecording::stopRecording, this);
	service_server_save_recorded_object_ = node_handle_.advertiseService("save_recorded_object", &ObjectRecording::saveRecordedObject, this);
}

ObjectRecording::~ObjectRecording()
{

	if (it_ != 0) delete it_;
	if (sync_input_ != 0) delete sync_input_;
}


bool ObjectRecording::startRecording(cob_object_detection_msgs::StartObjectRecording::Request &req, cob_object_detection_msgs::StartObjectRecording::Response &res)
{
	ROS_INFO("Request to start recording received.");

	// clear data containers

	// register callback function for data processing
	registered_callback_ = sync_input_->registerCallback(boost::bind(&ObjectRecording::inputCallback, this, _1, _2, _3));

	return true;
}

bool ObjectRecording::stopRecording(cob_object_detection_msgs::StopObjectRecording::Request &req, cob_object_detection_msgs::StopObjectRecording::Response &res)
{
	ROS_INFO("Request to stop recording received.");

	registered_callback_.disconnect();

	return true;
}

bool ObjectRecording::saveRecordedObject(cob_object_detection_msgs::SaveRecordedObject::Request &req, cob_object_detection_msgs::SaveRecordedObject::Response &res)
{
	ROS_INFO("Request to save recorded data received.");

	return true;
}

/// callback for the incoming pointcloud data stream
void ObjectRecording::inputCallback(const cob_object_detection_msgs::DetectionArray::ConstPtr& input_marker_detections_msg, const sensor_msgs::PointCloud2::ConstPtr& input_pointcloud_msg, const sensor_msgs::Image::ConstPtr& input_image_msg)
{
	//std::cout << "Recording data..." << std::endl;

	if (input_marker_detections_msg->detections.size() == 0)
	{
		ROS_INFO("No markers detected.\n");
		return;
	}

	// convert color image to cv::Mat
	cv_bridge::CvImageConstPtr color_image_ptr;
	cv::Mat color_image;
	if (convertColorImageMessageToMat(input_image_msg, color_image_ptr, color_image) == false)
		return;

	// compute mean coordinate system if multiple markers detected
	tf::Transform marker_pose = computeMarkerPose(input_marker_detections_msg);

	// compute rotation and translation matrices
	cv::Mat rot_3x3_c_from_marker(3, 3, CV_64FC1);
	cv::Mat trans_3x1_c_from_marker(3,1, CV_64FC1);
	for (int y=0; y<3; ++y)
		for (int x=0; x<3; ++x)
			rot_3x3_c_from_marker.at<double>(y,x) = marker_pose.getBasis()[y].m_floats[x];
	for (int x=0; x<3; ++x)
		trans_3x1_c_from_marker.at<double>(x) = marker_pose.getOrigin().m_floats[x];

	cv::Mat display_image = color_image.clone();
	cv::Mat point3d_marker(3,1,CV_64FC1);
	point3d_marker.at<double>(0) = 0;
	point3d_marker.at<double>(1) = 0.21;
	point3d_marker.at<double>(2) = 0;
	cv::Mat point3d_c = rot_3x3_c_from_marker*point3d_marker + trans_3x1_c_from_marker;
	int u, v;
	ProjectXYZ(point3d_c.at<double>(0), point3d_c.at<double>(1), point3d_c.at<double>(2), u, v);
	cv::circle(display_image, cv::Point(u,v), 2, CV_RGB(0,255,0), 2);

	// todo: select ROI for sharpness computation
	// y +/- 0.21m, x=0m

	// compute sharpness measure
	cv::Mat dx, dy;
	cv::Mat gray_image;
	cv::cvtColor(color_image, gray_image, CV_BGR2GRAY);
	cv::Sobel(gray_image, dx, CV_32FC1, 1, 0, 3);
	cv::Sobel(gray_image, dy, CV_32FC1, 0, 1, 3);
	double score = (cv::sum(cv::abs(dx)) + cv::sum(cv::abs(dy))).val[0] / ((double)color_image.cols*color_image.rows);
	std::cout << "sharpness score=" << score << std::endl;

	typedef pcl::PointXYZRGB PointType;
	pcl::PointCloud<PointType> input_pointcloud;
	pcl::fromROSMsg(*input_pointcloud_msg, input_pointcloud);

//	cv::Mat display_segmentation = cv::Mat::zeros(input_pointcloud_msg->height, input_pointcloud_msg->width, CV_8UC3);
	cv::imshow("color image", display_image);
	//cv::imshow("segmented image", display_segmentation);
	cv::waitKey(10);
}

//unsigned long ObjectRecording::ProjectXYZ(double x, double y, double z, int& u, int& v)
//{
//	cv::Mat XYZ(4, 1, CV_64FC1);
//	cv::Mat UVW(3, 1, CV_64FC1);
//
//	double* d_ptr = 0;
//	double du = 0;
//	double dv = 0;
//	double dw = 0;
//
//	x *= 1000;
//	y *= 1000;
//	z *= 1000;
//
//	d_ptr = XYZ.ptr<double>(0);
//	d_ptr[0] = x;
//	d_ptr[1] = y;
//	d_ptr[2] = z;
//	d_ptr[3] = 1.;
//
//	UVW = color_camera_matrix_ * XYZ;
//
//	d_ptr = UVW.ptr<double>(0);
//	du = d_ptr[0];
//	dv = d_ptr[1];
//	dw = d_ptr[2];
//
//	u = cvRound(du/dw);
//	v = cvRound(dv/dw);
//
//	return 0;
//}

/// Converts a color image message to cv::Mat format.
bool ObjectRecording::convertColorImageMessageToMat(const sensor_msgs::Image::ConstPtr& image_msg, cv_bridge::CvImageConstPtr& image_ptr, cv::Mat& image)
{
	try
	{
		image_ptr = cv_bridge::toCvShare(image_msg, sensor_msgs::image_encodings::BGR8);
	}
	catch (cv_bridge::Exception& e)
	{
		ROS_ERROR("ObjectCategorization: cv_bridge exception: %s", e.what());
		return false;
	}
	image = image_ptr->image;

	return true;
}

tf::Transform ObjectRecording::computeMarkerPose(const cob_object_detection_msgs::DetectionArray::ConstPtr& input_marker_detections_msg)
{
	tf::Vector3 mean_translation;
	tf::Quaternion mean_orientation;
	for (unsigned int i=0; i<input_marker_detections_msg->detections.size(); ++i)
	{
		tf::Point translation;
		tf::pointMsgToTF(input_marker_detections_msg->detections[i].pose.pose.position, translation);
		tf::Quaternion orientation;
		tf::quaternionMsgToTF(input_marker_detections_msg->detections[i].pose.pose.orientation, orientation);

		if (i==0)
		{
			mean_translation = translation;
			mean_orientation = orientation;
		}
		else
		{
			mean_translation += translation;
			mean_orientation += orientation;
		}
	}
	mean_translation /= (double)input_marker_detections_msg->detections.size();
	mean_orientation /= (double)input_marker_detections_msg->detections.size();
	mean_orientation.normalize();
	return tf::Transform(mean_orientation, mean_translation);
}

//void ObjectRecording::calibrationCallback(const sensor_msgs::CameraInfo::ConstPtr& calibration_msg)
//{
////	pointcloud_height_ = calibration_msg->height;
////	pointcloud_width_ = calibration_msg->width;
//	cv::Mat temp(3,4,CV_64FC1);
//	for (int i=0; i<12; i++)
//		temp.at<double>(i/4,i%4) = calibration_msg->P.at(i);
////		std::cout << "projection_matrix: [";
////		for (int v=0; v<3; v++)
////			for (int u=0; u<4; u++)
////				std::cout << temp.at<double>(v,u) << " ";
////		std::cout << "]" << std::endl;
//	color_camera_matrix_ = temp;
//}


int main (int argc, char** argv)
{
	// Initialize ROS, specify name of node
	ros::init(argc, argv, "object_recording");

	// Create a handle for this node, initialize node
	ros::NodeHandle nh;

	// Create and initialize an instance of ObjectRecording
	ObjectRecording objectRecording(nh);

	ros::spin();

	return (0);
}