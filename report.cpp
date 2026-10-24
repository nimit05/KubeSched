#include "report.h"

#include <iomanip>

using namespace std;

static const char *kindName(EventKind kind)
{
    switch (kind)
    {
    case EventKind::Bind:
        return "BIND";
    case EventKind::Preempt:
        return "PREEMPT";
    case EventKind::Unschedulable:
        return "UNSCHEDULABLE";
    case EventKind::Timeout:
        return "TIMEOUT";
    }
    return "?";
}

void printTrace(ostream &out, const SimulationInput &input, const SimulationResult &result)
{
    out << "Scheduling Events:\n";
    if (result.events.empty())
        out << "None\n";
    for (const auto &e : result.events)
    {
        out << "t=" << left << setw(8) << e.time << setw(14) << kindName(e.kind) << right
            << input.pods[e.podId].name;
        if (e.kind == EventKind::Bind)
            out << " -> " << input.nodes[e.nodeId].name;
        else if (e.kind == EventKind::Preempt)
            out << " evicted from " << input.nodes[e.nodeId].name << " by " << input.pods[e.byPodId].name;
        out << '\n';
    }
}

void printMetrics(ostream &out, const SimulationInput &input, const string &policyName,
                  const SimulationConfig &config, const SimulationResult &result)
{
    out << fixed << setprecision(2);
    out << "Policy: " << policyName << ", preemption " << (config.preemption ? "on" : "off") << ", timeout ";
    if (config.maxWait > 0)
        out << config.maxWait << '\n';
    else
        out << "none\n";
    out << "Pods: " << result.totalPods << " total, " << result.completedPods << " completed, "
        << result.unschedulablePods << " unschedulable, " << result.timedOutPods << " timed out\n";
    out << "Unschedulable Rate: " << 100.0 * static_cast<double>(result.unschedulableRate()) << "%\n";
    out << "Delayed Pods: " << result.delayedPods << '\n';
    out << "Average Waiting Time: " << static_cast<double>(result.avgWaiting) << '\n';
    out << "P95 Waiting Time: " << static_cast<double>(result.p95Waiting) << '\n';
    out << "Average Turnaround Time: " << static_cast<double>(result.avgTurnaround) << '\n';
    out << "Waiting Time by Priority:";
    if (result.avgWaitingByPriority.empty())
        out << " None";
    for (auto it = result.avgWaitingByPriority.rbegin(); it != result.avgWaitingByPriority.rend(); ++it)
        out << ' ' << it->first << '=' << static_cast<double>(it->second);
    out << '\n';
    out << "CPU Utilization: " << 100.0 * static_cast<double>(result.cpuUtilization) << "%\n";
    out << "Memory Utilization: " << 100.0 * static_cast<double>(result.memUtilization) << "%\n";
    out << "Utilization During Arrivals: cpu " << 100.0 * static_cast<double>(result.windowCpuUtilization)
        << "%, memory " << 100.0 * static_cast<double>(result.windowMemUtilization) << "%\n";
    out << "Average Active Nodes: " << static_cast<double>(result.avgActiveNodes) << " / "
        << input.nodes.size() << '\n';
    out << "Preemptions: " << result.preemptions << " (lost work " << result.lostWork << ")\n";
    out << "Makespan: " << result.makespan << '\n';
}

void printEvalTable(ostream &out, const vector<EvalRow> &rows)
{
    string current;
    out << fixed << setprecision(1);
    for (const auto &r : rows)
    {
        if (r.pattern != current)
        {
            current = r.pattern;
            out << "\nPattern: " << current << '\n';
            out << left << setw(18) << "policy" << right << setw(7) << "cpu%" << setw(7) << "mem%"
                << setw(7) << "nodes" << setw(9) << "wait" << setw(9) << "p95" << setw(11) << "crit-wait"
                << setw(10) << "unsched%" << setw(10) << "delayed%" << setw(9) << "preempt" << '\n';
        }
        out << left << setw(18) << r.policy << right << setw(7) << r.cpuUtil << setw(7) << r.memUtil
            << setw(7) << r.activeNodes << setw(9) << r.avgWait << setw(9) << r.p95Wait << setw(11)
            << r.critWait << setw(10) << r.unschedRate << setw(10) << r.delayedRate << setw(9)
            << r.preemptions << '\n';
    }
}

void writeEvalCsv(ostream &out, const vector<EvalRow> &rows)
{
    out << "pattern,policy,cpu_util_pct,mem_util_pct,makespan_cpu_util_pct,makespan_mem_util_pct,"
           "avg_active_nodes,avg_wait,p95_wait,crit_wait,unschedulable_pct,delayed_pct,preemptions,"
           "lost_work,makespan\n";
    out << fixed << setprecision(3);
    for (const auto &r : rows)
        out << r.pattern << ',' << r.policy << ',' << r.cpuUtil << ',' << r.memUtil << ','
            << r.makespanCpuUtil << ',' << r.makespanMemUtil << ',' << r.activeNodes
            << ',' << r.avgWait << ',' << r.p95Wait << ',' << r.critWait << ',' << r.unschedRate << ','
            << r.delayedRate << ',' << r.preemptions << ',' << r.lostWork << ',' << r.makespan << '\n';
}
