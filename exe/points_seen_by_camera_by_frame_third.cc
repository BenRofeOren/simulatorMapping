#include <math.h>
#include <string>
#include <nlohmann/json.hpp>
#include <opencv2/opencv.hpp>
#include <opencv2/calib3d.hpp>

#include "include/Auxiliary.h"

cv::Mat createTransformationMatrix(double x, double y, double z, double yaw, double pitch, double roll)
{
    // Create rotation matrix from yaw-pitch-roll angles
    cv::Mat R;
    cv::Vec3d angles(yaw, pitch, roll);
    cv::Rodrigues(angles, R);

    // Create translation vector from 3D position
    cv::Mat t = (cv::Mat_<double>(3,1) << x, y, z);

    // Concatenate rotation matrix and translation vector
    cv::Mat Rt = cv::Mat::zeros(3, 4, CV_64F);
    R.copyTo(Rt(cv::Rect(0, 0, 3, 3)));
    t.copyTo(Rt(cv::Rect(3, 0, 1, 3)));

    // Add fourth row [0 0 0 1] to obtain 4x4 transformation matrix
    cv::Mat T = cv::Mat::eye(4, 4, CV_64F);
    Rt.copyTo(T(cv::Rect(0, 0, 4, 3)));

    return T;
}

bool isPointInFOV(cv::Mat& point3d, const cv::Mat& K, const cv::Mat& T_cw, const cv::Size& imgSize)
{
    // Convert 3D point to camera coordinates

    cv::Mat point4d(4, 1, CV_64F);
    point4d.at<double>(3) = 1.0;
    point4d.at<double>(0, 0) = point3d.at<double>(0, 0);
    point4d.at<double>(1, 0) = point3d.at<double>(0, 1);
    point4d.at<double>(2, 0) = point3d.at<double>(0, 2);
    cv::Mat pointCam = T_cw.inv() * point4d;
    
    // Check that the point is in front of the camera
    if (pointCam.at<double>(2) < 0.01)
        return false;
    
    // Check that the point is within the image bounds
    cv::Mat pointImg = K * pointCam.rowRange(0, 3);
    float u = pointImg.at<double>(0) / pointImg.at<double>(2);
    float v = pointImg.at<double>(1) / pointImg.at<double>(2);
    if (u < 0 || u >= imgSize.width || v < 0 || v >= imgSize.height)
        return false;
    
    // Point is within the field of view
    return true;
}

int main()
{
    std::string settingPath = Auxiliary::GetGeneralSettingsPath();
    std::ifstream programData(settingPath);
    nlohmann::json data;
    programData >> data;
    programData.close();

    // Check settings file
    cv::FileStorage fsSettings(data["DroneYamlPathSlam"], cv::FileStorage::READ);
    if(!fsSettings.isOpened())
    {
       std::cerr << "Failed to open settings file at: " << data["DroneYamlPathSlam"] << std::endl;
       exit(-1);
    }

    cv::Mat K = cv::Mat::eye(3, 3, CV_64F);
    fsSettings["Camera.fx"] >> K.at<double>(0, 0);
    fsSettings["Camera.fy"] >> K.at<double>(1, 1);
    fsSettings["Camera.cx"] >> K.at<double>(0, 2);
    fsSettings["Camera.cy"] >> K.at<double>(1, 2);
    K.at<double>(2, 2) = 1.0;

    cv::Size image_size(fsSettings["Camera.width"], fsSettings["Camera.height"]);

    std::vector<cv::Point3f> points;

    int frame_to_check = data["frameToCheck"];
    std::string map_inpur_dir = data["mapInputDir"];
    std::ifstream pointData;
    pointData.open(map_inpur_dir + "frameData" + std::to_string(frame_to_check) + ".csv");

    std::vector<std::string> row;
    std::string line, word, temp;
    
    std::getline(pointData, line);

    std::stringstream words(line);

    while (std::getline(words, word, ',')) {
        row.push_back(word);
    }

    pointData.close();

    // Extract the camera position
    double x = stod(row[1]);
    double y = stod(row[2]);
    double z = stod(row[3]);

    double yaw_rad = stod(row[4]);
    double pitch_rad = stod(row[5]);
    double roll_rad = stod(row[6]);
    
    // Convert the Euler angles to degrees
    double yaw_degree = yaw_rad * 180.0 / CV_PI;
    double pitch_degree = pitch_rad * 180.0 / CV_PI;
    double roll_degree = roll_rad * 180.0 / CV_PI;

    cv::Mat T_cw = createTransformationMatrix(x, y, z, yaw_rad, pitch_rad, roll_rad);

    pointData.open(map_inpur_dir + "cloud1.csv");

    while (!pointData.eof()) {
        row.clear();
        
        std::getline(pointData, line);

        std::stringstream words(line);

        if (line == "") {
            continue;
        }

        while (std::getline(words, word, ',')) {
            row.push_back(word);
        }
        
        cv::Mat curr_point = (cv::Mat_<double>(1, 3) << std::stod(row[0]), std::stod(row[1]), std::stod(row[2]));
        if (isPointInFOV(curr_point, K, T_cw, image_size))
            points.push_back(cv::Point3d(curr_point.at<double>(0), curr_point.at<double>(1), curr_point.at<double>(2)));
    }
    pointData.close();

    for(cv::Point3f  point : points)
    {
        std::cout << "(" << point.x << ", " << point.y << ", " << point.z << ")" << std::endl;
    }

    std::cout << points.size() << std::endl;

    return 0;
}
