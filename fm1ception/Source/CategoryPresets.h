#pragma once

#include <cstddef>

namespace fm1c
{
//==============================================================================
// The category and sub category FM-1Ception gives each firmware it knows by
// name (FirmwareLibrary::presetOf). CategoryPresets.cpp is written from a
// library whose firmwares are all assigned:
//
//   ./FM1CeptionTests --export-presets FirmwareLibrary.json Source/CategoryPresets.cpp
//==============================================================================
struct CategoryPreset
{
    /** A friendly name (FirmwareLibrary::normalise makes it the key). */
    const char* name;
    /** FirmwareLibrary::categoryName's, and one of its sub categories ("" for none). */
    const char* category;
    const char* subcategory;
};

extern const CategoryPreset kCategoryPresets[];
extern const size_t kNumCategoryPresets;
} // namespace fm1c
