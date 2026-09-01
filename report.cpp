#include "report.h"

#include <algorithm>
#include <iomanip>

using namespace std;

void printResult(ostream &out, const SimulationResult &result)
{
    out << "Processed Request Order:\n";
    if (result.processedOrder.empty())
    {
        out << "None\n";
    }
    else
    {
        auto order = result.processedOrder;
        sort(order.begin(), order.end(), [](const AssignmentRecord &a, const AssignmentRecord &b)
             {
            if (a.startTime != b.startTime) return a.startTime < b.startTime;
            return a.requestId < b.requestId; });
        for (const auto &e : order)
        {
            out << "Request " << e.requestId << " -> Service " << e.serviceId
                << ", Thread " << e.threadId << ", Start " << e.startTime << '\n';
        }
    }

    out << fixed << setprecision(2);
    out << "Average Waiting Time: " << static_cast<double>(result.avgWaiting) << '\n';
    out << "Average Turnaround Time: " << static_cast<double>(result.avgTurnaround) << '\n';
    out << "Rejected Requests (insufficient resources): " << result.rejectedCount << '\n';
    out << "Delayed Requests: " << result.delayedCount << '\n';
    out << "Blocked Requests (no available worker threads): " << result.blockedCount << '\n';
}
