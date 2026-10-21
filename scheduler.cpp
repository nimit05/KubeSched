#include "scheduler.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <queue>
#include <set>
#include <tuple>
#include <vector>

using namespace std;

namespace
{

enum class PodState
{
    NotArrived,
    Pending,
    Running,
    Done,
    Dropped
};

struct PodStatus
{
    PodState state = PodState::NotArrived;
    int nodeId = -1;
    int runId = 0; // bumped on every bind so completions of preempted runs can be ignored
    long long enqueueTime = 0;
    long long startTime = -1;
    long long finishTime = -1;
    long long waited = 0; // total time spent pending, across every requeue
    bool delayed = false;
};

struct Completion
{
    long long finishTime = 0;
    int podId = -1;
    int runId = 0;

    bool operator>(const Completion &other) const
    {
        if (finishTime != other.finishTime)
            return finishTime > other.finishTime;
        return podId > other.podId;
    }
};

// pending pods are tried highest priority first, then oldest first
struct PendingOrder
{
    const vector<Pod> *pods = nullptr;

    bool operator()(int a, int b) const
    {
        const Pod &pa = (*pods)[a], &pb = (*pods)[b];
        if (pa.priority != pb.priority)
            return pa.priority > pb.priority;
        if (pa.arrivalTime != pb.arrivalTime)
            return pa.arrivalTime < pb.arrivalTime;
        return a < b;
    }
};

void addTo(Node &node, const Pod &pod)
{
    node.allocated.cpu += pod.request.cpu;
    node.allocated.mem += pod.request.mem;
    ++node.podCount;
}

void removeFrom(Node &node, const Pod &pod)
{
    node.allocated.cpu -= pod.request.cpu;
    node.allocated.mem -= pod.request.mem;
    --node.podCount;
}

// discrete-event simulation: time jumps between pod arrivals, pod completions and pending timeouts
class Simulator
{
public:
    Simulator(const SimulationInput &input, const SchedulingPolicy &policy, const SimulationConfig &config)
        : pods(input.pods), nodes(input.nodes), policy(policy), config(config),
          status(input.pods.size()), runningOn(input.nodes.size()), pending(PendingOrder{&input.pods})
    {
        for (size_t i = 0; i < nodes.size(); ++i)
        {
            nodes[i].id = static_cast<int>(i);
            nodes[i].allocated = Resources{};
            nodes[i].podCount = 0;
            totalCapacity.cpu += nodes[i].capacity.cpu;
            totalCapacity.mem += nodes[i].capacity.mem;
        }
    }

    SimulationResult run()
    {
        res.totalPods = static_cast<long long>(pods.size());
        if (pods.empty())
            return res;

        vector<int> order(pods.size());
        iota(order.begin(), order.end(), 0);
        stable_sort(order.begin(), order.end(), [&](int a, int b)
                    { return pods[a].arrivalTime < pods[b].arrivalTime; });

        const long long never = numeric_limits<long long>::max();
        long long start = pods[order[0]].arrivalTime;
        lastTime = start;
        windowEnd = pods[order.back()].arrivalTime;
        size_t next = 0;

        while (true)
        {
            long long now = never;
            if (next < order.size())
                now = min(now, pods[order[next]].arrivalTime);
            if (!completions.empty())
                now = min(now, completions.top().finishTime);
            if (config.maxWait > 0 && !pending.empty())
                now = min(now, nextDeadline());
            if (now == never)
                break;

            advanceTo(now);
            releaseFinished(now);
            while (next < order.size() && pods[order[next]].arrivalTime <= now)
                admit(order[next++], now);
            schedulePass(now);
            if (config.maxWait > 0)
                dropTimedOut(now);
        }

        // only reachable if a filter rejects every node forever, count such pods as unschedulable
        for (int pid : pending)
        {
            status[pid].state = PodState::Dropped;
            ++res.unschedulablePods;
        }
        pending.clear();

        finalize(start);
        return res;
    }

private:
    const vector<Pod> &pods;
    vector<Node> nodes;
    const SchedulingPolicy &policy;
    SimulationConfig config;
    vector<PodStatus> status;
    vector<vector<int>> runningOn;
    set<int, PendingOrder> pending;
    priority_queue<Completion, vector<Completion>, greater<Completion>> completions;
    multiset<int> runningPriorities;
    SimulationResult res;

    Resources totalCapacity;
    Resources clusterAllocated;
    int activeNodes = 0;
    long long lastTime = 0;
    long long windowEnd = 0; // last arrival time
    long double cpuArea = 0.0L, memArea = 0.0L, activeArea = 0.0L;
    long double cpuWindowArea = 0.0L, memWindowArea = 0.0L;

    // integrates cluster usage over the interval since the previous event
    void advanceTo(long long now)
    {
        long long dt = now - lastTime;
        if (dt > 0)
        {
            cpuArea += static_cast<long double>(dt) * clusterAllocated.cpu;
            memArea += static_cast<long double>(dt) * clusterAllocated.mem;
            activeArea += static_cast<long double>(dt) * activeNodes;
        }
        long long inWindow = min(now, windowEnd) - lastTime;
        if (inWindow > 0)
        {
            cpuWindowArea += static_cast<long double>(inWindow) * clusterAllocated.cpu;
            memWindowArea += static_cast<long double>(inWindow) * clusterAllocated.mem;
        }
        lastTime = now;
    }

    void place(int pid, int nid)
    {
        Node &node = nodes[nid];
        if (node.podCount == 0)
            ++activeNodes;
        addTo(node, pods[pid]);
        clusterAllocated.cpu += pods[pid].request.cpu;
        clusterAllocated.mem += pods[pid].request.mem;
        runningOn[nid].push_back(pid);
        runningPriorities.insert(pods[pid].priority);
        status[pid].nodeId = nid;
    }

    void unplace(int pid)
    {
        int nid = status[pid].nodeId;
        Node &node = nodes[nid];
        removeFrom(node, pods[pid]);
        if (node.podCount == 0)
            --activeNodes;
        clusterAllocated.cpu -= pods[pid].request.cpu;
        clusterAllocated.mem -= pods[pid].request.mem;
        auto &list = runningOn[nid];
        list.erase(find(list.begin(), list.end(), pid));
        runningPriorities.erase(runningPriorities.find(pods[pid].priority));
        status[pid].nodeId = -1;
    }

    void bind(int pid, int nid, long long now)
    {
        PodStatus &st = status[pid];
        place(pid, nid);
        st.state = PodState::Running;
        st.startTime = now;
        st.waited += now - st.enqueueTime;
        ++st.runId;
        completions.push(Completion{now + pods[pid].duration, pid, st.runId});
        res.events.push_back(ScheduleEvent{now, EventKind::Bind, pid, nid, -1});
    }

    void enqueue(int pid, long long now)
    {
        status[pid].state = PodState::Pending;
        status[pid].enqueueTime = now;
        pending.insert(pid);
    }

    bool fitsEmptyNode(const Pod &pod) const
    {
        for (const auto &n : nodes)
        {
            Node empty = n;
            empty.allocated = Resources{};
            empty.podCount = 0;
            if (policy.feasible(pod, empty))
                return true;
        }
        return false;
    }

    // a pod that cannot fit even on an empty node is rejected right away instead of waiting forever
    void admit(int pid, long long now)
    {
        if (!fitsEmptyNode(pods[pid]))
        {
            status[pid].state = PodState::Dropped;
            ++res.unschedulablePods;
            res.events.push_back(ScheduleEvent{now, EventKind::Unschedulable, pid, -1, -1});
            return;
        }
        enqueue(pid, now);
    }

    void releaseFinished(long long now)
    {
        while (!completions.empty() && completions.top().finishTime <= now)
        {
            Completion c = completions.top();
            completions.pop();
            PodStatus &st = status[c.podId];
            if (st.state != PodState::Running || st.runId != c.runId)
                continue; // this run was preempted
            unplace(c.podId);
            st.state = PodState::Done;
            st.finishTime = c.finishTime;
        }
    }

    void schedulePass(long long now)
    {
        for (auto it = pending.begin(); it != pending.end();)
        {
            int pid = *it;
            int nid = policy.selectNode(pods[pid], nodes);
            if (nid >= 0)
                bind(pid, nid, now);
            else if (!(config.preemption && tryPreempt(pid, now)))
            {
                status[pid].delayed = true;
                ++it;
                continue;
            }
            it = pending.erase(it);
        }
    }

    void evict(int victim, int preemptor, long long now)
    {
        int nid = status[victim].nodeId;
        res.lostWork += now - status[victim].startTime;
        ++res.preemptions;
        unplace(victim);
        res.events.push_back(ScheduleEvent{now, EventKind::Preempt, victim, nid, preemptor});
        // the victim restarts from scratch; it sorts after the preemptor so the current pass still reaches it
        enqueue(victim, now);
    }

    // picks the node where evicting the fewest, least important lower-priority pods lets the pod fit
    bool tryPreempt(int pid, long long now)
    {
        const Pod &pod = pods[pid];
        if (runningPriorities.empty() || *runningPriorities.begin() >= pod.priority)
            return false;

        int bestNode = -1;
        vector<int> bestVictims;
        tuple<int, long long, size_t> bestKey;

        for (size_t n = 0; n < nodes.size(); ++n)
        {
            vector<int> candidates;
            for (int r : runningOn[n])
                if (pods[r].priority < pod.priority)
                    candidates.push_back(r);
            if (candidates.empty())
                continue;

            Node trial = nodes[n];
            for (int c : candidates)
                removeFrom(trial, pods[c]);
            if (!policy.feasible(pod, trial))
                continue;

            // give the most important candidates back first and evict only those still in the way
            sort(candidates.begin(), candidates.end(), [&](int a, int b)
                 {
                if (pods[a].priority != pods[b].priority) return pods[a].priority > pods[b].priority;
                if (status[a].startTime != status[b].startTime) return status[a].startTime < status[b].startTime;
                return a < b; });
            vector<int> victims;
            for (int c : candidates)
            {
                addTo(trial, pods[c]);
                if (!policy.feasible(pod, trial))
                {
                    removeFrom(trial, pods[c]);
                    victims.push_back(c);
                }
            }

            int highest = numeric_limits<int>::min();
            long long sum = 0;
            for (int v : victims)
            {
                highest = max(highest, pods[v].priority);
                sum += pods[v].priority;
            }
            auto key = make_tuple(highest, sum, victims.size());
            if (bestNode == -1 || key < bestKey)
            {
                bestNode = static_cast<int>(n);
                bestVictims = victims;
                bestKey = key;
            }
        }

        if (bestNode == -1)
            return false;
        for (int v : bestVictims)
            evict(v, pid, now);
        bind(pid, bestNode, now);
        return true;
    }

    long long nextDeadline() const
    {
        long long best = numeric_limits<long long>::max();
        for (int pid : pending)
            best = min(best, status[pid].enqueueTime + config.maxWait);
        return best;
    }

    void dropTimedOut(long long now)
    {
        for (auto it = pending.begin(); it != pending.end();)
        {
            int pid = *it;
            if (now - status[pid].enqueueTime >= config.maxWait)
            {
                status[pid].state = PodState::Dropped;
                ++res.timedOutPods;
                res.events.push_back(ScheduleEvent{now, EventKind::Timeout, pid, -1, -1});
                it = pending.erase(it);
            }
            else
                ++it;
        }
    }

    void finalize(long long start)
    {
        res.makespan = lastTime - start;

        vector<long double> waits;
        map<int, pair<long double, long long>> byPriority;
        for (size_t i = 0; i < pods.size(); ++i)
        {
            const PodStatus &st = status[i];
            if (st.delayed)
                ++res.delayedPods;
            if (st.state != PodState::Done)
                continue;
            ++res.completedPods;
            waits.push_back(st.waited);
            res.avgTurnaround += static_cast<long double>(st.finishTime - pods[i].arrivalTime);
            auto &slot = byPriority[pods[i].priority];
            slot.first += st.waited;
            ++slot.second;
        }

        if (!waits.empty())
        {
            res.avgWaiting = accumulate(waits.begin(), waits.end(), 0.0L) / waits.size();
            res.avgTurnaround /= waits.size();
            sort(waits.begin(), waits.end());
            size_t idx = static_cast<size_t>(ceil(0.95L * waits.size())) - 1;
            res.p95Waiting = waits[idx];
        }
        for (const auto &kv : byPriority)
            res.avgWaitingByPriority[kv.first] = kv.second.first / kv.second.second;

        if (res.makespan > 0)
        {
            if (totalCapacity.cpu > 0)
                res.cpuUtilization = cpuArea / (static_cast<long double>(totalCapacity.cpu) * res.makespan);
            if (totalCapacity.mem > 0)
                res.memUtilization = memArea / (static_cast<long double>(totalCapacity.mem) * res.makespan);
            res.avgActiveNodes = activeArea / res.makespan;
        }

        long long window = windowEnd - start;
        if (window > 0 && totalCapacity.cpu > 0 && totalCapacity.mem > 0)
        {
            res.windowCpuUtilization = cpuWindowArea / (static_cast<long double>(totalCapacity.cpu) * window);
            res.windowMemUtilization = memWindowArea / (static_cast<long double>(totalCapacity.mem) * window);
        }
        else
        {
            // every pod arrived at once, there is no arrival window to measure
            res.windowCpuUtilization = res.cpuUtilization;
            res.windowMemUtilization = res.memUtilization;
        }
    }
};

} // namespace

SimulationResult runSimulation(const SimulationInput &input, const SchedulingPolicy &policy,
                               const SimulationConfig &config)
{
    Simulator sim(input, policy, config);
    return sim.run();
}
