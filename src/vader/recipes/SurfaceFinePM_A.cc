/*
 * (C) Copyright 2021-2023  UCAR.
 *
 * This software is licensed under the terms of the Apache Licence Version 2.0
 * which can be obtained at http://www.apache.org/licenses/LICENSE-2.0.
 */

#include <math.h>
#include <iostream>
#include <vector>

#include "atlas/array.h"
#include "atlas/field/Field.h"
#include "atlas/util/Metadata.h"
#include "oops/util/Logger.h"
#include "vader/recipes/SurfaceFinePM.h"


namespace vader
{
// ------------------------------------------------------------------------------------------------

// Static attribute initialization
const char SurfaceFinePM_A::Name[] = "SurfaceFinePM_A";
const std::vector<std::string> SurfaceFinePM_A::Ingredients = {"aso4i",
                                                                         "aso4j"};

// Register the maker
static RecipeMaker<SurfaceFinePM_A> makerSurfaceFinePM_A_(SurfaceFinePM_A::Name);

SurfaceFinePM_A::SurfaceFinePM_A(const Parameters_ & params,
                                    const VaderConfigVars & configVariables):
                                            configVariables_{configVariables}
{
    oops::Log::trace() << "SurfaceFinePM_A::SurfaceFinePM_A(params)"
        << std::endl;
}

std::string SurfaceFinePM_A::name() const
{
    return SurfaceFinePM_A::Name;
}

std::string SurfaceFinePM_A::product() const
{
    return "surface_fine_pm";
}

std::vector<std::string> SurfaceFinePM_A::ingredients() const
{
    return SurfaceFinePM_A::Ingredients;
}

size_t SurfaceFinePM_A::productLevels(const atlas::FieldSet & afieldset) const
{
    return afieldset.field("aso4i").levels();
}

atlas::FunctionSpace SurfaceFinePM_A::productFunctionSpace
                                                (const atlas::FieldSet & afieldset) const
{
    return afieldset.field("aso4i").functionspace();
}

bool SurfaceFinePM_A::executeNL(atlas::FieldSet & afieldset)
{
    oops::Log::trace() << "entering SurfaceFinePM_A::executeNL function"
        << std::endl;

    atlas::Field aso4i = afieldset.field("aso4i");
    atlas::Field aso4j = afieldset.field("aso4j");
    atlas::Field surface_fine_pm = afieldset.field("surface_fine_pm");

    auto aso4i_view = atlas::array::make_view<double, 2>(aso4i);
    auto aso4j_view = atlas::array::make_view<double, 2>(aso4j);
    auto surface_fine_pm_view = atlas::array::make_view<double, 2>(surface_fine_pm);

    size_t grid_size = aso4j.size();

    int nlevels = aso4i.levels();
    for (int level = 0; level < nlevels; ++level) {
      for ( size_t jnode = 0; jnode < grid_size ; ++jnode ) {
        surface_fine_pm_view(jnode, level) =
                      aso4i_view(jnode, level) + aso4j_view(jnode, level);
      }
    }

    oops::Log::trace() << "leaving SurfaceFinePM_A::executeNL function" << std::endl;

    return true;
}

}  // namespace vader
