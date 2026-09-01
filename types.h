#pragma once

#include <queue>
#include <vector>

struct WorkerThread
{
    int id = -1;
    int priority = 0;
    int totalResources = 0;
    int availableResources = 0;
};

struct Request
{
    int id = -1;
    int serviceId = -1;
    int resourcesRequired = 0;
    long long arrivalTime = 0;
    long long duration = 0;
    bool rejected = false;
};

struct RunningTask
{
    long long finishTime = 0;
    int serviceId = -1;
    int threadId = -1;
    int requestId = -1;
    int resourcesAllocated = 0;

    bool operator>(const RunningTask &other) const
    {
        if (finishTime != other.finishTime)
            return finishTime > other.finishTime;
        return requestId > other.requestId;
    }
};

struct AssignmentRecord
{
    long long startTime = 0;
    int requestId = -1;
    int serviceId = -1;
    int threadId = -1;
};

struct SimulationInput
{
    int n = 0;
    int m = 0;
    std::vector<std::vector<WorkerThread>> services;
    std::vector<int> maxThreadResources;
    std::vector<Request> requests;
};

struct SimulationResult
{
    std::vector<AssignmentRecord> processedOrder;
    long double avgWaiting = 0.0L;
    long double avgTurnaround = 0.0L;
    long long rejectedCount = 0;
    long long delayedCount = 0;
    long long blockedCount = 0;
};
