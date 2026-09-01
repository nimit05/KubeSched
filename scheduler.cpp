#include "scheduler.h"

#include <deque>
#include <queue>
#include <vector>

using namespace std;

using RunningQueue = priority_queue<RunningTask, vector<RunningTask>, greater<RunningTask>>;

static int chooseThread(const SimulationInput &input, int sid, int need)
{
    int best = -1;
    for (int t = 0; t < input.m; ++t)
    {
        const auto &thr = input.services[sid][t];
        if (thr.availableResources < need)
            continue;

        if (best == -1 || thr.priority > input.services[sid][best].priority ||
            (thr.priority == input.services[sid][best].priority &&
             thr.availableResources > input.services[sid][best].availableResources) ||
            (thr.priority == input.services[sid][best].priority &&
             thr.availableResources == input.services[sid][best].availableResources &&
             thr.id < input.services[sid][best].id))
        {
            best = t;
        }
    }
    return best;
}

// this function tries to assign a request to a worker thread, it calls the chooseThread function to find a suitable worker thread for the request,adds a new RunningTask to the running queue
static bool assignRequest(SimulationInput &input, SimulationResult &res,
                          vector<long long> &st, vector<long long> &ft,
                          RunningQueue &running, int rid, long long now)
{
    auto &q = input.requests[rid];
    if (q.serviceId < 0 || q.serviceId >= input.n || q.resourcesRequired < 0 ||
        q.resourcesRequired > input.maxThreadResources[q.serviceId])
    {
        if (!q.rejected)
            ++res.rejectedCount;
        q.rejected = true;
        return false;
    }

    int tid = chooseThread(input, q.serviceId, q.resourcesRequired);
    if (tid == -1)
        return false;

    input.services[q.serviceId][tid].availableResources -= q.resourcesRequired;
    st[rid] = now;
    ft[rid] = now + q.duration;
    running.push(RunningTask{ft[rid], q.serviceId, tid, rid, q.resourcesRequired});
    res.processedOrder.push_back(AssignmentRecord{now, rid, q.serviceId, tid});
    return true;
}

// if the request is not rejected, it is added back to the waiting queue , this process continues until there are no more requests in the waiting queue or no progress can be made in assigning requests.
static void scheduleWaitingForService(SimulationInput &input, SimulationResult &res,
                                      vector<long long> &st, vector<long long> &ft,
                                      vector<deque<int>> &waiting,
                                      RunningQueue &running, int sid, long long now)
{
    bool progress = true;
    while (progress && !waiting[sid].empty())
    {
        progress = false;
        int rounds = static_cast<int>(waiting[sid].size());
        for (int i = 0; i < rounds; ++i)
        {
            int rid = waiting[sid].front();
            waiting[sid].pop_front();
            if (assignRequest(input, res, st, ft, running, rid, now))
                progress = true;
            else if (!input.requests[rid].rejected)
                waiting[sid].push_back(rid);
        }
    }
}

// this function releases all finished tasks until the given time limit, it updates the available resources for the corresponding worker threads and schedules any waiting requests for the services that have finished tasks
static void releaseFinishedUntil(SimulationInput &input, SimulationResult &res,
                                 vector<long long> &st, vector<long long> &ft,
                                 vector<deque<int>> &waiting,
                                 RunningQueue &running, long long limit)
{
    while (!running.empty() && running.top().finishTime <= limit)
    {
        auto done = running.top();
        running.pop();
        input.services[done.serviceId][done.threadId].availableResources += done.resourcesAllocated;
        scheduleWaitingForService(input, res, st, ft, waiting, running,
                                  done.serviceId, done.finishTime);
    }
}

SimulationResult runSimulation(SimulationInput input)
{
    SimulationResult res;
    if (input.n <= 0 || input.m <= 0)
        return res;

    vector<deque<int>> waiting(input.n);
    vector<long long> st(input.requests.size(), -1), ft(input.requests.size(), -1);
    RunningQueue running;

    // waiting is a vector of deques, where each deque corresponds to a service and contains the request IDs that are waiting for that service, st and ft are vectors that store the start time and finish time of each request respectively

    for (size_t i = 0; i < input.requests.size(); ++i)
    {
        // now is the arrival time of the current request, releaseFinishedUntil is called to release any finished tasks until the arrival time of the current request
        long long now = input.requests[i].arrivalTime;
        releaseFinishedUntil(input, res, st, ft, waiting, running, now);

        int rid = static_cast<int>(i);
        if (assignRequest(input, res, st, ft, running, rid, now) || input.requests[rid].rejected)
            continue;

        ++res.delayedCount;
        int sid = input.requests[rid].serviceId;
        if (sid >= 0 && sid < input.n && chooseThread(input, sid, input.requests[rid].resourcesRequired) == -1)
        {
            ++res.blockedCount;
        }
        if (sid >= 0 && sid < input.n)
            waiting[sid].push_back(rid);
        else
        {
            input.requests[rid].rejected = true;
            ++res.rejectedCount;
        }
    }

    // after processing all requests, this loop releases any remaining finished tasks and updates the available resources for the corresponding worker threads,
    while (!running.empty())
    {
        auto done = running.top();
        running.pop();
        input.services[done.serviceId][done.threadId].availableResources += done.resourcesAllocated;
        scheduleWaitingForService(input, res, st, ft, waiting, running, done.serviceId, done.finishTime);
    }

    for (int s = 0; s < input.n; ++s)
    {
        for (int rid : waiting[s])
        {
            if (!input.requests[rid].rejected)
            {
                input.requests[rid].rejected = true;
                ++res.rejectedCount;
            }
        }
    }

    long long completed = 0;
    for (const auto &q : input.requests)
    {
        if (!q.rejected && st[q.id] >= 0)
        {
            ++completed;
            res.avgWaiting += static_cast<long double>(st[q.id] - q.arrivalTime);
            res.avgTurnaround += static_cast<long double>(ft[q.id] - q.arrivalTime);
        }
    }
    if (completed)
    {
        res.avgWaiting /= completed;
        res.avgTurnaround /= completed;
    }
    return res;
}
