//******************************************************************************
//* File:   oat posifilt main.cpp
//* Author: Jon Newman <jpnewman snail mit dot edu>
//*
//* Copyright (c) Jon Newman (jpnewman snail mit dot edu)
//* All right reserved.
//* This file is part of the Oat project.
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

#include <csignal>
#include <iostream>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <boost/interprocess/exceptions.hpp>
#include <boost/program_options.hpp>
#include <cpptoml.h>

#include "../../lib/utility/IOFormat.h"
#include "../../lib/utility/ProgramOptions.h"

#include "HomographyTransform2D.h"
#include "KalmanFilter2D.h"
#include "RegionFilter2D.h"

#define REQ_POSITIONAL_ARGS 3

namespace po = boost::program_options;

volatile sig_atomic_t quit = 0;
volatile sig_atomic_t source_eof = 0;

const char usage_type[] =
    "TYPE\n"
    "  kalman: Kalman filter\n"
    "  homography: homography transform\n"
    "  region: position region annotation";

const char usage_io[] =
    "SOURCE:\n"
    "  User-supplied name of the memory segment to receive positions "
    "from (e.g. pos).\n\n"
    "SINK:\n"
    "  User-supplied name of the memory segment to publish positions "
    "to (e.g. filt).";

const char purpose[] =
    "Filter positions from SOURCE and publish filtered positions "
    "to SINK.";

void printUsage(const po::options_description &options, const std::string &type)
{
    if (type.empty()) {
        std::cout <<
        "Usage: posifilt [INFO]\n"
        "   or: posifilt TYPE SOURCE SINK [CONFIGURATION]\n";

        std::cout << purpose << "\n";
        std::cout << options << "\n";
        std::cout << usage_type << "\n\n";
        std::cout << usage_io << std::endl;

    } else {
        std::cout <<
        "Usage: posifilt " << type << " [INFO]\n"
        "   or: posifilt " << type << " SOURCE SINK [CONFIGURATION]\n";

        std::cout << purpose << "\n\n";
        std::cout << usage_io << "\n";
        std::cout << options;
    }
}


// Signal handler to ensure shared resources are cleaned on exit due to ctrl-c
void sigHandler(int)
{
    quit = 1;
}

// Processing loop
void run(std::shared_ptr<oat::PositionFilter> filter)
{
    try {

        filter->connectToNode();

        while (!quit && !source_eof)
            source_eof = filter->process();

    } catch (const boost::interprocess::interprocess_exception &ex) {

        // Error code 1 indicates a SIGNINT during a call to wait(), which
        // is normal behavior
        if (ex.get_error_code() != 1)
            throw;
    }
}

int main(int argc, char *argv[])
{
    std::signal(SIGINT, sigHandler);

    // Results of command line input
    std::string type;
    std::string source;
    std::string sink;

    // Component specializations
    std::unordered_map<std::string, char> type_hash;
    type_hash["kalman"] = 'a';
    type_hash["homography"] = 'b';
    type_hash["region"] = 'c';

    // The component itself
    std::string comp_name = "posifilt";
    std::shared_ptr<oat::PositionFilter> filter;

    // Program options
    po::options_description visible_options;

    try {

        // Required positional options
        po::options_description positional_opt_desc("POSITIONAL");
        positional_opt_desc.add_options()
            ("type", po::value<std::string>(&type),
             "Type of position filter to use.")
            ("source", po::value<std::string>(&source),
             "User-supplied name of the memory segment to receive positions.")
            ("sink", po::value<std::string>(&sink),
             "User-supplied name of the memory segment to publish positions.")
            ("type-args", po::value<std::vector<std::string> >(),
             "type-specific arguments.")
            ;

        // Required positional arguments and type-specific configuration
        po::positional_options_description positional_options;
        positional_options.add("type", 1);
        positional_options.add("source", 1);
        positional_options.add("sink", 1);
        positional_options.add("type-args", -1);

        // Visible options for help message
        visible_options.add(oat::config::ComponentInfo::instance()->get());

        // All options, including positional
        po::options_description options;
        options.add(positional_opt_desc)
               .add(oat::config::ComponentInfo::instance()->get());

        // Parse options, including unrecognized options which may be
        // type-specific
        auto parsed_opt = po::command_line_parser(argc, argv)
            .options(options)
            .positional(positional_options)
            .allow_unregistered()
            .run();

        po::variables_map option_map;
        po::store(parsed_opt, option_map);

        // Check options for errors and bind options to local variables
        po::notify(option_map);

        // If a TYPE was provided, then specialize
        if (option_map.count("type")) {

            // Refine component type
            switch (type_hash[type]) {
                case 'a':
                {
                    filter = std::make_shared<oat::KalmanFilter2D>(source, sink);
                    break;
                }
                case 'b':
                {
                    filter = std::make_shared<oat::HomographyTransform2D>(source, sink);
                    break;
                }
                case 'c':
                {
                    filter = std::make_shared<oat::RegionFilter2D>(source, sink);
                    break;
                }
                default:
                {
                    printUsage(visible_options, "");
                    std::cerr << oat::Error("Invalid TYPE specified.\n");
                    return -1;
                }
            }

            // Specialize program options for the selected TYPE
            po::options_description detail_opts {"CONFIGURATION"};
            filter->appendOptions(detail_opts);
            visible_options.add(detail_opts);
            options.add(detail_opts);
        }
        
        // Check INFO arguments
        if (option_map.count("help")) {
            printUsage(visible_options, type);
            return 0;
        }

        if (option_map.count("version")) {
            std::cout << oat::config::VERSION_STRING;
            return 0;
        }

        // Check IO arguments
        bool io_error {false};
        std::string io_error_msg;

        if (!option_map.count("type")) {
            io_error_msg += "A TYPE must be specified.\n";
            io_error = true;
        }

        if (!option_map.count("source")) {
            io_error_msg += "A SOURCE must be specified.\n";
            io_error = true;
        }

        if (!option_map.count("sink")) {
            io_error_msg += "A SINK must be specified.\n";
            io_error = true;
        }

        if (io_error) {
            printUsage(visible_options, type);
            std::cerr << oat::Error(io_error_msg);
            return -1;
        }

        // Get specialized component name
        comp_name = filter->name();

        // Reparse specialized component options
        auto special_opt =
            po::collect_unrecognized(parsed_opt.options, po::include_positional);
        special_opt.erase(special_opt.begin(),special_opt.begin() + REQ_POSITIONAL_ARGS);

        po::store(po::command_line_parser(special_opt)
                 .options(options)
                 .run(), option_map);
        po::notify(option_map);

        filter->configure(option_map);

        // Tell user
        std::cout << oat::whoMessage(comp_name,
                     "Listening to source " + oat::sourceText(source) + ".\n")
                  << oat::whoMessage(comp_name,
                     "Steaming to sink " + oat::sinkText(sink) + ".\n")
                  << oat::whoMessage(comp_name,
                      "Press CTRL+C to exit.\n");

        // Infinite loop until ctrl-c or end of messages signal
        run(filter);

        // Tell user
        std::cout << oat::whoMessage(comp_name, "Exiting.")
                  << std::endl;

        // Exit success
        return 0;

    } catch (const po::error &ex) {
        printUsage(visible_options, type);
        std::cerr << oat::whoError(comp_name, ex.what()) << std::endl;
    } catch (const cpptoml::parse_exception &ex) {
        std::cerr << oat::whoError(comp_name, ex.what()) << std::endl;
    } catch (const std::runtime_error &ex) {
        std::cerr << oat::whoError(comp_name,ex.what()) << std::endl;
    } catch (const cv::Exception &ex) {
        std::cerr << oat::whoError(comp_name, ex.what()) << std::endl;
    } catch (const boost::interprocess::interprocess_exception &ex) {
        std::cerr << oat::whoError(comp_name, ex.what()) << std::endl;
    } catch (...) {
        std::cerr << oat::whoError(comp_name, "Unknown exception.")
                  << std::endl;
    }

    // exit failure
    return -1;
}
