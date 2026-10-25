#include <cmath>
#include <iostream>
#include <sstream>
#include <string>

#include "parser.h"
#include "policy.h"
#include "scheduler.h"
#include "workload.h"

using namespace std;

static int failures = 0;

#define CHECK(cond)                                                                \
    do                                                                             \
    {                                                                              \
        if (!(cond))                                                               \
        {                                                                          \
            ++failures;                                                            \
            cerr << __FILE__ << ':' << __LINE__ << ": CHECK failed: " #cond "\n"; \
        }                                                                          \
    } while (0)

static bool near(long double a, long double b) { return fabsl(a - b) < 1e-6L; }

static SimulationInput parse(const string &text)
{
    SimulationInput in;
    string err;
    istringstream ss(text);
    if (!parseInput(ss, in, err))
    {
        cerr << "parse error: " << err << '\n';
        ++failures;
    }
    return in;
}

static SimulationResult simulate(const string &text, const string &policyName, bool preempt = false,
                                 long long maxWait = 0)
{
    SchedulingPolicy policy;
    makePolicy(policyName, policy);
    SimulationConfig config;
    config.preemption = preempt;
    config.maxWait = maxWait;
    return runSimulation(parse(text), policy, config);
}

// node and time of a pod's last bind, -1 if it never ran
static int boundNode(const SimulationResult &r, int pod)
{
    int node = -1;
    for (const auto &e : r.events)
        if (e.kind == EventKind::Bind && e.podId == pod)
            node = e.nodeId;
    return node;
}

static long long bindTime(const SimulationResult &r, int pod)
{
    long long t = -1;
    for (const auto &e : r.events)
        if (e.kind == EventKind::Bind && e.podId == pod)
            t = e.time;
    return t;
}

static void testBinpackVersusSpread()
{
    const string in = "node a 4000 8192\n"
                      "node b 4000 8192\n"
                      "pod p0 1000 1024 0 0 10\n"
                      "pod p1 1000 1024 0 0 10\n";
    auto packed = simulate(in, "binpack");
    CHECK(boundNode(packed, 0) == 0);
    CHECK(boundNode(packed, 1) == 0);
    CHECK(near(packed.avgActiveNodes, 1.0L));

    auto spread = simulate(in, "spread");
    CHECK(boundNode(spread, 0) == 0);
    CHECK(boundNode(spread, 1) == 1);
    CHECK(near(spread.avgActiveNodes, 2.0L));
}

static void testBalancedPrefersMatchingShape()
{
    // a memory-heavy pod should land on the memory-heavy node
    const string in = "node cpu 8000 4096\n"
                      "node mem 2000 16384\n"
                      "pod p0 500 4096 0 0 10\n";
    CHECK(boundNode(simulate(in, "balanced"), 0) == 1);
}

static void testFiltersCheckBothResources()
{
    const string in = "node lots-of-cpu 8000 1024\n"
                      "node lots-of-mem 1000 16384\n"
                      "pod p0 500 4096 0 0 5\n";
    for (const auto &name : policyNames())
        CHECK(boundNode(simulate(in, name), 0) == 1);
}

static void testPodCountFilter()
{
    const string in = "node a 4000 8192 1\n"
                      "node b 4000 8192\n"
                      "pod p0 100 100 0 0 10\n"
                      "pod p1 100 100 0 0 10\n";
    auto r = simulate(in, "binpack");
    CHECK(boundNode(r, 0) != boundNode(r, 1));
}

static void testOversizedPodIsUnschedulable()
{
    auto r = simulate("node a 4000 8192\npod big 9000 1024 0 0 10\n", "balanced");
    CHECK(r.unschedulablePods == 1);
    CHECK(r.completedPods == 0);
    CHECK(near(r.unschedulableRate(), 1.0L));
}

static void testPreemption()
{
    const string in = "node a 2000 4096\n"
                      "pod low 2000 1024 0 0 100\n"
                      "pod high 2000 1024 1000 5 10\n";

    auto waits = simulate(in, "balanced", false);
    CHECK(bindTime(waits, 1) == 100);
    CHECK(waits.preemptions == 0);

    auto r = simulate(in, "balanced", true);
    CHECK(bindTime(r, 1) == 5);
    CHECK(r.preemptions == 1);
    CHECK(r.lostWork == 5);
    CHECK(bindTime(r, 0) == 15); // the victim restarts once the preemptor finishes
    CHECK(r.completedPods == 2);
    CHECK(near(r.avgWaitingByPriority[1000], 0.0L));
    CHECK(near(r.avgWaitingByPriority[0], 10.0L));
}

static void testNoPreemptionOfEqualPriority()
{
    const string in = "node a 2000 4096\n"
                      "pod a 2000 1024 100 0 100\n"
                      "pod b 2000 1024 100 5 10\n";
    auto r = simulate(in, "balanced", true);
    CHECK(r.preemptions == 0);
    CHECK(bindTime(r, 1) == 100);
}

static void testPreemptionEvictsMinimalVictims()
{
    // h needs a third of the node, so only one of the priority-0 pods should go
    const string in = "node a 3000 4096\n"
                      "pod l1 1000 512 0 0 100\n"
                      "pod l2 1000 512 10 0 100\n"
                      "pod l3 1000 512 0 0 100\n"
                      "pod h 1000 512 1000 1 10\n";
    auto r = simulate(in, "balanced", true);
    CHECK(r.preemptions == 1);
    int victim = -1;
    for (const auto &e : r.events)
        if (e.kind == EventKind::Preempt)
            victim = e.podId;
    CHECK(victim == 2);
}

static void testPreemptionPicksLeastImportantNode()
{
    const string in = "node a 1000 1024\n"
                      "node b 1000 1024\n"
                      "pod m 1000 512 100 0 50\n"
                      "pod l 1000 512 0 0 50\n"
                      "pod h 1000 512 1000 1 5\n";
    auto r = simulate(in, "balanced", true);
    CHECK(r.preemptions == 1);
    CHECK(boundNode(r, 2) == boundNode(r, 1));
    CHECK(bindTime(r, 0) == 0);
}

static void testTimeout()
{
    const string in = "node a 1000 1024\n"
                      "pod p0 1000 512 0 0 100\n"
                      "pod p1 1000 512 0 1 10\n";
    auto r = simulate(in, "balanced", false, 20);
    CHECK(r.timedOutPods == 1);
    CHECK(r.completedPods == 1);
    CHECK(r.events.back().kind == EventKind::Timeout);
    CHECK(r.events.back().time == 21);
}

static void testUtilization()
{
    auto r = simulate("node a 2000 2048\npod p0 1000 512 0 0 10\n", "balanced");
    CHECK(r.makespan == 10);
    CHECK(near(r.cpuUtilization, 0.5L));
    CHECK(near(r.memUtilization, 0.25L));
    CHECK(near(r.avgActiveNodes, 1.0L));

    // the window ends at the last arrival (t=10), the drain afterwards does not count
    auto w = simulate("node a 2000 2048\npod p0 1000 512 0 0 20\npod p1 1000 512 0 10 5\n", "balanced");
    CHECK(near(w.windowCpuUtilization, 0.5L));
    CHECK(near(w.cpuUtilization, 0.625L)); // (20 * 1000 + 5 * 1000) / (2000 * 20)
}

static void testParserErrors()
{
    SimulationInput in;
    string err;
    istringstream unknown("node a 1 1\nfoo x 1\n");
    CHECK(!parseInput(unknown, in, err));
    istringstream noNodes("pod p 1 1 0 0 1\n");
    CHECK(!parseInput(noNodes, in, err));
    istringstream badInt("node a 1x 1\n");
    CHECK(!parseInput(badInt, in, err));
    istringstream comments("# cluster\nnode a 100 100 # inline\n\n");
    CHECK(parseInput(comments, in, err));
    CHECK(in.nodes.size() == 1);
}

static void testWorkloadIsDeterministic()
{
    auto nodes = makeHeterogeneousCluster();
    for (const auto &pattern : workloadPatterns())
    {
        auto a = generateWorkload({pattern, 200, 7}, nodes);
        auto b = generateWorkload({pattern, 200, 7}, nodes);
        auto c = generateWorkload({pattern, 200, 8}, nodes);
        CHECK(a.size() == 200);
        bool same = true, differs = false, sorted = true;
        for (size_t i = 0; i < a.size(); ++i)
        {
            same = same && a[i].request.cpu == b[i].request.cpu && a[i].arrivalTime == b[i].arrivalTime &&
                   a[i].duration == b[i].duration && a[i].priority == b[i].priority;
            differs = differs || a[i].request.cpu != c[i].request.cpu || a[i].duration != c[i].duration;
            sorted = sorted && a[i].id == static_cast<int>(i) && (i == 0 || a[i - 1].arrivalTime <= a[i].arrivalTime);
        }
        CHECK(same);
        CHECK(differs);
        CHECK(sorted);
    }
}

// every pod ends up completed, unschedulable or timed out, under every policy and pattern
static void testConservation()
{
    SimulationInput in;
    in.nodes = makeHeterogeneousCluster();
    for (const auto &pattern : workloadPatterns())
    {
        in.pods = generateWorkload({pattern, 300, 3}, in.nodes);
        for (const auto &name : policyNames())
        {
            SchedulingPolicy policy;
            makePolicy(name, policy);
            for (bool preempt : {false, true})
            {
                SimulationConfig config;
                config.preemption = preempt;
                config.maxWait = 300;
                auto r = runSimulation(in, policy, config);
                CHECK(r.completedPods + r.unschedulablePods + r.timedOutPods == r.totalPods);
                CHECK(r.cpuUtilization <= 1.0L + 1e-9L);
                CHECK(r.memUtilization <= 1.0L + 1e-9L);
                CHECK(r.avgActiveNodes <= in.nodes.size() + 1e-9L);
                if (!preempt)
                    CHECK(r.preemptions == 0);
            }
        }
    }
}

int main()
{
    testBinpackVersusSpread();
    testBalancedPrefersMatchingShape();
    testFiltersCheckBothResources();
    testPodCountFilter();
    testOversizedPodIsUnschedulable();
    testPreemption();
    testNoPreemptionOfEqualPriority();
    testPreemptionEvictsMinimalVictims();
    testPreemptionPicksLeastImportantNode();
    testTimeout();
    testUtilization();
    testParserErrors();
    testWorkloadIsDeterministic();
    testConservation();

    if (failures)
    {
        cerr << failures << " check(s) failed\n";
        return 1;
    }
    cout << "all tests passed\n";
    return 0;
}
