#include "ConvLayer.hpp"
#include "PerformanceEstimation.hpp"
#include "VertexTemplates.hpp"

// Greatest common divisor
template <typename T>
static T gcd(T a, T b) {
  while (b != 0) {
    T tmp = b;
    b = a % b;
    a = tmp;
  }
  return a;
}

namespace {
  struct ConvolutionParams {
    unsigned kernelSize;
    unsigned stride;
    unsigned inputDepth;
    unsigned inputWidth;
    unsigned inputHeight;
    unsigned padding;
    unsigned outputDepth;
    unsigned getOutputWidth() const {
      return (inputWidth + padding - kernelSize) / stride + 1;
    }
    unsigned getOutputHeight() const {
      return (inputHeight + padding - kernelSize) / stride + 1;
    }
    ConvolutionParams(unsigned kernelSize,
                      unsigned stride,
                      unsigned inputDepth,
                      unsigned inputWidth,
                      unsigned inputHeight,
                      unsigned padding,
                      unsigned outputDepth) :
      kernelSize(kernelSize),
      stride(stride),
      inputDepth(inputDepth),
      inputWidth(inputWidth),
      inputHeight(inputHeight),
      padding(padding),
      outputDepth(outputDepth) {}
  };
}

/// Return the index of the input that is multiplied with the specified weight
/// and incorporated into the specified output. Return ~0U if there is no
/// such output.
static unsigned
getInputIndex(unsigned outputIndex, unsigned stride, unsigned kernelSize,
              unsigned inputSize, unsigned weightIndex) {
  const auto inputCentre = outputIndex * stride;
  auto inputIndex = static_cast<int>(inputCentre) +
                    static_cast<int>(weightIndex) -
                    static_cast<int>((kernelSize - 1) / 2);
  if (inputIndex < 0 || static_cast<unsigned>(inputIndex) >= inputSize)
    return ~0U;
  return inputIndex;
}

/// Given an output range, return the subset whose calculation involves the
/// specified weight.
static std::pair<unsigned, unsigned>
getOutputRange(std::pair<unsigned, unsigned> outputRange, unsigned stride,
               unsigned kernelSize, unsigned inputSize, unsigned weightIndex) {
  assert(outputRange.first <= outputRange.second);
  if (outputRange.first == outputRange.second) {
    return {0, 0};
  }
  unsigned outputBegin = 0, outputEnd = 0;
  for (unsigned i = outputRange.first; i != outputRange.second; ++i) {
    if (getInputIndex(i, stride, kernelSize, inputSize, weightIndex) == ~0U) {
      continue;
    }
    outputBegin = i;
    break;
  }
  for (unsigned i = outputRange.second; i != outputRange.first; --i) {
    if (getInputIndex(i - 1, stride, kernelSize, inputSize,
                      weightIndex) == ~0U) {
      continue;
    }
    outputEnd = i;
    break;
  }
  return {outputBegin, outputEnd};
}

/// Return the input range that is multiplied by the specified weight when
/// calculating the specified output range.
static std::pair<unsigned, unsigned>
getInputRange(std::pair<unsigned, unsigned> outputRange, unsigned stride,
              unsigned kernelSize, unsigned inputSize, unsigned weightIndex) {
  auto truncatedOutputRange =
      getOutputRange(outputRange, stride, kernelSize, inputSize,
                     weightIndex);
  if (truncatedOutputRange.first == truncatedOutputRange.second) {
    return {0, 0};
  }
  return {
    getInputIndex(truncatedOutputRange.first, stride, kernelSize, inputSize,
                  weightIndex),
    getInputIndex(truncatedOutputRange.second - 1, stride, kernelSize,
                  inputSize, weightIndex) + 1
  };
}

static std::pair<unsigned, unsigned>
getInputRange(unsigned outputIndex, unsigned stride, unsigned kernelSize,
              unsigned inputSize) {
  const auto inputCentre = outputIndex * stride;
  const auto distanceFromCentre = (kernelSize - 1) / 2;
  const auto begin =
      inputCentre > distanceFromCentre ? inputCentre - distanceFromCentre : 0;
  const auto end = std::min(inputCentre + distanceFromCentre + 1, inputSize);
  return {begin, end};
}

static std::pair<unsigned, unsigned>
getInputRange(std::pair<unsigned, unsigned> outputRange, unsigned stride,
              unsigned kernelSize, unsigned inputSize) {
  assert(outputRange.first <= outputRange.second);
  if (outputRange.first == outputRange.second) {
    return {0, 0};
  }
  const auto begin =
      getInputRange(outputRange.first, stride, kernelSize, inputSize).first;
  const auto end =
      getInputRange(outputRange.second - 1, stride, kernelSize,
                    inputSize).second;
  return {begin, end};
}

static std::pair<unsigned, unsigned>
getWeightRange(unsigned outputIndex, unsigned stride, unsigned kernelSize,
               unsigned inputSize) {
  const auto inputCentre = outputIndex * stride;
  const auto distanceFromCentre = (kernelSize - 1) / 2;
  unsigned inputBegin, inputEnd;
  std::tie(inputBegin, inputEnd) = getInputRange(outputIndex, stride,
                                                 kernelSize, inputSize);
  const auto weightBegin = inputBegin + distanceFromCentre - inputCentre;
  const auto weightEnd = inputEnd + distanceFromCentre - inputCentre;
  return { weightBegin, weightEnd };
}

static std::pair<unsigned, unsigned>
getWeightRange(std::pair<unsigned, unsigned> outputRange, unsigned stride,
               unsigned kernelSize, unsigned inputSize) {
  assert(outputRange.first <= outputRange.second);
  if (outputRange.first == outputRange.second) {
    return {0, 0};
  }
  const auto begin =
      getWeightRange(outputRange.first, stride, kernelSize, inputSize).first;
  const auto end =
      getWeightRange(outputRange.second - 1, stride, kernelSize,
                     inputSize).second;
  return {begin, end};
}

static unsigned
getMaxInputRangeSize(unsigned outputRangeSize, unsigned stride,
                     unsigned kernelSize, unsigned numPartitions,
                     unsigned inputSize, bool contiguousAccess) {
  if (outputRangeSize == 0)
    return 0;
  unsigned inputRangeSize;
  // If the number of partitions is small the input range is guaranteed
  // to contain padding.
  switch (numPartitions) {
  case 1:
  case 2:
    {
      auto inputRange = getInputRange({0, outputRangeSize}, stride, kernelSize,
                                      inputSize);
      inputRangeSize = inputRange.second - inputRange.first;
    }
    break;
  default:
    inputRangeSize = (outputRangeSize - 1) * stride + 1 + (kernelSize - 1);
    break;
  }
  if (!contiguousAccess && kernelSize == 1 && stride > 1) {
    inputRangeSize = (inputRangeSize - 1) / stride + 1;
  }
  return inputRangeSize;
}

static unsigned
estimateExchangeCost(bool isFloat, const ConvolutionParams &params,
                     const ConvLayerPartition &partition) {
  const auto tilesPerX = partition.tilesPerXAxis;
  const auto tilesPerY = partition.tilesPerYAxis;
  const auto tilesPerZ = partition.tilesPerZAxis;
  const auto tilesPerInZGroupAxis = partition.tilesPerInZGroupAxis;
  const auto inChansPerGroup = partition.inChansPerGroup;
  const auto partialChansPerGroup = partition.partialChansPerGroup;

  const auto tileOutWidth =
      (params.getOutputWidth() + tilesPerX - 1) / tilesPerX;
  const auto tileOutHeight =
      (params.getOutputHeight() + tilesPerY - 1) / tilesPerY;
  const auto numOutGroups =
      (params.outputDepth + (partialChansPerGroup - 1)) / partialChansPerGroup;
  const auto tileNumOutGroups =
      (numOutGroups + tilesPerZ - 1) / tilesPerZ;
  const auto tileOutDepth = tileNumOutGroups * partialChansPerGroup;
  const auto numInGroups =
      (params.inputDepth + (inChansPerGroup - 1)) / inChansPerGroup;
  const auto tileNumInGroups =
      (numInGroups + tilesPerInZGroupAxis - 1) / tilesPerInZGroupAxis;
  const auto tileInDepth = tileNumInGroups * inChansPerGroup;
  const auto tileInWidth =
      getMaxInputRangeSize(tileOutWidth, params.stride, params.kernelSize,
                           tilesPerX, params.inputWidth, true);
  const auto tileInHeight =
      getMaxInputRangeSize(tileOutHeight, params.stride, params.kernelSize,
                           tilesPerY, params.inputWidth, false);
  const auto numberOfInputElements = tileInWidth * tileInHeight * tileInDepth;
  const auto numberOfWeights =
      params.kernelSize * params.kernelSize * tileOutDepth * tileInDepth;
  const auto numberOfOutputElements =
      tileOutWidth * tileOutHeight * tileOutDepth;
  const auto numberOfPartialSums = numberOfOutputElements;
  const auto elementSize = isFloat ? 4 : 2;
  const auto inputElementsBytes = numberOfInputElements * elementSize;
  const auto weightBytes = numberOfWeights * elementSize;
  const auto partialSumBytes = numberOfPartialSums * 4;
  const auto numCycles = (inputElementsBytes + 3) / 4 +
                         (weightBytes + 3) / 4 +
                         (partialSumBytes + 3) / 4;
  return numCycles;
}

static unsigned
estimateVertexCycles(bool isFloat, const ConvolutionParams &params,
                     const ConvLayerPartition &partition,
                     bool useSupervisorVertices) {
  const auto tilesPerY = partition.tilesPerYAxis;
  const auto tilesPerX = partition.tilesPerXAxis;
  const auto tilesPerInZGroupAxis = partition.tilesPerInZGroupAxis;
  const auto verticesPerTilePerY = partition.verticesPerTilePerYAxis;
  const auto inChansPerGroup = partition.inChansPerGroup;
  const auto outChansPerGroup = partition.partialChansPerGroup;

  const auto tileOutHeight =
      (params.getOutputHeight() + tilesPerY - 1) / tilesPerY;
  const auto tileOutWidth =
      (params.getOutputWidth() + tilesPerX - 1) / tilesPerX;
  const auto numInGroups =
      (params.inputDepth + (inChansPerGroup - 1)) / inChansPerGroup;
  const auto tileNumInGroups =
      (numInGroups + tilesPerInZGroupAxis - 1) / tilesPerInZGroupAxis;

  const auto outRowsPerVertex =
      (tileOutHeight + verticesPerTilePerY - 1) / verticesPerTilePerY;
  const auto inputGroupsPerOutput = params.kernelSize * tileNumInGroups;
  return getConvPartialCycleEstimate(isFloat, inChansPerGroup, params.stride,
                                     params.kernelSize, inputGroupsPerOutput,
                                     outRowsPerVertex, tileOutWidth,
                                     outChansPerGroup, useSupervisorVertices);
}

static unsigned
estimateComputeCost(unsigned numWorkerContexts, bool isFloat,
                    const ConvolutionParams &params,
                    const ConvLayerPartition &partition,
                    bool targetConvSharedWeights) {
  const auto tilesPerY = partition.tilesPerYAxis;
  const auto tilesPerZ = partition.tilesPerZAxis;
  const auto outChansPerGroup = partition.partialChansPerGroup;

  const auto tileOutHeight =
      (params.getOutputHeight() + tilesPerY - 1) / tilesPerY;
  const auto numOutGroups =
      (params.outputDepth + (outChansPerGroup - 1)) / outChansPerGroup;

  const auto tileNumOutGroups =
      (numOutGroups + tilesPerZ - 1) / tilesPerZ;

  const auto verticesPerTilePerY =
      std::min(tileOutHeight, partition.verticesPerTilePerYAxis);
  const auto tileVertices = verticesPerTilePerY * tileNumOutGroups;
  // The use of supervisor vertices only affects vertices that use the
  // convolution instructions.
  bool useSupervisorVertices = false;
  unsigned numContexts = numWorkerContexts;
  if (targetConvSharedWeights &&
      canUseConvolutionInstruction(isFloat, params.stride,
                                   partition.inChansPerGroup,
                                   partition.partialChansPerGroup)) {
    useSupervisorVertices = true;
    numContexts = 1;
  }
  const auto vertexRuntime = estimateVertexCycles(isFloat, params, partition,
                                                  useSupervisorVertices);
  auto verticesPerWorker = (tileVertices + numContexts - 1) /
                           numContexts;
  auto computeCycles = vertexRuntime * verticesPerWorker * numContexts;
  return computeCycles;
}

static unsigned
estimatePartitionCostBounded(unsigned numWorkerContexts, bool isFloat,
                             const ConvolutionParams &params,
                             const ConvLayerPartition &partition,
                             bool targetConvSharedWeights,
                             unsigned maxBound) {
  auto cost = estimateExchangeCost(isFloat, params, partition);
  if (cost > maxBound)
    return maxBound;
  cost += estimateComputeCost(numWorkerContexts, isFloat, params, partition,
                              targetConvSharedWeights);
  return std::min(cost, maxBound);
}

static unsigned
estimatePartitionCost(unsigned numWorkerContexts, bool isFloat,
                      const ConvolutionParams &params,
                      const ConvLayerPartition &partition,
                      bool targetConvSharedWeights) {
  return estimatePartitionCostBounded(numWorkerContexts, isFloat, params,
                                      partition, targetConvSharedWeights,
                                      std::numeric_limits<unsigned>::max());
}

static ConvLayerPartition
choosePartition(unsigned numWorkerContexts,
                bool isFloat,
                unsigned inChansPerGroup,
                const ConvolutionParams &params,
                unsigned numTiles,
                bool targetConvSharedWeights) {
  unsigned bestCost = std::numeric_limits<unsigned>::max();
  ConvLayerPartition bestPartition;
  if (params.inputDepth % inChansPerGroup != 0) {
    // TODO handle this case.
    std::abort();
  }
  std::vector<unsigned> partialChansPerGroupCandidates;
  if (canUseConvolutionInstruction(isFloat, params.stride, inChansPerGroup,
                                   4)) {
    partialChansPerGroupCandidates.push_back(4);
  }
  partialChansPerGroupCandidates.push_back(1);
  // If tilesPerY is greater than one we end up splitting across the y axis of
  // the output volume. The input elements required to compute output elements
  // on one side of the split will overlap with the input elements required for
  // the otherside of the split, increasing communication.
  // An alternative strategy would be to split across the y axis of
  // the input volume. Now there is no overlap in input elements read by each
  // tile, but nx1 convolutions for rows near the boundary must be summed
  // with nx1 convolutions for rows the other side the boundary. This results
  // to the communication for more partial sums.
  // Assuming a stide of 1, the alterative strategy reads
  // inputsChannelsPerTile * (filterSize - 1) fewer input rows per tile pair
  // but it needs to sends (outputChannelsPerTile * (filterSize - 1) / 2) extra
  // rows of partial sum per tile pair.
  // TODO investigate the alternative strategy outlined above.
  for (const auto partialChansPerGroup : partialChansPerGroupCandidates) {
    const auto maxTilesPerX = std::min(params.getOutputWidth(), numTiles);
    for (unsigned tilesPerX = 1; tilesPerX <= maxTilesPerX; ++tilesPerX) {
      const auto maxTilesPerY = std::min(params.getOutputHeight(),
                                         numTiles / tilesPerX);
      for (unsigned tilesPerY = 1; tilesPerY <= maxTilesPerY; ++tilesPerY) {
        const auto maxTilesPerZ =
            std::min(params.outputDepth, numTiles / (tilesPerX * tilesPerY));
        for (unsigned tilesPerZ = 1; tilesPerZ <= maxTilesPerZ; ++tilesPerZ) {
          const auto tilesPerInZ =
              std::min(params.inputDepth / inChansPerGroup,
                       numTiles / (tilesPerX * tilesPerY * tilesPerZ));
          const auto maxVerticesPerTilePerY =
              (params.getOutputHeight() + tilesPerY - 1) / tilesPerY;
          const auto minVerticesPerTilePerY =
              partialChansPerGroup == 4 ? 1 : maxVerticesPerTilePerY;
          for (unsigned verticesPerTilePerY = minVerticesPerTilePerY;
               verticesPerTilePerY <= maxVerticesPerTilePerY;
               ++verticesPerTilePerY) {
            ConvLayerPartition candidate(tilesPerX, tilesPerY, tilesPerZ,
                                         verticesPerTilePerY, tilesPerInZ,
                                         inChansPerGroup, partialChansPerGroup);
            auto candidateCost =
                estimatePartitionCostBounded(numWorkerContexts, isFloat, params,
                                             candidate, targetConvSharedWeights,
                                             bestCost);
            if (candidateCost < bestCost) {
              bestPartition = candidate;
              bestCost = candidateCost;
            }
          }
        }
      }
    }
  }
  return bestPartition;
}

std::map<ConvImplSpec, ConvLayerImpl *> ConvLayerImpl::implMap;

ConvLayerImpl::ConvLayerImpl(Net &net,
                             int index,
                             unsigned kernelSize,
                             unsigned stride,
                             unsigned padding,
                             unsigned numChannels,
                             NonLinearityType nonLinearityType,
                             NormalizationType normalizationType,
                             unsigned resIndex,
                             enum ResidualMethod resMethod) :
  Layer(net, index),
  kernelSize(kernelSize),
  stride(stride),
  padding(padding),
  outNumChans(numChannels),
  nonLinearityType(nonLinearityType),
  normalizationType(normalizationType),
  createdForwardProg(false),
  resIndex(resIndex),
  resMethod(resMethod),
  reuseLayerImplGraphs(net.options.reuseLayerImplGraphs) {
  layerName = "Conv" + std::to_string(kernelSize) + "x" +
              std::to_string(kernelSize);
}

std::uint64_t ConvLayerImpl::getNumberOfMACs() {
  std::uint64_t numMACs = 0;
  for (unsigned y = 0; y < outDimY; ++y) {
    unsigned inYBegin, inYEnd;
    std::tie(inYBegin, inYEnd) = getInputRange(y, stride, kernelSize,
                                               inDimY);
    const auto height = inYEnd - inYBegin;
    for (unsigned x = 0; x < outDimX; ++x) {
      unsigned inXBegin, inXEnd;
      std::tie(inXBegin, inXEnd) = getInputRange(x, stride, kernelSize,
                                                 inDimX);
      const auto width = inXEnd - inXBegin;
      numMACs += width * height * outNumChans * inNumChans;
    }
  }
  return numMACs;
}

std::uint64_t ConvLayerImpl::getNumberOfAdds() {
  if (!resIndex)
    return 0;

  // An addition is required to add in the residual information
  return outNumChans * outDimX * outDimY;
}


std::uint64_t ConvLayerImpl::getNumberOfFlops() {
  return 2 * getNumberOfMACs() + getNumberOfAdds();
}


double ConvLayerImpl::getPerfectCycleCount() {
  const auto numTiles = getNumIPUs() * getTilesPerIPU();
  if (getDType() == "float") {
    // Can execute 2 f32 MACs per cycle.
    auto macCycles =
       static_cast<double>(getNumberOfMACs()) / (2 * numTiles);
    // Can execute 2 f32 ADDs per cycle.
    auto addCycles =
       static_cast<double>(getNumberOfAdds()) / (2 * numTiles);
    return macCycles + addCycles;
  }
  assert(getDType() == "half");
  auto macsPerCycles = useConvolutionInstruction() ? 16 : 4;
  auto macCycles = static_cast<double>(getNumberOfMACs()) /
                   (macsPerCycles * numTiles);

  // Can execute 4 f16 ADDs per cycle.
  auto addCycles = static_cast<double>(getNumberOfAdds()) / (4 * numTiles);
  return macCycles + addCycles;
}

void ConvLayerImpl::describe(std::ostream &out) {
  unsigned numParams = weights.numElements() + biases.numElements();
  if (resIndex)
    out << "   -- Convolutional layer (residual):\n";
  else
    out << "   -- Convolutional layer:\n";
  out << "        Size: " << kernelSize << "x" << kernelSize << "\n"
      << "        Stride: " << stride << "\n"
      << "        Padding: " << padding << "\n"
      << "        Input: " << inDimX << "x" << inDimY
                  <<   "x" << inNumChans << "\n"
      << "        Output: " << outDimX << "x" << outDimY
                   <<   "x" << outNumChans << "\n"
      << "        Params: " << numParams << "\n"
      << "        FLOPs: " << getNumberOfFlops() << "\n";
}

size_t ConvLayerImpl::getNumChannelGroupsIn(size_t xPrev, size_t yPrev,
                                            size_t zPrev) const {
  unsigned inChansPerGroup = zPrev;
  const bool isFloat = getDType() == "float";
  const auto numWorkerContexts = getWorkerContextsPerTile();
  const auto numTiles = getNumIPUs() * getTilesPerIPU();
  unsigned bestCost = std::numeric_limits<unsigned>::max();
  ConvolutionParams params(kernelSize, stride, zPrev, xPrev,
                           yPrev, padding, outNumChans);
  for (unsigned i = 1; i <= zPrev; ++i) {
    if (zPrev % i != 0)
      continue;
    if (!isFloat && i % 2 != 0)
      continue;
    const auto sharedWeights = targetSharedConvWeights();
    const auto candidate =
      choosePartition(numWorkerContexts, isFloat, i,
                      params, numTiles, sharedWeights);
    const auto candidateCost =
        estimatePartitionCost(numWorkerContexts, isFloat, params,
                              candidate, sharedWeights);
    if (candidateCost < bestCost) {
      inChansPerGroup = candidate.inChansPerGroup;
      bestCost = candidateCost;
    }
  }
  return zPrev / inChansPerGroup;
}

void ConvLayerImpl::
init(Graph &graph, IPUModelEngineBuilder::TileMapping *mapping) {
  const auto dType = getDType();
  bool isFloat = dType == "float";
  Layer *prev = getPrevLayer();
  Tensor prevOut = prev->getFwdActivations();
  inNumChanGroups = prevOut.dim(0);
  inDimY = prevOut.dim(1);
  inDimX = prevOut.dim(2);
  size_t inChansPerGroup = prevOut.dim(3);
  inNumChans = inChansPerGroup * inNumChanGroups;
  outDimX = (inDimX + padding - kernelSize) / stride + 1;
  outDimY = (inDimY + padding - kernelSize) / stride + 1;
  partition =
      choosePartition(this->getWorkerContextsPerTile(), isFloat,
                      inChansPerGroup,
                      ConvolutionParams(kernelSize, stride, inNumChans, inDimX,
                                        inDimY, padding, outNumChans),
                      getNumIPUs() * getTilesPerIPU(),
                      targetSharedConvWeights());
  Layer *next = getNextLayer();
  outNumChanGroups = next->getNumChannelGroupsIn(inDimX, inDimY, outNumChans);
  size_t outChansPerGroup;
  if (outNumChanGroups) {
    outChansPerGroup = outNumChans / outNumChanGroups;
  } else {
    outChansPerGroup = isFloat ? 1 : 2;
    outNumChanGroups = outNumChans / outChansPerGroup;
  }
  assert(outNumChanGroups * outChansPerGroup == outNumChans);
  // Each ConvComplete vertex writes outChansPerGroup output channels. Because
  // sub-word access is not atomic we must ensure output channels are grouped
  // in multiples of two.
  assert(isFloat || outChansPerGroup % 2 == 0);
  const auto partialChansPerGroup = partition.partialChansPerGroup;
  assert(outNumChans % partialChansPerGroup == 0);
  const auto partialNumChanGroups = outNumChans / partialChansPerGroup;
  if (useConvolutionInstruction()) {
    weights = graph.addTensor(dType, {partialNumChanGroups,
                                      inNumChanGroups,
                                      kernelSize,
                                      kernelSize,
                                      partialChansPerGroup,
                                      inChansPerGroup});
  } else {
    assert(partialChansPerGroup == 1);
    weights = graph.addTensor(dType, {inNumChanGroups,
                                      outNumChans,
                                      kernelSize,
                                      kernelSize,
                                      inChansPerGroup});
  }
  biases = graph.addTensor(dType, {outNumChans});
  mapTensor(biases, mapping);

  unsigned resDimX = 0, resDimY = 0, resNumChans = 0, resNumChanGroups = 0,
           resChansPerGroup;
  if (resIndex) {
    resLayer = this;
    for (unsigned i = 0; i < resIndex; ++i)
      resLayer = resLayer->getPrevLayer();
    auto act = resLayer->getFwdActivations();
    resDimY = act.dim(1);
    resDimX = act.dim(2);
    if (resDimX < outDimX || resDimY < outDimY) {
      throw net_creation_error("Residual layers must use previous layers "
                               "with X and Y dimensions that are larger"
                               "than the current layer's output.");
    }
    resStrideX = resDimX / outDimX;
    resStrideY = resDimY / outDimY;
    resNumChanGroups = act.dim(0);
    resChansPerGroup = act.dim(3);
    resNumChans = resNumChanGroups * resChansPerGroup;
  }

  auto implSpec =
    ConvImplSpec(inNumChans, inNumChanGroups,
                 inDimX, inDimY,
                 outNumChans, outNumChanGroups,
                 outDimX, outDimY,
                 resNumChans, resNumChanGroups,
                 resDimX, resDimY,
                 kernelSize, stride, padding);



  if (reuseLayerImplGraphs) {
    auto emplaceResult = implMap.emplace(implSpec, this);
    if (!emplaceResult.second) {
      // Matching implementation already exists
      reuseImpl = emplaceResult.first->second;
      return;
    }
  }

  in = graph.addTensor(dType, {prevOut.dim(0), prevOut.dim(1),
                               prevOut.dim(2), prevOut.dim(3)});
  mapActivations(in, mapping);

  z = graph.addTensor(dType, {outNumChanGroups, outDimY, outDimX,
                              outChansPerGroup});
  activations = graph.addTensor(dType, {outNumChanGroups, outDimY, outDimX,
                                        outChansPerGroup});
  if (useConvolutionInstruction()) {
    weightsIn = graph.addTensor(dType, {partialNumChanGroups,
                                        inNumChanGroups,
                                        kernelSize,
                                        kernelSize,
                                        partialChansPerGroup,
                                        inChansPerGroup});
  } else {
    assert(partialChansPerGroup == 1);
    weightsIn = graph.addTensor(dType, {inNumChanGroups,
                                        outNumChans,
                                        kernelSize,
                                        kernelSize,
                                        inChansPerGroup});
  }
  biasesIn = graph.addTensor(dType, {outNumChans});
  mapTensor(z, mapping);
  mapTensor(biasesIn, mapping);
  if (resIndex) {
    resIn = graph.addTensor(dType, {resNumChanGroups,
                                    resDimY, resDimY,
                                    resChansPerGroup});
    mapActivations(resIn, mapping);
  }
}

void ConvLayerImpl::
addResidualCalc(Graph &graph,
                ComputeSet cs,
                IPUModelEngineBuilder::TileMapping *mapping) {
  assert(resLayer);
  auto resNumChanGroups = resIn.dim(0);
  auto resChansPerGroup = resIn.dim(3);
  auto resNumChans = resNumChanGroups * resChansPerGroup;
  if (resMethod != RESIDUAL_WEIGHTED_CONV &&
      resNumChans == outNumChans &&
      resNumChanGroups == outNumChanGroups) {
    // We can directly add the output of the previous layer to this
    // layer's output.
    residual = resIn;
    return;
  }
  size_t outChansPerGroup = outNumChans / outNumChanGroups;
  size_t resOutNumChanGroups =
      (resNumChans + outChansPerGroup - 1) / outChansPerGroup;
  size_t resOutNumChans = resOutNumChanGroups * outChansPerGroup;
  residual = graph.addTensor(getDType(), {resOutNumChanGroups, outDimY, outDimX,
                                          outChansPerGroup});
  mapTensor(residual, mapping);

  switch (resMethod) {
  case RESIDUAL_PAD:
    for (unsigned outChanGroup = 0;
         outChanGroup < resOutNumChanGroups;
         ++outChanGroup) {
      for (unsigned y = 0; y < outDimY; ++y) {
        for (unsigned x = 0; x < outDimX; ++x) {
          auto chansPerVertex = getDTypeSize() == 2 ? 2 : 1;
          assert(outChansPerGroup % chansPerVertex == 0);
          assert(resChansPerGroup % chansPerVertex == 0);
          for (unsigned outChanGroupElement = 0;
               outChanGroupElement < outChansPerGroup;
               outChanGroupElement += chansPerVertex) {
            Tensor out = residual[outChanGroup][y][x]
              .slice(outChanGroupElement,
                     outChanGroupElement + chansPerVertex);
            auto outChan = outChanGroup * outChansPerGroup +
              outChanGroupElement;
            if (outChan >= resNumChans) {
              auto v = graph.addVertex(cs, templateVertex("Zero", getDType()),
                                       {{"out",out}});
              continue;
            }
            auto resChanGroup = outChan / resChansPerGroup;
            auto resChanGroupElement = outChan % resChansPerGroup;
            assert(resChanGroup < resNumChanGroups);
            assert(resChanGroupElement < resChansPerGroup);
            assert(y * resStrideX < resIn.dim(1));
            assert(x * resStrideY < resIn.dim(2));
            Tensor in = resIn[resChanGroup][y * resStrideY][x * resStrideX]
              .slice(resChanGroupElement,
                     resChanGroupElement + chansPerVertex);
            auto v = graph.addVertex(cs,
                                     templateVertex("CopyResidual", getDType()),
                                     {{"in", in}, {"out",out}});
          }
        }
      }
    }
    break;
  case RESIDUAL_WEIGHTED_CONV:
  case RESIDUAL_WEIGHTED_CONV_IF_SIZES_DIFFER:
    assert(0 && "Weighted calculation of residual input not implemented");
    break;
  default:
    assert(0 && "Unknown residual calculation method");
  }
  // This compute set may have more added with a specific mapping later. Here,
  // we map the current vertices of the compute set using the mapComputeSet
  // helper.
  mapComputeSet(graph, cs, mapping);
  resStrideX = resStrideY = 1;
}

bool ConvLayerImpl::useConvolutionInstruction() const {
  const bool isFloat = getDType() == "float";
  return canUseConvolutionInstruction(isFloat, stride,
                                      partition.inChansPerGroup,
                                      partition.partialChansPerGroup);
}

void ConvLayerImpl::
createConvPartial1x1InOutVertex(Graph &graph,
                                IPUModelEngineBuilder::TileMapping *mapping,
                                unsigned tile,
                                unsigned outXBegin, unsigned outXEnd,
                                unsigned outYBegin, unsigned outYEnd,
                                unsigned outZGroup,
                                unsigned inZGroupBegin, unsigned inZGroupEnd,
                                ComputeSet fwdCS,
                                const Tensor &out) {
  const auto inChansPerGroup = partition.inChansPerGroup;
  const auto outChansPerGroup = partition.partialChansPerGroup;
  const auto contextsPerVertex =
      targetSharedConvWeights() ? getWorkerContextsPerTile() : 1;
  const char *baseClass =
      targetSharedConvWeights() ? "poplar::SupervisorVertex" :
                                  "poplar::Vertex";

  // Add the vertex.
  auto v =
      graph.addVertex(fwdCS,
                      templateVertex("ConvPartial1x1InOut", baseClass));
  if (mapping) {
    mapping->setMapping(v, tile);
  }
  unsigned numWeights = 0;
  unsigned numConvolutions = 0;
  for (unsigned wy = 0; wy != kernelSize; ++wy) {
    unsigned convOutYBegin, convOutYEnd;
    std::tie(convOutYBegin, convOutYEnd) =
        getOutputRange({outYBegin, outYEnd}, stride, kernelSize, inDimY,
                       wy);
    const auto convOutHeight = convOutYEnd - convOutYBegin;
    if (convOutHeight == 0)
      continue;
    for (unsigned wx = 0; wx != kernelSize; ++wx) {
      unsigned convOutXBegin, convOutXEnd;
      std::tie(convOutXBegin, convOutXEnd) =
          getOutputRange({outXBegin, outXEnd}, stride, kernelSize,
                         inDimX, wx);
      const auto convOutWidth = convOutXEnd - convOutXBegin;
      if (convOutWidth == 0)
        continue;
      std::vector<std::vector<PartialRow>> workerPartition =
          partitionConvPartialByWorker(convOutHeight, convOutWidth,
                                       contextsPerVertex);
      assert(workerPartition.size() == contextsPerVertex);
      for (unsigned izg = inZGroupBegin; izg != inZGroupEnd; ++izg) {
        Tensor w =
            weightsIn[outZGroup][izg][wy][wx].flatten();
        graph.connect(v["weights"][numWeights], w);
        for (unsigned i = 0; i != contextsPerVertex; ++i) {
          graph.setInitialValue(
            v["weightReuseCount"][numWeights * contextsPerVertex + i],
            static_cast<std::uint32_t>(workerPartition[i].size())
          );
          for (const auto &partialRow : workerPartition[i]) {
            const auto workerOutY = convOutYBegin + partialRow.rowNumber;
            const auto workerOutXBegin = convOutXBegin + partialRow.begin;
            const auto workerOutXEnd = convOutXBegin + partialRow.end;
            const auto workerOutWidth = workerOutXEnd - workerOutXBegin;
            const auto workerInY =
              getInputIndex(workerOutY, stride, kernelSize, inDimY, wy);
            assert(workerInY != ~0U);
            unsigned workerInXBegin, workerInXEnd;
            std::tie(workerInXBegin, workerInXEnd) =
                getInputRange({workerOutXBegin, workerOutXEnd}, stride,
                               kernelSize, inDimX, wx);
            const auto workerInWidth = workerInXEnd - workerInXBegin;
            Tensor inWindow =
                in[izg][workerInY].slice(
                  {workerInXBegin, 0},
                  {workerInXEnd, inChansPerGroup}
                ).reshape({workerInWidth * inChansPerGroup});
            Tensor outWindow =
                out[outZGroup][workerOutY].slice(
                  {workerOutXBegin, 0},
                  {workerOutXEnd, outChansPerGroup}
                ).reshape({workerOutWidth * outChansPerGroup});
            if (mapping) {
              mapping->setMapping(outWindow, tile);
            }
            graph.connect(v["in"][numConvolutions], inWindow);
            graph.connect(v["out"][numConvolutions], outWindow);
            ++numConvolutions;
          }
        }
        ++numWeights;
      }
    }
  }
  graph.setFieldSize(v["in"], numConvolutions);
  graph.setFieldSize(v["out"], numConvolutions);
  graph.setFieldSize(v["weights"], numWeights);
  graph.setFieldSize(v["weightReuseCount"], numWeights * contextsPerVertex);
}


void ConvLayerImpl::
forwardTile(Graph &graph,
            IPUModelEngineBuilder::TileMapping *mapping,
            unsigned tile,
            unsigned tileOutXBegin, unsigned tileOutXEnd,
            unsigned tileOutYBegin, unsigned tileOutYEnd,
            unsigned tileOutZGroupBegin, unsigned tileOutZGroupEnd,
            unsigned tileInZGroupBegin, unsigned tileInZGroupEnd,
            ComputeSet zeroCS,
            ComputeSet fwdCS,
            const Tensor &out) {
  const auto inChansPerGroup = partition.inChansPerGroup;
  const auto outChansPerGroup = partition.partialChansPerGroup;
  const auto tileOutHeight = tileOutYEnd - tileOutYBegin;
  const auto tileOutWidth = tileOutXEnd - tileOutXBegin;
  const auto verticesPerY = partition.verticesPerTilePerYAxis;

  if (useConvolutionInstruction() && kernelSize == 1) {
    const auto inZGroups = tileInZGroupEnd - tileInZGroupBegin;
    for (unsigned ozg = tileOutZGroupBegin; ozg != tileOutZGroupEnd; ++ozg) {
      for (unsigned vy = 0; vy != verticesPerY; ++vy) {
        const auto outYBegin =
            tileOutYBegin + (vy * tileOutHeight) / verticesPerY;
        const auto outYEnd =
            tileOutYBegin + ((vy + 1) * tileOutHeight) / verticesPerY;
        const auto outHeight = outYEnd - outYBegin;
        if (outHeight == 0)
          continue;
        unsigned inYBegin, inYEnd, inXBegin, inXEnd;
        std::tie(inYBegin, inYEnd) =
            getInputRange({outYBegin, outYEnd}, stride, kernelSize, inDimY);
        std::tie(inXBegin, inXEnd) =
            getInputRange({tileOutXBegin, tileOutXEnd}, stride, kernelSize,
                          inDimX);
        // Window into previous layer.
        const auto inWidth = inXEnd - inXBegin;
        const auto inHeight = inYEnd - inYBegin;
        Tensor inWindow =
            in.slice(
              {tileInZGroupBegin, inYBegin, inXBegin, 0},
              {tileInZGroupEnd, inYEnd, inXEnd, inChansPerGroup}
            ).reshape({inHeight * inZGroups,
                       inWidth * inChansPerGroup});
        Tensor w =
            weightsIn[ozg].slice(
              {tileInZGroupBegin, 0, 0, 0, 0},
              {tileInZGroupEnd, 1, 1, outChansPerGroup, inChansPerGroup}
            ).flatten();
        Tensor outWindow =
            out[ozg].slice(
              {outYBegin, tileOutXBegin, 0},
              {outYEnd, tileOutXEnd, outChansPerGroup}
            ).reshape({outHeight, tileOutWidth * outChansPerGroup});
        if (stride == 1 && tileOutWidth == outDimX && inWidth == inDimX) {
          // If input rows are contiguous we can flatten the x and y dimensions,
          // reducing the number of in edge pointers.
          inWindow =
              inWindow.reshape({inZGroups, inHeight * inWidth *
                                inChansPerGroup});
          outWindow =
              outWindow.reshape({1, outHeight * tileOutWidth *
                                 outChansPerGroup});
        }
        // Add the vertex.
        const char *baseClass =
            targetSharedConvWeights() ? "poplar::SupervisorVertex" :
                                        "poplar::Vertex";
        auto v = graph.addVertex(
          fwdCS,
          templateVertex("ConvPartial1x1Out", baseClass),
          {{"weights", w}, {"out", outWindow}}
        );
        if (stride == 1) {
          graph.connect(v["in"], inWindow);
        } else {
          for (unsigned i = 0; i != inZGroups; ++i) {
            for (unsigned j = 0; j != outHeight; ++j) {
              graph.connect(v["in"][j + i * outHeight],
                           inWindow[stride * j + i * inHeight]);
            }
          }
          graph.setFieldSize(v["in"], inZGroups * outHeight);
        }
        // Map the vertex and output.
        if (mapping) {
          mapping->setMapping(v, tile);
          mapping->setMapping(outWindow, tile);
        }
      }
    }
  } else if (useConvolutionInstruction()) {
    // Zero the partial sums.
    Tensor tileOut =
        out.slice(
          {tileOutZGroupBegin, tileOutYBegin, tileOutXBegin, 0},
          {tileOutZGroupEnd, tileOutYEnd, tileOutXEnd, outChansPerGroup}
        );
    const auto outZGroups = tileOutZGroupEnd - tileOutZGroupBegin;
    Tensor tileOutFlattened =
        tileOut.reshape({outZGroups * tileOutHeight,
                         tileOutWidth * outChansPerGroup});
    const auto numWorkerContexts = getWorkerContextsPerTile();
    const auto tileOutRows = tileOutFlattened.dim(0);
    for (unsigned i = 0; i != numWorkerContexts; ++i) {
      const auto beginRow = (i * tileOutRows) / numWorkerContexts;
      const auto endRow = ((i + 1) * tileOutRows) / numWorkerContexts;
      if (beginRow == endRow)
        continue;
      auto zv = graph.addVertex(
        zeroCS, templateVertex("Zero2D", "float"),
        {{"out", tileOutFlattened.slice(beginRow, endRow)}}
      );
      if (mapping) {
        mapping->setMapping(zv, tile);
      }
    }
    for (unsigned ozg = tileOutZGroupBegin; ozg != tileOutZGroupEnd; ++ozg) {
      for (unsigned vy = 0; vy != verticesPerY; ++vy) {
        const auto outYBegin =
            tileOutYBegin + (vy * tileOutHeight) / verticesPerY;
        const auto outYEnd =
            tileOutYBegin + ((vy + 1) * tileOutHeight) / verticesPerY;
        const auto outHeight = outYEnd - outYBegin;
        if (outHeight == 0)
          continue;
        createConvPartial1x1InOutVertex(graph, mapping, tile,
                                        tileOutXBegin, tileOutXEnd,
                                        outYBegin, outYEnd,
                                        ozg,
                                        tileInZGroupBegin, tileInZGroupEnd,
                                        fwdCS, out);
      }
    }
  } else {
    const auto inZGroups = tileInZGroupEnd - tileInZGroupBegin;
    for (unsigned ozg = tileOutZGroupBegin; ozg != tileOutZGroupEnd; ++ozg) {
      assert(outChansPerGroup == 1);
      const auto z = ozg;
      for (unsigned vy = 0; vy != verticesPerY; ++vy) {
        const auto outYBegin =
            tileOutYBegin + (vy * tileOutHeight) / verticesPerY;
        const auto outYEnd =
            tileOutYBegin + ((vy + 1) * tileOutHeight) / verticesPerY;
        if (outYBegin == outYEnd)
          continue;
        assert(outYEnd - outYBegin == 1);
        const auto y = outYBegin;
        unsigned inYBegin, inYEnd, inXBegin, inXEnd;
        std::tie(inYBegin, inYEnd) =
            getInputRange(y, stride, kernelSize, inDimY);
        std::tie(inXBegin, inXEnd) =
            getInputRange({tileOutXBegin, tileOutXEnd}, stride, kernelSize,
                          inDimX);
        // Window into previous layer.
        const auto inWidth = inXEnd - inXBegin;
        const auto inHeight = inYEnd - inYBegin;
        // Weights that match the window.
        unsigned weightYBegin, weightYEnd;
        std::tie(weightYBegin, weightYEnd) =
          getWeightRange(y, stride, kernelSize, inDimY);
        Tensor inWindow =
            in.slice(
              {tileInZGroupBegin, inYBegin, inXBegin, 0},
              {tileInZGroupEnd, inYEnd, inXEnd, inChansPerGroup}
            ).reshape({inHeight * inZGroups,
                       inWidth * inChansPerGroup});
        Tensor w =
            weightsIn.slice(
              {tileInZGroupBegin, z, weightYBegin, 0, 0},
              {tileInZGroupEnd, z + 1, weightYEnd, kernelSize, inChansPerGroup}
            ).reshape({inHeight * inZGroups,
                       inChansPerGroup * kernelSize});
        Tensor outWindow = out[z][y].slice(tileOutXBegin, tileOutXEnd).flatten();
        // Add the vertex.
        auto v = graph.addVertex(fwdCS,
                                 templateVertex("ConvPartial", getDType()),
            { {"in", inWindow },
              {"weights", w },
              {"out", outWindow },
            });
        const auto padding =
            inXBegin + (kernelSize - 1) / 2 - tileOutXBegin * stride;
        graph.setInitialValue(v["stride"], stride);
        graph.setInitialValue(v["inChansPerGroup"], inChansPerGroup);
        graph.setInitialValue(v["padding"], padding);
        // Map the vertex and output.
        if (mapping) {
          mapping->setMapping(v, tile);
          mapping->setMapping(outWindow, tile);
        }
      }
    }
  }
}

/// Convert a set of indices over the different dimensions of the partition
/// into a tile number.
static unsigned
linearizeTileIndices(unsigned izg, unsigned ox, unsigned oy,
                     unsigned ozg,
                     const ConvLayerPartition &partition,
                     bool isMultiIPU) {
  const auto tilesPerX = partition.tilesPerXAxis;
  const auto tilesPerY = partition.tilesPerYAxis;
  const auto tilesPerZ = partition.tilesPerZAxis;
  const auto tilesPerInZGroup = partition.tilesPerInZGroupAxis;

  // If this is a multi IPU system then choose an order that avoids splitting
  // partial sums over IPUs
  if (isMultiIPU)
    return izg + tilesPerInZGroup *
             (ox + tilesPerX *
               (oy + tilesPerY *
                 ozg));
  // For single IPU systems this order appears to give the best results.
  // TODO understand why this is. Intuitively I'd expect the an ordering
  // that matches the input tensor, i.e. (izg, iy, ix, iz) to result in
  // less exchange.
  return ox + tilesPerX *
           (oy + tilesPerY *
             (ozg + tilesPerZ *
               izg));
}

void ConvLayerImpl::mapWeights(Graph &graph,
                               IPUModelEngineBuilder::TileMapping *mapping,
                               Tensor w) {
  const auto isMultiIPU = getNumIPUs() > 1;
  const auto inChansPerGroup = partition.inChansPerGroup;
  const auto partialChansPerGroup = partition.partialChansPerGroup;
  const auto tilesPerX = partition.tilesPerXAxis;
  const auto tilesPerY = partition.tilesPerYAxis;
  const auto tilesPerZ = partition.tilesPerZAxis;
  const auto tilesPerInZGroup = partition.tilesPerInZGroupAxis;
  const auto numInZGroups = inNumChans / inChansPerGroup;
  assert(outNumChans % partialChansPerGroup == 0);
  const auto partialNumChanGroups = outNumChans / partialChansPerGroup;

  if (mapping) {
    for (unsigned izg = 0; izg != tilesPerInZGroup; ++izg) {
      const auto inZGroupBegin = (izg * numInZGroups) / tilesPerInZGroup;
      const auto inZGroupEnd = ((izg + 1) * numInZGroups) / tilesPerInZGroup;
      const auto numInZGroups = inZGroupEnd - inZGroupBegin;
      for (unsigned ozg = 0; ozg != tilesPerZ; ++ozg) {
        const auto outZGroupBegin =
            (ozg * partialNumChanGroups) / tilesPerZ;
        const auto outZGroupEnd =
            ((ozg + 1) * partialNumChanGroups) / tilesPerZ;
        const auto numOutZGroups = outZGroupEnd - outZGroupBegin;
        // Group weights that are accessed contiguously by tiles within this
        // loop body.
        Tensor sharedWeights;
        if (useConvolutionInstruction()) {
          if (kernelSize == 1) {
            sharedWeights =
                w.slice(
                  {outZGroupBegin, inZGroupBegin, 0, 0, 0, 0},
                  {outZGroupEnd, inZGroupEnd, kernelSize, kernelSize,
                   partialChansPerGroup, inChansPerGroup}
                ).reshape({numOutZGroups,
                           numInZGroups * partialChansPerGroup *
                           inChansPerGroup});
          } else {
            sharedWeights =
                w.slice(
                  {outZGroupBegin, inZGroupBegin, 0, 0, 0, 0},
                  {outZGroupEnd, inZGroupEnd, kernelSize, kernelSize,
                   partialChansPerGroup, inChansPerGroup}
                ).reshape({numOutZGroups * numInZGroups * kernelSize *
                           kernelSize,
                           partialChansPerGroup * inChansPerGroup});
          }
        } else {
          sharedWeights =
              w.slice(
                {inZGroupBegin, outZGroupBegin, 0, 0, 0},
                {inZGroupEnd, outZGroupEnd, kernelSize, kernelSize,
                 inChansPerGroup}
              ).reshape({numInZGroups * numOutZGroups * kernelSize,
                         kernelSize * inChansPerGroup});
        }
        const auto numSharedWeightGroups = sharedWeights.dim(0);
        // Spread groups of weights equally across the tiles that read them.
        for (unsigned oy = 0; oy != tilesPerY; ++oy) {
          for (unsigned ox = 0; ox != tilesPerX; ++ox) {
            const auto iw = ox + tilesPerX * oy;
            const auto sharedWeightGroupBegin =
                (iw * numSharedWeightGroups) / (tilesPerY * tilesPerX);
            const auto sharedWeightGroupEnd =
                ((iw + 1) * numSharedWeightGroups) / (tilesPerY * tilesPerX);
            const auto tileWeights =
                sharedWeights.slice(sharedWeightGroupBegin,
                                    sharedWeightGroupEnd);
            const auto tile = linearizeTileIndices(izg, ox, oy, ozg, partition,
                                                   isMultiIPU);
            mapping->setMapping(tileWeights, tile);
          }
        }
      }
    }
  }
}

void ConvLayerImpl::
createFwdProg(Graph &graph, IPUModelEngineBuilder::TileMapping *mapping)  {
  assert(!createdForwardProg);
  const auto isMultiIPU = getNumIPUs() > 1;

  const auto inChansPerGroup = partition.inChansPerGroup;
  const auto partialChansPerGroup = partition.partialChansPerGroup;
  assert(outNumChans % partialChansPerGroup == 0);
  const auto partialNumChanGroups = outNumChans / partialChansPerGroup;

  const auto tilesPerX = partition.tilesPerXAxis;
  const auto tilesPerY = partition.tilesPerYAxis;
  const auto tilesPerZ = partition.tilesPerZAxis;
  const auto tilesPerInZGroup = partition.tilesPerInZGroupAxis;

  assert(inNumChans % inChansPerGroup == 0);
  const auto numInZGroups = inNumChans / inChansPerGroup;
  Tensor partials = graph.addTensor("float",
                                    {tilesPerInZGroup,
                                     partialNumChanGroups,
                                     outDimY,
                                     outDimX,
                                     partialChansPerGroup});
  ComputeSet zeroCS;
  if (useConvolutionInstruction() && kernelSize != 1) {
    zeroCS = graph.createComputeSet(layerName + ".zero");
    forwardProg.add(Execute(zeroCS));
  }
  ComputeSet fwdCS = graph.createComputeSet(layerName + ".fwd");
  forwardProg.add(Execute(fwdCS));
  for (unsigned izg = 0; izg != tilesPerInZGroup; ++izg) {
    const auto inZGroupBegin = (izg * numInZGroups) / tilesPerInZGroup;
    const auto inZGroupEnd = ((izg + 1) * numInZGroups) / tilesPerInZGroup;
    for (unsigned ozg = 0; ozg != tilesPerZ; ++ozg) {
      const auto outZGroupBegin = (ozg * partialNumChanGroups) / tilesPerZ;
      const auto outZGroupEnd = ((ozg + 1) * partialNumChanGroups) / tilesPerZ;
      for (unsigned oy = 0; oy != tilesPerY; ++oy) {
        const auto outYBegin = (oy * outDimY) / tilesPerY;
        const auto outYEnd = ((oy + 1) * outDimY) / tilesPerY;
        for (unsigned ox = 0; ox != tilesPerX; ++ox) {
          const auto outXBegin = (ox * outDimX) / tilesPerX;
          const auto outXEnd = ((ox + 1) * outDimX) / tilesPerX;
          const auto tile = linearizeTileIndices(izg, ox, oy, ozg, partition,
                                                 isMultiIPU);
          forwardTile(graph, mapping,
                      tile, outXBegin, outXEnd, outYBegin, outYEnd,
                      outZGroupBegin, outZGroupEnd, inZGroupBegin, inZGroupEnd,
                      zeroCS, fwdCS, partials[izg]);
        }
      }
    }
  }
  mapWeights(graph, mapping, weightsIn);
  Tensor reduced;
  ComputeSet reduceCS = graph.createComputeSet(layerName + ".fwd.reduce");
  bool executeReduceCS = false;
  if (resLayer) {
    addResidualCalc(graph, reduceCS, mapping);
    executeReduceCS = true;
  }
  if (tilesPerInZGroup == 1) {
    reduced = partials[0];
  } else {
    // Accumulate the partial sums.
    const auto numTiles = getNumIPUs() * getTilesPerIPU();
    reduced = graph.addTensor("float", {partialNumChanGroups, outDimY, outDimX,
                                        partialChansPerGroup});
    size_t outChansPerGroup = outNumChans / outNumChanGroups;
    if (outChansPerGroup % partialChansPerGroup == 0) {
      const auto partialGroupsPerOutGroup =
          outChansPerGroup / partialChansPerGroup;
      const auto numOutGroups = activations.numElements() / outChansPerGroup;
      for (unsigned tile = 0; tile != numTiles; ++tile) {
        const auto groupBegin = (tile * numOutGroups) / numTiles;
        const auto groupEnd = ((tile + 1) * numOutGroups) / numTiles;
        if (groupBegin == groupEnd)
          continue;
        const auto rowBegin = groupBegin / outDimX;
        const auto rowEnd = (groupEnd + outDimX - 1) / outDimX;
        for (unsigned row = rowBegin; row != rowEnd; ++row) {
          const auto xBegin = row == rowBegin ? groupBegin - row * outDimX : 0;
          const auto xEnd = row + 1 == rowEnd ? groupEnd - row * outDimX :
                                                outDimX;
          assert(xBegin != xEnd);
          const auto outChanGroup = row / outDimY;
          const auto y = row % outDimY;
          for (unsigned i = 0; i != partialGroupsPerOutGroup; ++i) {
            const auto zg = outChanGroup * partialGroupsPerOutGroup + i;
            Tensor in =
                partials.slice({0, zg, y, xBegin, 0},
                               {tilesPerInZGroup, zg + 1, y + 1, xEnd,
                                partialChansPerGroup}
                ).reshape({tilesPerInZGroup,
                           (xEnd - xBegin) * partialChansPerGroup});
            Tensor out = reduced[zg][y].slice(xBegin, xEnd).flatten();
            const auto v = graph.addVertex(reduceCS, "ConvReduce",
                                           {{"out", out},
                                           {"partials", in}});
            if (mapping) {
              mapping->setMapping(v, tile);
              mapping->setMapping(out, tile);
            }
          }
        }
      }
    } else {
      for (unsigned z = 0; z != partialNumChanGroups; ++z) {
        for (unsigned y = 0; y != outDimY; ++y) {
          Tensor in =
              partials.slice({0, z, y, 0, 0},
                             {tilesPerInZGroup, z + 1, y + 1, outDimX,
                              partialChansPerGroup}
              ).reshape({tilesPerInZGroup, outDimX * partialChansPerGroup});
          Tensor out = reduced[z][y].flatten();
          const auto v = graph.addVertex(reduceCS, "ConvReduce",
                                         {{"out", out},
                                         {"partials", in}});
          if (mapping) {
            const auto tile =
                (numTiles * (outDimY * z + y)) / (outDimY * outNumChans);
            mapping->setMapping(v, tile);
            mapping->setMapping(out, tile);
          }
        }
      }
    }
    executeReduceCS = true;
  }
  if (executeReduceCS) {
    forwardProg.add(Execute(reduceCS));
  }

  // Apply the non linearity and write back results in the layout desired by
  // the next layer. Each vertex handles outChansPerGroup output elements.
  // TODO: This step could be merged with the reduction step above.
  ComputeSet completionCS =
     graph.createComputeSet(layerName + ".fwd.complete");
  size_t outChansPerGroup = outNumChans / outNumChanGroups;
  Tensor biasesByChanGroup =
      biasesIn.reshape({outNumChanGroups, outChansPerGroup});

  unsigned partialChanChunkSize =
    gcd<unsigned>(outChansPerGroup, partialChansPerGroup);
  for (unsigned outChanGroup = 0; outChanGroup != outNumChanGroups;
       ++outChanGroup) {
    for (unsigned y = 0; y != outDimY; ++y) {
      for (unsigned x = 0; x != outDimX; ++x) {
        Tensor actOut = activations[outChanGroup][y][x];
        Tensor biasSlice = biasesByChanGroup[outChanGroup];
        Tensor reducedChans = reduced.slice(
          {0, y, x, 0},
          {partialNumChanGroups, y + 1, x + 1, partialChansPerGroup}
        ).flatten();
        Tensor reducedByChanGroup =
            reducedChans.reshape({outNumChanGroups,
                                  outChansPerGroup / partialChanChunkSize,
                                  partialChanChunkSize});
        Tensor in =
            reducedByChanGroup[outChanGroup];
        auto resOutChanGroups = resLayer ? residual.dim(0) : 0;
        bool needsResidual = resLayer && outChanGroup < resOutChanGroups;
        std::string vertexType =
            needsResidual ? "ConvCompleteRes" : "ConvComplete";
        auto v = graph.addVertex(completionCS,
                                 templateVertex(vertexType, getDType()),
                                 {{ "in", in },
                                  { "bias", biasSlice },
                                  { "out", actOut} });
        graph.setInitialValue(v["nonLinearityType"], nonLinearityType);
        if (needsResidual) {
          // If the residual is taken directly from the previous layer (
          // as opposed to being zero-padded or converted), then striding over
          // the X,Y plane may still be needed (in this case resStride will not
          // be 1).
          Tensor res = residual[outChanGroup][y * resStrideY][x * resStrideX];
          graph.connect(res, v["res"]);
        }
      }
    }
  }
  mapComputeSet(graph, completionCS, mapping);
  mapActivations(activations, mapping);
  forwardProg.add(Execute(completionCS));
  createdForwardProg = true;
}

Program ConvLayerImpl::
forward(Graph &graph, IPUModelEngineBuilder::TileMapping *mapping)  {
  Layer *prev = getPrevLayer();
  auto impl = reuseImpl ? reuseImpl : this;
  auto prog = Sequence();
  prog.add(Copy(impl->getInputTensor(), prev->getFwdActivations()));
  impl->mapWeights(graph, mapping, weights);
  prog.add(Copy(impl->getInputWeights(), weights));
  prog.add(Copy(impl->getInputBiases(), biases));
  if (resLayer) {
    prog.add(Copy(impl->getInputResidual(),
                  resLayer->getFwdActivations()));
  }
  prog.add(impl->getOrCreateFwdProg(graph, mapping));
  return prog;
}

