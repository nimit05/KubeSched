#pragma once

#include "policy.h"
#include "types.h"

SimulationResult runSimulation(const SimulationInput &input, const SchedulingPolicy &policy,
                               const SimulationConfig &config);
