#ifndef _net_hpp_
#define _net_hpp_
#include <poplar/Graph.hpp>
#include <poplar/CPUEngine.hpp>
#include <poplar/IPUModelEngine.hpp>
#include <cstdint>
#include <memory>
#include <vector>
#include <map>
#include <random>
#include "neural_net_common.h"
#include "DeviceInfo.hpp"
#include "ConvReuse.hpp"
#include "exceptions.hpp"
#include "FullyConnectedPlan.hpp"
#include "ConvPlan.hpp"

class Layer { public: virtual ~Layer() {};};

class ConvLayer : public Layer {
public:
  unsigned kernelSize;
  unsigned stride;
  unsigned padding;
  unsigned numChannels;
  NonLinearityType nonLinearityType;
  ConvLayer(unsigned kernelSize,
            unsigned stride,
            unsigned padding,
            unsigned numChannels,
            NonLinearityType nonLinearityType) :
  kernelSize(kernelSize),
  stride(stride),
  padding(padding),
  numChannels(numChannels),
  nonLinearityType(nonLinearityType) {}
};

class ConvResLayer : public Layer {
public:
  unsigned kernelSize;
  unsigned stride;
  unsigned padding;
  unsigned numChannels;
  NonLinearityType nonLinearityType;
  unsigned resIndex;
  enum ResidualMethod resMethod;
  ConvResLayer(unsigned kernelSize,
               unsigned stride,
               unsigned padding,
               unsigned numChannels,
               NonLinearityType nonLinearityType,
               unsigned resIndex,
               enum ResidualMethod resMethod) :
    kernelSize(kernelSize),
    stride(stride),
    padding(padding),
    numChannels(numChannels),
    nonLinearityType(nonLinearityType),
    resIndex(resIndex), resMethod(resMethod) {}
};


class MaxPoolLayer : public Layer {
public:
  unsigned kernelSize;
  unsigned stride;
  unsigned padding;
  MaxPoolLayer(unsigned kernelSize,
               unsigned stride,
               unsigned padding=0) :
  kernelSize(kernelSize),
  stride(stride),
  padding(padding) {}
};

class FullyConnectedLayer : public Layer {
public:
  unsigned size;
  NonLinearityType nonLinearityType;
  FullyConnectedLayer(unsigned size,
                      NonLinearityType nonLinearityType) :
    size(size), nonLinearityType(nonLinearityType) {}
};

/* This utility function wraps a vector of normal pointers as unique_ptrs.
   It allows the hidden layer array to be initializes with an
   initializer list. */
static std::vector<std::unique_ptr<Layer>>
makeLayers(std::vector<Layer *> vs)
{
  std::vector<std::unique_ptr<Layer>> xs;
  for (auto p: vs)
    xs.push_back(std::unique_ptr<Layer>(p));
  return xs;
}


enum NetType {
  TrainingNet,
  TestOnlyNet
};

/* A data set full of test and training data along with its dimensions */
class DataSet {
public:
  std::unique_ptr<float[]> testData, trainingData;
  std::unique_ptr<unsigned[]> testLabels, trainingLabels;
  unsigned dataSize, numTest, numTraining;
  std::vector<std::size_t> dim;
};

enum DType {
  FP16,
  FP32
};

class NetOptions {
public:
  bool useIPUModel = false;
  bool doComputation = true;
  bool doTestsDuringTraining = true;
  unsigned numIPUs = 1;
  unsigned tilesPerIPU = 1216;
  unsigned ipuExchangeBandwidth = 4;
  unsigned memoryBytesPerTile = 1024 * 256;
  unsigned numBatchesBetweenTest = 2500;
  bool reuseLayerImplGraphs = true;
  bool ignoreData = false;
  bool retainActivations = false;
  unsigned dataPathWidth = 64;
  unsigned convUnitPipelineDepth = 4;
  unsigned fp16AccumConvUnitsPerTile = 8;
  unsigned fp32AccumConvUnitsPerTile = 4;
  bool sharedConvWeights = true;
};

bool parseCommandLine(int argc, char **argv, NetOptions &options,
                      bool &doTraining);

/* This class represent the entire network. */
class Net {
  NetType netType;
  NetOptions options;

  unsigned batchSize;
  float eta;
  std::vector<std::unique_ptr<Layer>> layers;

  /* Poplar program creation state. */
  std::unique_ptr<poplar::GraphProgEnv> env;
  std::unique_ptr<poplar::Graph> graph;
  std::unique_ptr<poplar::EngineBuilder> engineBuilder;
  std::unique_ptr<DeviceInfo> deviceInfo;
  std::unique_ptr<poplar::Engine> engine;
  std::unique_ptr<poplar::IPUModelEngineBuilder::TileMapping> mapping;
  std::unique_ptr<char[]> hAct;
  std::vector<std::unique_ptr<float[]>> hParams;
  std::mt19937 randomEngine;
  unsigned hIsTraining;
  unsigned numTestBatches;
  unsigned hNumCorrect;
  std::string dType;

  std::map<ConvImplSpec, ReusableLayer> convImpls;
  std::map<unsigned, fc::Plan> fullyConnectedPlan;
  std::vector<poplar::Tensor> acts, z, deltas;
  std::vector<std::vector<poplar::Tensor>> params;
  std::map<unsigned, conv::ConvPlan> convPlans;
  std::uint64_t numFlops;
  double perfectCycleTime;

  conv::ConvPlan getConvPlan(unsigned i, unsigned inDimY, unsigned inDimX,
                             unsigned inNumChans);

  unsigned
  getRequiredChansPerGroupFwd(unsigned i, unsigned inDimY, unsigned inDimX,
                              unsigned inNumChans);

  unsigned getRequiredChansPerGroupBwd(int i);

  poplar::program::Program
  createConvLayerFwd(unsigned i, unsigned kernelSize, unsigned stride,
                     unsigned padding, unsigned numChannels,
                     NonLinearityType nonLinearityType,
                     unsigned resIndex, ResidualMethod resMethod,
                     poplar::program::Sequence &initParamsProg);

  void outputConvDescription(unsigned inDimY, unsigned inDimX,
                             unsigned inNumChans,
                             unsigned kernelSize, unsigned stride,
                             unsigned padding, unsigned outNumChans,
                             bool doResidual);

  void outputDescription(const Layer *layer, poplar::Tensor in);

  void initialize(DataSet &dataSet, LossType lossType);

public:
  /* When a Net object is constructed the corrensponding poplar graph is
     made */
  Net(DataSet &data, unsigned batchSize,
      std::vector<std::unique_ptr<Layer>> &layers,
      LossType lossType,
      float learningRate,
      NetType netType,
      DType dType,
      NetOptions options = NetOptions());

  Net(DataSet &data, unsigned batchSize,
      std::vector<std::unique_ptr<Layer>> &&layers,
      LossType lossType,
      float learningRate,
      NetType netType,
      DType dType,
      NetOptions options = NetOptions());

  void run(unsigned numBatches);
};

#endif //_net_hpp_
