#include "parser.h"

#include <algorithm>
#include <cctype>
#include <climits>
#include <sstream>
#include <stdexcept>
#include <vector>

using namespace std;

// this function checks if the given string is a valid integer token
static bool isIntegerToken(const string &s)
{
    if (s.empty())
        return false;
    size_t i = (s[0] == '+' || s[0] == '-') ? 1 : 0;
    if (i == s.size())
        return false;
    for (; i < s.size(); ++i)
        if (!isdigit(static_cast<unsigned char>(s[i])))
            return false;
    return true;
}

static bool toNumber(const string &s, long long &out)
{
    if (!isIntegerToken(s))
        return false;
    try
    {
        out = stoll(s);
    }
    catch (const out_of_range &)
    {
        return false;
    }
    return true;
}

// input format, one record per line, '#' starts a comment:
//   node <name> <cpu_millicores> <mem_mib> [max_pods]
//   pod  <name> <cpu_millicores> <mem_mib> <priority> <arrival> <duration>
bool parseInput(istream &in, SimulationInput &out, string &err)
{
    SimulationInput data;
    string line;
    int lineNo = 0;

    while (getline(in, line))
    {
        ++lineNo;
        size_t hash = line.find('#');
        if (hash != string::npos)
            line.erase(hash);

        stringstream ss(line);
        vector<string> tok;
        for (string t; ss >> t;)
            tok.push_back(t);
        if (tok.empty())
            continue;

        auto fail = [&](const string &why)
        {
            err = "line " + to_string(lineNo) + ": " + why;
            return false;
        };

        // every field after the name is an integer
        vector<long long> v;
        for (size_t i = 2; i < tok.size(); ++i)
        {
            long long x = 0;
            if (!toNumber(tok[i], x))
                return fail("expected an integer, got '" + tok[i] + "'");
            v.push_back(x);
        }

        if (tok[0] == "node")
        {
            if (v.size() != 2 && v.size() != 3)
                return fail("usage: node <name> <cpu_millicores> <mem_mib> [max_pods]");
            if (v[0] <= 0 || v[1] <= 0)
                return fail("node capacity must be positive");
            Node n;
            n.name = tok[1];
            n.capacity = {v[0], v[1]};
            if (v.size() == 3)
            {
                if (v[2] <= 0 || v[2] > INT_MAX)
                    return fail("max_pods must be a positive int");
                n.maxPods = static_cast<int>(v[2]);
            }
            data.nodes.push_back(n);
        }
        else if (tok[0] == "pod")
        {
            if (v.size() != 5)
                return fail("usage: pod <name> <cpu_millicores> <mem_mib> <priority> <arrival> <duration>");
            if (v[0] < 0 || v[1] < 0)
                return fail("pod requests cannot be negative");
            if (v[2] < INT_MIN || v[2] > INT_MAX)
                return fail("priority out of range");
            if (v[3] < 0)
                return fail("arrival time cannot be negative");
            if (v[4] < 1)
                return fail("duration must be at least 1");
            Pod p;
            p.name = tok[1];
            p.request = {v[0], v[1]};
            p.priority = static_cast<int>(v[2]);
            p.arrivalTime = v[3];
            p.duration = v[4];
            data.pods.push_back(p);
        }
        else
            return fail("unknown record '" + tok[0] + "'");
    }

    if (data.nodes.empty())
    {
        err = "no nodes defined";
        return false;
    }

    stable_sort(data.pods.begin(), data.pods.end(), [](const Pod &a, const Pod &b)
                { return a.arrivalTime < b.arrivalTime; });
    for (size_t i = 0; i < data.nodes.size(); ++i)
        data.nodes[i].id = static_cast<int>(i);
    for (size_t i = 0; i < data.pods.size(); ++i)
        data.pods[i].id = static_cast<int>(i);

    out = std::move(data);
    return true;
}

void writeInput(ostream &out, const SimulationInput &input)
{
    out << "# node <name> <cpu_millicores> <mem_mib> [max_pods]\n";
    for (const auto &n : input.nodes)
        out << "node " << n.name << ' ' << n.capacity.cpu << ' ' << n.capacity.mem << ' ' << n.maxPods << '\n';
    out << "# pod <name> <cpu_millicores> <mem_mib> <priority> <arrival> <duration>\n";
    for (const auto &p : input.pods)
        out << "pod " << p.name << ' ' << p.request.cpu << ' ' << p.request.mem << ' ' << p.priority << ' '
            << p.arrivalTime << ' ' << p.duration << '\n';
}
