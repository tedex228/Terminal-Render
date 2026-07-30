#pragma once

#include "common/types.h"
#include <string>

std::string frameToAnsi(const Frame& frame, int& out_term_width, int& out_term_height);
