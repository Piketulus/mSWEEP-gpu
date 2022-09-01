#include <iostream>
#include <vector>
#include <memory>
#include <fstream>

#include "rcgpar.hpp"
#include "msweep_log.hpp"

#include "mSWEEP.hpp"
#include "parse_arguments.hpp"
#include "Sample.hpp"
#include "Reference.hpp"

#include "Matrix.hpp"

#include "version.h"
#include "openmp_config.hpp"
#include "mpi_config.hpp"

void finalize(const std::string &msg, Log &log, bool abort = false) {
  log << msg;
  if (abort != 0)  {
#if defined(MSWEEP_MPI_SUPPORT) && (MSWEEP_MPI_SUPPORT) == 1
    MPI_Abort(MPI_COMM_WORLD, 1);
#endif
  }
#if defined(MSWEEP_MPI_SUPPORT) && (MSWEEP_MPI_SUPPORT) == 1
  MPI_Finalize();
#endif
  log.flush();
}

seamat::DenseMatrix<double> rcg_optl(const Arguments &args, const seamat::DenseMatrix<double> &ll_mat, const std::vector<double> &log_ec_counts, Log &log) {
  // Wrapper for calling rcgpar with omp or mpi depending on config
#if defined(MSWEEP_MPI_SUPPORT) && (MSWEEP_MPI_SUPPORT) == 1
  // MPI parallellization
  int rank;
  MPI_Comm_rank(MPI_COMM_WORLD,&rank);
  std::ofstream of; // Silence output from ranks > 1 with an empty ofstream
  const seamat::DenseMatrix<double> &ec_probs = rcgpar::rcg_optl_mpi(ll_mat, log_ec_counts, args.optimizer.alphas, args.optimizer.tolerance, args.optimizer.max_iters, (rank == 0 ? log.stream() : of));

#else
  // OpenMP parallelllization
  const seamat::DenseMatrix<double> &ec_probs = rcgpar::rcg_optl_omp(ll_mat, log_ec_counts, args.optimizer.alphas, args.optimizer.tolerance, args.optimizer.max_iters, log.stream());
#endif
  return ec_probs;
}

int main (int argc, char *argv[]) {
  int rank = 0; // If MPI is not supported then we are always on the root process
  Log log(std::cerr, true, false);

#if defined(MSWEEP_MPI_SUPPORT) && (MSWEEP_MPI_SUPPORT) == 1
  // Initialize MPI
  int rc = MPI_Init(&argc, &argv);
  if (rc != MPI_SUCCESS) {
    finalize("MPI initialization failed\n\n", log);
    return 1;
  }
  int ntasks;
  rc=MPI_Comm_size(MPI_COMM_WORLD,&ntasks);
  rc=MPI_Comm_rank(MPI_COMM_WORLD,&rank);
#endif

  // Parse command-line arguments
  log << "mSWEEP-" << MSWEEP_BUILD_VERSION << " abundance estimation" << '\n';
  if (CmdOptionPresent(argv, argv+argc, "--version")) {
    log << "mSWEEP-" << MSWEEP_BUILD_VERSION << '\n';
  }
  if (CmdOptionPresent(argv, argv+argc, "--help")) {
    PrintHelpMessage();
  }
  if (CmdOptionPresent(argv, argv+argc, "--cite")) {
    PrintCitationInfo();
  }
  if (CmdOptionPresent(argv, argv+argc, "--version") || CmdOptionPresent(argv, argv+argc, "--help") || CmdOptionPresent(argv, argv+argc, "--cite")) {
    finalize("", log);
    return 0;
  }

  Arguments args;
  try {
    log << "Parsing arguments" << '\n';
    ParseArguments(argc, argv, args);
  } catch (const std::exception &e) {
    finalize("Error in parsing arguments:\n  " + std::string(e.what()) + "\nexiting\n", log);
    return 1;
  }

#if defined(MSWEEP_OPENMP_SUPPORT) && (MSWEEP_OPENMP_SUPPORT) == 1
  omp_set_num_threads(args.optimizer.nr_threads);
#endif

  std::unique_ptr<Sample> sample;
  Reference reference;

  if (args.bootstrap_mode) {
    sample.reset(new BootstrapSample(args.seed));
  } else {
    sample.reset(new Sample());
  }

  try {
    log << "Reading the input files" << '\n';
    if (rank == 0) { // Only root reads in data
      ReadGroupIndicators(args, log.stream(), &reference);
      if (!args.read_likelihood_mode) {
	log << "  reading pseudoalignments" << '\n';
	ReadPseudoalignments(args, reference, &sample->pseudos);
	sample->process_aln(args.bootstrap_mode);
	log << "  read " << sample->num_ecs() << " unique alignments" << '\n';
	log.flush();
      } else {
	ReadLikelihoodFromFile(args, reference, log.stream(), &(*sample));
      }
    }
  } catch (std::exception &e) {
    finalize("Reading the input files failed:\n  " + std::string(e.what()) + "\nexiting\n", log, true);
    return 1;
  }

  // Estimate abundances with all groupings that were provided
  uint16_t n_groupings;
  if (rank == 0) { // rank 0
    n_groupings = reference.get_n_groupings();
  }
#if defined(MSWEEP_MPI_SUPPORT) && (MSWEEP_MPI_SUPPORT) == 1
  // Only root process has read in the input.
  MPI_Bcast(&n_groupings, 1, MPI_UINT16_T, 0, MPI_COMM_WORLD);
#endif

  for (uint16_t i = 0; i < n_groupings; ++i) {
      // Send the number of groups from root to all processes
      uint16_t n_groups;
      if (rank == 0) // rank 0
	n_groups = reference.get_grouping(i).get_n_groups();
#if defined(MSWEEP_MPI_SUPPORT) && (MSWEEP_MPI_SUPPORT) == 1
      MPI_Bcast(&n_groups, 1, MPI_UINT16_T, 0, MPI_COMM_WORLD);
#endif

      log << "Building log-likelihood array" << '\n';
      if (rank == 0) // rank 0
	ConstructLikelihood(args, reference.get_grouping(i), reference.get_group_indicators(i), sample, (i == n_groupings - 1));

      // Process the reads accordingly
      if (args.optimizer.no_fit_model == 1) {
	log << "Skipping relative abundance estimation (--no-fit-model toggled)" << '\n';
      } else {
	log << "Estimating relative abundances" << '\n';
	args.optimizer.alphas = std::vector<double>(n_groups, 1.0); // Prior parameters

	// Run estimation
	sample->ec_probs = rcg_optl(args, sample->ll_mat, sample->log_ec_counts, log);
	if (rank == 0) // rank 0
	  sample->relative_abundances = rcgpar::mixture_components(sample->ec_probs, sample->log_ec_counts);

	if (args.bootstrap_mode) {
	  // Bootstrap the ec_counts and estimate from the bootstrapped data
	  log << "Running estimation with " << args.iters << " bootstrap iterations" << '\n';
	  BootstrapSample* bs = static_cast<BootstrapSample*>(&(*sample));
	  if (rank == 0)
	    bs->init_bootstrap();

	  for (uint16_t k = 0; k < args.iters; ++k) {
	    // Bootstrap the counts
	    std::vector<double> resampled_log_ec_counts;
	    log << "Bootstrap" << " iter " << k + 1 << "/" << args.iters << '\n';
	    if (rank == 0)
	      resampled_log_ec_counts = bs->resample_counts((args.bootstrap_count == 0 ? bs->counts_total : args.bootstrap_count));

	    // Estimate with the bootstrapped counts
	    const seamat::DenseMatrix<double> &bootstrapped_ec_probs = rcg_optl(args, bs->ll_mat, resampled_log_ec_counts, log);
	    if (rank == 0)
	      bs->bootstrap_results.emplace_back(rcgpar::mixture_components(bootstrapped_ec_probs, resampled_log_ec_counts));
	  }
	}
      }
      // Write results to file from the root process
      if (rank == 0)
	WriteResults(args, sample, reference.get_grouping(i), n_groupings, i);
  }
  finalize("", log);
  sample.release();
  return 0;
}
