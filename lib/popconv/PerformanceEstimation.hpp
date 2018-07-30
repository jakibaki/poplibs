#ifndef _performance_estimation_h_
#define _performance_estimation_h_

#include <limits>
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <numeric>
#include <vector>

inline std::uint64_t
getDenseDotProductCycles(bool isFloat, unsigned size, unsigned dataPathWidth) {
  if (isFloat) {
    const auto floatVectorWidth = dataPathWidth / 32;
    return (size + floatVectorWidth - 1) / floatVectorWidth + 2;
  }
  const auto halfVectorWidth = dataPathWidth / 16;
  return (size + halfVectorWidth - 1) / halfVectorWidth + 2;
}


template <class InputIterator>
bool allEqual(InputIterator begin, InputIterator end) {
  if (begin == end)
    return true;
  const auto &first = *begin;
  for (auto it = begin + 1; it != end; ++it) {
    if (*it != first)
      return false;
  }
  return true;
}

inline std::uint64_t
getConvPartialHorizontalMacCycleEstimate(
    bool isFloat,
    unsigned numChans,
    const std::vector<unsigned> &convSizes,
    unsigned dataPathWidth) {
  uint64_t cycles = 0;
  for (auto convSize : convSizes) {
    // 3 to load in offset, out offset and width,
    // 3 to setup pointers + tapack
    // 1 additional cycles for pipeline
    cycles += 7 + convSize * getDenseDotProductCycles(isFloat, numChans,
                                                      dataPathWidth);
  }
  return cycles;
}

inline std::uint64_t
getZeroSupervisorVertexCycleEstimate(const std::vector<unsigned> &worklist,
                                     unsigned numGroups,
                                     unsigned dataPathWidth,
                                     unsigned numWorkerContexts,
                                     bool isFloat) {
  const unsigned vectorWidth = dataPathWidth / (isFloat ? 32 : 16);
  std::uint64_t maxWorkerCyclesZero = 0;
  for (unsigned context = 0; context != worklist.size(); ++context) {
    uint64_t numVectors = (worklist[context] + vectorWidth - 1) / vectorWidth;
    maxWorkerCyclesZero = std::max(maxWorkerCyclesZero, numVectors + 4);
  }
  uint64_t zeroCycles = ((maxWorkerCyclesZero * numGroups) *
                         numWorkerContexts + 12);
  return zeroCycles;
}

inline std::uint64_t
getConvPartialHorizontalMacSupervisorInnerLoopCycleEstimate(
    const std::vector<std::vector<std::vector<unsigned>>> &workerPartitions,
    unsigned kernelSize,
    unsigned numInChansPerGroup,
    unsigned dataPathWidth,
    unsigned numWorkerContexts,
    bool isFloat) {
  unsigned usedContexts = workerPartitions.size();
  uint64_t cycles = 0;
  uint64_t maxWorkerCycles = 0;
  uint64_t minWorkerCycles = usedContexts < numWorkerContexts ?
                               0 : std::numeric_limits<uint64_t>::max();
  for (auto context = 0U; context != usedContexts; ++context) {
    uint64_t thisWorkerCycles = 0;
    for (auto k = 0U; k != kernelSize; ++k) {
      thisWorkerCycles +=
        getConvPartialHorizontalMacCycleEstimate(isFloat, numInChansPerGroup,
                                                 workerPartitions[context][k],
                                                 dataPathWidth);
      // to load partition with post increment and branch
      // + additional cycles to create pointer to worklist
      thisWorkerCycles += 2 + 2;
    }
    const unsigned workerNonLoopOverhead = 6;
    thisWorkerCycles += workerNonLoopOverhead;
    maxWorkerCycles =
        std::max(maxWorkerCycles, numWorkerContexts * thisWorkerCycles);
    minWorkerCycles =
        std::min(minWorkerCycles, numWorkerContexts * thisWorkerCycles);
  }
  cycles += std::max(maxWorkerCycles, minWorkerCycles + 11);
  return cycles;
}

inline std::uint64_t
getConvPartialHorizontalMacSupervisorOuterLoopCycleEstimate(
    std::uint64_t innerLoopCycles,
    unsigned numConvGroups,
    unsigned numInGroups,
    unsigned numOutGroups) {
  uint64_t cycles = innerLoopCycles;
  return 6  + numConvGroups
            * (16 + numOutGroups
                * (8 + numInGroups
                   * (2 + cycles)));
}

inline std::uint64_t
getConvPartialHorizontalMacSupervisorCycleEstimate(
    const std::vector<std::vector<std::vector<unsigned>>> &workerPartitions,
    unsigned numConvGroups,
    unsigned numInGroups,
    unsigned numOutGroups,
    unsigned kernelSize,
    unsigned numInChansPerGroup,
    unsigned dataPathWidth,
    unsigned numWorkerContexts,
    bool isFloat) {
  // 8 cycles to extract from vectorlist::deltan
  auto cycles = 8 +
      getConvPartialHorizontalMacSupervisorInnerLoopCycleEstimate(
        workerPartitions, kernelSize, numInChansPerGroup, dataPathWidth,
        numWorkerContexts, isFloat);
  return getConvPartialHorizontalMacSupervisorOuterLoopCycleEstimate(
        cycles, numConvGroups, numInGroups, numOutGroups);
}

inline std::uint64_t
getConvPartial1x1SupervisorInnerLoopCycleEstimate(
    const std::vector<std::vector<unsigned>> &workerPartitions,
    unsigned numWorkerContexts, bool outputZeroing) {
  unsigned usedContexts = workerPartitions.size();
  uint64_t maxWorkerCycles = 0;
  uint64_t minWorkerCycles = usedContexts < numWorkerContexts ?
                             0 : std::numeric_limits<uint64_t>::max();
  // TODO: These estimates are incorrect for float inputs
  for (const auto &worker : workerPartitions) {
    // fixed overhead for loading pointers worklist pointers and dividing
    // partitions by 3
    uint64_t thisWorkerCycles = 14;
    for (auto wi : worker) {
      const auto numElems =  wi;
      switch (numElems) {
        case 0:
          thisWorkerCycles += 7;
          break;
        case 1:
          thisWorkerCycles += 36 + (2 + 4) * outputZeroing;
          break;
        case 2:
          thisWorkerCycles += 44 + (2 + 4 * 2) * outputZeroing;
          break;
        default:
          thisWorkerCycles += 49 + (2 + 4 * numElems) * outputZeroing +
                                   (numElems - 3) * 4;
      }
    }
    maxWorkerCycles =
      std::max(maxWorkerCycles, numWorkerContexts * thisWorkerCycles);
    minWorkerCycles =
      std::min(minWorkerCycles, numWorkerContexts * thisWorkerCycles);
  }

  // tag cost to worker with min cycles
  maxWorkerCycles = std::max(maxWorkerCycles, minWorkerCycles + 14);

  return maxWorkerCycles;
}

inline std::uint64_t
getConvPartial1x1SupervisorOuterLoopCycleEstimate(
    std::uint64_t innerLoopCyclesWithZeroing,
    std::uint64_t innerLoopCyclesWithoutZeroing,
    unsigned numConvGroups,
    unsigned numInGroups,
    unsigned numOutGroups,
    unsigned outChansPerGroup,
    unsigned convUnitInputLoadElemsPerCycle,
    unsigned numConvUnitsPerTile,
    unsigned convUnitCoeffLoadBytesPerCycle,
    bool floatWeights) {
  const auto outputPassesPerGroup =
      (outChansPerGroup + numConvUnitsPerTile - 1) / numConvUnitsPerTile;

  const auto numInputLoadsInnerLoop = 4;
  const auto numLoads = convUnitInputLoadElemsPerCycle * numInputLoadsInnerLoop
                          * numConvUnitsPerTile
                          * (floatWeights ? 4 : 2)
                          / convUnitCoeffLoadBytesPerCycle;
  const uint64_t supervisorNonloopOverhead = 80;
  return supervisorNonloopOverhead + numConvGroups
           * (13 + (numInGroups - 1)
              * (13 + numOutGroups
                 * (11 + outputPassesPerGroup
                   * (6 + numLoads + innerLoopCyclesWithoutZeroing))) +
                (13 + numOutGroups
                 * (11 + outputPassesPerGroup
                   * (6 + numLoads + innerLoopCyclesWithZeroing))));
}

inline std::uint64_t
getConvPartial1x1SupervisorCycleEstimate(
    const std::vector<std::vector<unsigned>> &workerPartitions,
    unsigned numConvGroups,
    unsigned numInGroups,
    unsigned numOutGroups,
    unsigned outChansPerGroup,
    unsigned convUnitInputLoadElemsPerCycle,
    unsigned numConvUnitsPerTile,
    unsigned convUnitCoeffLoadBytesPerCycle,
    unsigned numWorkerContexts,
    bool floatWeights) {
  auto innerLoopCyclesWithZeroing =
      getConvPartial1x1SupervisorInnerLoopCycleEstimate(workerPartitions,
                                                        numWorkerContexts,
                                                        true);
  auto innerLoopCyclesWithoutZeroing =
      getConvPartial1x1SupervisorInnerLoopCycleEstimate(workerPartitions,
                                                        numWorkerContexts,
                                                        false);
  return getConvPartial1x1SupervisorOuterLoopCycleEstimate(
            innerLoopCyclesWithZeroing, innerLoopCyclesWithoutZeroing,
            numConvGroups, numInGroups, numOutGroups,
            outChansPerGroup, convUnitInputLoadElemsPerCycle,
            numConvUnitsPerTile, convUnitCoeffLoadBytesPerCycle, floatWeights);
}

inline std::uint64_t
getConvPartialnx1SupervisorCycleOuterLoopEstimate(
    std::uint64_t innerLoopCycles,
    unsigned numConvGroups,
    unsigned numOutGroups,
    unsigned numInGroups,
    unsigned outChansPerGroup,
    unsigned numConvUnitsPerTile) {
  uint64_t cycles = innerLoopCycles;
  return 93 + numConvGroups
             * (15 + numOutGroups
              * (16 + numInGroups
                * (16 + cycles)));
}

inline std::uint64_t
getConvPartialnx1SupervisorCycleInnerLoopEstimate(
    const std::vector<std::vector<std::vector<unsigned>>> &workerPartitions,
    unsigned kernelInnerElems,
    unsigned kernelOuterElems,
    unsigned filterHeight,
    unsigned outChansPerGroup,
    unsigned convUnitInputLoadElemsPerCycle,
    unsigned numConvUnitsPerTile,
    unsigned convUnitCoeffLoadBytesPerCycle,
    unsigned numWorkerContexts,
    bool floatWeights) {
  unsigned usedContexts = workerPartitions.size();
  unsigned numOutChanPasses = outChansPerGroup / numConvUnitsPerTile;
  // TODO: Update for float input when assembler code is written
  if (filterHeight == 4 &&  convUnitCoeffLoadBytesPerCycle >= 8)
    convUnitCoeffLoadBytesPerCycle = 8;
  const auto numInputLoadsInnerLoop = 4;
  const auto numLoads = convUnitInputLoadElemsPerCycle * numInputLoadsInnerLoop
                        * numConvUnitsPerTile
                        * (floatWeights ? 4 : 2)
                        / convUnitCoeffLoadBytesPerCycle;
  uint64_t innermostLoopCycles = numLoads;

  // additional load cycles dependent on filterHeight
  switch (filterHeight) {
    case 4:
      innermostLoopCycles += 54;
      break;
    case 2:
      innermostLoopCycles += 40;
      break;
    case 1:
      innermostLoopCycles += 20;
      break;
    default:
      // non-limited version will pick this up and we don't estimate unlimited
      // version correctly
      innermostLoopCycles += 20 * filterHeight;
  }
  uint64_t innerLoopCycles = 0;
  for (auto ky = 0U; ky != kernelOuterElems; ++ky) {
    innerLoopCycles += 15;
    for (auto kx = 0U; kx != kernelInnerElems; ++kx) {
      // load coefficients
      innerLoopCycles += 18 + innermostLoopCycles * numOutChanPasses;
      uint64_t maxWorkerCycles = 0;
      uint64_t minWorkerCycles = usedContexts < numWorkerContexts ?
                                 0 : std::numeric_limits<uint64_t>::max();
      for (auto context = 0U; context != usedContexts; ++context) {
        uint64_t thisWorkerCycles = 19;
        const auto k = ky * kernelInnerElems + kx;
        for (auto &numElems :  workerPartitions[context][k]) {
          switch (numElems) {
          case 0:
              thisWorkerCycles += 2;
              break;
          case 1:
              thisWorkerCycles += 28;
              break;
          case 2:
              thisWorkerCycles += 35;
              break;
          default:
              thisWorkerCycles += 39 + (numElems - 3) * 4;

          }
        }
        maxWorkerCycles =
          std::max(maxWorkerCycles, numWorkerContexts * thisWorkerCycles);
        minWorkerCycles =
          std::min(minWorkerCycles, numWorkerContexts * thisWorkerCycles);
      }
      innerLoopCycles += std::max(maxWorkerCycles, minWorkerCycles + 9);
    }
  }
  return innerLoopCycles;
}

inline std::uint64_t
getConvPartialnx1SupervisorCycleEstimate(
    const std::vector<std::vector<std::vector<unsigned>>> &workerPartitions,
    unsigned numConvGroups,
    unsigned numOutGroups,
    unsigned numInGroups,
    unsigned kernelInnerElems,
    unsigned kernelOuterElems,
    unsigned filterHeight,
    unsigned inChansPerGroup,
    unsigned outChansPerGroup,
    unsigned convUnitInputLoadElemsPerCycle,
    unsigned numConvUnitsPerTile,
    unsigned convUnitCoeffLoadBytesPerCycle,
    unsigned numWorkerContexts,
    bool floatWeights) {
  auto innerLoopCycles =
      getConvPartialnx1SupervisorCycleInnerLoopEstimate(
        workerPartitions, kernelInnerElems, kernelOuterElems, filterHeight,
        outChansPerGroup, convUnitInputLoadElemsPerCycle, numConvUnitsPerTile,
        convUnitCoeffLoadBytesPerCycle, numWorkerContexts, floatWeights);
  return getConvPartialnx1SupervisorCycleOuterLoopEstimate(
           innerLoopCycles,
           numConvGroups,
           numOutGroups,
           numInGroups,
           outChansPerGroup,
           numConvUnitsPerTile);
}

inline std::uint64_t
getMatMul1PartialCycleEstimate(bool isFloat, unsigned size,
                               unsigned dataPathWidth) {
  return 5 + getDenseDotProductCycles(isFloat, size, dataPathWidth);
}

inline std::uint64_t
getMatMul2CycleEstimate(unsigned size) {
  // Inner loop is dominated by loads (load pointer, load 64bits, load 16
  // bits). This could be improved if we uses strided loads instead of
  // pointers.
  return 5 + size * 3;
}

inline uint64_t getWgdDataTransformCycles(
                              unsigned numChannels,
                              bool isFloat) {
  unsigned chansPerOp = isFloat ? 2 : 4;
  return 13 + 56 * ((numChannels + chansPerOp - 1)/chansPerOp);
}


inline uint64_t getWgdKernelTransformCycles(
                              unsigned numChannels,
                              bool isFloat) {
  unsigned chansPerOp = isFloat ? 2 : 4;
  return 2 + 35 * ((numChannels + chansPerOp - 1)/chansPerOp);
}

inline uint64_t getWgdInvTransformCycles(
                              unsigned numChannels,
                              bool isFloat) {
  unsigned chansPerOp = isFloat ? 2 : 4;
  return 15 + 30 * ((numChannels + chansPerOp - 1)/chansPerOp);
}

/**
 * The accumulator operates on pencils which are of depth "pencilDepth".
 * An inner product of a coefficient vector and data vector is computed.
 * "comPencils" gives the number of pencils which share a common coefficient
 * vector. "numPencils" gives a set of pencils which share common coefficients
 */
inline uint64_t getWgdAccumCycles(
                             unsigned numPencils,
                             unsigned comPencils,
                             unsigned pencilDepth,
                             unsigned outDepth,
                             unsigned numWorkers,
                             unsigned numConvUnits,
                             unsigned weightsPerConvUnit,
                             unsigned convUnitCoeffLoadBytesPerCycle,
                             bool isFloat) {

  unsigned numCoeffSets = (outDepth + numConvUnits - 1)/numConvUnits;
  numCoeffSets *= (pencilDepth + weightsPerConvUnit - 1)/weightsPerConvUnit;
  numCoeffSets *= numPencils;
  const auto coeffLoadCycles = numConvUnits * weightsPerConvUnit
                          * (isFloat ? 2 : 4) / convUnitCoeffLoadBytesPerCycle;
  const auto overhead = 4;

  const auto numPencilsPerWorker = (comPencils + numWorkers - 1) / numWorkers;
  return (overhead + coeffLoadCycles + numPencilsPerWorker
          * numWorkers * 4) * numCoeffSets;
}

inline uint64_t getWgdReduceCycles(unsigned numPencils, unsigned depth,
                          bool isFloat) {
  unsigned chansPerOp = isFloat ? 2 : 4;
  return 5 + ((numPencils * depth + chansPerOp - 1)/chansPerOp);
}


inline uint64_t getWgdCompleteCycles(
                            unsigned numChannels,
                            bool isFloat) {
  unsigned divFactor = isFloat ? 2 : 4;

  return 5 + numChannels/divFactor;
}

inline std::uint64_t
getOuterProductCycleEstimate(bool isFloat,
                             unsigned width, unsigned numChannels,
                             unsigned chansPerGroup,
                             unsigned dataPathWidth) {
  assert(numChannels % chansPerGroup == 0);
  const auto numChanGroups = numChannels / chansPerGroup;
  const auto elementWidth = isFloat ? 32 : 16;
  auto vectorsPerGroup =
      (chansPerGroup * elementWidth + dataPathWidth - 1) / dataPathWidth;
  // Taken from conv_outer_product_f16 microbenchmark.
  std::uint64_t cycles =
      9 + numChanGroups * (8 + width * (1 + vectorsPerGroup));
  return cycles;
}

inline uint64_t
getReduceCycleEstimate(const std::vector<unsigned> &outSizes,
                       unsigned partialsSize,
                       unsigned dataPathWidth,
                       bool isUpdate, bool isScale,
                       bool isPartialsFloat,
                       bool isOutTypeFloat) {
  unsigned vectorWidth = dataPathWidth / (isPartialsFloat ? 32 : 16);
  bool conversionCyles = isPartialsFloat != isOutTypeFloat;
  unsigned cycles;
  const unsigned numReductions = outSizes.size();
  const unsigned numPartials = partialsSize / numReductions;
  const unsigned version=1;
  unsigned addCycles = 0;
  if (isUpdate) {
    addCycles = 2;
  }
  if (isScale) {
    addCycles = 1;
  }
  switch (version) {
  case 0: // Original optimistic estimate
  default:
    cycles = 4;
    for (unsigned r = 0; r < numReductions; ++r) {
      unsigned numElem = outSizes[r];
      auto numVectors = (numElem + vectorWidth - 1) / vectorWidth;
      cycles += 1 + numPartials * (1 + numVectors)
                + conversionCyles * numVectors;
    }
    break;
  case 1:
    // Innermost loop accumulates vector across all input tiles
    // This estimate based on float->float code
    // Inner loop processes 128bits/3 cycles (1 for masking the deltaN)
    // Inner loop cycles would halve for strided data given f32v4add IS addtion
    cycles = 5+1;
    // VectorList costs 7 or 9 cycles to load n+base+descriptorPtr.
    // These vertices have two VectorList::DELTAN so we'll have one of each and
    // save a cycle (basemem only created once)
    cycles += 7 + 8 - 1;
    for (unsigned r = 0; r < numReductions; ++r) {
      cycles += 6;
      const unsigned numElem = outSizes[r];
      auto numVectorWidths = (numElem + 2 * vectorWidth - 1)
                             / (2 * vectorWidth);
      cycles += (3 * numPartials + 1 + 3) * numVectorWidths;
      cycles += numVectorWidths * addCycles;
    }
    break;
  case 2:
    // Innermost loop adds one tile's input accross a region
    // This estimate based on float->float code Reductions
    // in loop overhead are expected given IS changes.
    // Note this isn't suitable for half->float reduction
    assert(isOutTypeFloat);
    cycles = 2+7+1;
    for (unsigned r = 0; r < numReductions; ++r) {
      unsigned numElem = outSizes[r];
      auto numVectorWidths = (numElem + vectorWidth - 1) / vectorWidth;
      cycles += 9 + numVectorWidths + 1;
      cycles += (7 + numVectorWidths + 1) * (numPartials - 1);
      cycles += numVectorWidths * addCycles;
    }
    break;
  }
  return cycles;
}

#endif // _performance_estimation_h_
