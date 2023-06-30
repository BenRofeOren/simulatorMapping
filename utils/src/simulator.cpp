#include "include/simulator.h"

#define RESULT_POINT_X 0.1
#define RESULT_POINT_Y 0.2
#define RESULT_POINT_Z 0.3

cv::Mat readDesc(const std::string& filename, int rows, int cols)
{
    std::ifstream file(filename);
    if (!file.is_open())
    {
        // Handle file open error
        return cv::Mat();
    }

    cv::Mat mat(rows, cols, CV_8UC1);

    std::string line;
    std::getline(file, line);
    std::istringstream iss(line);
    
    int row = 0, col = 0;
    std::string value;
    while (std::getline(iss, value, ','))
    {
        mat.at<uchar>(row, col) = static_cast<uchar>(std::stoi(value));
        col++;
    }

    file.close();

    return mat;
}

void Simulator::initPoints() {
    std::string settingPath = Auxiliary::GetGeneralSettingsPath();
    std::ifstream programData(settingPath);
    nlohmann::json data;
    programData >> data;
    programData.close();

    std::ifstream pointData;
    std::ifstream descData;
    std::vector<std::string> row;
    std::string line, word, temp;
    int pointIndex;
    cv::Mat currDesc;
    std::string currDescFilename;

    std::string mappingPath = data["simulatorPointsPath"];
    std::string pointsPath = mappingPath + "cloud0.csv";

    cv::Vec<double, 8> point;
    OfflineMapPoint *offlineMapPoint;
    pointData.open(pointsPath, std::ios::in);

    while (!pointData.eof()) {
        row.clear();

        std::getline(pointData, line);

        std::stringstream words(line);

        if (line == "") {
            continue;
        }

        while (std::getline(words, word, ',')) {
            try
            {
                std::stod(word);
            }
            catch(std::out_of_range)
            {
                word = "0";
            }
            row.push_back(word);
        }
        point = cv::Vec<double, 8>(std::stod(row[1]), std::stod(row[2]), std::stod(row[3]), std::stod(row[4]), std::stod(row[5]), std::stod(row[6]), std::stod(row[7]), std::stod(row[8]));

        pointIndex = std::stoi(row[0]);
        currDescFilename = mappingPath + "point" + std::to_string(pointIndex) + ".csv";
        currDesc = readDesc(currDescFilename, 1, 32);
        offlineMapPoint = new OfflineMapPoint(cv::Point3d(point[0], point[1], point[2]), point[3], point[4], cv::Point3d(point[5], point[6], point[7]), currDesc);
        this->mPoints.emplace_back(offlineMapPoint);
    }
    pointData.close();
}

Simulator::Simulator() {
    std::string settingPath = Auxiliary::GetGeneralSettingsPath();
    std::ifstream programData(settingPath);
    nlohmann::json data;
    programData >> data;
    programData.close();

    this->mPoints = std::vector<OfflineMapPoint*>();
    this->initPoints();

    this->mCloudScanned = std::vector<OfflineMapPoint*>();

    this->mRealResultPoint = cv::Point3d(RESULT_POINT_X, RESULT_POINT_Y, RESULT_POINT_Z);
    this->mResultPoint = cv::Point3d();

    this->mSimulatorViewerTitle = "Simulator Viewer";
    this->mResultsWindowTitle = "Results";

    std::string configPath = data["DroneYamlPathSlam"];
    cv::FileStorage fSettings(configPath, cv::FileStorage::READ);

    this->mViewpointX = fSettings["Viewer.ViewpointX"];
    this->mViewpointY = fSettings["Viewer.ViewpointY"];
    this->mViewpointZ = fSettings["Viewer.ViewpointZ"];
    this->mViewpointF = fSettings["Viewer.ViewpointF"];

    std::string map_input_dir = data["mapInputDir"];
    this->mCloudPointPath = map_input_dir + "cloud1.csv";

    double startPointX = data["startingCameraPosX"];
    double startPointY = data["startingCameraPosY"];
    double startPointZ = data["startingCameraPosZ"];

    this->mStartPosition = cv::Point3d(startPointX, startPointY, startPointZ);

    this->mStartYaw = data["yawRad"];
    this->mStartPitch = data["pitchRad"];
    this->mStartRoll = data["rollRad"];

    this->mPointSize = fSettings["Viewer.PointSize"];
    this->mResultsPointSize = this->mPointSize * 5;

    this->mTwc.SetIdentity();
    this->mTcw.SetIdentity();

    this->mMovingScale = data["movingScale"];
    this->mRotateScale = data["rotateScale"];

    this->build_window(this->mSimulatorViewerTitle);

    this->mFollowCamera = true;
    this->mShowPoints = true;
    this->mReset = false;
    this->mMoveLeft = false;
    this->mMoveRight = false;
    this->mMoveDown = false;
    this->mMoveUp = false;
    this->mMoveBackward = false;
    this->mMoveForward = false;
    this->mRotateLeft = false;
    this->mRotateRight = false;
    this->mRotateDown = false;
    this->mRotateUp = false;
    this->mFinishScan = false;

    // Define Camera Render Object (for view / scene browsing)
    this->mS_cam = pangolin::OpenGlRenderState(
            pangolin::ProjectionMatrix(1024, 768, this->mViewpointF, this->mViewpointF, 512, 389, 0.1, 1000),
            pangolin::ModelViewLookAt(this->mViewpointX, this->mViewpointY, this->mViewpointZ, 0, 0, 0, 0.0, -1.0, 0.0)
    );

    // Add named OpenGL viewport to window and provide 3D Handler
    this->mD_cam = pangolin::CreateDisplay()
            .SetBounds(0.0, 1.0, pangolin::Attach::Pix(175), 1.0, -1024.0f / 768.0f)
            .SetHandler(new pangolin::Handler3D(this->mS_cam));

    this->reset();
}

void Simulator::build_window(std::string title) {
    pangolin::CreateWindowAndBind(title, 1024, 768);

    // 3D Mouse handler requires depth testing to be enabled
    glEnable(GL_DEPTH_TEST);

    // Issue specific OpenGl we might need
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

std::vector<OfflineMapPoint*> Simulator::getPointsFromTcw() {
    std::string settingPath = Auxiliary::GetGeneralSettingsPath();
    std::ifstream programData(settingPath);
    nlohmann::json data;
    programData >> data;
    programData.close();

    std::ifstream pointData;
    std::vector<std::string> row;
    std::string line, word, temp;

    // Check settings file
    cv::FileStorage fsSettings(data["DroneYamlPathSlam"], cv::FileStorage::READ);
    if(!fsSettings.isOpened())
    {
        std::cerr << "Failed to open settings file at: " << data["DroneYamlPathSlam"] << std::endl;
        exit(-1);
    }

    double fx = fsSettings["Camera.fx"];
    double fy = fsSettings["Camera.fy"];
    double cx = fsSettings["Camera.cx"];
    double cy = fsSettings["Camera.cy"];
    int width = fsSettings["Camera.width"];
    int height = fsSettings["Camera.height"];

    double minX = 3.7;
    double maxX = width;
    double minY = 3.7;
    double maxY = height;

    cv::Mat Tcw_cv = cv::Mat::eye(4, 4, CV_64FC1);
    for(int i=0;i<4;i++){
        for(int j=0;j<4;j++){
            Tcw_cv.at<double>(i,j) = this->mTcw.m[j * 4 + i];
        }
    }

    cv::Mat Rcw = Tcw_cv.rowRange(0, 3).colRange(0, 3);
    cv::Mat Rwc = Rcw.t();
    cv::Mat tcw = Tcw_cv.rowRange(0, 3).col(3);
    cv::Mat mOw = -Rcw.t() * tcw;

    // Save Twc for s_cam
    cv::Mat Twc_cv = cv::Mat::eye(4, 4, CV_64FC1);
    Rwc.copyTo(Twc_cv.rowRange(0,3).colRange(0,3));
    Twc_cv.at<double>(0, 3) = mOw.at<double>(0);
    Twc_cv.at<double>(1, 3) = mOw.at<double>(1);
    Twc_cv.at<double>(2, 3) = mOw.at<double>(2);

    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            this->mTwc.m[j * 4 + i] = Twc_cv.at<double>(i, j);
        }
    }

    std::vector<OfflineMapPoint*> seen_points;

    for(OfflineMapPoint*  point : this->mPoints)
    {
        cv::Mat worldPos = cv::Mat::zeros(3, 1, CV_64F);
        worldPos.at<double>(0) = point->point.x;
        worldPos.at<double>(1) = point->point.y;
        worldPos.at<double>(2) = point->point.z;

        const cv::Mat Pc = Rcw*worldPos+tcw;
        const double &PcX = Pc.at<double>(0);
        const double &PcY= Pc.at<double>(1);
        const double &PcZ = Pc.at<double>(2);

        // Check positive depth
        if(PcZ<0.0f)
            continue;

        // Project in image and check it is not outside
        const double invz = 1.0f/PcZ;
        const double u=fx*PcX*invz+cx;
        const double v=fy*PcY*invz+cy;

        if(u<minX || u>maxX)
            continue;
        if(v<minY || v>maxY)
            continue;

        // Check distance is in the scale invariance region of the MapPoint
        const double minDistance = point->minDistanceInvariance;
        const double maxDistance = point->maxDistanceInvariance;
        const cv::Mat PO = worldPos-mOw;
        const double dist = cv::norm(PO);

        if(dist<minDistance || dist>maxDistance)
            continue;

        // Check viewing angle
        cv::Mat Pn = cv::Mat(3, 1, CV_64F);
        Pn.at<double>(0) = point->normal.x;
        Pn.at<double>(1) = point->normal.y;
        Pn.at<double>(2) = point->normal.z;

        const double viewCos = PO.dot(Pn)/dist;

        if(viewCos<0.5)
            continue;

        seen_points.push_back(point);
    }

    return seen_points;
}

void Simulator::reset() {
    this->mShowPoints = true;
    this->mFollow = true;
    this->mFollowCamera = true;
    this->mReset = false;

    this->mCurrentPosition = this->mStartPosition;
    this->mCurrentYaw = this->mStartYaw;
    this->mCurrentPitch = this->mStartPitch;
    this->mCurrentRoll = this->mStartRoll;

    // Opengl has inversed Y axis
    // Assign yaw, pitch and roll rotations and translation
    Eigen::Matrix4d Tcw_eigen = Eigen::Matrix4d::Identity();
    Tcw_eigen.block<3, 3>(0, 0) = (Eigen::AngleAxisd(this->mCurrentRoll, Eigen::Vector3d::UnitZ()) * 
                            Eigen::AngleAxisd(this->mCurrentYaw, Eigen::Vector3d::UnitY()) *
                            Eigen::AngleAxisd(this->mCurrentPitch, Eigen::Vector3d::UnitX())).toRotationMatrix();
    Tcw_eigen(0, 3) = this->mCurrentPosition.x;
    Tcw_eigen(1, 3) = -this->mCurrentPosition.y;
    Tcw_eigen(2, 3) = this->mCurrentPosition.z;

    for(int i=0;i<4;i++){
        for(int j=0;j<4;j++){
            this->mTcw.m[j * 4 + i] = Tcw_eigen(i,j);
        }
    }

    this->mNewPointsSeen = this->getPointsFromTcw();

    this->mPointsSeen = std::vector<OfflineMapPoint*>();
}

void Simulator::ToggleFollowCamera() {
    this->mFollowCamera = !this->mFollowCamera;
}

void Simulator::ToggleShowPoints() {
    this->mShowPoints = !this->mShowPoints;
}

void Simulator::DoReset() {
    this->mReset = true;
}

void Simulator::MoveLeft() {
    this->mMoveLeft = true;
}

void Simulator::MoveRight() {
    this->mMoveRight = true;
}

void Simulator::MoveDown() {
    this->mMoveDown = true;
}

void Simulator::MoveUp() {
    this->mMoveUp = true;
}

void Simulator::MoveBackward() {
    this->mMoveBackward = true;
}

void Simulator::MoveForward() {
    this->mMoveForward = true;
}

void Simulator::RotateLeft() {
    this->mRotateLeft = true;
}

void Simulator::RotateRight() {
    this->mRotateRight = true;
}

void Simulator::RotateDown() {
    this->mRotateDown = true;
}

void Simulator::RotateUp() {
    this->mRotateUp = true;
}

void Simulator::FinishScan() {
    this->mFinishScan = true;
}

void Simulator::applyUpToModelCam(double value) {
    // Values are opposite
    this->mTcw.m[3 * 4 + 1] -= value;
}

void Simulator::applyRightToModelCam(double value) {
    // Values are opposite
    this->mTcw.m[3 * 4 + 0] -= value;
}

void Simulator::applyForwardToModelCam(double value) {
    // Values are opposite
    this->mTcw.m[3 * 4 + 2] -= value;
}

void Simulator::applyYawRotationToModelCam(double value) {
    Eigen::Matrix4d Tcw_eigen = pangolin::ToEigen<double>(this->mTcw);

    // Values are opposite
    double rand = -value * (M_PI / 180);
    double c = std::cos(rand);
    double s = std::sin(rand);

    Eigen::Matrix3d R;
    R << c, 0, s,
        0, 1, 0,
        -s, 0, c;

    Eigen::Matrix4d pangolinR = Eigen::Matrix4d::Identity();
    pangolinR.block<3, 3>(0, 0) = R;

    // Left-multiply the rotation
    Tcw_eigen = pangolinR * Tcw_eigen;

    // Convert back to pangolin matrix and set
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            this->mTcw.m[j * 4 + i] = Tcw_eigen(i, j);
        }
    }
}

void Simulator::applyPitchRotationToModelCam(double value) {
    Eigen::Matrix4d Tcw_eigen = pangolin::ToEigen<double>(this->mTcw);

    // Values are opposite
    double rand = -value * (M_PI / 180);
    double c = std::cos(rand);
    double s = std::sin(rand);

    Eigen::Matrix3d R;
    R << 1, 0, 0,
        0, c, -s,
        0, s, c;

    Eigen::Matrix4d pangolinR = Eigen::Matrix4d::Identity();
    pangolinR.block<3, 3>(0, 0) = R;

    // Left-multiply the rotation
    Tcw_eigen = pangolinR * Tcw_eigen;

    // Convert back to pangolin matrix and set
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            this->mTcw.m[j * 4 + i] = Tcw_eigen(i, j);
        }
    }
}

void Simulator::drawMapPoints()
{
    std::vector<OfflineMapPoint*> pointsExceptThisFrame = this->mPointsSeen;
    std::vector<OfflineMapPoint*>::iterator it;
    for (it = pointsExceptThisFrame.begin(); it != pointsExceptThisFrame.end();)
    {
        if (std::find(this->mCurrentFramePoints.begin(), this->mCurrentFramePoints.end(), *it) != this->mCurrentFramePoints.end())
        {
            it = pointsExceptThisFrame.erase(it);
        }
        else
        {
            ++it;
        }
    }

    std::vector<OfflineMapPoint*> oldPointsFromFrame = this->mCurrentFramePoints;
    for (it = oldPointsFromFrame.begin(); it != oldPointsFromFrame.end();)
    {
        if (std::find(this->mNewPointsSeen.begin(), this->mNewPointsSeen.end(), *it) != this->mNewPointsSeen.end())
        {
            it = oldPointsFromFrame.erase(it);
        }
        else
        {
            ++it;
        }
    }

    glPointSize((GLfloat)this->mPointSize);
    glBegin(GL_POINTS);
    glColor3f(0.0,0.0,0.0);

    for(auto point : pointsExceptThisFrame)
    {
        glVertex3f((float)point->point.x, (float)point->point.y, (float)point->point.z);
    }
    glEnd();

    glPointSize((GLfloat)this->mPointSize);
    glBegin(GL_POINTS);
    glColor3f(1.0,0.0,0.0);

    for(auto point : oldPointsFromFrame)
    {
        glVertex3f((float)point->point.x, (float)point->point.y, (float)point->point.z);

    }
    glEnd();

    glPointSize((GLfloat)this->mPointSize);
    glBegin(GL_POINTS);
    glColor3f(0.0,1.0,0.0);

    for(auto point : this->mNewPointsSeen)
    {
        glVertex3f((float)point->point.x, (float)point->point.y, (float)point->point.z);

    }
    glEnd();
}

bool areMatricesEqual(const pangolin::OpenGlMatrix& matrix1, const pangolin::OpenGlMatrix& matrix2) {
    for (int i = 0; i < 16; i++) {
        if (matrix1.m[i] != matrix2.m[i])
            return false;
    }
    return true;
}

void Simulator::saveOnlyNewPoints() {
    this->mNewPointsSeen = this->mCurrentFramePoints;
    std::vector<OfflineMapPoint*>::iterator it;
    for (it = this->mNewPointsSeen.begin(); it != this->mNewPointsSeen.end();)
    {
        if (std::find(this->mPointsSeen.begin(), this->mPointsSeen.end(), *it) != this->mPointsSeen.end())
        {
            it = this->mNewPointsSeen.erase(it);
        }
        else
        {
            ++it;
        }
    }
}

void assignPreviousTwc(pangolin::OpenGlMatrix& matrix1, const pangolin::OpenGlMatrix& matrix2) {
    for (int i = 0; i < 16; i++) {
        matrix1.m[i] = matrix2.m[i];
    }
}

void Simulator::Run() {
    pangolin::OpenGlMatrix previousTwc;

    while (!this->mFinishScan) {
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        if (this->mFollowCamera && this->mFollow) {
            this->mS_cam.Follow(this->mTwc);
        } else if (this->mFollowCamera && !this->mFollow) {
            this->mS_cam.SetModelViewMatrix(
                    pangolin::ModelViewLookAt(mViewpointX, mViewpointY, mViewpointZ, 0, 0, 0, 0.0, -1.0, 0.0));
            this->mS_cam.Follow(this->mTwc);
            this->mFollow = true;
        } else if (!this->mFollowCamera && this->mFollow) {
            this->mFollow = false;
        }

        this->mCurrentFramePoints = this->getPointsFromTcw();

        if (!areMatricesEqual(previousTwc, this->mTwc)) {
            this->saveOnlyNewPoints();
            this->mPointsSeen.insert(this->mPointsSeen.end(), this->mNewPointsSeen.begin(), this->mNewPointsSeen.end());
        }

        this->mD_cam.Activate(this->mS_cam);

        glClearColor(1.0f, 1.0f, 1.0f, 1.0f);
        if (this->mShowPoints) {
            this->drawMapPoints();
        }

        pangolin::FinishFrame();

        assignPreviousTwc(previousTwc, this->mTwc);

        if (this->mMoveLeft)
        {
            this->applyRightToModelCam(-this->mMovingScale);
            this->mMoveLeft = false;
        }

        if (this->mMoveRight)
        {
            this->applyRightToModelCam(this->mMovingScale);
            this->mMoveRight = false;
        }

        if (this->mMoveDown)
        {
            // Opengl has inversed Y axis so we pass -value
            this->applyUpToModelCam(this->mMovingScale);
            this->mMoveDown = false;
        }

        if (this->mMoveUp)
        {
            // Opengl has inversed Y axis so we pass -value
            this->applyUpToModelCam(-this->mMovingScale);
            this->mMoveUp = false;
        }

        if (this->mMoveBackward)
        {
            this->applyForwardToModelCam(-this->mMovingScale);
            this->mMoveBackward = false;
        }

        if (this->mMoveForward)
        {
            this->applyForwardToModelCam(this->mMovingScale);
            this->mMoveForward = false;
        }

        if (this->mRotateLeft)
        {
            this->applyYawRotationToModelCam(-this->mRotateScale);
            this->mRotateLeft = false;
        }

        if (this->mRotateRight)
        {
            this->applyYawRotationToModelCam(this->mRotateScale);
            this->mRotateRight = false;
        }

        if (this->mRotateDown)
        {
            this->applyPitchRotationToModelCam(-this->mRotateScale);
            this->mRotateDown = false;
        }

        if (this->mRotateUp)
        {
            this->applyPitchRotationToModelCam(this->mRotateScale);
            this->mRotateUp = false;
        }

        if (mReset) {
            this->reset();
        }
    }

    pangolin::DestroyWindow(this->mSimulatorViewerTitle);
    this->build_window(this->mResultsWindowTitle);

    this->BuildCloudScanned();
}

std::vector<OfflineMapPoint*> Simulator::GetCloudPoint() {
    return this->mCloudScanned;
}

void Simulator::BuildCloudScanned() {
    // Erased mNewPointsSeen to only new points but not combined yet so insert both
    this->mCloudScanned.insert(this->mCloudScanned.end(), this->mNewPointsSeen.begin(), this->mNewPointsSeen.end());
    this->mCloudScanned.insert(this->mCloudScanned.end(), this->mPointsSeen.begin(), this->mPointsSeen.end());
}

void Simulator::SetResultPoint(cv::Point3d resultPoint) {
    this->mResultPoint = resultPoint;
}

void Simulator::drawResultPoints() {
    // Remove result point and real result point from cloud scanned if exist
    for(int i = 0; i < this->mCloudScanned.size(); i++) {
        if (*this->mCloudScanned[i] == this->mResultPoint) {
            this->mCloudScanned.erase(this->mCloudScanned.begin() + i);
            break;
        }
    }
    for(int i = 0; i < this->mCloudScanned.size(); i++) {
        if (*this->mCloudScanned[i] == this->mRealResultPoint) {
            this->mCloudScanned.erase(this->mCloudScanned.begin() + i);
            break;
        }
    }
    // this->mCloudScanned.erase(std::remove(this->mCloudScanned.begin(), this->mCloudScanned.end(), this->mResultPoint), this->mCloudScanned.end());
    // this->mCloudScanned.erase(std::remove(this->mCloudScanned.begin(), this->mCloudScanned.end(), this->mRealResultPoint), this->mCloudScanned.end());

    glPointSize((GLfloat)this->mPointSize);
    glBegin(GL_POINTS);
    glColor3f(0.0,0.0,0.0);

    for(auto point : this->mCloudScanned)
    {
        glVertex3f((float)point->point.x, (float)point->point.y, (float)point->point.z);
    }

    glEnd();

    glPointSize((GLfloat)this->mResultsPointSize);
    glBegin(GL_POINTS);
    glColor3f(1.0,0.0,0.0);

    glVertex3f((float)this->mResultPoint.x, (float)this->mResultPoint.y, (float)this->mResultPoint.z);

    glEnd();

    glPointSize((GLfloat)this->mResultsPointSize);
    glBegin(GL_POINTS);
    glColor3f(0.0,1.0,0.0);

    glVertex3f((float)this->mRealResultPoint.x, (float)this->mRealResultPoint.y, (float)this->mRealResultPoint.z);

    glEnd();
}

void Simulator::updateTwcByResultPoint() {
}

void Simulator::CheckResults() {
    this->mCloseResults = false;

    while (!this->mCloseResults) {
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        this->updateTwcByResultPoint();

        this->mS_cam.Follow(this->mTwc);
        this->mD_cam.Activate(this->mS_cam);

        glClearColor(1.0f, 1.0f, 1.0f, 1.0f);
        
        this->drawResultPoints();

        pangolin::FinishFrame();
    }
}

Simulator::~Simulator() {
    pangolin::DestroyWindow(this->mResultsWindowTitle);
    for (auto ptr : this->mPoints) {
        free(ptr);
    }
}
