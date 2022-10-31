/*
 * (C) Copyright 2021  UCAR.
 *
 * This software is licensed under the terms of the Apache Licence Version 2.0
 * which can be obtained at http://www.apache.org/licenses/LICENSE-2.0.
 */

#include <algorithm>
#include <iostream>
#include <memory>
#include <unordered_map>
#include <utility>
#include <vector>

#include "atlas/array.h"
#include "atlas/field/Field.h"
#include "atlas/functionspace/FunctionSpace.h"
#include "atlas/option/Options.h"
#include "oops/util/Logger.h"
#include "oops/util/Timer.h"
#include "vader/cookbook.h"
#include "vader/vader.h"

namespace vader {

// ------------------------------------------------------------------------------------------------
Vader::~Vader() {
    oops::Log::trace() << "Vader::~Vader done" << std::endl;
}
// ------------------------------------------------------------------------------------------------
void Vader::createCookbook(std::unordered_map<std::string,
                                              std::vector<std::string>>
                                              definition,
                        const std::vector<RecipeParametersWrapper> &
                    allRecpParamWraps) {
    oops::Log::trace() << "entering Vader::createCookbook" << std::endl;
    std::vector<std::unique_ptr<RecipeBase>> recipes;
    for (auto defEntry : definition) {
        recipes.clear();
        for (auto recipeName : defEntry.second) {
            // There might not be any recipe parameters at all.
            // There might not be parameters for THIS recipe.
            // We must prepare for all eventualities.
            bool parametersFound = false;
            for (auto & singleRecpParamWrap : allRecpParamWraps) {
                if (singleRecpParamWrap.recipeParams.value().name.value()
                                                            == recipeName) {
                    recipes.push_back(std::unique_ptr<RecipeBase>
                        (RecipeFactory::create(recipeName,
                            singleRecpParamWrap.recipeParams)));
                    parametersFound = true;
                    break;
                }
            }
            if (!parametersFound) {
                auto emptyRecipeParams = RecipeFactory::createParameters(recipeName);
                recipes.push_back(std::unique_ptr<RecipeBase>
                                  (RecipeFactory::create(recipeName, *emptyRecipeParams)));
            }
        }
        cookbook_[defEntry.first] = std::move(recipes);
    }
    oops::Log::trace() << "leaving Vader::createCookbook" << std::endl;
}
// ------------------------------------------------------------------------------------------------
Vader::Vader(const VaderParameters & parameters) {
    util::Timer timer(classname(), "Vader");
    // TODO(vahl): Parameters can alter the default cookbook here
    std::unordered_map<std::string, std::vector<std::string>> definition =
        getDefaultCookbookDef();
    oops::Log::trace() << "entering Vader::Vader(parameters) " << std::endl;
    oops::Log::debug() << "Vader::Vader parameters = " << parameters << std::endl;

    // Vader is designed to function without parameters. So VaderParameters
    // should not have any RequiredParameters.
    //
    // To simplify things for vader clients, they should declare vader Parameters with a
    // default construction of empty/default VaderParameters. i.e. their Parameters should contain:
    // oops::Parameter<vader::VaderParameters> vader{"vader", {}, this};
    //
    if (parameters.recipeParams.value() == boost::none) {
        createCookbook(definition);
    } else {
        createCookbook(definition, *parameters.recipeParams.value());
    }
}
// ------------------------------------------------------------------------------------------------
/*! \brief Change Variable
*
* \details **changeVar** is is called externally to invoke Vader's non-linear variable change
* functionality. The caller passes an Atlas FieldSet that contains two kinds
* of fields:
* * Fields that have already been populated with values
* * Fields that have been allocated but need to be calculated and populated
* The already-populated fields serve as the ingredients for recipes which then
* populate fields. The names of the variables that still need to be
* populated are passed via the neededVars parameter. After this method is
* complete, Vader will have popluated all the variables it can based on
* the ingredients it was given and the recipes in its cookbook. The names of the
* variables it was able to populate will have been removed from the neededVars
* list. Any variable names remaining in neededVars remain unpopulated.
*
* \param[in,out] afieldset This is the FieldSet described above
* \param[in,out] neededVars Names of unpopulated Fields in afieldset
* \returns List of variables VADER was able to populate
*
*/
oops::Variables Vader::changeVar(atlas::FieldSet & afieldset,
                                 oops::Variables & neededVars) const {
    util::Timer timer(classname(), "changeVar");
    oops::Log::trace() << "entering Vader::changeVar " << std::endl;
    oops::Log::debug() << "neededVars passed to Vader::changeVar: " << neededVars << std::endl;

    oops::Variables varsProduced(neededVars);

    auto fieldSetFieldNames = afieldset.field_names();
    // Loop through all the requested fields in neededVars
    // Since neededVars can be modified by planVariable and planVariable calls
    // itself recursively, we make a copy of the list here before we start.
    std::vector<std::string> targetVariables{neededVars.variables()};
    std::vector<std::pair<std::string, const std::unique_ptr<RecipeBase> &>> plan;
    bool recipesNeedTLAD = false;  // It's OK here to plan recipes with no TL/AD methods

    for (auto targetVariable : targetVariables) {
        oops::Log::debug() <<
            "Vader::changeVar calling Vader::planVariable for: "
            << targetVariable << std::endl;
        planVariable(afieldset, neededVars, targetVariable, recipesNeedTLAD, plan);
    }
    executePlanNL(afieldset, plan);

    oops::Log::debug() << "neededVars remaining after Vader::changeVar: " << neededVars
        << std::endl;
    varsProduced -= neededVars;
    oops::Log::trace() << "leaving Vader::changeVar" << std::endl;
    return varsProduced;
}
// ------------------------------------------------------------------------------------------------
/*! \brief Change Variable-Trajectory 
*
* \details **changeVarTraj** is called externally to set up the trajectory for subsequent calls to
* vader's changeVarTL and changeVarAD methods. (The corresponding method used to be called 
* 'setTrajectory' in OOPS.) It performs the same non-linear variable change logic
* as the changeVar method, but also saves the result in Vader's trajectory_ property.
*
* \param[in,out] afieldset This is the input/output FieldSet, same as in changeVar.
* \param[in,out] neededVars Names of unpopulated Fields in afieldset
*
*/
oops::Variables Vader::changeVarTraj(atlas::FieldSet & afieldset,
                      oops::Variables & neededVars) {
    util::Timer timer(classname(), "changeVar");
    oops::Log::trace() << "entering Vader::changeVarTraj " << std::endl;
    oops::Log::debug() << "neededVars passed to Vader::changeVarTraj: " << neededVars << std::endl;

    oops::Variables varsProduced(neededVars);

    auto fieldSetFieldNames = afieldset.field_names();
    // Loop through all the requested fields in neededVars
    // Since neededVars can be modified by planVariable and planVariable calls
    // itself recursively, we make a copy of the list here before we start.
    std::vector<std::string> targetVariables{neededVars.variables()};
    bool recipesNeedTLAD = true;  // Only plan recipes with TL/AD methods implemented

    for (auto targetVariable : targetVariables) {
        oops::Log::debug() <<
            "Vader::changeVarTraj calling Vader::planVariable for: "
            << targetVariable << std::endl;
        planVariable(afieldset, neededVars, targetVariable, recipesNeedTLAD, recipeExecutionPlan_);
    }
    executePlanNL(afieldset, recipeExecutionPlan_);
    // Save the trajectory in Vader's private variable
    trajectory_.clear();
    for (const auto from_Field : afieldset) {
        // Make a deep copy of each field and put it in trajectory_
        atlas::Field to_Field(from_Field.name(), from_Field.datatype(), from_Field.shape());
        auto from_view = atlas::array::make_view<double, 2>(from_Field);
        auto to_view = atlas::array::make_view<double, 2>(to_Field);
        to_view.assign(from_view);
        trajectory_.add(to_Field);
    }

    oops::Log::debug() << "neededVars remaining after Vader::changeVarTraj: " << neededVars
        << std::endl;
    varsProduced -= neededVars;
    oops::Log::trace() << "leaving Vader::changeVarTraj" << std::endl;
    return varsProduced;
}
// ------------------------------------------------------------------------------------------------
/*! \brief Change Variable Tangent Linear 
*
* \details **changeVarTL** is called externally to perform the tangent linear (TL) variable change.
* Note that unlike changeVar and changeVarTraj, the vader planVariable algorithm to determine which
* recipes to call is not invoked. Instead, the same recipe plan determined during changeVarTraj is
* executed, calling the TL methods of the planned recipes. Also note that neededVars
* is not used as input, but is kept for interface consistency, and as another way to
* communicate back to the caller which variables VADER has created.
*
* \param[in,out] afieldset This is the input/output fieldset, same as in changeVar
* \param[in,out] neededVars Names of unpopulated Fields in afieldset
*
*/
oops::Variables Vader::changeVarTL(atlas::FieldSet & afieldset,
                      oops::Variables & neededVars) const {
    oops::Log::trace() << "entering Vader::changeVarTL" << std::endl;
    oops::Variables varsPopulated;
    executePlanTL(afieldset, recipeExecutionPlan_);
    for (auto varplan : recipeExecutionPlan_) {
        varsPopulated.push_back(varplan.first);
    }
    neededVars -= varsPopulated;
    oops::Log::trace() << "leaving Vader::changeVarTL" << std::endl;
    return varsPopulated;
}
// ------------------------------------------------------------------------------------------------
/*! \brief Change Variable Adjoint 
*
* \details **changeVarAD** is called externally to perform the adjoint (AD) variable change.
* Note that unlike changeVar and changeVarTraj, the vader planVariable algorithm to determine which
* recipes to call is not invoked. Instead, the same recipe plan determined during changeVarTraj is
* executed, but in reverse order, calling the AD methods of the planned recipes. 
* Also note that varsToAdjoint here should be the SAME VARIABLES that are passed to changeVarTraj
* and changeVarTL in the 'neededVars' parameter. But in this case these variables should already be
* populated in afieldset. VADER will perform the adjoint methods of the recipes that produce these
* variables.
*
* \param[in,out] afieldset This is the input/output fieldset
* \param[in,out] varsToAdjoint Same vars as 'neededVars' in changeVarTraj and changeVarTL
*
*/
oops::Variables Vader::changeVarAD(atlas::FieldSet & afieldset,
                      oops::Variables & varsToAdjoint) const {
    oops::Log::trace() << "entering Vader::changeVarAD" << std::endl;
    oops::Variables varsAdjointed;
    executePlanAD(afieldset, recipeExecutionPlan_);
    for (auto varplan : recipeExecutionPlan_) {
        oops::Log::debug() << "Adding to varsAdjointed: " << varplan.first << std::endl;
        varsAdjointed.push_back(varplan.first);
    }
    varsToAdjoint -= varsAdjointed;
    oops::Log::trace() << "leaving Vader::changeVarAD" << std::endl;
    return varsAdjointed;
}
// ------------------------------------------------------------------------------------------------
/*! \brief Plan Variable
*
* \details **planVariable** contains Vader's primary algorithm for attempting to
* populate an unpopulated field. It:
* * Checks the cookbook for recipes for the desired field (the targetVariable)
* * Checks each recipe to see if its required ingredients have been provided
* * If an ingredient is missing, recursively calls itself to attempt to get it
* * Adds the variable and recipe name to the "recipeExecutionPlan" if the recipe is viable.
* * If successful, removes the targetVariable from neededVars and returns 'true'
*
* \param[in,out] afieldset A fieldset containg both populated and unpopulated fields
* \param[in,out] neededVars Names of unpopulated Fields in afieldset
* \param[in] targetVariable variable name this instance is trying to populate
* \param[in] needsTLDA Flag to only consider recipes that have TLAD implemented
* \param[in,out] plan ordered list of viable recipes that will get exectued later
* \return boolean 'true' if it successfully creates a plan for targetVariable, else false
*
*/
bool Vader::planVariable(atlas::FieldSet & afieldset,
                         oops::Variables & neededVars,
                         const std::string targetVariable,
                         const bool needsTLAD,
                         std::vector<std::pair<std::string,
                                              const std::unique_ptr<RecipeBase> &>> & plan) const {
    bool variablePlanned = false;

    oops::Log::trace() << "entering Vader::planVariable for variable: " << targetVariable <<
        std::endl;

    auto fieldSetFieldNames = afieldset.field_names();

    // Since this function is called recursively, make sure targetVariable is
    // still needed
    if (!neededVars.has(targetVariable)) {
        oops::Log::debug() << targetVariable <<
            " is no longer in the variable list neededVars." << std::endl;
        oops::Log::trace() << "leaving Vader::planVariable for variable: "
            << targetVariable << std::endl;
        return true;
    }

    auto recipeList = cookbook_.find(targetVariable);

    // If recipeList is found, recipeList->second is a vector of unique_ptr's
    // to Recipe objects that produce 'variableName'
    if ((recipeList != cookbook_.end()) && !recipeList->second.empty()) {
        oops::Log::debug() <<
            "Vader cookbook contains at least one recipe for '" << targetVariable << "'" <<
            std::endl;
        for (const auto & recipe : recipeList->second) {
            if (needsTLAD && !recipe->hasTLAD()) {
                oops::Log::debug() << "Not checking recipe: '" << recipe->name() <<
                    "' since it does not have TL/AD methods implemented.";
                continue;
            }
            oops::Log::debug() << "Checking to see if we have ingredients for recipe: " <<
                recipe->name() << std::endl;
            bool haveIngredient = false;
            for (auto ingredient : recipe->ingredients()) {
                if (ingredient == targetVariable) {
                    oops::Log::error() << "Error: Ingredient list for " <<
                        recipe->name() << " contains the target." << std::endl;
                    // This could cause infinite recursion if we didn't check.
                    // TODO(vahl): infinite recursion probably still possible
                    //       with badly-constructed cookbook.
                    break;
                }
                haveIngredient =
                    (std::find(fieldSetFieldNames.begin(), fieldSetFieldNames.end(), ingredient)
                        != fieldSetFieldNames.end()) && (!neededVars.has(ingredient));
                if (!haveIngredient) {
                    oops::Log::debug() << "ingredient " << ingredient <<
                        " not found. Recursively checking if Vader can make it." << std::endl;
                    haveIngredient = planVariable(afieldset, neededVars, ingredient,
                                                  needsTLAD, plan);
                }
                oops::Log::debug() << "ingredient " << ingredient <<
                    (haveIngredient ? " is" : " is not") << " available." << std::endl;
                if (!haveIngredient) break;  // Missing an ingredient. Don't check the others.
            }
            if (haveIngredient) {
                oops::Log::debug() <<
                    "All ingredients are in the fieldset. Adding recipe to recipeExecutionPlan." <<
                    std::endl;
                plan.push_back(std::pair<std::string, const std::unique_ptr<RecipeBase> &>
                                                                    ({targetVariable, recipe}));
                variablePlanned = true;
                neededVars -= targetVariable;
                break;  // Found a viable recipe. Don't need to check any other potential recipes.
            } else {
                oops::Log::debug() << "Do not have all the ingredients for this recipe." <<
                    std::endl;
            }
        }
    } else {
        oops::Log::debug() << "Vader cookbook does not contain a recipe for: "
            << targetVariable << std::endl;
    }
    oops::Log::trace() << "leaving Vader::planVariable for variable: " << targetVariable <<
        std::endl;
    return variablePlanned;
}
// ------------------------------------------------------------------------------------------------
/*! \brief Execute Plan (non-linear)
*
* \details **executePlanNL** calls, in order, the 'executeNL' method of the
* recipes specified in the recipeExecutionPlan that is passed in. (The recipeExecutionPlan is
* created through calls to planVariable.) Before executing the recipe, validations
* are performed on the passed FieldSet to ensure it has the ingredients, and the
* product Field is created and added if not already present.
*
* \param[in,out] afieldset A fieldset containg both populated and unpopulated fields
* \param[in] recipeExecutionPlan ordered list of recipes that are to be exectued
*
*/
void Vader::executePlanNL(atlas::FieldSet & afieldset,
            const std::vector<std::pair<std::string,
                              const std::unique_ptr<RecipeBase> &>> & recipeExecutionPlan) const {
    oops::Log::trace() << "entering Vader::executePlanNL" <<  std::endl;
    for (auto varPlan : recipeExecutionPlan) {
        oops::Log::debug() << "Attempting to calculate variable " << varPlan.first <<
            " using recipe with name: " << varPlan.second->name() << std::endl;
        for (auto ingredient :  varPlan.second->ingredients()) {
            ASSERT(afieldset.has(ingredient));
        }

        if (afieldset.has(varPlan.first))
        {
            // Verify the number of levels in the Field is enough for the recipe
            ASSERT(afieldset.field(varPlan.first).levels() >=
                   varPlan.second->productLevels(afieldset));
        } else {
            // Create the field and put it in the FieldSet
            atlas::Field newField =
                varPlan.second->productFunctionSpace(afieldset).createField<double>(
                    atlas::option::name(varPlan.first) |
                    atlas::option::levels(varPlan.second->productLevels(afieldset)));
            oops::Log::debug() << "Vader adding Field " << newField.name() <<
                " to fieldset." << std::endl;
            afieldset.add(newField);
        }

        if (varPlan.second->requiresSetup()) {
            varPlan.second->setup(afieldset);
        }
        const bool recipeSuccess = varPlan.second->executeNL(afieldset);
        ASSERT(recipeSuccess);  // At least for now, we'll require the execution to be successful
    }
    oops::Log::trace() << "leaving Vader::executePlanNL" <<  std::endl;
}
// ------------------------------------------------------------------------------------------------
/*! \brief Execute Plan (tangent linear)
*
* \details **executePlanTL** calls, in order, the 'execute' (tangent linear) method of the
* recipes specified in the recipeExecutionPlan that is passed in. (The recipeExecutionPlan is
* created through calls to planVariable.)
*
* \param[in,out] afieldset A fieldset containg both populated and unpopulated fields
* \param[in] recipeExecutionPlan ordered list of recipes that are to be exectued
*
*/
void Vader::executePlanTL(atlas::FieldSet & afieldset,
            const std::vector<std::pair<std::string,
                              const std::unique_ptr<RecipeBase> &>> & recipeExecutionPlan) const {
    oops::Log::trace() << "entering Vader::executePlanTL" <<  std::endl;
    // We must get the recipes specified in the recipeExecutionPlan out of the cookbook,
    // where they live
    for (auto varPlan : recipeExecutionPlan) {
        oops::Log::debug() << "Attempting to calculate variable " << varPlan.first <<
            " using recipe with name: " << varPlan.second->name() << std::endl;
        ASSERT(afieldset.has(varPlan.first));
        for (auto ingredient :  varPlan.second->ingredients()) {
            ASSERT(afieldset.has(ingredient));
        }
        if (varPlan.second->requiresSetup()) {
            varPlan.second->setup(afieldset);
        }
        const bool recipeSuccess =
            varPlan.second->executeTL(afieldset, trajectory_);
        ASSERT(recipeSuccess);  // At least for now, we'll require the execution to be successful
    }
    oops::Log::trace() << "leaving Vader::executePlanTL" <<  std::endl;
}
// ------------------------------------------------------------------------------------------------
/*! \brief Execute Plan (adjoint)
*
* \details **executePlanAD** calls, in reverse order, the 'execute' (adjoint) method of the
* recipes specified in the recipeExecutionPlan that is passed in. (The recipeExecutionPlan is
* created through calls to planVariable.)
*
* \param[in,out] afieldset A fieldset containg both populated and unpopulated fields
* \param[in] recipeExecutionPlan ordered list of recipes that are to be exectued
*
*/
void Vader::executePlanAD(atlas::FieldSet & afieldset,
            const std::vector<std::pair<std::string,
                              const std::unique_ptr<RecipeBase> &>> & recipeExecutionPlan) const {
    oops::Log::trace() << "entering Vader::executePlanAD" <<  std::endl;
    // We must get the recipes specified in the recipeExecutionPlan out of the cookbook,
    // where they live
    // We execute the adjoints in reverse order of the recipeExecutionPlan
    for (auto varPlanIt = recipeExecutionPlan.rbegin();
         varPlanIt != recipeExecutionPlan.rend();
         ++varPlanIt) {
        oops::Log::debug()  << "Performing adjoint of recipe with name: " <<
            varPlanIt->second->name() << std::endl;
        ASSERT(afieldset.has(varPlanIt->first));
        for (auto ingredient :  varPlanIt->second->ingredients()) {
            ASSERT(afieldset.has(ingredient));
        }
        if (varPlanIt->second->requiresSetup()) {
            varPlanIt->second->setup(afieldset);
        }
        const bool recipeSuccess =
            varPlanIt->second->executeAD(afieldset, trajectory_);
        ASSERT(recipeSuccess);  // At least for now, we'll require the execution to be successful
    }
    oops::Log::trace() << "leaving Vader::executePlanAD" <<  std::endl;
}
}  // namespace vader
