#ifndef _performance_estimation_h_
#define _performance_estimation_h_

#include <algorithm>
#include <cassert>
#include <cstdint>

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
getConvPartialnx1SupervisorCycleEstimate(
    const std::vector<std::vector<std::vector<unsigned>>> &
    convSizesByWeightAndWorker,
    unsigned convUnitPipelineDepth,
    unsigned numConvUnitsPerTile,
    unsigned numInputPointers) {
  const unsigned numOutputPointers = 1;
  const auto numWorkerContexts = 6;

  unsigned cycles = 0;
  for (const auto &convSizesByWorker : convSizesByWeightAndWorker) {
    assert(convSizesByWorker.size() <= numWorkerContexts);
    // Load weights in the supervisor.
    const auto numLoads = convUnitPipelineDepth * numConvUnitsPerTile;
    cycles += numLoads;
    unsigned maxWorkerCycles = 0;
    // Start workers.
    for (const auto &convSizes : convSizesByWorker) {
      unsigned workerCycles = 0;
      const auto vertexOverhead = 5U;
      const auto coolDownCycles = 5U;
      workerCycles += vertexOverhead;
      for (const auto convSize : convSizes) {
        workerCycles += numInputPointers + numOutputPointers +
                        convSize * convUnitPipelineDepth;
      }
      workerCycles += coolDownCycles;
      maxWorkerCycles = std::max(maxWorkerCycles, workerCycles);
    }
    cycles += maxWorkerCycles * numWorkerContexts;
    cycles += 1; // Sync.
    cycles += numWorkerContexts - 1; // Pipeline bubble.
  }
  return cycles;
}

inline std::uint64_t
getConvPartialnx1CycleWorkerEstimate(
    const std::vector<std::vector<unsigned>> &convSizesByWeight,
    unsigned convUnitPipelineDepth,
    unsigned numConvUnitsPerTile,
    unsigned numInputPointers) {
  const unsigned numOutputPointers = 1;
  const unsigned vertexOverhead = 5;
  unsigned cycleCount = vertexOverhead;
  for (const auto &convSizes : convSizesByWeight) {
    const auto numElements = std::accumulate(convSizes.begin(), convSizes.end(),
                                             0);
    const auto pointerLoadCycles =
        convSizes.size() * (numInputPointers + numOutputPointers);
    unsigned warmUpCycles = numConvUnitsPerTile * convUnitPipelineDepth + 3;


    unsigned innerLoopCycles =
        numElements * convUnitPipelineDepth;
    unsigned coolDownCycles = 5;
    cycleCount += warmUpCycles + innerLoopCycles + coolDownCycles +
                  pointerLoadCycles;
  }
  return cycleCount;
}

inline std::uint64_t
getConvPartialByDotProductCycleEstimate(bool isFloat, unsigned inChansPerGroup,
                                        unsigned kernelWidth,
                                        unsigned inputGroupsPerOutput,
                                        unsigned outputWidth,
                                        unsigned dataPathWidth,
                                        unsigned outputStride)
{
  unsigned vertexOverhead = 15;
  unsigned innerLoopCycles =
      getDenseDotProductCycles(isFloat, kernelWidth * inChansPerGroup,
                               dataPathWidth) / outputStride;
  unsigned middleLoopCycles = inputGroupsPerOutput * (5 + innerLoopCycles);
  unsigned outerLoopCycles = outputWidth * (10 + middleLoopCycles);

  return vertexOverhead + outerLoopCycles;
}

inline std::uint64_t
getFullyConnectedPartialCycleEstimate(bool isFloat, unsigned size,
                                      unsigned dataPathWidth) {
  return 5 + getDenseDotProductCycles(isFloat, size, dataPathWidth);
}

inline std::uint64_t
getFullyConnectedBwdCycleEstimate(unsigned size) {
  // Inner loop is dominated by loads (load pointer, load 64bits, load 16
  // bits). This could be improved if we uses strided loads instead of
  // pointers.
  return 5 + size * 3;
}

inline std::uint64_t
getWeightGradCalcCycles(unsigned numOutRows, unsigned numInRows,
                        unsigned outputWidth, unsigned inputWidth,
                        unsigned outChansPerGroup, unsigned inChansPerGroup,
                        unsigned stride, unsigned kernelSize,
                        unsigned xpadding, unsigned ypadding,
                        unsigned vectorWidth) {
  std::uint64_t cycles = 0;
  for (int wy = 0; wy < kernelSize; ++wy) {
    cycles += 2;
    for (int wx = 0; wx < kernelSize; ++wx) {
      cycles += 5;
      for (unsigned outChan = 0; outChan < outChansPerGroup; ++outChan) {
        cycles += 1;
        int inRow = wy - static_cast<int>(ypadding);
        unsigned outRow = 0;
        while (inRow < 0) {
          inRow += stride;
          outRow += 1;
        }
        while (outRow < numOutRows && inRow < numInRows) {
          cycles += 1;
          int inCol = wx - static_cast<int>(xpadding);
          unsigned outCol = 0;
          while (inCol < 0) {
            inCol += stride;
            outCol += 1;
          }
          while (outCol < outputWidth && inCol < inputWidth) {
            cycles += 2 * (inChansPerGroup + vectorWidth - 1) / vectorWidth;
            outCol += 1;
            inCol += stride;
          }
          outRow += 1;
          inRow += stride;
        }
      }
    }
  }
  return 15 + cycles;
}


inline std::uint64_t getWgdKernelTransformCycles(unsigned int numChannels, 
                                                 bool isFloat) {
  if (isFloat)
    return 2 + (numChannels + 1 )/2  * 35;
  else
    return 2 + (numChannels + 3 )/4  * 35;    
}


inline std::uint64_t getWgdDataTransformCycles(unsigned int numChannels, 
                                               bool isFloat) {
  if (isFloat)
    return 15 + (numChannels + 1 )/2  * 56;
  else
    return 15 + (numChannels + 3 )/4  * 56;    
}

#endif // _performance_estimation_h_
