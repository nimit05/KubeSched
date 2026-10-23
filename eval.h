#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct EvalOptions
{
    int pods = 500;
    int seeds = 3;
    uint64_t baseSeed = 1;
    long long maxWait = 300;
};

// one policy on one workload pattern, averaged over every seed
struct EvalRow
{
    std::string pattern;
    std::string policy;
    double cpuUtil = 0; // percent, over the arrival window
    double memUtil = 0;
    double makespanCpuUtil = 0; // percent, over the whole run including the drain
    double makespanMemUtil = 0;
    double activeNodes = 0; // time-weighted
    double avgWait = 0;
    double p95Wait = 0;
    double critWait = 0; // average wait of the highest priority class
    double unschedRate = 0;
    double delayedRate = 0;
    double preemptions = 0;
    double lostWork = 0;
    double makespan = 0;
};

// runs every policy, with and without preemption, on every workload pattern
std::vector<EvalRow> runEvaluation(const EvalOptions &options);
