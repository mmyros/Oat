//******************************************************************************
//* File:   Source.h
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

#ifndef SOURCE_H
#define	SOURCE_H

#include <string>
#include <boost/interprocess/managed_shared_memory.hpp>
#include <boost/thread/thread_time.hpp>

#include "ForwardsDecl.h"
#include "Node.h"
#include "SharedCVMat.h"

namespace oat {

template<typename T>
class SourceBase  {
public:
    SourceBase();
    virtual ~SourceBase();

    void bind(const std::string &address, const size_t bytes);
    void connect();
    void wait();
    void post();

protected:

    void * read();
    T * object_;
    Node * node_;

private:

    std::string address_;
    //bip::managed_shared_memory shmem_;
    bool shmem_bound_ {false};
    size_t this_index_;
};

template<typename T>
SourceBase<T>::SourceBase()
{
    // Nothing
}

template<typename T>
SourceBase<T>::~SourceBase() {

    // If the client reference count is 0 and there is no server
    // attached to the node, deallocate the shmem
    if (shmem_bound_ &&
        node_->decrementSourceRefCount() == 0 &&
        node_->sink_state() != oat::SinkState::BOUND) {

        // Ensure that no server is deadlocked
        post();

        bip::shared_memory_object::remove(address_.c_str());
    }
}

template<typename T>
void SourceBase<T>::bind(const std::string& address, const size_t bytes) {

    // Addresses for this block of shared memory
    address_ = address;
    std::string node_address = address_ + "/shmgr";
    std::string obj_address = address_ + "/shobj";

    // Define shared memory
    shmem_t shmem = bip::managed_shared_memory(
            bip::open_or_create,
            address.c_str(),
            bytes + sizeof (oat::Node) + sizeof(T));

    // Facilitates synchronized access to shmem
    node_ = shmem.find_or_construct<oat::Node>(node_address.c_str())();

    // Find an existing shared object or construct one with default parameters
    object_ = shmem.find_or_construct<T>(obj_address.c_str())();

    // Let the node know this source is attached and get our index
    this_index_ = node_->incrementSourceRefCount();
    shmem_bound_ = true;
}

// TODO: This is specific to shared cv::Mats. Therefore, should be a specialization?
template<typename T>
void * SourceBase<T>::read() {

    if (node_->sink_state() != oat::SinkState::BOUND)
        throw std::runtime_error("Source tried to read from node without bound sink");

    // Reopen the previously allocated shared memory segment
    shmem_t shmem = bip::managed_shared_memory(
            bip::open_only,
            address_.c_str());

    return shmem.get_address_from_handle(object_->data());
}

template<typename T>
void SourceBase<T>::wait() {

    boost::system_time timeout = boost::get_system_time() + msec_t(10);

    // Only wait if there is a SOURCE attached to the node
    // Wait with timed wait with period check to prevent deadlocks
    while (!node_->readBarrier(this_index_).timed_wait(timeout)) {

        // Loops checking if wait has been released
        timeout = boost::get_system_time() + msec_t(10);
    }

    // Obtain ownership over mutex so that shared memory can be written to.
    // This must be released using signal()
    //manager_->mutex.wait();
}

template<typename T>
void SourceBase<T>::connect() {

    if (node_->sink_state() != oat::SinkState::BOUND)
        wait();
}

template<typename T>
void SourceBase<T>::post() {

    // Release exclusive lock over shared memory
    //manager_->mutex.post();

    if( node_->incrementSourceReadCount() == node_->source_ref_count()) {
        node_->write_barrier.post();
    }
}

// Specializations

template<typename T>
class Source : public SourceBase<T> { };

template<>
class Source<SharedCVMat> : public SourceBase<SharedCVMat> {

public:
    void connect() {

        // Wait for the SINK to bind the node and provide matrix
        // header info.
        if (node_->sink_state() != oat::SinkState::BOUND)
            wait();

        frame_ = cv::Mat(object_->size(),
                       object_->type(),
                       read());
    }

    inline cv::Mat frame() const { return frame_; }
    inline cv::Mat cloneFrame() { return frame_.clone(); }

private :
    cv::Mat frame_;

};

} // namespace oat

#endif	/* SOURCE_H */
