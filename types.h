#pragma once

#include <map>
#include <string>
#include <vector>

// cpu is in millicores, memory in MiB
struct Resources
{
    long long cpu = 0;
    long long mem = 0;
};

struct Node
{
    int id = -1;
    std::string name;
    Resources capacity;
    Resources allocated;
    int maxPods = 110;
    int podCount = 0;
};

struct Pod
{
    int id = -1;
    std::string name;
    Resources request;
    int priority = 0;
    long long arrivalTime = 0;
    long long duration = 0;
};

enum class EventKind
{
    Bind,
    Preempt,
    Unschedulable,
    Timeout
};

struct ScheduleEvent
{
    long long time = 0;
    EventKind kind = EventKind::Bind;
    int podId = -1;
    int nodeId = -1;
    int byPodId = -1; // the preemptor, only set for Preempt events
};

struct SimulationInput
{
    std::vector<Node> nodes;
    std::vector<Pod> pods;
};

struct SimulationConfig
{
    bool preemption = false;
    long long maxWait = 0; // a pod pending this long is dropped, 0 means wait forever
};

struct SimulationResult
{
    std::vector<ScheduleEvent> events;
    long long totalPods = 0;
    long long completedPods = 0;
    long long unschedulablePods = 0; // does not fit on any node even when the node is empty
    long long timedOutPods = 0;      // stayed pending longer than maxWait
    long long delayedPods = 0;       // failed at least one scheduling attempt
    long long preemptions = 0;
    long long lostWork = 0; // run time thrown away by evictions
    long double avgWaiting = 0.0L;
    long double p95Waiting = 0.0L;
    long double avgTurnaround = 0.0L;
    std::map<int, long double> avgWaitingByPriority;
    long double cpuUtilization = 0.0L; // time-weighted share of cluster capacity over the makespan
    long double memUtilization = 0.0L;
    // same, but only from the first to the last arrival, so the drain at the end does not dilute it
    long double windowCpuUtilization = 0.0L;
    long double windowMemUtilization = 0.0L;
    long double avgActiveNodes = 0.0L; // time-weighted count of nodes running at least one pod
    long long makespan = 0;

    long double unschedulableRate() const
    {
        if (totalPods == 0)
            return 0.0L;
        return static_cast<long double>(unschedulablePods + timedOutPods) / totalPods;
    }
};
