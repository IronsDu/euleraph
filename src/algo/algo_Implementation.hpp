#pragma once

#include "interface/algo/algo.hpp"

class AlgoImpl : public AlgoInterface
{
public:
    uint64_t get_k_hop_neighbor_count(const KHopQueryParams&           params,
                                      std::shared_ptr<ReaderInterface> reader,
                                      WT_CONNECTION*                   conn) override;
    int      get_common_neighbor_count(const CommonNeighborQueryParams& params,
                                       std::shared_ptr<ReaderInterface> reader) override;
    int      get_wcc_count(const WCCParams& params, std::shared_ptr<ReaderInterface> reader) override;
    int      get_subgraph_matching_count(const SubgraphMatchingParams&    params,
                                         std::shared_ptr<ReaderInterface> reader) override;

    uint64_t get_adj_count(const AdjCountQueryParams&       params,
                           std::shared_ptr<ReaderInterface> reader,
                           WT_CONNECTION*                   conn) override;
};