// Copyright (c) 2019 Graphcore Ltd. All rights reserved.
#ifndef _poplibs_popops_CollectivesProgram_hpp_
#define _poplibs_popops_CollectivesProgram_hpp_

#include <boost/optional.hpp>
#include <boost/variant.hpp>
#include <cassert>
#include <poplar/Graph.hpp>
#include <poplar/Program.hpp>
#include <poplibs_support/Visitor.hpp>
#include <popops/ElementWise.hpp>
#include <poputil/exceptions.hpp>

// The collectives operation is created as a program that calls
// ReduceScatter:
//   Index = (RingIndex(repId) - 1) % N
//   bufferA = slice(MyFragments, Index)
//   Index = (Index + 1) % N
//   Repeat (N - 1) {
//     bufferB = CrossReplica(bufferA)
//     bufferA = slice(MyFragments, Index)
//     bufferA = add(bufferA, bufferB)
//     Index = (Index + 1) % N
//   }
//
// AllGather:
//   Index = RingIndex(repId)
//   MyFragment[Index] = bufferA  // bufferA initialised by scatter
//   Index = (Index + 1) % N
//   Repeat (N - 1) {
//     bufferB = CrossReplica(bufferA)
//     bufferA = bufferB
//     MyFragment[Index] = bufferA
//     Index = (Index + 1) % N
//
//
// Note this tries to represent the program for going both ways around
// the ring so many fields need a clockwise and anticlockwise version
//
//

namespace popops {

enum Direction { CLOCKWISE, ANTICLOCKWISE };

// This struct is used for the cross replica copy
// and the copies in the switch. For the switch
// we do not currently ever have both types but it may be
// something we are able to do in the future
template <class CopyType> struct BufferCopies {
  boost::optional<CopyType> clockwiseCopy;
  boost::optional<CopyType> anticlockwiseCopy;
  poplar::program::Sequence createProgram() const {
    // at least on of these should be populated
    assert(clockwiseCopy || anticlockwiseCopy);
    if (!clockwiseCopy) {
      return anticlockwiseCopy.get();
    } else if (!anticlockwiseCopy) {
      return clockwiseCopy.get();
    }
    return poplar::program::Sequence(clockwiseCopy.get(),
                                     anticlockwiseCopy.get());
  }

  void setCopy(CopyType copy, const Direction direction) {
    if (direction == Direction::CLOCKWISE) {
      assert(!static_cast<bool>(clockwiseCopy));
      clockwiseCopy = copy;
    } else if (direction == Direction::ANTICLOCKWISE) {
      assert(!static_cast<bool>(anticlockwiseCopy));
      anticlockwiseCopy = copy;
    }
  }
};

using cases_type = std::vector<BufferCopies<poplar::program::Copy>>;

struct DynamicSliceCopy {
  poplar::Tensor sliceIndex;
  // expect dynamic slice to populate this sequence
  poplar::program::Sequence copyProg;

  poplar::program::Sequence createProgram() const { return copyProg; }
  void setSlice(poplar::Tensor index) { sliceIndex = std::move(index); }
  poplar::Tensor getSlice() const { return sliceIndex; }
  poplar::program::Sequence &getProgram() { return copyProg; }
  cases_type &getCases() const { std::abort(); }
};

struct SwitchSliceCopy {
  // this is going to get transformed into a switch
  std::vector<BufferCopies<poplar::program::Copy>> cases;
  poplar::Tensor sliceIndex;
  explicit SwitchSliceCopy(const unsigned numCases)
      : cases(std::vector<BufferCopies<poplar::program::Copy>>(numCases)) {}
  poplar::program::Sequence createProgram() const {
    // Assert Checking
    //----------------------------------------------------------------
    // shouldn't be creating slice of entire tensor
    assert(cases.size() > 1);
    // if one of them is a clockwise copy all of them should be
    assert(std::all_of(cases.begin(), cases.end(),
                       [&](const BufferCopies<poplar::program::Copy> &val) {
                         const bool clockwiseAreSame =
                             static_cast<bool>(val.clockwiseCopy) ==
                             static_cast<bool>(cases[0].clockwiseCopy);
                         const bool anticlockwiseAreSame =
                             static_cast<bool>(val.anticlockwiseCopy) ==
                             static_cast<bool>(cases[0].anticlockwiseCopy);
                         return clockwiseAreSame && anticlockwiseAreSame;
                       }));
    // --------------------------------------------------------------
    poplar::program::Switch sliceProg(sliceIndex);
    for (unsigned i = 0; i < cases.size(); ++i) {
      sliceProg.add(i, cases[i].createProgram());
    }
    return poplar::program::Sequence(std::move(sliceProg));
  }
  void setSlice(poplar::Tensor index) { sliceIndex = std::move(index); }
  poplar::Tensor getSlice() const { return sliceIndex; }
  poplar::program::Sequence &getProgram() { std::abort(); }
  cases_type &getCases() { return cases; }
};

struct SliceCopy {
  boost::variant<SwitchSliceCopy, DynamicSliceCopy> copy;
  SliceCopy(SwitchSliceCopy copy) : copy(std::move(copy)) {}
  SliceCopy(DynamicSliceCopy copy) : copy(std::move(copy)) {}

  poplar::program::Sequence createProgram() const {
    return boost::apply_visitor(
        poplibs_support::make_visitor<poplar::program::Sequence>(
            [&](const auto &x) { return x.createProgram(); }),
        copy);
  }
  void setSliceIndex(poplar::Tensor sliceIndex) {
    return boost::apply_visitor(
        poplibs_support::make_visitor<void>(
            [&](auto &x) { return x.setSlice(sliceIndex); }),
        copy);
  }
  poplar::Tensor getSliceIndex() const {
    return boost::apply_visitor(
        poplibs_support::make_visitor<poplar::Tensor>(
            [&](const auto &x) { return x.getSlice(); }),
        copy);
  }
  poplar::program::Sequence &getCopyProgram() {
    return boost::apply_visitor(
        poplibs_support::make_visitor<poplar::program::Sequence &>(
            [&](auto &x) -> poplar::program::Sequence & {
              return x.getProgram();
            }),
        copy);
  }
  cases_type &cases() {
    return boost::apply_visitor(
        poplibs_support::make_visitor<cases_type &>(
            [&](auto &x) -> cases_type & { return x.getCases(); }),
        copy);
  }
};

struct ReduceProg {
  poplar::Tensor A;
  poplar::Tensor B;
  popops::Operation op;
  std::string debugPrefix;
  ReduceProg(poplar::Tensor A, poplar::Tensor B, popops::Operation op,
             std::string prefix)
      : A(A), B(B), op(op), debugPrefix(prefix) {}

  ReduceProg operator+(const ReduceProg &other) const {
    assert(op == other.op);
    return ReduceProg(concat(A, other.A), concat(B, other.B), op, debugPrefix);
  }
};

struct CollectivesProgram {
  unsigned repeatCounter = 0;
  poplar::Tensor undefTensor; // These will be undeffed in the sequence
  // The src buffer that the slice program will slice into
  // The reduce scatter step returns this buffer, all gather doesn't set this
  boost::optional<poplar::Tensor> srcBuffer;
  boost::optional<poplar::Tensor> dstBuffer; // only used in reduce scatter
  poplar::program::Sequence
      initIndex; // program to set sliceTensor to ring index
  poplar::program::Sequence
      incrementIndex; // program to  update sliceIndex per iteration
  BufferCopies<poplar::program::CrossReplicaCopy>
      exchangeProg;                        // the cross replica copy
  SliceCopy sliceFragments;                // dynamic slice of tensor
  boost::optional<ReduceProg> reduceProg;  // only used in reduce scatter
  poplar::program::Sequence allgatherCopy; // only used in all gather
  poplar::program::Sequence
      firstGatherCopy; // on first iteration copy is from scatter output
  CollectivesProgram(SliceCopy sliceCopy) : sliceFragments(sliceCopy) {}
};

static void opInPlace(poplar::Graph &graph, popops::Operation op,
                      const poplar::Tensor &a, const poplar::Tensor &b,
                      poplar::program::Sequence &prog,
                      const std::string &debugPrefix) {
  switch (op) {
  case Operation::ADD:
    addInPlace(graph, a, b, prog, debugPrefix);
    break;
  case Operation::MUL:
    mulInPlace(graph, a, b, prog, debugPrefix);
    break;
  case Operation::MIN:
    minInPlace(graph, a, b, prog, debugPrefix);
    break;
  case Operation::MAX:
    maxInPlace(graph, a, b, prog, debugPrefix);
    break;
  case Operation::LOGICAL_AND:
    logicalAndInPlace(graph, a, b, prog, debugPrefix);
    break;
  case Operation::LOGICAL_OR:
    logicalOrInPlace(graph, a, b, prog, debugPrefix);
    break;
  case Operation::SQUARE_ADD:
    throw poputil::poplibs_error("Collective reduction using the SQUARE_ADD "
                                 "operation is not yet supported");
  }
}

static poplar::program::Sequence
opInPlace(poplar::Graph &graph, const boost::optional<ReduceProg> &reduceProg) {
  poplar::program::Sequence prog;
  if (!reduceProg) {
    return prog;
  }
  opInPlace(graph, reduceProg->op, reduceProg->A, reduceProg->B, prog,
            reduceProg->debugPrefix);
  return prog;
}

poplar::program::Sequence unidirectionalSequence(CollectivesProgram &program,
                                                 poplar::Graph &graph) {
  using namespace poplar::program;
  const auto sliceFunction =
      graph.addFunction(program.sliceFragments.createProgram());
  Sequence loopBody(std::move(program.incrementIndex),
                    program.exchangeProg.createProgram(),
                    std::move(program.allgatherCopy), Call(sliceFunction),
                    opInPlace(graph, program.reduceProg));
  return Sequence(WriteUndef(program.undefTensor), std::move(program.initIndex),
                  std::move(program.firstGatherCopy), Call(sliceFunction),
                  Repeat(program.repeatCounter, std::move(loopBody)));
}
// Create a program that does a clockwise and anticlockwise collective
// simultaneously
poplar::program::Sequence
bidirectionalSequence(CollectivesProgram &clockwise,
                      CollectivesProgram &anticlockwise, poplar::Graph &graph) {
  assert(clockwise.repeatCounter == anticlockwise.repeatCounter);
  using namespace poplar::program;
  const auto sliceFunction =
      graph.addFunction(Sequence(clockwise.sliceFragments.createProgram(),
                                 anticlockwise.sliceFragments.createProgram()));
  boost::optional<ReduceProg> combinedReduceProg;
  assert(static_cast<bool>(clockwise.reduceProg) ==
         static_cast<bool>(anticlockwise.reduceProg));
  if (clockwise.reduceProg && anticlockwise.reduceProg) {
    combinedReduceProg =
        clockwise.reduceProg.get() + anticlockwise.reduceProg.get();
  }
  Sequence loopBody(std::move(clockwise.incrementIndex),
                    std::move(anticlockwise.incrementIndex),
                    clockwise.exchangeProg.createProgram(),
                    anticlockwise.exchangeProg.createProgram(),
                    std::move(clockwise.allgatherCopy),
                    std::move(anticlockwise.allgatherCopy), Call(sliceFunction),
                    opInPlace(graph, combinedReduceProg));
  return Sequence(
      WriteUndef(concat(clockwise.undefTensor, anticlockwise.undefTensor)),
      std::move(clockwise.initIndex), std::move(anticlockwise.initIndex),
      std::move(clockwise.firstGatherCopy),
      std::move(anticlockwise.firstGatherCopy), Call(sliceFunction),
      Repeat(clockwise.repeatCounter, std::move(loopBody)));
}

// Create the sequence needed for the meet in the middle collective
poplar::program::Sequence meetInMiddleReduceScatterSequence(
    CollectivesProgram &clockwise, CollectivesProgram &anticlockwise,
    poplar::Graph &subGraph, poplar::program::Sequence combineBuffersProg,
    unsigned controlTile) {
  using namespace poplar;
  using namespace poplar::program;
  auto graph = subGraph.getTopLevelGraph();
  const auto isFirstStep = graph.addVariable(BOOL, {}, "isFirstStep");
  const auto trueConst = graph.addConstant(BOOL, {}, true, "trueConst");
  const auto falseConst = graph.addConstant(BOOL, {}, false, "falseConst");
  const auto zeroConst = graph.addConstant(UNSIGNED_INT, {}, 0, "zeroConst");
  const auto lastConst = graph.addConstant(
      UNSIGNED_INT, {}, clockwise.repeatCounter - 1, "lastConst");
  const auto loopCounter = graph.addVariable(UNSIGNED_INT, {}, "loopCounter");
  graph.setTileMapping(isFirstStep, controlTile);
  graph.setTileMapping(trueConst, controlTile);
  graph.setTileMapping(falseConst, controlTile);
  graph.setTileMapping(loopCounter, controlTile);
  graph.setTileMapping(zeroConst, controlTile);
  graph.setTileMapping(lastConst, controlTile);

  const auto clockwiseSliceFunction =
      graph.addFunction(clockwise.sliceFragments.createProgram());
  const auto anticlockwiseSliceFunction =
      graph.addFunction(anticlockwise.sliceFragments.createProgram());

  using namespace popops::expr;
  Sequence isLastProg;
  auto isLastStep =
      popops::map(graph, _1 == _2, {loopCounter, lastConst}, isLastProg);

  Sequence incrementLoopCounter;
  popops::mapInPlace(graph, _1 + 1, {loopCounter}, incrementLoopCounter);

  assert(clockwise.repeatCounter - 1 == anticlockwise.repeatCounter);
  // I think it is possible to remove the anticlockwise slice for before the
  // loop and use conditionals within the loop to do it
  Sequence loopBody(
      std::move(clockwise.incrementIndex),
      clockwise.exchangeProg.createProgram(),
      // here unconditionally create the cross replica copy. In the first
      // step this will transfer the uninitialised data but as the rest of the
      // repeat will be conditional on it not being step 0 it won't be
      // used and it will be overwritten in the next iteration of the repeat
      // It being done unconditionally means it can be merged with the
      // clockwise cross replica copy
      anticlockwise.exchangeProg.createProgram(), Call(clockwiseSliceFunction),
      opInPlace(subGraph, clockwise.reduceProg),
      If(isFirstStep, Sequence(Copy(falseConst, isFirstStep)),
         Sequence(std::move(isLastProg),
                  If(isLastStep, Sequence(std::move(combineBuffersProg)),
                     Sequence(Call(anticlockwiseSliceFunction),
                              opInPlace(subGraph, anticlockwise.reduceProg))))),
      std::move(anticlockwise.incrementIndex), std::move(incrementLoopCounter));
  return Sequence(
      WriteUndef(concat(clockwise.undefTensor, anticlockwise.undefTensor)),
      Copy(std::move(trueConst), isFirstStep), Copy(falseConst, isLastStep),
      Copy(std::move(zeroConst), std::move(loopCounter)),
      std::move(clockwise.initIndex), std::move(anticlockwise.initIndex),
      Call(clockwiseSliceFunction),
      // TODO: T12922 Put this in first iteration of repeat loop.
      Call(anticlockwiseSliceFunction),
      Repeat(clockwise.repeatCounter, std::move(loopBody)));
}

// Create the sequence needed for the meet in the middle collective
poplar::program::Sequence
meetInMiddleAllGatherSequence(CollectivesProgram &clockwise,
                              CollectivesProgram &anticlockwise,
                              poplar::Graph &subGraph, unsigned controlTile) {
  using namespace poplar;
  using namespace poplar::program;
  auto graph = subGraph.getTopLevelGraph();
  const auto isFirstStep = graph.addVariable(BOOL, {}, "isFirstStep");
  const auto trueConst = graph.addConstant(BOOL, {}, true, "trueConst");
  const auto falseConst = graph.addConstant(BOOL, {}, false, "falseConst");
  const auto zeroConst = graph.addConstant(UNSIGNED_INT, {}, 0, "zeroConst");
  const auto lastConst = graph.addConstant(
      UNSIGNED_INT, {}, clockwise.repeatCounter - 1, "lastConst");
  const auto loopCounter = graph.addVariable(UNSIGNED_INT, {}, "loopCounter");
  graph.setTileMapping(isFirstStep, controlTile);
  graph.setTileMapping(trueConst, controlTile);
  graph.setTileMapping(falseConst, controlTile);
  graph.setTileMapping(loopCounter, controlTile);
  graph.setTileMapping(zeroConst, controlTile);
  graph.setTileMapping(lastConst, controlTile);

  const auto clockwiseSliceFunction =
      graph.addFunction(clockwise.sliceFragments.createProgram());
  const auto anticlockwiseSliceFunction =
      graph.addFunction(anticlockwise.sliceFragments.createProgram());

  using namespace popops::expr;
  Sequence isLastProg;
  auto isLastStep =
      popops::map(graph, _1 == _2, {loopCounter, lastConst}, isLastProg);

  Sequence incrementLoopCounter;
  popops::mapInPlace(graph, _1 + 1, {loopCounter}, incrementLoopCounter);

  assert(clockwise.repeatCounter - 1 == anticlockwise.repeatCounter);
  // In the loopbody i can choose to either put the anticlockwise slice or
  // the allgatherCopy behind the `if`. We have chosen the slice as it gives the
  // opportunity for the allgatherCopy to be merged with the clockwise one. This
  // decision should be reviewed if we ever merge the slice copies.
  Sequence loopBody(
      std::move(clockwise.incrementIndex),
      std::move(anticlockwise.incrementIndex),
      clockwise.exchangeProg.createProgram(),
      // here unconditionally create the cross replica copy. In the first
      // step this will transfer the uninitialised data but as the rest of the
      // repeat will be conditional on it not being step 0 it won't be
      // used and it will be overwritten in the next iteration of the repeat
      // It being done unconditionally means it can be merged with the
      // clockwise cross replica copy (same for the gather copy)
      anticlockwise.exchangeProg.createProgram(),
      std::move(clockwise.allgatherCopy),
      std::move(anticlockwise.allgatherCopy), Call(clockwiseSliceFunction),
      std::move(isLastProg),
      If(isLastStep, Sequence(), Sequence(Call(anticlockwiseSliceFunction))),
      std::move(incrementLoopCounter));
  return Sequence(
      WriteUndef(concat(clockwise.undefTensor, anticlockwise.undefTensor)),
      Copy(std::move(trueConst), std::move(isFirstStep)),
      Copy(std::move(falseConst), std::move(isLastStep)),
      Copy(std::move(zeroConst), std::move(loopCounter)),
      std::move(clockwise.initIndex), std::move(anticlockwise.initIndex),
      std::move(clockwise.firstGatherCopy),
      std::move(anticlockwise.firstGatherCopy), Call(clockwiseSliceFunction),
      Repeat(clockwise.repeatCounter, std::move(loopBody)));
}

} // namespace popops
#endif
