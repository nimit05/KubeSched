#pragma once

#include <iostream>
#include <string>

#include "types.h"

// reads "node" and "pod" records, returns false and sets err on malformed input
bool parseInput(std::istream &in, SimulationInput &out, std::string &err);

// writes the input back in the format parseInput reads
void writeInput(std::ostream &out, const SimulationInput &input);
