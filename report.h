#pragma once

#include <ostream>
#include <string>
#include <vector>

#include "eval.h"
#include "types.h"

void printTrace(std::ostream &out, const SimulationInput &input, const SimulationResult &result);
void printMetrics(std::ostream &out, const SimulationInput &input, const std::string &policyName,
                  const SimulationConfig &config, const SimulationResult &result);
void printEvalTable(std::ostream &out, const std::vector<EvalRow> &rows);
void writeEvalCsv(std::ostream &out, const std::vector<EvalRow> &rows);
