#define BOOST_TEST_MODULE AddToChannel

#include <boost/test/unit_test.hpp>
#include <poputil/TileMapping.hpp>
#include <poplar/Engine.hpp>
#include <popconv/codelets.hpp>
#include <poplibs_test/Util.hpp>
#include <poputil/VertexTemplates.hpp>
#include <iostream>
#include <functional>
#include <limits>
#include <boost/multi_array.hpp>
#include "TestDevice.hpp"

// Tolerances used in tests
#define FLOAT_REL_TOL  0.01
#define HALF_REL_TOL   0.1
#define FLOAT_ABS_TOL  1e-6
#define HALF_ABS_TOL   1e-5

using namespace poplar;
using namespace poplar::program;
using namespace poputil;
using namespace poplibs_test::util;

const OptionFlags options {
  {"target.textSectionSizeInBytes", "0xa000"},
  {"target.workerStackSizeInBytes", "0x400" }

};

struct TestCase {
  poplar::Type type;
  std::size_t addendLen;
  std::size_t actsLen;
  float scale;
};

struct TestCaseData {
  std::vector<double> addend;
  std::vector<double> acts;

  std::unique_ptr<char[]> rawAddend;
  std::unique_ptr<char[]> rawActs;
};

static bool addToChannelTests(const std::vector<TestCase> &cases) {

  const std::size_t overwriteLen = 32;

  auto device = createTestDevice(TEST_TARGET, 1, 1);
  const auto &target = device.getTarget();
  Graph graph(device);
  popconv::addCodelets(graph);

  // One compute set, with a vertex for each test case.
  auto cs = graph.addComputeSet("cs");

  std::vector<std::pair<std::string, char *>> tmap;
  std::vector<TestCaseData> tcData(cases.size());

  for (std::size_t i = 0; i < cases.size(); ++i) {
    const auto &tc = cases[i];
    if (tc.actsLen % tc.addendLen != 0) {
      return false;
    }

    std::cout << "Test case [" << i << "]: "
              << " addendLen: " << tc.addendLen
              << " actsLen: " << tc.actsLen
              << " scale: " << tc.scale
              << " type: " << tc.type.toString()
              << "\n";

    std::string suffix = "_" + std::to_string(i);

    auto addend = graph.addVariable(tc.type, {tc.addendLen},
                                    "addend" + suffix);
    graph.setTileMapping(addend, 0);
    auto acts = graph.addVariable(tc.type, {tc.actsLen + overwriteLen},
                                  "acts" + suffix);
    graph.setTileMapping(acts, 0);

    auto vertexName = tc.scale == 1.0f ? "popconv::AddToChannel"
                                       : "popconv::ScaledAddToChannel";

    auto v = graph.addVertex(cs,
                             templateVertex(vertexName, tc.type),
                             {{"acts", acts}, {"addend", addend}});

    auto actsBlockCount = tc.actsLen / tc.addendLen;
    auto actsBlockCountPacked = ((actsBlockCount / 6) << 3)
                               | (actsBlockCount % 6);
    uint16_t actsBlockCountPacked16 = actsBlockCountPacked;

    BOOST_CHECK_EQUAL(actsBlockCountPacked16, actsBlockCountPacked);

    graph.setInitialValue(v["actsBlockCountPacked"], actsBlockCountPacked16);

    if (tc.scale != 1.0f)
      graph.setInitialValue(v["scale"], tc.scale);

    graph.setTileMapping(v, 0);

    tcData[i].rawAddend =
        allocateHostMemoryForTensor(addend, "addend" + suffix, graph, tmap);
    tcData[i].rawActs =
        allocateHostMemoryForTensor(acts, "acts" + suffix, graph, tmap);

    tcData[i].addend.resize(tc.addendLen);
    tcData[i].acts.resize(tc.actsLen + overwriteLen);

    std::mt19937 randomEngine;
    writeRandomValues(target,
                      addend.elementType(),
                      tcData[i].addend.data(),
                      tcData[i].addend.data() + tcData[i].addend.size(),
                      -2, 2,
                      randomEngine);
    writeRandomValues(target,
                      acts.elementType(),
                      tcData[i].acts.data(),
                      tcData[i].acts.data() + tcData[i].acts.size(),
                      -2, 2,
                      randomEngine);

    copy(target, tcData[i].addend.data(), tcData[i].addend.size(),
         addend.elementType(), tcData[i].rawAddend.get());
    copy(target, tcData[i].acts.data(), tcData[i].acts.size(),
         acts.elementType(), tcData[i].rawActs.get());
  }

  std::cout << "Executing engine\n";

  auto prog = Execute(cs);
  Engine engine(graph, prog, options);

  engine.load(device);

  upload(engine, tmap);
  engine.run(0);
  download(engine, tmap);

  std::cout << "Checking results\n";

  // Check the results for each test case.

  for (std::size_t i = 0; i < tcData.size(); ++i) {
    const auto &tc = cases[i];

    std::cout << "Checking case [" << i << "]\n";

    // Convert back to double.
    std::vector<double> actsOut(tcData[i].acts.size(), 0.0);

    copy(target, tc.type, tcData[i].rawActs.get(),
         actsOut.data(), actsOut.size());

    // Check the answer.

    auto actsRef = tcData[i].acts;
    for (std::size_t k = 0; k < tc.actsLen; ++k)
      actsRef[k] += tcData[i].addend[k % tc.addendLen] * tc.scale;

    const double absoluteTolerance = tc.type == FLOAT ? FLOAT_ABS_TOL :
                                                        HALF_ABS_TOL;
    const double relativeTolerance = tc.type == FLOAT ? FLOAT_REL_TOL :
                                                        HALF_REL_TOL;

    auto matchesModel = checkIsClose("out",
                                     actsOut.data(),
                                     {actsOut.size()},
                                     actsRef.data(),
                                     actsOut.size(),
                                     relativeTolerance, absoluteTolerance);
    if (!matchesModel)
      return false;
  }

  return true;
}

void runAddToChannelTests(std::vector<TestCase> cases) {
  // Test ScaledAddToChannel
  BOOST_TEST(addToChannelTests(cases));

  // Test AddToChannel
  for (auto &tc : cases)
    tc.scale = 1.0f;
  BOOST_TEST(addToChannelTests(cases));
}

BOOST_AUTO_TEST_CASE(AddToChannelSmall) {
  std::vector<TestCase> cases = {
    {HALF, 1, 800, 3.0f},
    {HALF, 4, 800, 3.0f},
    {HALF, 8, 800, 3.0f},
    {HALF, 1, 15, 3.0f},
    {HALF, 4, 12, 3.0f},
    {HALF, 8, 40, 3.0f},
    {HALF, 5, 15, 3.0f},
    {HALF, 8, 168, 3.0f},

    {FLOAT, 1, 800, 3.0f},
    {FLOAT, 4, 800, 3.0f},
    {FLOAT, 8, 800, 3.0f},
    {FLOAT, 1, 15, 3.0f},
    {FLOAT, 4, 12, 3.0f},
    {FLOAT, 8, 40, 3.0f},
    {FLOAT, 5, 15, 3.0f},
    {FLOAT, 8, 168, 3.0f},
  };
  runAddToChannelTests(cases);
}

BOOST_AUTO_TEST_CASE(AddToChannelLarge1_half) {
  std::vector<TestCase> cases = {
    {HALF, 1, 8000, 3.0f},
  };
  runAddToChannelTests(cases);
}

BOOST_AUTO_TEST_CASE(AddToChannelLarge8_half) {
  std::vector<TestCase> cases = {
    {HALF, 8, 8000, 3.0f},
  };
  runAddToChannelTests(cases);
}

BOOST_AUTO_TEST_CASE(AddToChannelLarge1_float) {
  std::vector<TestCase> cases = {
    {FLOAT, 1, 8000, 3.0f},
  };
  runAddToChannelTests(cases);
}

BOOST_AUTO_TEST_CASE(AddToChannelLarge8_float) {
  std::vector<TestCase> cases = {
    {FLOAT, 8, 8000, 3.0f},
  };
  runAddToChannelTests(cases);
}
