#pragma once

#include "interface/algo/algo.hpp"

class AlgoImpl : public AlgoInterface
{
public:
    int get_k_hop_neighbor_count(const KHopQueryParams& params, std::shared_ptr<ReaderInterface> reader) override;
    int get_common_neighbor_count(const CommonNeighborQueryParams& params,
                                  std::shared_ptr<ReaderInterface> reader) override;
};