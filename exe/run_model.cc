#include <thread>
#include <future>
#include <queue>

#include <pangolin/pangolin.h>
#include <pangolin/geometry/geometry.h>
#include <pangolin/gl/glsl.h>
#include <pangolin/gl/glvbo.h>

#include <pangolin/utils/file_utils.h>

#include <pangolin/geometry/geometry_ply.h>
#include <pangolin/geometry/glgeometry.h>

#include "include/run_model/TextureShader.h"
#include "include/Auxiliary.h"

#include <Eigen/SVD>
#include <Eigen/Geometry>

#include <opencv2/core.hpp>
#include <opencv2/opencv.hpp>
#include <opencv2/calib3d.hpp>

void writeMatrixToCsv(Eigen::Matrix4d mat, nlohmann::json filename)
{
    std::ofstream csv_file(filename, std::ios::trunc);

    if (!csv_file.is_open()) {
        std::cerr << "Error: could not open file '" << filename << "' for writing." << std::endl;
        return;
    }

    for (int i = 0; i < mat.rows(); i++) {
        for (int j = 0; j < mat.cols(); j++) {
            csv_file << mat(i, j) << ",";
        }
        csv_file << std::endl;
    }
    csv_file.close();
}

std::vector<cv::Point3d> read_orb_points(std::string filename) {
    std::vector<cv::Point3d> points;

    std::ifstream pointData;
    std::vector<std::string> row;
    std::string line, word, temp;

    pointData.open(filename, std::ios::in);

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
        points.emplace_back(std::stod(row[0]), std::stod(row[1]), std::stod(row[2]));
        std::cout << "X: " << std::stod(row[0]) << ", Y: " << std::stod(row[1]) << ", Z: " << std::stod(row[2]) << std::endl;
    }
    pointData.close();

    return points;
}

void drawPoints(std::vector<cv::Point3d> seen_points, std::vector<cv::Point3d> new_points_seen) {
    std::string settingPath = Auxiliary::GetGeneralSettingsPath();
    std::ifstream programData(settingPath);
    nlohmann::json data;
    programData >> data;
    programData.close();

    const int point_size = data["pointSize"];

    glPointSize(point_size);
    glBegin(GL_POINTS);
    glColor3f(0.0, 0.0, 0.0);

    for (auto point: seen_points) {
        glVertex3f((float) (point.x), (float) (point.y), (float) (point.z));
    }
    glEnd();

    glPointSize(point_size);
    glBegin(GL_POINTS);
    glColor3f(1.0, 0.0, 0.0);

    for (auto point: new_points_seen) {
        glVertex3f((float) (point.x), (float) (point.y), (float) (point.z));
    }
    std::cout << new_points_seen.size() << std::endl;

    glEnd();
}

void saveCameraParameters(const std::string& filename, const cv::Mat& K, const cv::Mat& R, const cv::Mat& t) {
    cv::FileStorage fs(filename, cv::FileStorage::WRITE);
    fs << "K" << K;
    fs << "R" << R;
    fs << "t" << t;
    fs.release();
}

void saveDepthBuffer(const std::string& filename, std::string filename_visual, int width, int height) {
    cv::Mat depth(height, width, CV_32FC1);
    glReadPixels(0, 0, width, height, GL_DEPTH_COMPONENT, GL_FLOAT, depth.data);
    cv::flip(depth, depth, 0); // Flip the image vertically
    cv::imwrite(filename, depth * 1e6); // Save depth in 16-bit format (scaled)

    // Normalize the depth buffer to a range of 0 to 255
    double minVal, maxVal;
    cv::minMaxIdx(depth, &minVal, &maxVal);
    cv::Mat depth_normalized;
    depth.convertTo(depth_normalized, CV_8UC1, 255 / (maxVal - minVal), -minVal * 255 / (maxVal - minVal));

    // Apply a colormap to the depth image for visualization
    cv::Mat depth_colored;
    cv::applyColorMap(depth_normalized, depth_colored, cv::COLORMAP_JET);

    // Save the depth buffer as a colored image
    cv::imwrite(filename_visual, depth_colored);

}

// cv::Mat to Eigen::Matrix4d conversion function
cv::Mat eigenToCVMat(const Eigen::Matrix4d& mat) {
    // Create a 4x4 matrix
    cv::Mat mat_cv = cv::Mat::zeros(4, 4, CV_64F);

    // Check if the input matrix is of size 4x4 and has the correct type
    CV_Assert(mat.rows() == 4 && mat.cols() == 4);

    // Copy the values from cv::Mat to Eigen::Matrix4d
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            mat_cv.at<double>(i, j) = mat(i, j);
        }
    }

    return mat_cv;
}

cv::Mat eigenToCVMat(const Eigen::Matrix3d& mat) {
    // Create a 3x3 matrix
    cv::Mat mat_cv = cv::Mat::zeros(3, 3, CV_64F);;

    // Check if the input matrix is of size 3x3 and has the correct type
    CV_Assert(mat.rows() == 3 && mat.cols() == 3);

    // Copy the values from cv::Mat to Eigen::Matrix4d
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            mat_cv.at<double>(i, j) = mat(i, j);
        }
    }

    return mat_cv;
}

int main(int argc, char **argv) {
    const float w = 640.0f;
    const float h = 480.0f;

    using namespace pangolin;

    std::string settingPath = Auxiliary::GetGeneralSettingsPath();
    std::ifstream programData(settingPath);
    nlohmann::json data;
    programData >> data;
    programData.close();

    std::string configPath = data["DroneYamlPathSlam"];
    cv::FileStorage fSettings(configPath, cv::FileStorage::READ);
    
    float fx = fSettings["Camera.fx"];
    float fy = fSettings["Camera.fy"];
    float cx = fSettings["Camera.cx"];
    float cy = fSettings["Camera.cy"];
    float viewpointX = fSettings["RunModel.ViewpointX"];
    float viewpointY = fSettings["RunModel.ViewpointY"];
    float viewpointZ = fSettings["RunModel.ViewpointZ"];

    Eigen::Matrix3d K;
    K << fx, 0.0, cx, 0.0, fy, cy, 0.0, 0.0, 1.0;
    Eigen::Vector2i viewport_size(640, 480);


    // Options
    bool show_bounds = false;
    bool show_axis = false;
    bool show_x0 = false;
    bool show_y0 = false;
    bool show_z0 = false;
    bool cull_backfaces = false;

    // Create Window for rendering
    pangolin::CreateWindowAndBind("Main", w, h);
    glEnable(GL_DEPTH_TEST);

    // Define Projection and initial ModelView matrix
    pangolin::OpenGlRenderState s_cam(
            pangolin::ProjectionMatrix(viewport_size(0), viewport_size(1), K(0, 0), K(1, 1), K(0, 2), K(1, 2), 0.1, 10000),
            pangolin::ModelViewLookAt(viewpointX, viewpointY, viewpointZ, 0, 0, 0, 0.0, -1.0, pangolin::AxisY)
    );

    // Create Interactive View in window
    pangolin::Handler3D handler(s_cam);
    pangolin::View &d_cam = pangolin::CreateDisplay()
            .SetBounds(0.0, 1.0, 0.0, 1.0, -w / h)
            .SetHandler(&handler);

    // Load Geometry asynchronously
    std::string model_path = data["modelPath"];
    const pangolin::Geometry geom_to_load = pangolin::LoadGeometry(model_path);
    auto aabb = pangolin::GetAxisAlignedBox(geom_to_load);
    Eigen::AlignedBox3f total_aabb;
    total_aabb.extend(aabb);
    const auto mvm = pangolin::ModelViewLookAt(viewpointX, viewpointY, viewpointZ, 0, 0, 0, 0.0, -1.0, pangolin::AxisY);
    const auto proj = pangolin::ProjectionMatrix(viewport_size(0), viewport_size(1), K(0, 0), K(1, 1), K(0, 2), K(1, 2), 0.1, 10000);
    s_cam.SetModelViewMatrix(mvm);
    s_cam.SetProjectionMatrix(proj);
    const pangolin::GlGeometry geomToRender = pangolin::ToGlGeometry(geom_to_load);
    // Render tree for holding object position
    pangolin::GlSlProgram default_prog;
    auto LoadProgram = [&]() {
        default_prog.ClearShaders();
        default_prog.AddShader(pangolin::GlSlAnnotatedShader, shader);
        default_prog.Link();
    };
    LoadProgram();
    pangolin::RegisterKeyPressCallback('b', [&]() { show_bounds = !show_bounds; });
    pangolin::RegisterKeyPressCallback('0', [&]() { cull_backfaces = !cull_backfaces; });

    // Show axis and axis planes
    pangolin::RegisterKeyPressCallback('a', [&]() { show_axis = !show_axis; });
    pangolin::RegisterKeyPressCallback('x', [&]() { show_x0 = !show_x0; });
    pangolin::RegisterKeyPressCallback('y', [&]() { show_y0 = !show_y0; });
    pangolin::RegisterKeyPressCallback('z', [&]() { show_z0 = !show_z0; });

    Eigen::Vector3d Pick_w = handler.Selected_P_w();
    std::vector<Eigen::Vector3d> Picks_w;
    cv::Mat Twc;

    int frame_to_check = data["frameNumber"];
    std::string orbs_filename = std::string(data["framesOutput"]) + "frame_" + std::to_string(frame_to_check) + "_orbs.csv";
    std::vector<cv::Point3d> orbs_points = read_orb_points(orbs_filename);

    while (!pangolin::ShouldQuit()) {
        if ((handler.Selected_P_w() - Pick_w).norm() > 1E-6) {
            Pick_w = handler.Selected_P_w();
            Picks_w.push_back(Pick_w);
            std::cout << pangolin::FormatString("\"Translation\": [%,%,%]", Pick_w[0], Pick_w[1], Pick_w[2])
                      << std::endl;
        }

        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        // Load any pending geometry to the GPU.
        if (d_cam.IsShown()) {
            d_cam.Activate();

            if (cull_backfaces) {
                glEnable(GL_CULL_FACE);
                glCullFace(GL_BACK);
            }
            default_prog.Bind();
            default_prog.SetUniform("KT_cw",  s_cam.GetProjectionMatrix() *  s_cam.GetModelViewMatrix());
            pangolin::GlDraw( default_prog, geomToRender, nullptr);
            default_prog.Unbind();
            Eigen::Matrix4d mv_mat = s_cam.GetModelViewMatrix();
            Eigen::Matrix4d proj_mat = s_cam.GetProjectionMatrix();

            // Extract the translation part (the last column) of the matrix
            Eigen::Vector4d translation = Eigen::Vector4d(mv_mat(12), mv_mat(13), mv_mat(14), mv_mat(15));

            // Divide by the homogeneous coordinate to get the camera position in Euclidean space
            Eigen::Vector3d camera_pos = translation.hnormalized().head<3>();

            // Compute the camera position by inverting the model-view matrix and extracting the translation component
            Eigen::Matrix4d inv_mv_mat = mv_mat.inverse();

            // Compute the yaw, pitch, and roll angles from the rotation component of the model-view matrix
            Eigen::Matrix3d R = mv_mat.block<3, 3>(0, 0);
            // Compute the yaw, pitch, and roll angles
            double yaw   = std::atan2(R(1,0), R(0,0));
            double pitch = std::atan2(-R(2,0), std::sqrt(R(2,1)*R(2,1) + R(2,2)*R(2,2)));
            double roll  = std::atan2(R(2,1), R(2,2));

            std::cout << "Camera position: " << camera_pos << ", yaw: " << yaw << ", pitch: " << pitch << ", roll: "
                      << roll << std::endl;

            // Save camera parameters and depth buffer.
            cv::Mat Rot = eigenToCVMat(mv_mat).t(); // Get the rotation matrix
            cv::Mat trans = -Rot * eigenToCVMat(mv_mat).col(3); // Get the translation vector
            std::string camera_params_file = std::string(data["framesOutput"]) + "frame_" + std::to_string(frame_to_check) + "_camera_params.yml";
            saveCameraParameters(camera_params_file, eigenToCVMat(K), Rot, trans);
            std::string depth_buffer_file = std::string(data["framesOutput"]) + "frame_" + std::to_string(frame_to_check) + "_depth_buffer.png";
            std::string depth_buffer_visual_file = std::string(data["framesOutput"]) + "frame_" + std::to_string(frame_to_check) + "_depth_buffer_visual.png";
            saveDepthBuffer(depth_buffer_file, depth_buffer_visual_file, (int)w, (int)h);

            // Save the model viewer matrix and projection matrix
            std::string mv_filename = std::string(data["framesOutput"]) + "frame_" + std::to_string(frame_to_check) + "_mv.csv";
            writeMatrixToCsv(mv_mat, mv_filename);
            std::string proj_filename = std::string(data["framesOutput"]) + "frame_" + std::to_string(frame_to_check) + "_proj.csv";
            writeMatrixToCsv(proj_mat, proj_filename);

            s_cam.Apply();
            if (show_x0) pangolin::glDraw_x0(10.0, 10);
            if (show_y0) pangolin::glDraw_y0(10.0, 10);
            if (show_z0) pangolin::glDraw_z0(10.0, 10);
            if (show_axis) pangolin::glDrawAxis(10.0);
            if (show_bounds) pangolin::glDrawAlignedBox(total_aabb);

            glDisable(GL_CULL_FACE);

            std::string map_input_dir = data["mapInputDir"];
            const std::string cloud_points = map_input_dir + "cloud1.csv";
            const float x_offset = data["xOffset"];
            const float y_offset = data["yOffset"];
            const float z_offset = data["zOffset"];
            const float yaw_offset = data["yawOffset"];
            const float pitch_offset = data["pitchOffset"];
            const float roll_offset = data["rollOffset"];
            const float scale_factor = data["scaleFactor"];

            std::vector<cv::Point3d> seen_points = Auxiliary::getPointsFromPos(cloud_points,
                   cv::Point3d(camera_pos[0] * scale_factor + x_offset, camera_pos[1] * scale_factor + y_offset,
                               camera_pos[2] * scale_factor + z_offset),
                               yaw + yaw_offset, pitch + pitch_offset, roll + roll_offset, Twc);
            drawPoints(std::vector<cv::Point3d>(), seen_points);

            drawPoints(std::vector<cv::Point3d>(), orbs_points);
        }

        pangolin::FinishFrame();
    }

    return 0;
}
