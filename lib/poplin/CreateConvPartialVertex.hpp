// Copyright (c) 2020 Graphcore Ltd. All rights reserved.

#include <poplar/Graph.hpp>
#include <poplar/Program.hpp>
#include <poplar/Tensor.hpp>
#include <poplin/ConvParams.hpp>
#include <string>

namespace poplin {

void createConvPartialSlicVertex(
    poplar::Graph &graph, unsigned slicWindowWidth, unsigned convGroupsPerGroup,
    unsigned chansPerGroup, unsigned tile, ConvParams params,
    poplar::program::Sequence &transformPre, poplar::Tensor &copyWritten,
    poplar::ComputeSet fwdCS, poplar::Tensor in, poplar::Tensor weights,
    poplar::Tensor out, const std::string &debugPrefix);

} // namespace poplin