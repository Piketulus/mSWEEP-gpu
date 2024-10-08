// mSWEEP: Estimate abundances of reference lineages in DNA sequencing reads.
//
// MIT License
//
// Copyright (c) 2023 Probabilistic Inference and Computational Biology group @ UH
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.
//
#include "Sample.hpp"

#include <exception>

#include "mSWEEP_openmp_config.hpp"
#include "mSWEEP_version.h"

namespace mSWEEP {
void BootstrapSample::init_bootstrap(const telescope::Alignment &alignment) {
  // Clear the bootstrap abundances in case we're estimating the same sample again.
  this->bootstrap_results = std::vector<std::vector<double>>();

  // Initialize ec_distribution for bootstrapping
  std::vector<uint32_t> weights(this->num_ecs);
#pragma omp parallel for schedule(static)
  for (size_t i = 0; i < this->num_ecs; ++i) {
    weights[i] = alignment.reads_in_ec(i);
  }
  ec_distribution = std::discrete_distribution<uint32_t>(weights.begin(), weights.end());
}

void BootstrapSample::construct(const telescope::Alignment &alignment, const size_t _iters, const int32_t seed, const size_t _bootstrap_count) {
  this->count_alignments(alignment);
  if (seed == 26012023) {
    std::random_device rd;
    this->gen = std::mt19937_64(rd());
  } else {
    this->gen = std::mt19937_64(seed);
  }
  this->num_ecs = alignment.n_ecs();
  this->iters = _iters;
  this->bootstrap_count = (_bootstrap_count == 0 ? this->get_counts_total() : _bootstrap_count);
  this->init_bootstrap(alignment);
}

std::vector<double> BootstrapSample::resample_counts() {
  std::vector<uint32_t> tmp_counts(this->num_ecs);

  for (size_t i = 0; i < this->bootstrap_count; ++i) {
    size_t ec_id = ec_distribution(this->gen);
    tmp_counts[ec_id] += 1;
  }
  std::vector<double> resampled_log_ec_counts(this->num_ecs);
#pragma omp parallel for schedule(static)
  for (size_t i = 0; i < this->num_ecs; ++i) {
    resampled_log_ec_counts[i] = std::log(tmp_counts[i]);
  }
  return resampled_log_ec_counts;
}

void BootstrapSample::write_abundances(const std::vector<std::string> &group_names, std::ostream *of) const {
  // Write relative abundances to a file,
  // outputs to std::cout if outfile is empty.
  if (of->good()) {
    (*of) << "#mSWEEP_version:" << '\t' << MSWEEP_BUILD_VERSION << '\n';
    (*of) << "#num_reads:" << '\t' << this->get_n_reads() << '\n';
    (*of) << "#num_aligned:" << '\t' << this->get_counts_total() << '\n';
    (*of) << "#bootstrap_iters:" << '\t' << this->iters << '\n';
    (*of) << "#c_id" << '\t' << "mean_theta" << '\t' << "bootstrap_mean_thetas" << '\n';

    for (size_t i = 0; i < group_names.size(); ++i) {
      (*of) << group_names[i] << '\t';
      (*of) << this->bootstrap_results[0][i] << '\t'; // First vec has the relative abundances without bootstrapping
      for (size_t j = 0; j < this->iters; ++j) {
	(*of) << this->bootstrap_results[j + 1][i] << (j == this->iters - 1 ? '\n' : '\t');
      }
    }
    of->flush();
  } else {
    throw std::runtime_error("Could not write to abundances file.");
  }
}

void BootstrapSample::write_abundances2(const std::vector<std::string> &estimated_group_names,
					const std::vector<std::string> &zero_group_names, std::ostream *of) const {
  // Write relative abundances to a file,
  // outputs to std::cout if outfile is empty.
  if (of->good()) {
    (*of) << "#mSWEEP_version:" << '\t' << MSWEEP_BUILD_VERSION << '\n';
    (*of) << "#num_reads:" << '\t' << this->get_n_reads() << '\n';
    (*of) << "#num_aligned:" << '\t' << this->get_counts_total() << '\n';
    (*of) << "#bootstrap_iters:" << '\t' << this->iters << '\n';
    (*of) << "#c_id" << '\t' << "mean_theta" << '\t' << "bootstrap_mean_thetas" << '\n';

    size_t n_targets = estimated_group_names.size() + zero_group_names.size();
    for (size_t i = 0; i < n_targets; ++i) {
	if (i < estimated_group_names.size()) {
	    (*of) << estimated_group_names[i] << '\t';
	    (*of) << this->bootstrap_results[0][i] << '\t'; // First vec has the relative abundances without bootstrapping
	    for (size_t j = 0; j < this->iters; ++j) {
		(*of) << this->bootstrap_results[j + 1][i] << (j == this->iters - 1 ? '\n' : '\t');
	    }
	} else {
	    (*of) << zero_group_names[i - estimated_group_names.size()] << '\t';
	    (*of) << (double)0.0 << '\t'; // First vec has the relative abundances without bootstrapping
	    for (size_t j = 0; j < this->iters; ++j) {
		(*of) << (double)0.0 << (j == this->iters - 1 ? '\n' : '\t');
	    }

	}
    }
    of->flush();
  } else {
    throw std::runtime_error("Could not write to abundances file.");
  }
}
}
