//
// Created by Liam Vanunu
//

#include "include/Auxiliary.h"

#ifndef ORB_SLAM2_SIMULATOR_H
#define ORB_SLAM2_SIMULATOR_H

class Simulator {
public:
    // Methods
    Simulator();

    void Run();

    void ToggleFollowCamera();
    void ToggleShowPoints();
    void DoReset();
    void MoveLeft();
    void MoveRight();
    void MoveDown();
    void MoveUp();
    void MoveBackward();
    void MoveForward();
    void RotateLeft();
    void RotateRight();
    void RotateDown();
    void RotateUp();
    void FinishScan();

    std::vector<cv::Point3d> GetCloudPoint();

private:
    // Methods
    void reset();
    void applyUpToModelCam(double value);
    void applyRightToModelCam(double value);
    void applyForwardToModelCam(double value);

    void applyYawRotationToModelCam(double value);
    void applyPitchRotationToModelCam(double value);

    void drawMapPoints();

    void saveOnlyNewPoints();

    void BuildCloudScanned();

    // Members
    std::vector<cv::Point3d> mPointsSeen;
    std::vector<cv::Point3d> mNewPointsSeen;

    std::vector<cv::Point3d> mCloudScanned;

    double mPointSize;

    double mRotateScale;
    double mMovingScale;

    pangolin::OpenGlRenderState mS_cam;

    pangolin::View mD_cam;

    pangolin::OpenGlMatrix mTcw;
    pangolin::OpenGlMatrix mTwc;    

    bool mFollow;

    bool mFollowCamera;
    bool mShowPoints;
    bool mReset;
    bool mMoveLeft;
    bool mMoveRight;
    bool mMoveDown;
    bool mMoveUp;
    bool mMoveBackward;
    bool mMoveForward;
    bool mRotateLeft;
    bool mRotateRight;
    bool mRotateDown;
    bool mRotateUp;
    bool mFinishScan;

    float mViewpointX;
    float mViewpointY;
    float mViewpointZ;
    float mViewpointF;

    cv::Point3d mStartPosition;
    cv::Point3d mCurrentPosition;

    double mStartYaw;
    double mCurrentYaw;
    double mStartPitch;
    double mCurrentPitch;
    double mStartRoll;
    double mCurrentRoll;

    std::string mCloudPointPath;

};

#endif //ORB_SLAM2_SIMULATOR_H
