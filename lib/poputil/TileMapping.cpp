#include "poputil/TileMapping.hpp"

#include "poputil/Util.hpp"
#include "poputil/exceptions.hpp"
#include <algorithm>
#include <cassert>
#include <functional>
#include <numeric>
#include "poplibs_support/gcd.hpp"

namespace poputil {

std::vector<std::vector<poplar::Interval>>
calcLinearTileMapping(const poplar::Graph &graph,
                      std::vector<std::size_t> shape,
                      unsigned minElementsPerTile,
                      unsigned grainSize) {
  const auto numTiles = graph.getTarget().getNumTiles();
  const auto numElements = std::accumulate(shape.begin(), shape.end(), 1UL,
                                           std::multiplies<std::size_t>());
  std::vector<poplar::Interval> regions = {
    {0, numElements}
  };
  return splitRegions(regions, grainSize, numTiles, minElementsPerTile);
}

std::vector<std::vector<poplar::Interval>>
calcLinearTileMapping(const poplar::Graph &graph,
                      const poplar::Tensor &t) {
  const auto dType = t.elementType();
  const auto &target = graph.getTarget();
  const auto typeSize = target.getTypeSize(dType);
  unsigned grainSize = target.getVectorWidth(dType);
  const auto minBytesPerTile = 128;
  const auto minElementsPerTile =
    (minBytesPerTile + typeSize - 1) / minBytesPerTile;
  return calcLinearTileMapping(graph, t.shape(), minElementsPerTile,
                               grainSize);
}

void
mapTensorLinearly(poplar::Graph &graph, const poplar::Tensor &t,
                  unsigned minElementsPerTile ,
                  unsigned grainSize) {
  graph.setTileMapping(t, calcLinearTileMapping(graph, t.shape(),
                                                minElementsPerTile, grainSize));
}

void
mapTensorLinearly(poplar::Graph &graph, const poplar::Tensor &t) {
  graph.setTileMapping(t, calcLinearTileMapping(graph, t));
}

} // end namespace popops