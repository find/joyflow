#pragma once

#include "def.h"
#include "oparg.h"
#include "opdesc.h"
#include <nlohmann/json.hpp>
#include <glm/glm.hpp>

BEGIN_JOYFLOW_NAMESPACE


NLOHMANN_JSON_SERIALIZE_ENUM(ArgType, {
    {ArgType::REAL,          "real"},
    {ArgType::INT,           "int"},
    {ArgType::BOOL,          "bool"},
    {ArgType::COLOR,         "color"},
    {ArgType::MENU,          "menu"},
    {ArgType::STRING,        "string"},
    {ArgType::CODEBLOCK,     "codeblock"},
    {ArgType::DIRPATH,       "dirpath"},
    {ArgType::FILEPATH_OPEN, "openfilepath"},
    {ArgType::FILEPATH_SAVE, "savefilepath"},
    {ArgType::OPREF,         "opref"},
    {ArgType::BUTTON,        "button"},
    {ArgType::TOGGLE,        "toggle"}});

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(ArgDesc,
    type,
    name,
    label,
    tupleSize,
    description,
    defaultExpression,
    valueRange,
    closeRange,
    menu,
    updateScript,
    callback);

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(OpDesc,
    name,
    numRequiredInput,
    numMaxInput,
    numOutputs,
    inputPinNames,
    outputPinNames,
    argDescs);

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(glm::ivec2, x,y);
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(glm::ivec3, x,y,z);
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(glm::ivec4, x,y,z,w);

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(glm::vec2, x,y);
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(glm::vec3, x,y,z);
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(glm::vec4, x,y,z,w);

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(glm::dvec2, x,y);
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(glm::dvec3, x,y,z);
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(glm::dvec4, x,y,z,w);

END_JOYFLOW_NAMESPACE

