#include <opencv2/highgui.hpp>
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/video/tracking.hpp>
#include <opencv2/core/cuda.hpp>
#include <opencv2/cudaimgproc.hpp>
#include <opencv2/cudaarithm.hpp>
#include <opencv2/cudafeatures2d.hpp>
#include <opencv2/xfeatures2d.hpp>
#include <iostream>
#include <chrono>
#include <opencv2/pythoncuda.hpp>

using namespace std;
using namespace cv;
using namespace std::chrono; 

#define RATIO    0.75

int cpuFindSimilaritiesBetweenImages(Mat &original, Mat &image_to_compare, float ratio=0.75) {
    auto start_time = std::chrono::high_resolution_clock::now(); 
    
    // Check if 2 images are equals
    // Mat original_gray,image_to_compare_gray , dst;
    Mat original_hsv, original_channels[3];
    Mat image_to_compare_hsv, image_to_compare_channels[3];
    Mat dst;

    cv::cvtColor(original, original_hsv, cv::COLOR_BGR2HSV);
    cv::split(original_hsv, original_channels);
    original_channels[2].convertTo(original_channels[2], CV_8UC1);
    cv::cvtColor(image_to_compare, image_to_compare_hsv, cv::COLOR_BGR2HSV);
    cv::split(image_to_compare_hsv, image_to_compare_channels);
    image_to_compare_channels[2].convertTo(image_to_compare_channels[2], CV_8UC1);

    absdiff(original_channels[2], image_to_compare_channels[2], dst);
    cv::Scalar s = sum(dst);

    if (s == cv::Scalar::all(0)) {
        std::cout << "The images are completely Equal" << std::endl;
    }else {
        // std::cout << "The images are NOT equal" << endl;
    }

    //提取特征点方法
    // double min_hessian = 400;
    // cv::Ptr<cv::xfeatures2d::SURF> surf = cv::xfeatures2d::SURF::create(min_hessian);
    cv::Ptr< cv::ORB > orb = cv::ORB::create(1500);
    
    //特征点
    std::vector<cv::KeyPoint> kp_1, kp_2;

    //特征点匹配
    cv::Mat desc_1, desc_2;
    //提取特征点并计算特征描述子
    orb->detectAndCompute(original_hsv, cv::Mat(), kp_1, desc_1);
    orb->detectAndCompute(image_to_compare_hsv, cv::Mat(), kp_2, desc_2);

    std::vector<std::vector<cv::DMatch>> matches;

    // cv::Ptr<cv::FlannBasedMatcher> matcher = cv::FlannBasedMatcher::create();
    cv::Ptr< cv::DescriptorMatcher > matcher = cv::BFMatcher::create();
    matcher->knnMatch(desc_1, desc_2, matches, 2);

    std::vector<cv::DMatch> goodMatches;
    for (unsigned int i = 0; i < matches.size(); ++i) {
        if (matches[i][0].distance < matches[i][1].distance * ratio){
            goodMatches.push_back(matches[i][0]);
        }
    }

    auto stop_time = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double, std::milli> fp_ms = stop_time - start_time;
    // std::cout << "Time taken by cpu function: "<< fp_ms.count() << " ms" << endl;

    return goodMatches.size();
}

int gpuFindSimilaritiesBetweenImages(Mat &original, Mat &image_to_compare, float ratio=0.75) {
    auto start_time = std::chrono::high_resolution_clock::now();
    
    cv::cuda::GpuMat original_gpu, original_gray_gpu, image_to_compare_gpu, image_to_compare_gray_gpu, log;
    original_gpu.upload(original);
    cv::cuda::cvtColor(original_gpu, original_gray_gpu, cv::COLOR_BGR2GRAY);
    original_gray_gpu.convertTo(original_gray_gpu, CV_8UC1);

    image_to_compare_gpu.upload(image_to_compare);
    cv::cuda::cvtColor(image_to_compare_gpu, image_to_compare_gray_gpu, cv::COLOR_BGR2GRAY);
    image_to_compare_gray_gpu.convertTo(image_to_compare_gray_gpu, CV_8UC1);

    // Check if 2 images are equals
    cv::cuda::GpuMat dst;
    cv::cuda::absdiff(original_gpu, image_to_compare_gpu, dst);
    cv::Scalar s = cv::cuda::sum(dst);

    if (s == cv::Scalar::all(0)) {
        std::cout << "The images are completely Equal" << std::endl;
    }else {
        // std::cout << "The images are NOT equal" << endl;
    }

    //提取特征点方法
    std::vector<cv::KeyPoint> cpukp_1, cpukp_2;
    cv::cuda::GpuMat kp_1, kp_2;
    cv::cuda::GpuMat desc_1, desc_2;
    cv::cuda::GpuMat mask_1;
    cv::cuda::GpuMat mask_2;
    cv::cuda::Stream m_stream;
    cv::Ptr< cv::cuda::ORB > orb = cv::cuda::ORB::create(1500);
    cv::Ptr< cv::cuda::DescriptorMatcher > matcher = cv::cuda::DescriptorMatcher::createBFMatcher(cv::NORM_HAMMING);

    orb->detectAndComputeAsync(original_gray_gpu, mask_1, kp_1, desc_1, false, m_stream);
    m_stream.waitForCompletion();
    orb->convert(kp_1, cpukp_1);

    orb->detectAndComputeAsync(image_to_compare_gray_gpu, mask_2, kp_2, desc_2, false, m_stream);
    m_stream.waitForCompletion();
    orb->convert(kp_2, cpukp_2);

    std::vector<cv::DMatch> goodMatches;
    if (desc_2.rows > 0) {
        std::vector<std::vector<cv::DMatch>> cpuKnnMatches;
        cv::cuda::GpuMat gpuKnnMatches;
        matcher -> knnMatchAsync(desc_2, desc_1, gpuKnnMatches, 2, noArray(), m_stream);
        m_stream.waitForCompletion();
        matcher->knnMatchConvert(gpuKnnMatches, cpuKnnMatches); // download matches from gpu and put into vector<vector<DMatch>> form on cpu
        
        for (std::vector<std::vector<cv::DMatch> >::const_iterator it = cpuKnnMatches.begin(); it != cpuKnnMatches.end(); ++it) {
            if (it->size() > 1 && (*it)[0].distance <  (*it)[1].distance* ratio) {
                DMatch m = (*it)[0];
                goodMatches.push_back(m); 
            }
        }
    }

    auto stop_time = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double, std::milli> fp_ms = stop_time - start_time;
    // std::cout << "Time taken by gpu function: "<< fp_ms.count() << " ms" << endl;

    return goodMatches.size();
}

std::vector<cv::String> readDirectory(cv::String directory_name, bool useCuda, float ratio=0.75) {
    auto start_time = std::chrono::high_resolution_clock::now();

    cv::String path(directory_name); //select only jpg
    std::vector<cv::String> fn;
    std::vector<cv::String> filenames;
    std::vector<cv::String> names;
    std::vector<cv::Mat> images;
    cv::glob(path,fn,false);
    size_t count = fn.size();
    std::vector<cv::String> check_img_list;

    //要对fn进行排序,不然fn是乱序的
    // sort(fn.begin(), fn.end(), [](cv::String a, cv::String b) {return stoi(a) < stoi(b); });
	sort(fn.begin(),fn.end(), [](cv::String a, cv::String b) {
		return stoi(a.substr(a.rfind("/") + 1, a.rfind("."))) < stoi(b.substr(b.rfind("/") + 1, b.rfind(".")));
	});

    if (count == 0) {
        std::cout << "File " << directory_name << " not exits" << std::endl;
        // return check_img_list;
    }

    // for (size_t k=0; k<fn.size(); ++k){
    //     std::cout << "fn:" << fn[k] << std::endl;
    // }
    
    for (size_t k=0; k<fn.size(); ++k)
    {
        cv::Mat im = cv::imread(fn[k]);
        if (im.empty()) continue;
        images.push_back(im);
        cv::String::size_type iPos = fn[k].find_last_of('/' ) + 1;
        cv::String filename = fn[k].substr(iPos, fn[k].length() - iPos);
        cv::String name = filename.substr(0, filename.rfind("."));
        filenames.push_back(filename);
        names.push_back(name);
        // std::cout << "name" << name << std::endl;
    }

    // sort(names.begin(), names.end(), [](cv::String c, cv::String d) {return stoi(c) < stoi(d); });
    
    // for (size_t k=0; k<names.size(); ++k){
    //     std::cout << "name:" << names[k] << std::endl;
    // }

    // for (size_t k=0; k<images.size(); ++k){
    //     std::cout << "image:" << images[k] << std::endl;
    // }
	// sort(names.begin(), names.end(), [](cv::String a, cv::String b) {
	// 	return stoi(a.substr(a.rfind("\\") + 1, a.rfind("."))) < stoi(b.substr(b.rfind("\\") + 1, b.rfind(".")));
	// });

    std::cout << "images length:" << images.size() << std::endl;

    if (images.size() > 1) {
        int good_matcher;
        std::vector<cv::DMatch> goodMatches;
        for (unsigned int i = 0; i < images.size() - 1; ++i) {
            cv::Mat original = images[i];
            cv::Mat image_to_compare = images[i+1];
            if (useCuda == true) {
                good_matcher = gpuFindSimilaritiesBetweenImages(original, image_to_compare, ratio);
                // cout << i <<" , good_matcher with gpu:" << good_matcher  << endl;
                // cout << i <<" , file path:" << names[i]  << ".jpg" << endl;
            }else {
                good_matcher = cpuFindSimilaritiesBetweenImages(original, image_to_compare, ratio);
                // cout << i <<" , good_matcher with cpu:" << good_matcher  << endl;
                // cout << i <<" , file path:" << names[i]  << ".jpg" << endl;
            }

            if (good_matcher<=120) {
                if (check_img_list.size() == 0){
                    check_img_list.push_back(names[i] + ".jpg");
                    check_img_list.push_back(names[i+1] + ".jpg");
                } else {
                    check_img_list.push_back(names[i+1]  + ".jpg");
                }
            } else {
                if (check_img_list.size() == 0){
                    check_img_list.push_back(names[i] + ".jpg");
                }
            }
        }
        std::cout << "check_img_list length: " << check_img_list.size() << std::endl;

        // for (unsigned int j = 0; j < check_img_list.size() ; ++j) {
        //     cout << check_img_list[j] << endl;
        // }

        auto stop_time = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double, std::milli> fp_ms = stop_time - start_time;
        if (useCuda == true) {
            std::cout << "Time taken by read_directory gpu function: "<< fp_ms.count() << " ms" << std::endl;
        }else {
            std::cout << "Time taken by read_directory cpu function: "<< fp_ms.count() << " ms" << std::endl;
        }
        
    }else {
        std::cout << "images length must be more then one!" << std::endl;
    }

    return check_img_list;
}

int main()
{
    Mat original=imread("/var/www/gallery/media/videos/capture_out_images/5cfe084a-31e4-11ea-bf2d-408d5c891351/1.jpg");
    Mat image_to_compare=imread("/var/www/gallery/media/videos/capture_out_images/5cfe084a-31e4-11ea-bf2d-408d5c891351/38.jpg");

    // Mat original=imread("/home/nhydev/github/ai/nhyai/backend/api/python-opencv-cuda/python/find_similarities_between_images/images/black_and_white.jpg");
    // Mat image_to_compare=imread("/home/nhydev/github/ai/nhyai/backend/api/python-opencv-cuda/python/find_similarities_between_images/images/blurred.jpg");

    cv::String path = "/var/www/gallery/media/videos/capture_out_images/16e261c6-124a-11ea-ae52-408d5c891351/*.jpg";
    bool useCuda = false;
    float ratio=0.75;
    std::vector<string> check_img_list;

    // int good_matcher_cpu = cpuFindSimilaritiesBetweenImages(original, image_to_compare, ratio + 0.01);
    // cout << "good_matcher with cpu:" << good_matcher_cpu  << endl;

    // int good_matcher_gpu = gpuFindSimilaritiesBetweenImages(original, image_to_compare, ratio);
    // cout << "good_matcher with gpu:" << good_matcher_gpu  << endl;

    check_img_list = readDirectory(path, useCuda, ratio);
    for (unsigned int j = 0; j < check_img_list.size() ; ++j) {
         cout << check_img_list[j] << endl;
    }

    // test pythoncuda.hpp
    // int good_matcher_cpu =  cv::pythoncuda::cpuFindSimilaritiesBetweenImages(original, image_to_compare, ratio + 0.01);
    // cout << "good_matcher with cpu:" << good_matcher_cpu  << endl;

    // int good_matcher_gpu = cv::pythoncuda::gpuFindSimilaritiesBetweenImages(original, image_to_compare, ratio);
    // cout << "good_matcher with gpu:" << good_matcher_gpu  << endl;

    // check_img_list = cv::pythoncuda::readDirectory(path, useCuda, ratio);
    // for (unsigned int j = 0; j < check_img_list.size() ; ++j) {
    //      cout << check_img_list[j] << endl;
    // }

}