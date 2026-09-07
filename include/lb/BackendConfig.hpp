#pragma once

#include "lb/Backend.hpp"

#include <vector>
#include <string>

class BackendConfig {

public:
    static std::vector<Backend> load(
        const std::string& path
    );
};