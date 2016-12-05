#include <algorithm>
#include <boost/multi_array.hpp>
#include <boost/program_options.hpp>
#include <boost/test/tools/floating_point_comparison.hpp>
#include <cassert>
#include <exception>
#include <istream>
#include <ostream>
#include <poplar/Graph.hpp>
#include <poplar/Engine.hpp>
#include <popnn/ActivationMapping.hpp>
#include <popnn/MaxPool.hpp>
#include <poplar/HalfFloat.hpp>
#include <popnn/Net.hpp>
#include <popnn/NonLinearity.hpp>
#include <popnn_ref/MaxPooling.hpp>
#include <popnn_ref/Util.hpp>
#include <popnn/Compiler.hpp>
#include <random>

using namespace poplar;
using namespace poplar::program;
using namespace ref::util;

int main(int argc, char **argv) {
  namespace po = boost::program_options;

  unsigned chans;
  unsigned width;
  unsigned height;
  unsigned kernelHeight;
  unsigned kernelWidth;
  unsigned paddingHeight;
  unsigned paddingWidth;
  unsigned strideH;
  unsigned strideW;
  unsigned fwdChansPerGroup;
  unsigned bwdChansPerGroup;
  unsigned batchSize;
  FPDataType dataType;
  double relativeTolerance;
  DeviceInfo info;
  info.IPUExchangeType =
      DeviceInfo::ExchangeType::BARE_NAKED_WITH_AGGRESSIVE_MULTICAST;

  /* these are used when the same value is shared across both height and width*/
  unsigned kernelSize;
  unsigned padding;
  unsigned stride;

  po::options_description desc("Options");
  desc.add_options()
    ("help", "Produce help message")
    ("channels", po::value<unsigned>(&chans)->required(),
     "Number of channels")
    ("width", po::value<unsigned>(&width)->required(), "Field width")
    ("height", po::value<unsigned>(&height)->required(), "Field height")

    ("kernel-size",
      po::value<unsigned>(&kernelSize)->default_value(1),
     "Size of square kernel. If set, it is an error to also set either "
     "kernel-height and/or kernel-width")
    ("kernel-height",
      po::value<unsigned>(&kernelHeight)->default_value(1),
     "Size of kernel height")
    ("kernel-width",
      po::value<unsigned>(&kernelWidth)->default_value(1),
     "Size of kernel width")
    ("data-type",
     po::value<FPDataType>(&dataType)->default_value(FPDataType::HALF),
     "Type of the data and the parameters")

    ("padding", po::value<unsigned>(&padding)->default_value(0),
     "Amount of zero padding for height and width. If set, it is an "
     "error to also set either padding-height and/or padding-width")
    ("padding-height", po::value<unsigned>(&paddingHeight)->default_value(0),
     "Amount of zero padding in the height dimension")
    ("padding-width", po::value<unsigned>(&paddingWidth)->default_value(0),
     "Amount of zero padding in the width dimension")

    ("stride", po::value<unsigned>(&stride)->default_value(1),
     "Kernel stride for both height and width. If set, it is an error "
     "to also set either stride-height and/or stride-width")
    ("stride-height", po::value<unsigned>(&strideH)->default_value(1),
     "Kernel stride in the height dimension")
    ("stride-width", po::value<unsigned>(&strideW)->default_value(1),
     "Kernel stride in the width dimension")

    ("fwd-chans-per-group",
     po::value<unsigned>(&fwdChansPerGroup),
     "The number of channels per group of the activations written in the "
     "forward pass")
    ("bwd-chans-per-group",
     po::value<unsigned>(&bwdChansPerGroup),
     "The number of channels per group of the deltas written in the backwards "
     "pass")
    ("inference-only", "Benchmark inference only")
    ("tolerance", po::value<double>(&relativeTolerance)->default_value(0.01),
     "Relative tolerance to use when validating results against the reference "
     "model")
    ("tiles-per-ipu",
     po::value<unsigned>(&info.tilesPerIPU)->default_value(info.tilesPerIPU),
     "Number of tiles per IPU")
     ("ipus",
     po::value<unsigned>(&info.numIPUs)->default_value(info.numIPUs),
     "Number of tiles per IPU")
    ("batch-size",
     po::value<unsigned>(&batchSize)->default_value(1),
     "Batch size")
  ;
  po::variables_map vm;
  try {
    po::store(po::parse_command_line(argc, argv, desc), vm);
    if (vm.count("help")) {
      std::cout << desc << "\n";
      return 1;
    }
    po::notify(vm);
  } catch (std::exception& e) {
    std::cerr << "error: " << e.what() << "\n";
    return 1;
  }

  if (!vm["kernel-size"].defaulted()) {
    if (!vm["kernel-height"].defaulted()) {
      std::cerr << "--kernel as well as --kernel-height set\n";
      return 1;
    }
    if (!vm["kernel-width"].defaulted()) {
      std::cerr << "--kernel as well as --kernel-width set\n";
      return 1;
    }
    kernelHeight = kernelSize;
    kernelWidth = kernelSize;
  }

  if (!vm["padding"].defaulted()) {
    if (!vm["padding-height"].defaulted()) {
      std::cerr << "--padding as well as --padding-height set\n";
      return 1;
    }
    if (!vm["padding-width"].defaulted()) {
      std::cerr << "--padding as well as --padding-width set\n";
      return 1;
    }
    paddingHeight = padding;
    paddingWidth = padding;
  }

  if (!vm["stride"].defaulted()) {
    if (!vm["stride-height"].defaulted()) {
      std::cerr << "--stride as well as --stride-height set\n";
      return 1;
    }
    if (!vm["stride-width"].defaulted()) {
      std::cerr << "--stride as well as --stride-width set\n";
      return 1;
    }
    strideH = stride;
    strideW = stride;
  }

  if (kernelHeight != kernelWidth) {
    std::cerr << "Max pooling cannot handle non-square kernels\n";
    return 1;
  } else {
    kernelSize = kernelHeight;
  }

  if (paddingHeight != paddingWidth) {
    std::cerr << "Max pooling cannot handle different width padding to "
                 "height padding\n";
    return 1;
  } else {
    padding = paddingHeight;
  }

  if (strideH != strideW) {
    std::cerr << "Max pooling cannot handle different width stride "
                 "to height stride\n";
    return 1;
  } else {
    stride = strideH;
  }

  bool inferenceOnly = vm.count("inference-only");
  GraphProgEnv env(popnn::findGraphProg(), GraphProgFileType::Object);
  Graph graph(env, createIPUModelDevice(info));

  std::string dataTypeStr(asString(dataType));
  // If the output grouping is unspecified, assume the output uses the same
  // grouping as the input unless that is impossible.
  if (!vm.count("fwd-chans-per-group")) {
    if (chans % 16 == 0)
      fwdChansPerGroup = 16;
    else
      fwdChansPerGroup = 1;
  }
  if (!inferenceOnly &&
      !vm.count("bwd-chans-per-group")) {
    if (chans % 16 == 0)
      bwdChansPerGroup = 16;
    else
      bwdChansPerGroup = 1;
  }
  const auto outDims =
      maxpool::getOutputDim(height, width, kernelSize, stride, padding);
  const auto outHeight = outDims.first;
  const auto outWidth = outDims.second;
  // Create tensors.
  Tensor prevAct = graph.addTensor(dataTypeStr,
                                   {batchSize,
                                    chans / fwdChansPerGroup,
                                    height,
                                    width,
                                    fwdChansPerGroup}, "prevAct");
  mapActivations(graph, prevAct);
  Tensor nextAct =
      graph.addTensor(dataTypeStr, {batchSize,
                                    chans / fwdChansPerGroup,
                                    outHeight,
                                    outWidth, fwdChansPerGroup},
                      "nextAct");
  mapActivations(graph, nextAct);
  Tensor prevDeltas, zDeltas;
  if (!inferenceOnly) {
    zDeltas =
        graph.addTensor(dataTypeStr, {batchSize,
                                      chans / bwdChansPerGroup,
                                      outHeight,
                                      outWidth, bwdChansPerGroup},
                        "zDeltas");
    mapActivations(graph, zDeltas);
    prevDeltas =
        graph.addTensor(dataTypeStr, {batchSize,
                                      chans / bwdChansPerGroup,
                                      height,
                                      width, bwdChansPerGroup},
                        "prevDeltas");
    mapActivations(graph, prevDeltas);
  }


  auto upload = Sequence();
  auto download = Sequence();
  auto rawHostPrevAct = allocateHostMemoryForTensor(graph, prevAct, upload,
                                                    download);
  auto rawHostNextAct = allocateHostMemoryForTensor(graph, nextAct, upload,
                                                    download);
  std::unique_ptr<char[]> rawHostZDeltas;
  std::unique_ptr<char[]> rawHostPrevDeltas;
  if (!inferenceOnly) {
    rawHostZDeltas = allocateHostMemoryForTensor(graph, zDeltas, upload,
                                                 download);
    rawHostPrevDeltas = allocateHostMemoryForTensor(graph, prevDeltas, upload,
                                                    download);
  }

  auto fwdProg = Sequence();
  fwdProg.add(maxpool::maxPool(graph, kernelSize, stride, padding,
                               prevAct, nextAct));

  auto bwdProg = Sequence();
  if (!inferenceOnly) {
    bwdProg.add(
          maxpool::maxPoolBackward(graph, kernelSize, stride, padding,
                                   prevAct, nextAct, zDeltas, prevDeltas)
    );
  }
  Engine engine(graph, {std::move(upload), std::move(download),
                        std::move(fwdProg), std::move(bwdProg)});


  boost::multi_array<double, 4>
      hostPrevAct(boost::extents[batchSize][chans][height][width]);
  boost::multi_array<double, 4>
      hostNextAct(boost::extents[batchSize][chans][outHeight][outWidth]);
  std::mt19937 randomEngine;
  writeRandomValues(hostPrevAct, 0.0, 1.0, randomEngine);
  groupActivations(hostPrevAct, dataTypeStr, prevAct.dims(),
                   rawHostPrevAct.get());
  // Run the forward pass.
  engine.run(0); // Upload.
  engine.run(2); // Run.
  engine.run(1); // Download.

  // Validate against a reference model.
  ungroupActivations(dataTypeStr, nextAct.dims(), rawHostNextAct.get(),
                     hostNextAct);
  boost::multi_array<double, 4>
      modelNextAct(boost::extents[batchSize][chans][outHeight][outWidth]);
  ref::maxpool::maxPooling(stride, kernelSize, padding, hostPrevAct,
                           modelNextAct);
  bool matchesModel = checkIsClose("fwd", hostNextAct, modelNextAct,
                                   relativeTolerance);

  if (!inferenceOnly) {
    boost::multi_array<double, 4> hostZDeltas(
      boost::extents[batchSize][chans][outHeight][outWidth]
    );
    boost::multi_array<double, 4> hostPrevDeltas(
      boost::extents[batchSize][chans][height][width]
    );
    // Run the backwards pass.
    writeRandomValues(hostZDeltas, 0.0, 1.0, randomEngine);
    groupActivations(hostZDeltas, dataTypeStr, zDeltas.dims(),
                     rawHostZDeltas.get());
    engine.run(0); // Upload.
    engine.run(3); // Run.
    engine.run(1); // Download.
    ungroupActivations(dataTypeStr, zDeltas.dims(), rawHostZDeltas.get(),
                       hostZDeltas);
    ungroupActivations(dataTypeStr, prevDeltas.dims(), rawHostPrevDeltas.get(),
                       hostPrevDeltas);

    // Validate against a reference model.
    boost::multi_array<double, 4>
        modelPrevDeltas(boost::extents[batchSize][chans][height][width]);
    ref::maxpool::maxPoolingBackward(stride, kernelSize, padding,
                                     hostPrevAct, modelNextAct,
                                     hostZDeltas, modelPrevDeltas);
    matchesModel &= checkIsClose("bwd", hostPrevDeltas, modelPrevDeltas,
                                 relativeTolerance);
  }

  Engine::ReportOptions opt;
  opt.doLayerWiseProfile = true;
  engine.report(std::cout, opt);
  if (!matchesModel) {
    std::cerr << "Validation failed\n";
    return 1;
  }
  return 0;
}
