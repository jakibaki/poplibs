// Copyright (c) 2018 Graphcore Ltd. All rights reserved.

#ifndef poplibs_support_ContiguousRegionsByTile_hpp
#define poplibs_support_ContiguousRegionsByTile_hpp

#include <poplar/Graph.hpp>
#include <poplar/Interval.hpp>
#include <poplar/Tensor.hpp>

#include <cstdint>
#include <vector>

namespace poplibs {

/// For the given tensor return a list for each tile of contiguous memory
/// regions on that tile. Each contiguous memory region is made up of a list
/// of intervals in the flattened tensor A.
///
/// Equivalent to the following code, but should be faster in some cases:
///
///     vector<vector<vector<Interval<size_t>>>> contiguousRegionsByTile;
///
///     for (const auto &m : mapping) {
///       contiguousRegionsByTile.emplace_back(
///         graph.getSortedContiguousRegions(A, m)
///       );
///     }
///
/// \param graph    The compute graph
/// \param A        The tensor
/// \param mapping  Must be the result of graph.getTileMapping(A). This is
///                 passed as a parameter because getTileMapping can be slow
///                 and you may already have the data.
std::vector<std::vector<std::vector<poplar::Interval>>>
getSortedContiguousRegionsByTile(
    const poplar::Graph &graph, const poplar::Tensor &A,
    const poplar::Graph::TileToTensorMapping &mapping);

} // namespace poplibs

#endif // poplibs_support_ContiguousRegionsByTile_hpp
