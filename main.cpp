#include <cerrno>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <string>

#include "eval.h"
#include "parser.h"
#include "policy.h"
#include "report.h"
#include "scheduler.h"
#include "workload.h"

using namespace std;

static void usage(ostream &out)
{
    out << "usage:\n"
           "  kubesched run  [--policy binpack|spread|balanced] [--preempt] [--timeout T] [--quiet] < input.txt\n"
           "  kubesched gen  [--pattern steady|bursty|mixed|overload] [--pods N] [--seed S] > workload.txt\n"
           "  kubesched eval [--pods N] [--seeds K] [--seed S] [--timeout T] [--csv FILE]\n";
}

static bool toInt(const string &s, long long &out)
{
    if (s.empty())
        return false;
    char *end = nullptr;
    errno = 0;
    out = strtoll(s.c_str(), &end, 10);
    return errno == 0 && *end == '\0';
}

// options are either "--flag" or "--name value"
static bool parseArgs(int argc, char **argv, int first, const set<string> &valueOpts,
                      const set<string> &flagOpts, map<string, string> &out)
{
    for (int i = first; i < argc; ++i)
    {
        string arg = argv[i];
        if (arg.rfind("--", 0) != 0)
        {
            cerr << "unexpected argument: " << arg << '\n';
            return false;
        }
        string name = arg.substr(2);
        if (flagOpts.count(name))
            out[name] = "1";
        else if (valueOpts.count(name))
        {
            if (i + 1 >= argc)
            {
                cerr << "missing value for --" << name << '\n';
                return false;
            }
            out[name] = argv[++i];
        }
        else
        {
            cerr << "unknown option --" << name << '\n';
            return false;
        }
    }
    return true;
}

// reads an optional integer option, failing if it is present but malformed or below min
static bool intOption(const map<string, string> &opt, const string &name, long long min, long long &value)
{
    auto it = opt.find(name);
    if (it == opt.end())
        return true;
    if (!toInt(it->second, value) || value < min)
    {
        cerr << "--" << name << " expects an integer >= " << min << ", got '" << it->second << "'\n";
        return false;
    }
    return true;
}

static int runCommand(const map<string, string> &opt)
{
    string name = opt.count("policy") ? opt.at("policy") : "balanced";
    SchedulingPolicy policy;
    if (!makePolicy(name, policy))
    {
        cerr << "unknown policy: " << name << '\n';
        return 1;
    }

    SimulationConfig config;
    config.preemption = opt.count("preempt") > 0;
    if (!intOption(opt, "timeout", 0, config.maxWait))
        return 1;

    SimulationInput input;
    string err;
    if (!parseInput(cin, input, err))
    {
        cerr << "input error: " << err << '\n';
        return 1;
    }

    SimulationResult result = runSimulation(input, policy, config);
    if (!opt.count("quiet"))
        printTrace(cout, input, result);
    printMetrics(cout, input, policy.name, config, result);
    return 0;
}

static int genCommand(const map<string, string> &opt)
{
    WorkloadSpec spec;
    if (opt.count("pattern"))
        spec.pattern = opt.at("pattern");
    if (!isWorkloadPattern(spec.pattern))
    {
        cerr << "unknown pattern: " << spec.pattern << '\n';
        return 1;
    }
    long long pods = spec.pods, seed = 1;
    if (!intOption(opt, "pods", 1, pods) || !intOption(opt, "seed", 0, seed))
        return 1;
    spec.pods = static_cast<int>(pods);
    spec.seed = static_cast<uint64_t>(seed);

    SimulationInput input;
    input.nodes = makeHeterogeneousCluster();
    input.pods = generateWorkload(spec, input.nodes);
    cout << "# pattern " << spec.pattern << ", " << spec.pods << " pods, seed " << spec.seed << '\n';
    writeInput(cout, input);
    return 0;
}

static int evalCommand(const map<string, string> &opt)
{
    EvalOptions options;
    long long pods = options.pods, seeds = options.seeds, seed = 1;
    if (!intOption(opt, "pods", 1, pods) || !intOption(opt, "seeds", 1, seeds) ||
        !intOption(opt, "seed", 0, seed) || !intOption(opt, "timeout", 0, options.maxWait))
        return 1;
    options.pods = static_cast<int>(pods);
    options.seeds = static_cast<int>(seeds);
    options.baseSeed = static_cast<uint64_t>(seed);

    vector<EvalRow> rows = runEvaluation(options);
    cout << "Cluster: 12 heterogeneous nodes, " << options.pods << " pods per run, " << options.seeds
         << " seeds from " << options.baseSeed << ", timeout ";
    if (options.maxWait > 0)
        cout << options.maxWait << '\n';
    else
        cout << "none\n";
    printEvalTable(cout, rows);

    if (opt.count("csv"))
    {
        ofstream file(opt.at("csv"));
        if (!file)
        {
            cerr << "cannot write " << opt.at("csv") << '\n';
            return 1;
        }
        writeEvalCsv(file, rows);
    }
    return 0;
}

int main(int argc, char **argv)
{
    ios::sync_with_stdio(false);
    cin.tie(nullptr);

    // with no command, or when the first argument is an option, behave like "run"
    string cmd = "run";
    int first = 1;
    if (argc > 1 && string(argv[1]).rfind("--", 0) != 0)
    {
        cmd = argv[1];
        first = 2;
    }
    if (cmd == "help" || (argc > 1 && string(argv[1]) == "--help"))
    {
        usage(cout);
        return 0;
    }

    map<string, string> opt;
    if (cmd == "run")
    {
        if (!parseArgs(argc, argv, first, {"policy", "timeout"}, {"preempt", "quiet"}, opt))
            return 1;
        return runCommand(opt);
    }
    if (cmd == "gen")
    {
        if (!parseArgs(argc, argv, first, {"pattern", "pods", "seed"}, {}, opt))
            return 1;
        return genCommand(opt);
    }
    if (cmd == "eval")
    {
        if (!parseArgs(argc, argv, first, {"pods", "seeds", "seed", "timeout", "csv"}, {}, opt))
            return 1;
        return evalCommand(opt);
    }

    usage(cerr);
    return 1;
}
