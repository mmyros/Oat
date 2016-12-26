//******************************************************************************
//* File:   Format.h
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
//*****************************************************************************

#ifndef OAT_FORMAT_H
#define OAT_FORMAT_H

#include <string>
#include <vector>

#include "Writer.h"

namespace oat {

static constexpr int NPY_PREFIX_LEN {10};
static constexpr int NPY_DICT_START_BYTE {11};

std::vector<char> getNumpyHeader(const std::string &dtype_str);
void emplaceNumpyShape(FILE *fd, int64_t n);

template <typename L, typename R>
void append(L &lhs, R const &rhs)
{
    lhs.insert(lhs.end(), rhs.begin(), rhs.end());
}

}      /* namespace oat */
#endif /* OAT_FORMAT_H */
