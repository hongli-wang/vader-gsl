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
const std::vector<std::string> SurfaceFinePM_A::Ingredients = {"air_temperature",
                                                                         "surface_pressure"};

// Register the maker
static RecipeMaker<SurfaceFinePM_A> makerTempToPTemp_(SurfaceFinePM_A::Name);

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
    return afieldset.field("air_temperature").levels();
}

atlas::FunctionSpace SurfaceFinePM_A::productFunctionSpace
                                                (const atlas::FieldSet & afieldset) const
{
    return afieldset.field("air_temperature").functionspace();
}

bool SurfaceFinePM_A::executeNL(atlas::FieldSet & afieldset)
{
    oops::Log::trace() << "entering SurfaceFinePM_A::executeNL function"
        << std::endl;

    // Extract values from client config
    const double p0 = configVariables_.getDouble("reference_pressure");
    const double kappa = configVariables_.getDouble("kappa");  // Need better name

    atlas::Field temperature = afieldset.field("air_temperature");
    atlas::Field surface_pressure = afieldset.field("surface_pressure");
    atlas::Field surface_fine_pm = afieldset.field("surface_fine_pm");
    std::string t_units, ps_units;

    temperature.metadata().get("units", t_units);
    ASSERT_MSG(t_units == "K", "SurfaceFinePM_A::executeNL: Incorrect units for "
                            "air_temperature");
    surface_pressure.metadata().get("units", ps_units);
    ASSERT_MSG(ps_units == "Pa", "SurfaceFinePM_A::executeNL: Incorrect units for "
                            "surface_air_pressure");
    oops::Log::debug() << "SurfaceFinePM_A::execute: p0 value: " << p0 <<
        std::endl;
    oops::Log::debug() << "SurfaceFinePM_A::execute: kappa value: " << kappa <<
    std::endl;

    auto temperature_view = atlas::array::make_view<double, 2>(temperature);
    auto surface_pressure_view = atlas::array::make_view<double, 2>(surface_pressure);
    auto surface_fine_pm_view = atlas::array::make_view<double, 2>(surface_fine_pm);

    size_t grid_size = surface_pressure.size();

    int nlevels = temperature.levels();
    for (int level = 0; level < nlevels; ++level) {
      for ( size_t jnode = 0; jnode < grid_size ; ++jnode ) {
        surface_fine_pm_view(jnode, level) =
            temperature_view(jnode, level) * pow(p0 / surface_pressure_view(jnode, 0), kappa);
      }
    }

    oops::Log::trace() << "leaving SurfaceFinePM_A::executeNL function" << std::endl;

    return true;
}

}  // namespace vader
