#pragma once

#include "common/types.h"

struct Capturer {
    virtual ~Capturer() = default;
    virtual bool init() = 0;
    virtual bool selectWindow() = 0;
    virtual Frame capture() = 0;
    virtual int sourceWidth() const = 0;
    virtual int sourceHeight() const = 0;
};
