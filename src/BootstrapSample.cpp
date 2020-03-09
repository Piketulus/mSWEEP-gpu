#include "Sample.hpp"

#include "likelihood.hpp"
#include "rcg.hpp"
#include "version.h"

void BootstrapSample::InitBootstrap(Grouping &grouping) {
  ec_distribution = std::discrete_distribution<uint32_t>(pseudos.ec_counts.begin(), pseudos.ec_counts.end());
  likelihood_array_mat(*this, grouping);
}

void BootstrapSample::ResampleCounts(std::mt19937_64 &generator) {
  std::vector<uint32_t> tmp_counts(m_num_ecs);
  for (uint32_t i = 0; i < counts_total; ++i) {
    uint32_t ec_id = ec_distribution(generator);
    tmp_counts[ec_id] += 1;
  }
#pragma omp parallel for schedule(static)
  for (uint32_t i = 0; i < m_num_ecs; ++i) {
    log_ec_counts[i] = std::log(tmp_counts[i]);
  }
}

void BootstrapSample::BootstrapIter(const std::vector<double> &alpha0, const double tolerance, const uint16_t max_iters) {
  // Process pseudoalignments but return the abundances rather than writing.
  ec_probs = rcg_optl_mat(ll_mat, *this, alpha0, tolerance, max_iters);
  this->relative_abundances.emplace_back(group_abundances());
}

void BootstrapSample::BootstrapAbundances(Reference &reference, Arguments &args) {
  std::random_device rd;
  std::mt19937_64 gen(rd());
  std::cerr << "Running estimation with " << args.iters << " bootstrap iterations" << '\n';
  // Which sample are we processing?
  std::string name = (args.batch_mode ? this->cell_id : "0");
  std::cout << "Processing " << (args.batch_mode ? name : "the sample") << std::endl;
  // Init the bootstrap variables
  InitBootstrap(reference.grouping);
  //  bootstrap_abundances = std::vector<std::vector<double>>(args.iters, std::vector<double>());
  for (unsigned i = 0; i <= args.iters; ++i) {
    if (i > 0) {
      std::cout << "Bootstrap" << " iter " << i << "/" << args.iters << std::endl;
    } else {
      std::cerr << "Estimating relative abundances without bootstrapping" << std::endl;
    }
    BootstrapIter(args.optimizer.alphas, args.optimizer.tolerance, args.optimizer.max_iters);
    // Resample the pseudoalignment counts (here because we want to include the original)
    ResampleCounts(gen);
  }
}


void BootstrapSample::WriteBootstrap(const std::vector<std::string> &cluster_indicators_to_string, std::string &outfile, const unsigned iters, const bool batch_mode) {
  // Write relative abundances to a file,
  // outputs to std::cout if outfile is empty.
  outfile = (outfile.empty() || !batch_mode ? outfile : outfile + '/' + cell_id);
  std::streambuf *buf;
  std::ofstream of;
  if (outfile.empty()) {
    buf = std::cout.rdbuf();
  } else {
    outfile += "_abundances.txt";
    of.open(outfile);
    buf = of.rdbuf();
  }
  std::ostream out(buf);
  out << "#mSWEEP_version:" << '\t' << _BUILD_VERSION << '\n';
  out << "#total_hits:" << '\t' << counts_total << '\n';
  out << "#bootstrap_iters:" << '\t' << iters << '\n';
  out << "#c_id" << '\t' << "mean_theta" << '\t' << "bootstrap_mean_thetas" << '\n';

  for (size_t i = 0; i < cluster_indicators_to_string.size(); ++i) {
    out << cluster_indicators_to_string[i] << '\t';
    for (unsigned j = 0; j <= iters; ++j) {
      out << relative_abundances[j][i] << (j == iters ? '\n' : '\t');
    }
  }
  out << std::endl;
  if (!outfile.empty()) {
    of.close();
  }
}
