// SPDX-License-Identifier: GPL-2.0-or-later
#include "setup-request.h"
#include <cassert>
#include <vector>
struct Ops {
    uint32_t time = 0, wait_cost = 0, child_cost = 0;
    unsigned waits = 0, launches = 0;
    bool log = true, launch_ok = true;
    std::vector<uint32_t> results, busy_results, timeouts;
    uint32_t now() {
        return time;
    }
    bool wait(uint32_t limit) {
        ++waits;
        time += wait_cost;
        return wait_cost <= limit;
    }
    bool launch(uint32_t limit, uint32_t &code) {
        timeouts.push_back(limit);
        time += child_cost;
        code = results.at(launches++);
        return launch_ok && child_cost <= limit;
    }
    bool busy(unsigned attempt, uint32_t) {
        busy_results.push_back(attempt);
        return log;
    }
    void pause(uint32_t n) {
        time += n;
    }
};
int main() {
    uint32_t code;
    Ops race;
    race.results = {28, 28, 11};
    assert(setup::request_setup(race, code) && code == 11 && race.launches == 3 &&
           race.busy_results.size() == 2);
    for (uint32_t result : {0u, 11u, 12u, 13u, 26u, 29u}) {
        Ops x;
        x.results = {result};
        assert(setup::request_setup(x, code) && code == result && x.launches == 1);
    }
    Ops bound;
    bound.results.assign(16, 28);
    assert(setup::request_setup(bound, code) && code == 28 && bound.launches == 16 &&
           bound.busy_results.size() == 16);
    Ops log;
    log.results = {28};
    log.log = false;
    assert(!setup::request_setup(log, code) && log.launches == 1);
    Ops child;
    child.results = {11};
    child.launch_ok = false;
    assert(!setup::request_setup(child, code) && child.launches == 1);
    Ops deadline;
    deadline.results = {28, 28, 28, 28};
    deadline.wait_cost = 80000;
    assert(!setup::request_setup(deadline, code) && deadline.launches == 4);
    Ops wrap;
    wrap.time = UINT32_MAX - 100;
    wrap.results = {28, 0};
    assert(setup::request_setup(wrap, code) && code == 0 && wrap.launches == 2);
}
