#include "policy.h"

#include <algorithm>
#include <cmath>

using namespace std;

static double fraction(long long used, long long capacity)
{
    if (capacity <= 0)
        return 1.0;
    return min(1.0, static_cast<double>(used) / static_cast<double>(capacity));
}

// node usage of each resource if the pod were placed on it
static double cpuAfter(const Pod &pod, const Node &node)
{
    return fraction(node.allocated.cpu + pod.request.cpu, node.capacity.cpu);
}

static double memAfter(const Pod &pod, const Node &node)
{
    return fraction(node.allocated.mem + pod.request.mem, node.capacity.mem);
}

class ResourceFitFilter : public FilterPlugin
{
public:
    string name() const override { return "ResourceFit"; }

    bool feasible(const Pod &pod, const Node &node) const override
    {
        return node.allocated.cpu + pod.request.cpu <= node.capacity.cpu &&
               node.allocated.mem + pod.request.mem <= node.capacity.mem;
    }
};

class PodCountFilter : public FilterPlugin
{
public:
    string name() const override { return "PodCount"; }

    bool feasible(const Pod &, const Node &node) const override
    {
        return node.podCount < node.maxPods;
    }
};

// prefers the node left with the most free capacity, which spreads pods out
class LeastAllocatedScore : public ScorePlugin
{
public:
    string name() const override { return "LeastAllocated"; }

    double score(const Pod &pod, const Node &node) const override
    {
        return 50.0 * ((1.0 - cpuAfter(pod, node)) + (1.0 - memAfter(pod, node)));
    }
};

// prefers the fullest node, which packs pods tightly and leaves other nodes empty
class MostAllocatedScore : public ScorePlugin
{
public:
    string name() const override { return "MostAllocated"; }

    double score(const Pod &pod, const Node &node) const override
    {
        return 50.0 * (cpuAfter(pod, node) + memAfter(pod, node));
    }
};

// prefers nodes whose cpu and memory usage stay even, so neither resource is stranded
class BalancedAllocationScore : public ScorePlugin
{
public:
    string name() const override { return "BalancedAllocation"; }

    double score(const Pod &pod, const Node &node) const override
    {
        return 100.0 * (1.0 - fabs(cpuAfter(pod, node) - memAfter(pod, node)));
    }
};

bool SchedulingPolicy::feasible(const Pod &pod, const Node &node) const
{
    for (const auto &f : filters)
        if (!f->feasible(pod, node))
            return false;
    return true;
}

double SchedulingPolicy::score(const Pod &pod, const Node &node) const
{
    double total = 0.0, weights = 0.0;
    for (const auto &s : scorers)
    {
        total += s.weight * s.plugin->score(pod, node);
        weights += s.weight;
    }
    return weights > 0.0 ? total / weights : 0.0;
}

int SchedulingPolicy::selectNode(const Pod &pod, const vector<Node> &nodes) const
{
    int best = -1;
    double bestScore = 0.0;
    for (size_t i = 0; i < nodes.size(); ++i)
    {
        if (!feasible(pod, nodes[i]))
            continue;
        double s = score(pod, nodes[i]);
        if (best == -1 || s > bestScore + 1e-9)
        {
            best = static_cast<int>(i);
            bestScore = s;
        }
    }
    return best;
}

bool makePolicy(const string &name, SchedulingPolicy &out)
{
    SchedulingPolicy p;
    p.name = name;
    p.filters = {make_shared<ResourceFitFilter>(), make_shared<PodCountFilter>()};

    if (name == "binpack")
        p.scorers = {{make_shared<MostAllocatedScore>(), 1.0}};
    else if (name == "spread")
        p.scorers = {{make_shared<LeastAllocatedScore>(), 1.0}};
    else if (name == "balanced")
        p.scorers = {{make_shared<LeastAllocatedScore>(), 1.0},
                     {make_shared<BalancedAllocationScore>(), 1.0}};
    else
        return false;

    out = p;
    return true;
}

vector<string> policyNames()
{
    return {"binpack", "spread", "balanced"};
}
