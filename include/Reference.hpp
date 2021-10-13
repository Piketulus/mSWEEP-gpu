#ifndef MSWEEP_REFERENCE_HPP
#define MSWEEP_REFERENCE_HPP

#include <vector>
#include <array>
#include <string>
#include <fstream>

struct Grouping {
  std::vector<uint32_t> indicators;
  std::vector<uint16_t> sizes;
  std::vector<std::array<double, 2>> bb_params;
  uint32_t n_groups;
};

class Reference {
public:
  Grouping grouping;
  std::vector<std::string> group_names;
  uint32_t n_refs;
  
  void calculate_bb_parameters(double params[2]);
  void verify() const;
};

void ReadClusterIndicators(std::istream &indicator_file, Reference &reference);
void MatchClusterIndicators(const char delim, std::istream &groups, std::istream &fasta, Reference &reference);

#endif
