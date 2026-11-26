#pragma once

#include "roadef2018/lib/instance.hpp"
#include "roadef2018/lib/solution.hpp"

namespace roadef2018
{

struct MBAStarData
{
    const Instance& ins;
    double growth_factor;
    int criterion_id;
    Solution& sol_best;
    Info info = Info();
    int seed = 0;
    double eps = 0.0;
    double waste_slack = 0.0;
};

void sol_mbastar(MBAStarData d);

}

