#include "parser.h"

#include <algorithm>
#include <cctype>
#include <limits>
#include <sstream>
#include <string>
#include <unordered_map>
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

// this function parses the service index from the given token, it also maintains a mapping of service names to their corresponding indices
static int parseServiceIndex(const string &token, int serviceCount,
                             unordered_map<string, int> &serviceMap,
                             int &nextServiceId)
{
    if (isIntegerToken(token))
    {
        long long v = stoll(token);
        if (v >= 1 && v <= serviceCount)
            return static_cast<int>(v - 1);
        if (v >= 0 && v < serviceCount)
            return static_cast<int>(v);
        return -1;
    }

    auto it = serviceMap.find(token);
    if (it != serviceMap.end())
        return it->second;
    if (nextServiceId >= serviceCount)
        return -1;

    serviceMap[token] = nextServiceId;
    return nextServiceId++;
}

SimulationInput parseInput(istream &in)
{
    SimulationInput data;
    if (!(in >> data.n >> data.m) || data.n <= 0 || data.m <= 0)
        return data;
    data.services.assign(data.n, vector<WorkerThread>(data.m));
    data.maxThreadResources.assign(data.n, 0);

    // this loop reads the worker thread information for each service and updates the maximum thread resources for each service
    for (int s = 0; s < data.n; ++s)
    {
        for (int t = 0; t < data.m; ++t)
        {
            int p = 0, r = 0;
            if (!(in >> p >> r))
                return SimulationInput{};
            data.services[s][t] = WorkerThread{t, p, r, r};
            data.maxThreadResources[s] = max(data.maxThreadResources[s], r);
        }
    }

    string line;
    getline(in, line);
    vector<vector<string>> rows;

    while (getline(in, line))
    {
        if (line.empty())
            continue;
        stringstream ss(line);
        vector<string> tok;
        for (string t; ss >> t;)
            tok.push_back(t);

        // tok is a vector of tokens that contains the service index and the resources required for a request, if tok is not empty, it is added to the rows vector
        if (!tok.empty())
            rows.push_back(tok);
    }

    // Accept an optional explicit request count line before request rows.
    if (!rows.empty() && rows[0].size() == 1 && isIntegerToken(rows[0][0]))
    {
        long long q = stoll(rows[0][0]);
        if (q >= 0 && static_cast<size_t>(q) == rows.size() - 1)
            rows.erase(rows.begin());
    }

    unordered_map<string, int> serviceMap;
    int nextServiceId = 0;
    data.requests.reserve(rows.size());

    // serviceMap is used to store the mapping of service names to their corresponding indices, nextServiceId is used to assign the next available index to a new service name, this loop parses the service index and resources required for each request and adds it to the requests vector in the SimulationInput structure

    for (size_t i = 0; i < rows.size(); ++i)
    {
        if (rows[i].size() < 2)
            continue;
        int sid = parseServiceIndex(rows[i][0], data.n, serviceMap, nextServiceId);
        int need = numeric_limits<int>::max();
        if (isIntegerToken(rows[i][1]))
        {
            long long rr = stoll(rows[i][1]);
            if (rr >= 0 && rr <= numeric_limits<int>::max())
                need = static_cast<int>(rr);
        }
        data.requests.push_back(Request{static_cast<int>(data.requests.size()), sid, need,
                                        static_cast<long long>(i), max(1, need), false});
    }
    return data;
}
