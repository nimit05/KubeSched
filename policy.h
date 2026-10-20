#pragma once

#include <memory>
#include <string>
#include <vector>

#include "types.h"

// filter plugins drop nodes that cannot host the pod
class FilterPlugin
{
public:
    virtual ~FilterPlugin() = default;
    virtual std::string name() const = 0;
    virtual bool feasible(const Pod &pod, const Node &node) const = 0;
};

// score plugins rank the nodes that passed every filter, in the range [0, 100], higher is better
class ScorePlugin
{
public:
    virtual ~ScorePlugin() = default;
    virtual std::string name() const = 0;
    virtual double score(const Pod &pod, const Node &node) const = 0;
};

struct WeightedScore
{
    std::shared_ptr<ScorePlugin> plugin;
    double weight = 1.0;
};

struct SchedulingPolicy
{
    std::string name;
    std::vector<std::shared_ptr<FilterPlugin>> filters;
    std::vector<WeightedScore> scorers;

    bool feasible(const Pod &pod, const Node &node) const;
    double score(const Pod &pod, const Node &node) const;
    // returns the index of the best feasible node, ties go to the lower index, -1 if none fits
    int selectNode(const Pod &pod, const std::vector<Node> &nodes) const;
};

// builds one of the policies listed by policyNames(), returns false for an unknown name
bool makePolicy(const std::string &name, SchedulingPolicy &out);
std::vector<std::string> policyNames();
