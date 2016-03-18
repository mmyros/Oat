//******************************************************************************
//* File:   Recorder.cpp
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
//******************************************************************************

#include "OatConfig.h" // Generated by CMake

#include <algorithm>
#include <cstdio>
#include <chrono>
#include <iomanip>
#include <iterator>
#include <mutex>
#include <string.h>
#include <string>
#include <vector>

#include <sys/stat.h>
#include <boost/dynamic_bitset.hpp>

#include "../../lib/utility/FileFormat.h"
#include "../../lib/utility/IOFormat.h"
#include "../../lib/utility/make_unique.h"
#include "../../lib/shmemdf/SharedFrameHeader.h"

#include "Recorder.h"

namespace oat {

Recorder::Recorder(const std::vector<std::string> &position_source_addresses,
                   const std::vector<std::string> &frame_source_addresses)
{
    // Start recorder name construction
    name_ = "recorder[" ;

    // Setup position sources
    if (!position_source_addresses.empty()) {

        name_ += position_source_addresses[0];
        if (position_source_addresses.size() > 1)
            name_ += "..";

        for (auto &addr : position_source_addresses) {

            oat::Position2D pos(addr);
            positions_.push_back(std::move(pos));
            position_write_number_.push_back(0);
            position_sources_.push_back(std::make_pair(
                addr,
                std::make_unique<oat::Source < oat::Position2D >>())
            );
        }
    }

    // Setup the frame sources
    if (!frame_source_addresses.empty()) {

        if (!position_source_addresses.empty())
            name_ += ", ";

        name_ += frame_source_addresses[0];
        if (frame_source_addresses.size() > 1)
            name_ += "..";

        uint32_t idx = 0;
        for (auto &addr : frame_source_addresses) {

            frame_write_buffers_.push_back(std::make_unique<FrameQueue>());

            frame_sources_.push_back(
                std::make_pair(
                    addr, std::make_unique<oat::Source<oat::SharedFrameHeader>>()
                )
            );

            // Spawn frame writer threads and synchronize to incoming data
            frame_write_mutexes_.push_back(std::make_unique<std::mutex>());

            frame_write_condition_variables_.push_back(
                std::make_unique<std::condition_variable>()
            );

            frame_write_threads_.push_back(
                std::make_unique<std::thread>(
                    &Recorder::writeFramesToFileFromBuffer, this, idx++
                )
            );
        }
    }

    name_ +="]";
}

Recorder::~Recorder() {

    // NOTE: The cv::VideoWriter class has internal buffering. Its flushes its
    // buffer on destruction. Because VideoWriter's are accessed via
    // std::unique_ptr, this will happen automatically. However -- don't try to
    // look at a video before the recorder destructs because it will be
    // incomplete! Same with the position file.

    // Set running to false to trigger thread join
    running_ = false;
    for (auto &value : frame_write_condition_variables_)
        value->notify_one();

    // Join all threads
    for (auto &value : frame_write_threads_)
        value->join();

    // Flush the position writer
    if (position_fp_ != nullptr) {
        json_writer_.EndArray();
        json_writer_.EndObject();
        file_stream_->Flush();
    }
}

void Recorder::connectToNodes() {

    // Connect to frame source nodes
    for (auto &fs: frame_sources_)
        fs.second->touch(fs.first);

    // Connect to position source nodes
    for (auto &ps : position_sources_)
        ps.second->touch(ps.first);

    // Verify connections and check sample rates If sample rates are variable,
    // the user should be using multiple recorders instead of just one, which
    // enforces sample synchronization
    bool ts_consistent = true;
    double ts {-1.0}, ts_last {-1.0};

    // TODO: The following procedure may be appropriate for all multi source
    // components

    // Frame sources
    for (auto &fs: frame_sources_) {
        fs.second->connect();
        ts = fs.second->retrieve().sample().period_sec();
        if (ts_last != -1.0 && ts != ts_last) {
            ts = ts > ts_last ? ts : ts_last;
            ts_consistent = false;
        } else {
            ts_last = ts;
        }
    }

    // Position sources
    for (auto &ps : position_sources_) {
        ps.second->connect();
        ts = ps.second->retrieve()->sample().period_sec();
        if (ts_last != -1.0 && ts != ts_last) {
            ts = ts > ts_last ? ts : ts_last;
            ts_consistent = false;
        } else {
            ts_last = ts;
        }
    }

    sample_rate_hz_ = 1.0 / ts;

    if (!ts_consistent) {
        std::cerr << oat::Warn(
                     "Warning: Sample rates of SOURCEs are inconsistent.\n"
                     "This recorder forces synchronization at the lowest SOURCE sample rate.\n"
                     "You should probably use separate recorders to capture these SOURCEs.\n"
                     "Specified sample rate set to: " + std::to_string(sample_rate_hz_) + "\n"
                     );
    }
}

bool Recorder::writeStreams() {

    // Read frames
    for (fvec_size_t i = 0; i !=  frame_sources_.size(); i++) {

         // START CRITICAL SECTION //
        ////////////////////////////
        source_eof_ |= (frame_sources_[i].second->wait() == oat::NodeState::END);

        // Push newest frame into client N's queue
        if (record_on_) {
            if (!frame_write_buffers_[i]->push(frame_sources_[i].second->clone())) {
                throw (std::runtime_error("Frame buffer overrun. Decrease the frame "
                                          "rate or get a faster hard-disk."));
            }
        }

        // Notify a writer thread that there might be new data in the queue
        frame_write_condition_variables_[i]->notify_one();

        frame_sources_[i].second->post();
        ////////////////////////////
        //  END CRITICAL SECTION  //
    }

    // Read positions
    for (psvec_size_t i = 0; i !=  position_sources_.size(); i++) {

        // START CRITICAL SECTION //
        ////////////////////////////
        source_eof_ |= (position_sources_[i].second->wait() == oat::NodeState::END);

        position_write_number_[i] = position_sources_[i].second->write_number();
        positions_[i] = position_sources_[i].second->clone();

        position_sources_[i].second->post();
        ////////////////////////////
        //  END CRITICAL SECTION  //
    }

    // Push frames to buffers
    // Write the frames to file
    if (record_on_)
        writePositionsToFile();

    return source_eof_;
}

void Recorder::writeFramesToFileFromBuffer(uint32_t writer_idx) {

    cv::Mat m;
    while (running_) {

        std::unique_lock<std::mutex> lk(*frame_write_mutexes_[writer_idx]);
        frame_write_condition_variables_[writer_idx]->wait_for(lk, std::chrono::milliseconds(10));

        while (frame_write_buffers_[writer_idx]->pop(m)) {

            if (!video_writers_[writer_idx]->isOpened()) {
                initializeVideoWriter(*video_writers_[writer_idx],
                                      video_file_names_.at(writer_idx),
                                      m);
            }

            video_writers_[writer_idx]->write(m);
        }
    }
}

void Recorder::writePositionsToFile() {

    if (position_fp_) {

        json_writer_.StartObject();

        for (pvec_size_t i = 0; i !=  positions_.size(); i++) {
            json_writer_.String(positions_[i].label());
            positions_[i].Serialize(json_writer_);
        }

        json_writer_.EndObject();
    }
}

void Recorder::writePositionFileHeader(const std::string& date,
                                       const double sample_rate,
                                       const std::vector<std::string>& sources) {

    json_writer_.StartObject();

    json_writer_.String("date");
    json_writer_.String(date.c_str());

    json_writer_.String("sample_rate_hz");
    json_writer_.Double(sample_rate);

    json_writer_.String("position_sources");
    json_writer_.StartArray();
    for (auto &s : sources)
        json_writer_.String(s.c_str());
    json_writer_.EndArray();

    json_writer_.EndObject();

}

void Recorder::initializeVideoWriter(cv::VideoWriter& writer,
                                const std::string  &file_name,
                                const oat::Frame &image) {

    // Initialize writer using the first frame taken from server
    int fourcc = CV_FOURCC('H', '2', '6', '4');
    writer.open(file_name, fourcc, sample_rate_hz_, image.size());
}

void Recorder::initializeRecording(const std::string &save_directory,
                                   const std::string &file_name,
                                   const bool prepend_timestamp,
                                   const bool prepend_source,
                                   const bool allow_overwrite) {

    // Generate timestamp for headers and potentially for file names
    std::string timestamp = oat::createTimeStamp();

    if (!position_sources_.empty()) {

        //// If there we are starting a new file, finish up the old one
        //if (position_fp_ != nullptr) {
        //    json_writer_.EndArray();
        //    json_writer_.EndObject();
        //    file_stream_->Flush();
        //}

        // Create a single position file
        std::string posi_fid;
        std::string base_fid;
        if (prepend_source || file_name.empty())
           base_fid = position_sources_[0].first;
        if (!file_name.empty() && base_fid.empty())
           base_fid = file_name;
        else if (!file_name.empty() && !base_fid.empty())
           base_fid += "_" + file_name;
        base_fid += ".json";

        int err = oat::createSavePath(posi_fid,
                                      save_directory,
                                      base_fid,
                                      timestamp + "_",
                                      prepend_timestamp,
                                      allow_overwrite);
        if (err) {
            throw std::runtime_error("Recording file initialization exited "
                                     "with error " + std::to_string(err));
        }

        position_fp_ = fopen(posi_fid.c_str(), "wb");

        file_stream_.reset(new rapidjson::FileWriteStream(position_fp_,
                position_write_buffer, sizeof(position_write_buffer)));
        json_writer_.Reset(*file_stream_);

        // Main object, end this object before write flush
        json_writer_.StartObject();

        // Coordinate system
        char version[255];
        strcpy (version, Oat_VERSION_MAJOR);
        strcat (version, ".");
        strcat (version, Oat_VERSION_MINOR);
        json_writer_.String("oat_version");
        json_writer_.String(version);

        // Complete header object
        json_writer_.String("header");
        std::vector<std::string> pos_addrs;
        pos_addrs.reserve(std::distance(position_sources_.begin(),position_sources_.end()));
        std::transform(position_sources_.begin(),
                       position_sources_.end(),
                       std::back_inserter(pos_addrs),
                       [](PositionSource& p){ return p.first; });

        writePositionFileHeader(timestamp, sample_rate_hz_, pos_addrs);

        // Start data object
        json_writer_.String("positions");
        json_writer_.StartArray();
    }

    if (!frame_sources_.empty()) {

        // Create a file and writer for each frame source
        for (auto &s : frame_sources_) {

            // Generate file name for this video
            // Create a single position file
            std::string frame_fid;
            std::string base_fid;
            if (prepend_source || file_name.empty())
               base_fid = s.first;
            if (!file_name.empty() && base_fid.empty())
               base_fid = file_name;
            else if (!file_name.empty() && !base_fid.empty())
               base_fid += "_" + file_name;
            base_fid += ".avi";

            int err = oat::createSavePath(frame_fid,
                                          save_directory,
                                          base_fid,
                                          timestamp + "_",
                                          prepend_timestamp,
                                          allow_overwrite);
            if (err) {
                throw std::runtime_error("Recording file initialization exited "
                                         "with error " + std::to_string(err));
            }

            video_file_names_.push_back(frame_fid);
            video_writers_.push_back(std::make_unique<cv::VideoWriter>());
        }
    }
}

} /* namespace oat */
