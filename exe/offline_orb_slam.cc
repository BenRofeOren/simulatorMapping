#include <memory>
#include <string>
#include <thread>
#include <iostream>
#include <unistd.h>
#include <unordered_set>
#include <nlohmann/json.hpp>
#include <opencv2/videoio.hpp>
#include <opencv2/highgui.hpp>

#include "System.h"
#include "Converter.h"
#include "include/Point.h"
#include "include/Auxiliary.h"

/************* SIGNAL *************/
std::unique_ptr<ORB_SLAM2::System> SLAM;
std::string simulatorOutputDir;


void saveFrame(cv::Mat &img, cv::Mat pose, int currentFrameId) {
    if (img.empty()) 
    {
        std::cout << "Image is empty!!!" << std::endl;
        return;
    }
    std::ofstream frameData;
    frameData.open(simulatorOutputDir + "frameData" +
                   std::to_string(currentFrameId) + ".csv");

    cv::Mat Rwc = pose.rowRange(0, 3).colRange(0, 3);
    cv::Mat twc = -Rwc.t() * pose.rowRange(0, 3).col(3);
    frameData << currentFrameId << ',' << twc.at<float>(0) << ',' << twc.at<float>(2) << ',' << twc.at<float>(1) << ','
              << Rwc.at<float>(0, 0) << ',' << Rwc.at<float>(0, 1) << ',' << Rwc.at<float>(0, 2)
              << ',' << Rwc.at<float>(1, 0) << ',' << Rwc.at<float>(1, 1) << ',' << Rwc.at<float>(1, 2) << ','
              << Rwc.at<float>(2, 0)
              << ',' << Rwc.at<float>(2, 1) << ',' << Rwc.at<float>(2, 2) << std::endl;
    cv::imwrite(simulatorOutputDir + "frame_" + std::to_string(currentFrameId) + ".png", img);
    frameData.close();
}

void saveMap(int mapNumber) {
    std::ofstream pointData;
    std::unordered_set<int> seen_frames;

    pointData.open(simulatorOutputDir + "cloud" + std::to_string(mapNumber) + ".csv");
    for (auto &p: SLAM->GetMap()->GetAllMapPoints()) {
        if (p != nullptr && !p->isBad()) {
            auto point = p->GetWorldPos();
            Eigen::Matrix<double, 3, 1> v = ORB_SLAM2::Converter::toVector3d(point);
            pointData << v.x() << "," << v.y() << "," << v.z();
            std::map<ORB_SLAM2::KeyFrame*, size_t> observations = p->GetObservations();
            for (auto obs : observations) {
                ORB_SLAM2::KeyFrame *currentFrame = obs.first;
                if (!currentFrame->image.empty())
                {
                    size_t pointIndex = obs.second;
                    cv::KeyPoint keyPoint = currentFrame->mvKeysUn[pointIndex];
                    cv::Point2f featurePoint = keyPoint.pt;
                    pointData << "," << currentFrame->mnId << "," << featurePoint.x << "," << featurePoint.y;
                    if (seen_frames.count(currentFrame->mnId) <= 0)
                    {
                        saveFrame(currentFrame->image, currentFrame->GetPose(), currentFrame->mnId);
                        seen_frames.insert(currentFrame->mnId);
                    }
                    // cv::Mat image = cv::imread(simulatorOutputDir + "frame_" + std::to_string(currentFrame->mnId) + ".png");
                    // cv::arrowedLine(image, featurePoint, cv::Point2f(featurePoint.x, featurePoint.y - 100), cv::Scalar(0, 0, 255), 2, 8, 0, 0.1);
                    // cv::imshow("image", image);
                    // cv::waitKey(0);
                }
            }
            pointData << std::endl;
        }
    }
    pointData.close();
    std::cout << "saved map" << std::endl;

}

void stopProgramHandler(int s) {
    saveMap(std::chrono::steady_clock::now().time_since_epoch().count());
    SLAM->Shutdown();
    cvDestroyAllWindows();
    std::cout << "stoped program" << std::endl;
    exit(1);
}

int main() {
    signal(SIGINT, stopProgramHandler);
    signal(SIGTERM, stopProgramHandler);
    signal(SIGABRT, stopProgramHandler);
    signal(SIGSEGV, stopProgramHandler);
    std::string settingPath = Auxiliary::GetGeneralSettingsPath();
    std::ifstream programData(settingPath);
    nlohmann::json data;
    programData >> data;
    programData.close();
    char currentDirPath[256];
    getcwd(currentDirPath, 256);

    char time_buf[21];
    time_t now;
    std::time(&now);
    std::strftime(time_buf, 21, "%Y-%m-%d_%H:%S:%MZ", gmtime(&now));
    std::string currentTime(time_buf);
    std::string vocPath = data["VocabularyPath"];
    std::string droneYamlPathSlam = data["DroneYamlPathSlam"];
    std::string videoPath = data["offlineVideoTestPath"];
    bool loadMap = data["loadMap"];
    bool isSavingMap = data["saveMap"];
    std::string loadMapPath = data["loadMapPath"];
    std::string simulatorOutputDirPath = data["simulatorOutputDir"];
    simulatorOutputDir = simulatorOutputDirPath + currentTime + "/";
    std::filesystem::create_directory(simulatorOutputDir);
    SLAM = std::make_unique<ORB_SLAM2::System>(vocPath, droneYamlPathSlam, ORB_SLAM2::System::MONOCULAR, true, loadMap,
                                               loadMapPath,
                                               true);
    int amountOfAttepmpts = 0;
    while (amountOfAttepmpts++ < 1) {
        cv::VideoCapture capture(videoPath);
        if (!capture.isOpened()) {
            std::cout << "Error opening video stream or file" << std::endl;
            return 0;
        } else {
            std::cout << "Success opening video stream or file" << std::endl;
        }

        cv::Mat frame;
        std::chrono::steady_clock::time_point begin = std::chrono::steady_clock::now();
        for (int i = 0; i < 170; ++i) {
            capture >> frame;

        }
        int amount_of_frames = 1;

        for (;;) {
            auto pose = SLAM->TrackMonocular(frame, capture.get(CV_CAP_PROP_POS_MSEC));
            if (!pose.empty()) {
                // saveFrame(frame, *pose, SLAM->GetTracker()->mCurrentFrame.mnId);
            }
            capture >> frame;

            if (frame.empty()) {
                break;
            }
        }
        saveMap(amountOfAttepmpts);
        std::chrono::steady_clock::time_point end = std::chrono::steady_clock::now();

        std::cout << "Time difference = " << std::chrono::duration_cast<std::chrono::milliseconds>(end - begin).count()
                  << std::endl;
        std::cout << amount_of_frames << std::endl;
        capture.release();
    }

    //saveMap(amountOfAttepmpts);
    if (isSavingMap) {
        SLAM->SaveMap(simulatorOutputDir + "simulatorMap.bin");
    }
    //sleep(20);
    SLAM->Shutdown();
    cvDestroyAllWindows();

    return 0;
}

