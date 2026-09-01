#include <iostream>

#include "parser.h"
#include "report.h"
#include "scheduler.h"

int main()
{
    std::ios::sync_with_stdio(false);
    std::cin.tie(nullptr);

    SimulationInput input = parseInput(std::cin);
    SimulationResult result = runSimulation(input);
    printResult(std::cout, result);
    return 0;
}
