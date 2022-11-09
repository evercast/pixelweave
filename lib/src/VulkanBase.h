#pragma once

#include "Macros.h"

#ifdef PW_DEBUG
#define VULKAN_HPP_ASSERT(condition) PW_UNUSED(condition)
#else
#define VULKAN_HPP_ASSERT(condition) PW_UNUSED(condition)
#endif
#include <vulkan/vulkan.hpp>
