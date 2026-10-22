#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "types.h"

struct WorkloadSpec
{
    std::string pattern = "steady"; // one of workloadPatterns()
    int pods = 500;
    uint64_t seed = 1;
};

// 12 nodes of four shapes: small, general purpose, cpu-optimised and memory-optimised
std::vector<Node> makeHeterogeneousCluster();

// pods come back sorted by arrival time with id equal to their index
std::vector<Pod> generateWorkload(const WorkloadSpec &spec, const std::vector<Node> &nodes);

std::vector<std::string> workloadPatterns();
bool isWorkloadPattern(const std::string &name);
