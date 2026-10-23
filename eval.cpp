#include "eval.h"

#include <algorithm>
#include <climits>

#include "policy.h"
#include "scheduler.h"
#include "workload.h"

using namespace std;

static void accumulateRow(EvalRow &row, const SimulationInput &input, const SimulationResult &r)
{
    int top = INT_MIN;
    for (const auto &p : input.pods)
        top = max(top, p.priority);
    auto crit = r.avgWaitingByPriority.find(top);

    double total = r.totalPods > 0 ? static_cast<double>(r.totalPods) : 1.0;
    row.cpuUtil += 100.0 * static_cast<double>(r.windowCpuUtilization);
    row.memUtil += 100.0 * static_cast<double>(r.windowMemUtilization);
    row.makespanCpuUtil += 100.0 * static_cast<double>(r.cpuUtilization);
    row.makespanMemUtil += 100.0 * static_cast<double>(r.memUtilization);
    row.activeNodes += static_cast<double>(r.avgActiveNodes);
    row.avgWait += static_cast<double>(r.avgWaiting);
    row.p95Wait += static_cast<double>(r.p95Waiting);
    row.critWait += crit == r.avgWaitingByPriority.end() ? 0.0 : static_cast<double>(crit->second);
    row.unschedRate += 100.0 * static_cast<double>(r.unschedulableRate());
    row.delayedRate += 100.0 * r.delayedPods / total;
    row.preemptions += static_cast<double>(r.preemptions);
    row.lostWork += static_cast<double>(r.lostWork);
    row.makespan += static_cast<double>(r.makespan);
}

static void divideRow(EvalRow &row, int n)
{
    double d = n > 0 ? n : 1;
    for (double *f : {&row.cpuUtil, &row.memUtil, &row.makespanCpuUtil, &row.makespanMemUtil, &row.activeNodes, &row.avgWait, &row.p95Wait,
                      &row.critWait, &row.unschedRate, &row.delayedRate, &row.preemptions,
                      &row.lostWork, &row.makespan})
        *f /= d;
}

vector<EvalRow> runEvaluation(const EvalOptions &options)
{
    vector<Node> cluster = makeHeterogeneousCluster();
    vector<EvalRow> rows;

    for (const auto &pattern : workloadPatterns())
    {
        // every policy sees exactly the same workloads
        vector<SimulationInput> inputs;
        for (int s = 0; s < options.seeds; ++s)
        {
            WorkloadSpec spec{pattern, options.pods, options.baseSeed + static_cast<uint64_t>(s)};
            inputs.push_back(SimulationInput{cluster, generateWorkload(spec, cluster)});
        }

        for (const auto &name : policyNames())
        {
            SchedulingPolicy policy;
            makePolicy(name, policy);
            for (bool preempt : {false, true})
            {
                EvalRow row;
                row.pattern = pattern;
                row.policy = preempt ? name + "+preempt" : name;
                SimulationConfig config;
                config.preemption = preempt;
                config.maxWait = options.maxWait;
                for (const auto &in : inputs)
                    accumulateRow(row, in, runSimulation(in, policy, config));
                divideRow(row, static_cast<int>(inputs.size()));
                rows.push_back(row);
            }
        }
    }
    return rows;
}
