#include "likelihood.hpp"

#include "rcgpar.hpp"

#include <vector>
#include <cmath>

inline double lbeta(double x, double y) {
  return(std::lgamma(x) + std::lgamma(y) - std::lgamma(x + y));
}

inline double log_bin_coeff(uint16_t n, uint16_t k) {
  return (std::lgamma(n + 1) - std::lgamma(k + 1) - std::lgamma(n - k + 1));
}

inline double ldbb_scaled(uint16_t k, uint16_t n, double alpha, double beta) {
  return (log_bin_coeff(n, k) + lbeta(k + alpha, n - k + beta) - lbeta(n + alpha, beta));
}

void precalc_lls(const Grouping &grouping, const double bb_constants[2], rcgpar::Matrix<double> &ll_mat) {
  const std::vector<std::array<double, 2>> &bb_params = grouping.bb_parameters(bb_constants);
  uint32_t n_groups = grouping.get_n_groups();

  uint16_t max_size = 0;
  for (uint32_t i = 0; i < n_groups; ++i) {
    max_size = (grouping.get_sizes()[i] > max_size ? grouping.get_sizes()[i] : max_size);
  }

  ll_mat.resize(n_groups, max_size + 1, -4.60517);
#pragma omp parallel for schedule(static)
  for (uint32_t i = 0; i < n_groups; ++i) {
    for (uint16_t j = 1; j <= max_size; ++j) {
      ll_mat(i, j) = ldbb_scaled(j, grouping.get_sizes()[i], bb_params[i][0], bb_params[i][1]) - 0.01005034; // log(0.99) = -0.01005034
    }
  }
}

void likelihood_array_mat(const Grouping &grouping, const std::vector<uint32_t> &group_indicators, const double bb_constants[2], Sample &sample) {
  uint32_t num_ecs = sample.num_ecs();
  uint16_t n_groups = grouping.get_n_groups();
  rcgpar::Matrix<double> precalc_lls_mat;
  precalc_lls(grouping, bb_constants, precalc_lls_mat);
  sample.ll_mat.resize(n_groups, num_ecs, -4.60517);

#pragma omp parallel for schedule(static)
  for (unsigned j = 0; j < num_ecs; ++j) {
      const std::vector<short unsigned> &counts = sample.group_counts(group_indicators, j, n_groups);
    for (unsigned short i = 0; i < n_groups; ++i) {
      sample.ll_mat(i, j) = precalc_lls_mat(i, counts[i]);
    }
  }
}
