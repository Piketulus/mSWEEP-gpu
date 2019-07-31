#ifndef BOOTSTRAP_H
#define BOOTSTRAP_H

#include <vector>
#include <string>
#include <unordered_map>

#include "thread_pool.hpp"
#include "Sample.hpp"
#include "parse_arguments.hpp"

void write_bootstrap(const std::vector<std::string> &cluster_indicators_to_string, const std::vector<std::vector<double>> &abundances, std::string &outfile, unsigned iters);
std::unordered_map<std::string, std::vector<std::vector<double>>> bootstrap_abundances(const std::vector<Sample> &bitfields, Reference &reference, ThreadPool &pool, const OptimizerArgs &optimizer, bool batch_mode, short unsigned iters);

#endif
