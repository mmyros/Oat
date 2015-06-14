//******************************************************************************
//* Copyright (c) Jon Newman (jpnewman at mit snail edu) 
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
//******************************************************************************

#include <csignal>
#include <unordered_map>

#include <boost/program_options.hpp>

#include "../../lib/utility/IOFormat.h"
#include "PGGigECam.h"
#include "WebCam.h"
#include "FileReader.h"

namespace po = boost::program_options;

volatile sig_atomic_t quit = 0;
volatile sig_atomic_t server_eof = 0;

// Signal handler to ensure shared resources are cleaned on exit due to ctrl-c
void sigHandler(int s) {
    quit = 1;
}

void run(Camera* camera) {

    while (!quit && !server_eof) {
        camera->grabMat();
        camera->undistortMat();
        server_eof = camera->serveMat();
    }
}

void printUsage(po::options_description options) {
    std::cout << "Usage: frameserve [INFO]\n"
              << "   or: frameserve TYPE SINK [CONFIGURATION]\n"
              << "Serve image stream to a frame SINK\n\n"
              << "TYPE:\n"
              << "  wcam: Onboard or USB webcam.\n"
              << "  gige: Point Grey GigE camera.\n"
              << "  file: Video from file (*.mpg, *.avi, etc.).\n\n"
              << "SINK:\n"
              << "  User supplied sink name (e.g. raw).\n\n"
              << options << "\n";
}

int main(int argc, char *argv[]) {
    
    std::signal(SIGINT, sigHandler);

    std::string sink;
    std::string type;
    std::string video_file;
    double frames_per_second = 30;
    std::string config_file;
    std::string config_key;
    bool config_used = false;
    po::options_description visible_options("OPTIONAL ARGUMENTS");

    std::unordered_map<std::string, char> type_hash;
    type_hash["wcam"] = 'a';
    type_hash["gige"] = 'b';
    type_hash["file"] = 'c';

    try {

        po::options_description options("INFO");
        options.add_options()
                ("help", "Produce help message.")
                ("version,v", "Print version information.")
                ;

        po::options_description config("CONFIGURATION");
        config.add_options()
                ("video-file,f", po::value<std::string>(&video_file),
                "Path to video file if \'file\' is selected as the server TYPE.")
                ("fps,r", po::value<double>(&frames_per_second),
                "Frames per second. Overriden by information in configuration file if provided.")
                ("config-file,c", po::value<std::string>(&config_file), "Configuration file.")
                ("config-key,k", po::value<std::string>(&config_key), "Configuration key.")
                ;

        po::options_description hidden("HIDDEN OPTIONS");
        hidden.add_options()
                ("type", po::value<std::string>(&type), "Camera TYPE.")
                ("sink", po::value<std::string>(&sink),
                "The name of the sink through which images collected by the camera will be served.\n")
                ;

        po::positional_options_description positional_options;
        positional_options.add("type", 1);
        positional_options.add("sink", 1);

        visible_options.add(options).add(config);

        po::options_description all_options("ALL OPTIONS");
        all_options.add(options).add(config).add(hidden);

        po::variables_map variable_map;
        po::store(po::command_line_parser(argc, argv)
                .options(all_options)
                .positional(positional_options)
                .run(),
                variable_map);
        po::notify(variable_map);

        // Use the parsed options
        if (variable_map.count("help")) {
            printUsage(visible_options);
            return 0;
        }

        if (variable_map.count("version")) {
            std::cout << "Simple-Tracker GigECamera Server version 1.0\n"; //TODO: Cmake managed versioning
            std::cout << "Written by Jonathan P. Newman in the MWL@MIT.\n";
            std::cout << "Licensed under the GPL3.0.\n";
            return 0;
        }

        if (!variable_map.count("type")) {
            printUsage(visible_options);
            std::cout << "Error: a TYPE must be specified. Exiting.\n";
            return -1;
        }

        if (!variable_map.count("sink")) {
            printUsage(visible_options);
            std::cout << "Error: a SINK must be specified. Exiting.\n";
            return -1;
        }

        if ((variable_map.count("config-file") && !variable_map.count("config-key")) ||
                (!variable_map.count("config-file") && variable_map.count("config-key"))) {
            printUsage(visible_options);
            std::cout << "Error: config file must be supplied with a corresponding config-key. Exiting.\n";
            return -1;
        } else if (variable_map.count("config-file")) {
            config_used = true;
        }

        if (type.compare("file") == 0 && !variable_map.count("video-file")) {
            printUsage(visible_options);
            std::cout << "Error: when TYPE=file, a video-file path must be specified. Exiting.\n";
            return -1;
        }


    } catch (std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    } catch (...) {
        std::cerr << "Exception of unknown type!" << std::endl;
    }

    // Create the specified TYPE of detector
    Camera* camera;
    switch (type_hash[type]) {
        case 'a':
        {
            camera = new WebCam(sink);
            break;
        }
        case 'b':
        {
            camera = new PGGigECam(sink);
            break;
        }
        case 'c':
        {
            camera = new FileReader(video_file, sink, frames_per_second);
            break;
        }
        default:
        {
            printUsage(visible_options);
            std::cout << "Error: invalid TYPE specified. Exiting.\n";
            return -1;
        }
    }

    if (config_used)
        camera->configure(config_file, config_key);
    else
        camera->configure();
    
    
        // Tell user
    std::cout << oat::whoMessage(camera->get_name(),
                 "Steaming to sink " + oat::boldSink(sink) + ".\n")
              << oat::whoMessage(camera->get_name(), 
                 "Press CTRL+C to exit.\n");
    
    // Infinite loop until ctrl-c or end of stream signal
    run(camera);

    // Tell user
    std::cout << oat::whoMessage(camera->get_name(), "Exiting.\n");
    
    // Free heap memory allocated to camera 
    delete camera;

    // Exit
    return 0;
}
