#include "workload.h"

#include <algorithm>
#include <cmath>
#include <random>

using namespace std;

namespace
{

// mt19937_64 output is fixed by the standard, the std distributions are not,
// so the distributions are written by hand to keep a seed reproducible across compilers
class Rng
{
public:
    explicit Rng(uint64_t seed) : gen(seed) {}

    double uniform() { return static_cast<double>(gen() >> 11) * (1.0 / 9007199254740992.0); }

    long long range(long long lo, long long hi)
    {
        return lo + static_cast<long long>(uniform() * static_cast<double>(hi - lo + 1));
    }

    double exponential(double mean) { return -mean * log(1.0 - uniform()); }

    double lognormal(double mu, double sigma)
    {
        const double pi = 3.14159265358979323846;
        double u1 = 1.0 - uniform(), u2 = uniform();
        double z = sqrt(-2.0 * log(u1)) * cos(2.0 * pi * u2);
        return exp(mu + sigma * z);
    }

private:
    mt19937_64 gen;
};

enum class Shape
{
    Web,
    CpuHeavy,
    MemHeavy,
    Oversized
};

struct Mix
{
    double web, cpuHeavy, memHeavy, oversized;
};

struct PatternSpec
{
    Mix mix;
    double targetLoad; // offered load on the busier resource, 1.0 means the cluster is exactly full
    bool heavyTail;    // lognormal durations instead of exponential
    bool bursty;       // pods arrive in batches instead of one at a time
};

PatternSpec patternSpec(const string &pattern)
{
    if (pattern == "bursty")
        return {{0.70, 0.15, 0.15, 0.0}, 0.6, false, true};
    if (pattern == "mixed")
        return {{0.40, 0.30, 0.29, 0.01}, 0.8, true, false};
    if (pattern == "overload")
        return {{0.40, 0.30, 0.29, 0.01}, 1.3, true, false};
    return {{0.70, 0.15, 0.15, 0.0}, 0.6, false, false};
}

Shape pickShape(const Mix &m, Rng &rng)
{
    double u = rng.uniform();
    if (u < m.web)
        return Shape::Web;
    if (u < m.web + m.cpuHeavy)
        return Shape::CpuHeavy;
    if (u < m.web + m.cpuHeavy + m.memHeavy)
        return Shape::MemHeavy;
    return Shape::Oversized;
}

Resources sampleRequest(Shape s, Rng &rng)
{
    switch (s)
    {
    case Shape::Web:
        return {rng.range(1, 10) * 100, rng.range(1, 8) * 128};
    case Shape::CpuHeavy:
        return {rng.range(4, 16) * 500, rng.range(2, 8) * 512};
    case Shape::MemHeavy:
        return {rng.range(1, 4) * 250, rng.range(8, 32) * 512};
    case Shape::Oversized:
        // larger than the biggest node in makeHeterogeneousCluster
        return {rng.range(20, 32) * 1000, rng.range(64, 128) * 1024};
    }
    return {};
}

// 10% critical, 30% normal, 60% best-effort
int samplePriority(Rng &rng)
{
    double u = rng.uniform();
    if (u < 0.1)
        return 1000;
    if (u < 0.4)
        return 100;
    return 0;
}

} // namespace

vector<Node> makeHeterogeneousCluster()
{
    struct NodeType
    {
        const char *prefix;
        long long cpu;
        long long mem;
        int count;
    };
    const NodeType types[] = {
        {"small", 2000, 4096, 4},
        {"general", 4000, 16384, 4},
        {"highcpu", 16000, 16384, 2},
        {"highmem", 4000, 65536, 2},
    };

    vector<Node> nodes;
    for (const auto &t : types)
    {
        for (int i = 0; i < t.count; ++i)
        {
            Node n;
            n.id = static_cast<int>(nodes.size());
            n.name = string(t.prefix) + "-" + to_string(i + 1);
            n.capacity = {t.cpu, t.mem};
            nodes.push_back(n);
        }
    }
    return nodes;
}

vector<Pod> generateWorkload(const WorkloadSpec &spec, const vector<Node> &nodes)
{
    PatternSpec ps = patternSpec(spec.pattern);
    Rng rng(spec.seed);
    vector<Pod> pods(static_cast<size_t>(max(0, spec.pods)));
    if (pods.empty())
        return pods;

    Resources capacity;
    for (const auto &n : nodes)
    {
        capacity.cpu += n.capacity.cpu;
        capacity.mem += n.capacity.mem;
    }

    long double cpuWork = 0.0L, memWork = 0.0L;
    for (size_t i = 0; i < pods.size(); ++i)
    {
        Shape s = pickShape(ps.mix, rng);
        Pod &p = pods[i];
        p.id = static_cast<int>(i);
        p.name = "p" + to_string(i);
        p.request = sampleRequest(s, rng);
        p.priority = samplePriority(rng);
        double d = ps.heavyTail ? rng.lognormal(log(30.0), 1.0) : rng.exponential(60.0);
        p.duration = min(3600LL, max(1LL, llround(d)));
        if (s != Shape::Oversized)
        {
            cpuWork += static_cast<long double>(p.request.cpu) * p.duration;
            memWork += static_cast<long double>(p.request.mem) * p.duration;
        }
    }

    // choose the arrival rate that puts the busier resource at the target load
    long double perPod = 0.0L;
    if (capacity.cpu > 0 && capacity.mem > 0)
        perPod = max(cpuWork / capacity.cpu, memWork / capacity.mem) / pods.size();
    double rate = perPod > 0.0L ? static_cast<double>(ps.targetLoad / perPod) : 1.0;

    double t = 0.0;
    size_t i = 0;
    while (i < pods.size())
    {
        if (ps.bursty)
        {
            // bursts average 25 pods, so the gap between bursts keeps the same mean rate
            long long burst = rng.range(10, 40);
            t += rng.exponential(25.0 / rate);
            for (long long k = 0; k < burst && i < pods.size(); ++k)
                pods[i++].arrivalTime = llround(t);
        }
        else
        {
            t += rng.exponential(1.0 / rate);
            pods[i++].arrivalTime = llround(t);
        }
    }
    return pods;
}

vector<string> workloadPatterns()
{
    return {"steady", "bursty", "mixed", "overload"};
}

bool isWorkloadPattern(const string &name)
{
    auto all = workloadPatterns();
    return find(all.begin(), all.end(), name) != all.end();
}
