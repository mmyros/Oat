//******************************************************************************
//* File:   HSVDetector.cpp
//* Author: Jon Newman <jpnewman snail mit dot edu>
//
//* Copyright (c) Jon Newman (jpnewman snail mit dot edu) 
//* All right reserved.
//* This file is part of the Simple Tracker project.
//* This is free software: you can redistribute it and/or modify
//* it under the terms of the GNU General Public License as published by
//* the Free Software Foundation, either version 3 of the License, or
//* (at your option) any later version.
//* This software is distributed in the hope that it will be useful,
//* but WITHOUT ANY WARRANTY; without even the implied warranty of
//* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//* GNU General Public License for more details.
//* You should have received a copy of the GNU General Public License
//* along with this source code.  If not, see <http://www.gnu.org/licenses/>.
//****************************************************************************

#include "HSVDetector.h"

#include <string>
#include <limits>
#include <math.h>
#include <opencv2/opencv.hpp>
#include <cmath>

#include "../../lib/datatypes/Position2D.h"
#include "../../lib/cpptoml/cpptoml.h"

HSVDetector::HSVDetector(const std::string& image_source_name, const std::string& position_sink_name) :
  PositionDetector(image_source_name, position_sink_name)
, h_min(0)
, h_max(256)
, s_min(0)
, s_max(256)
, v_min(0)
, v_max(256)
, min_object_area(0)
, max_object_area(std::numeric_limits<double>::max())
, tuning_on(false) {

    // Set defaults for the erode and dilate blocks
    // Cannot use initializer because if these are set to 0, erode_on or 
    // dilate_on must be set to false
    set_erode_size(0);
    set_dilate_size(10);
}

oat::Position2D HSVDetector::detectPosition(cv::Mat& frame_in) {

    // If we are able to get a an image
    hsv_image = frame_in;
    cv::cvtColor(hsv_image, hsv_image, cv::COLOR_BGR2HSV);
    applyThreshold();
    erodeDilate();
    siftBlobs();
    tune();

    return object_position;

}

void HSVDetector::applyThreshold() {

    cv::inRange(hsv_image, cv::Scalar(h_min, s_min, v_min), cv::Scalar(h_max, s_max, v_max), threshold_image);
    hsv_image.setTo(0, threshold_image == 0);

}

void HSVDetector::erodeDilate() {

    if (erode_on) {
        cv::erode(threshold_image, threshold_image, erode_element);
    }

    if (dilate_on) {
        cv::dilate(threshold_image, threshold_image, dilate_element);
    }

}

void HSVDetector::siftBlobs() {

    cv::Mat thesh_cpy = threshold_image.clone();
    std::vector< std::vector < cv::Point > > contours;
    std::vector< cv::Vec4i > hierarchy;

    // This function will modify the threshold_img data.
    cv::findContours(thesh_cpy, contours, hierarchy, cv::RETR_CCOMP, cv::CHAIN_APPROX_SIMPLE);

    object_area = 0;
    object_position.position_valid = false;

    if (hierarchy.size() > 0) {

        for (int index = 0; index >= 0; index = hierarchy[index][0]) {

            cv::Moments moment = cv::moments((cv::Mat)contours[index]);
            double area = moment.m00;

            // Isolate the largest contour within the min/max range.
            if (area > min_object_area && area < max_object_area && area > object_area) {
                object_position.position.x = moment.m10 / area;
                object_position.position.y = moment.m01 / area;
                object_position.position_valid = true;
                object_area = area;
            }
        }
    }

    if (tuning_on) {
        
        std::string msg = cv::format("Object not found"); 

        // Plot a circle representing found object
        if (object_position.position_valid) {
            auto radius = std::sqrt(object_area / PI);
            cv::Point center;
            center.x = object_position.position.x;
            center.y = object_position.position.y;
            cv::circle(hsv_image, center, radius, cv::Scalar(0, 0, 255), 2);
            msg = cv::format("(%d, %d) pixels", (int) object_position.position.x, (int) object_position.position.y);
        }

        int baseline = 0;
        cv::Size textSize = cv::getTextSize(msg, 1, 1, 1, &baseline);
        cv::Point text_origin(
                hsv_image.cols - textSize.width - 10,
                hsv_image.rows - 2 * baseline - 10);

        cv::putText(hsv_image, msg, text_origin, 1, 1, cv::Scalar(0, 255, 0));
    }
}

void HSVDetector::configure(const std::string& config_file, const std::string& config_key) {

    // This will throw cpptoml::parse_exception if a file 
    // with invalid TOML is provided
    cpptoml::table config;
    config = cpptoml::parse_file(config_file);

    // See if a camera configuration was provided
    if (config.contains(config_key)) {

        auto this_config = *config.get_table(config_key);

        if (this_config.contains("erode")) {
            int val = *this_config.get_as<int64_t>("erode");
            if (val < 0 || !this_config.get("erode")->is_value())
                throw (std::runtime_error(
                    "erode value in " + config_key +
                    " in " + config_file + " must be > 0.")
                    );
            set_erode_size(val);
        }

        if (this_config.contains("dilate")) {
            int val = *this_config.get_as<int64_t>("dilate");
            if (val < 0 || !this_config.get("dilate")->is_value())
                throw (std::runtime_error(
                    "dilate value in " + config_key +
                    " in " + config_file + " must be > 0.")
                    );
            set_dilate_size(val);
        }

        if (this_config.contains("min_area")) {
            min_object_area = *this_config.get_as<int64_t>("min_area");
            if (min_object_area < 0 || !this_config.get("min_area")->is_value())
                throw (std::runtime_error(
                    "min_area value in " + config_key +
                    " in " + config_file + " must be > 0.")
                    );
        }

        if (this_config.contains("max_area")) {
            max_object_area = *this_config.get_as<int64_t>("max_area");
            if (max_object_area < 0 || !this_config.get("max_area")->is_value())
                throw (std::runtime_error(
                    "max_area value in " + config_key +
                    " in " + config_file + " must be > 0.")
                    );
        }

        if (this_config.contains("h_thresholds")) {
    
            if (!this_config.get("h_thresholds")->is_table()){
                throw (std::runtime_error(oat::configValueError(
                       "h_thresholds", config_key, config_file, 
                        "must be a TOML table specifying a min and max double value."))
                      );
            }
            
            auto t = *this_config.get_table("h_thresholds");

            if (t.contains("min")) {
                h_min = *t.get_as<int64_t>("min");
                if (h_min < 0 || !t.get("min")->is_value())
                    throw (std::runtime_error(oat::configValueError(
                       "h_min", config_key, config_file, "must be a double > 0."))
                        );
            }
            if (t.contains("max")) {

                h_max = *t.get_as<int64_t>("max");
                if (h_max < 0 || !t.get("max")->is_value())
                    throw (std::runtime_error(oat::configValueError(
                       "h_max", config_key, config_file, "must be a double > 0."))
                        );
            }
        }

        if (this_config.contains("s_thresholds")) {

            if (!this_config.get("s_thresholds")->is_table()){
                throw (std::runtime_error(oat::configValueError(
                       "s_thresholds", config_key, config_file, 
                        "must be a TOML table specifying a min and max double value."))
                      );
            }
            
            auto t = *this_config.get_table("s_thresholds");

            if (t.contains("min")) {
                s_min = *t.get_as<int64_t>("min");
                if (s_min < 0 || !t.get("min")->is_value())
                    throw (std::runtime_error(oat::configValueError(
                       "s_min", config_key, config_file, "must be a double > 0."))
                        );
            }
            if (t.contains("max")) {
                s_max = *t.get_as<int64_t>("max");
                if (s_max < 0 || !t.get("max")->is_value())
                    throw (std::runtime_error(oat::configValueError(
                       "s_max", config_key, config_file, "must be a double > 0."))
                        );
            }
        }

        if (this_config.contains("v_thresholds")) {
            
            if (!this_config.get("v_thresholds")->is_table()){
                throw (std::runtime_error(oat::configValueError(
                       "v_thresholds", config_key, config_file, 
                        "must be a TOML table specifying a min and max double value."))
                      );
            }
            
            auto t = *this_config.get_table("v_thresholds");

            if (t.contains("min")) {
                v_min = *t.get_as<int64_t>("min");
                if (v_min < 0 || !t.get("min")->is_value())
                    throw (std::runtime_error(oat::configValueError(
                       "v_min", config_key, config_file, "must be a double> 0."))
                        );
            }
            if (t.contains("max")) {
                v_max = *t.get_as<int64_t>("max");
                if (v_max < 0 || !t.get("max")->is_value())
                    throw (std::runtime_error(oat::configValueError(
                       "v_max", config_key, config_file, "must be a double > 0."))
                        );
            }
        }

        if (this_config.contains("tune")) {

            if (!this_config.get("tune")->is_value()) {
                throw (std::runtime_error(oat::configValueError(
                        "tune", config_key, config_file, "must be a boolean value."))
                        );
            }

            if (*this_config.get_as<bool>("tune")) {
                tuning_on = true;
                createTuningWindows();
            }
        }

    } else {
        throw (std::runtime_error(oat::configNoTableError(config_key, config_file)));
    }
}

void HSVDetector::tune() {

    if (tuning_on) {
        if (!tuning_windows_created) {
            createTuningWindows();
        }
        cv::imshow(tuning_image_title, hsv_image);
        cv::waitKey(1);
    } else if (!tuning_on && tuning_windows_created) {
        
        // TODO: Window will not actually close!!
        // Destroy the tuning windows
        cv::destroyWindow(tuning_image_title);
        tuning_windows_created = false;
    }
}

void HSVDetector::createTuningWindows() {

    // Create window for sliders
    cv::namedWindow(tuning_image_title, cv::WINDOW_NORMAL);

    // Create sliders and insert them into window
    cv::createTrackbar("H MIN", tuning_image_title, &h_min, 256);
    cv::createTrackbar("H MAX", tuning_image_title, &h_max, 256);
    cv::createTrackbar("S MIN", tuning_image_title, &s_min, 256);
    cv::createTrackbar("S MAX", tuning_image_title, &s_max, 256);
    cv::createTrackbar("V MIN", tuning_image_title, &v_min, 256);
    cv::createTrackbar("V MAX", tuning_image_title, &v_max, 256);
    cv::createTrackbar("MIN AREA", tuning_image_title, &min_object_area, 10000);
    cv::createTrackbar("MAX AREA", tuning_image_title, &max_object_area, 10000);
    cv::createTrackbar("ERODE", tuning_image_title, &erode_px, 50, &HSVDetector::erodeSliderChangedCallback, this);
    cv::createTrackbar("DILATE", tuning_image_title, &dilate_px, 50, &HSVDetector::dilateSliderChangedCallback, this);

    tuning_windows_created = true;
}

void HSVDetector::set_erode_size(int value) {

    if (value > 0) {
        erode_on = true;
        erode_px = value;
        erode_element = cv::getStructuringElement(cv::MORPH_RECT, cv::Size(erode_px, erode_px));
    } else {
        erode_on = false;
    }
}

void HSVDetector::set_dilate_size(int value) {
    if (value > 0) {
        dilate_on = true;
        dilate_px = value;
        dilate_element = cv::getStructuringElement(cv::MORPH_RECT, cv::Size(dilate_px, dilate_px));
    } else {
        dilate_on = false;
    }
}

void HSVDetector::erodeSliderChangedCallback(int value, void* object) {
    HSVDetector* hsv_detector = (HSVDetector*) object;
    hsv_detector->set_erode_size(value);
}

void HSVDetector::dilateSliderChangedCallback(int value, void* object) {
    HSVDetector* hsv_detector = (HSVDetector*) object;
    hsv_detector->set_dilate_size(value);
}