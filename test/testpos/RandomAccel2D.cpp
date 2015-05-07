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

#include "RandomAccel2D.h"

#include <string>
#include <iostream>
#include <limits>
#include <math.h>
#include <opencv2/opencv.hpp>

#include "../../lib/cpptoml/cpptoml.h"
#include "../../lib/shmem/MatServer.h"

RandomAccel2D::RandomAccel2D(std::string position_sink_name) :
  TestPosition<datatypes::Position2D>(position_sink_name) 
, accel_distribution(0.0, 5.0) {

    position_sink.createSharedObject();
    createStaticMatracies();
    
    // Initial condition
    state(0) = 0.0; // x
    state(1) = 0.0; // x'
    state(2) = 0.0; // y
    state(3) = 0.0; // y'

}

void RandomAccel2D::simulateAndServePosition() {
    
    // Simulate one step of random, but smooth, motion
    simulateMotion(); 
    
    // Transform into datatype::Position2D type
    datatypes::Position2D pos;
    
    // Simulated position info
    pos.position_valid = true;
    pos.position.x = state(0);
    pos.position.y = state(2);
    
    // We have access to the velocity info for comparison
    pos.velocity_valid = true;
    pos.velocity.x = state(1);
    pos.velocity.y = state(3);

    // Publish simulated position
    position_sink.pushObject(pos);
    
}

void RandomAccel2D::simulateMotion() {
    
    // Generate random acceleration
    accel_vec(0) = accel_distribution(accel_generator);
    accel_vec(1) = accel_distribution(accel_generator);
    
    // Apply acceleration and transition matrix to the simulated position
    state = state_transition_mat * state + input_mat * accel_vec;
}

void RandomAccel2D::createStaticMatracies() {
    
    // State transition matrix
    state_transition_mat(0, 0) = 1.0;
    state_transition_mat(0, 1) = DT;
    state_transition_mat(0, 2) = 0.0;
    state_transition_mat(0, 3) = 0.0;
    
    state_transition_mat(1, 0) = 0.0;
    state_transition_mat(1, 1) = 1.0;
    state_transition_mat(1, 2) = 0.0;
    state_transition_mat(1, 3) = 0.0;
    
    state_transition_mat(2, 0) = 0.0;
    state_transition_mat(2, 1) = 0.0;
    state_transition_mat(2, 2) = 1.0;
    state_transition_mat(2, 3) = DT;
    
    state_transition_mat(3, 0) = 0.0;
    state_transition_mat(3, 1) = 0.0;
    state_transition_mat(3, 2) = 0.0;
    state_transition_mat(3, 3) = 1.0;
    
    // Input Matrix
    input_mat(0, 0) = (DT*DT)/2;
    input_mat(0, 1) = 0.0;

    input_mat(1, 0) = DT;
    input_mat(1, 1) = 0.0;

    input_mat(2, 0) = 0.0;
    input_mat(2, 1) = (DT*DT)/2;
    
    input_mat(3, 0) = 0.0;
    input_mat(3, 1) = DT;
   
}
