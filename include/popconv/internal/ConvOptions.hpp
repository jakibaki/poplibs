#ifndef popconv_internal_ConvOptions_hpp
#define popconv_internal_ConvOptions_hpp

namespace popconv {

enum class WeightUpdateMethod {
  AMP,
  AUTO
};

const char *asString(const WeightUpdateMethod &method);
std::ostream &operator<<(std::ostream &os, const WeightUpdateMethod &method);
std::istream &operator>>(std::istream &is, WeightUpdateMethod &method);

enum class Pass {
  NONE,
  INFERENCE_FWD,
  TRAINING_FWD,
  TRAINING_BWD,
  TRAINING_WU,
  FC_INFERENCE_FWD,
  FC_TRAINING_FWD,
  FC_TRAINING_BWD,
  FC_TRAINING_WU
};

/** Options to control the implementation of a convolution */
struct ConvOptions {
  WeightUpdateMethod weightUpdateMethod = WeightUpdateMethod::AUTO;
  bool useWinograd = false;
  unsigned winogradPatchSize = 4;
  unsigned percentageCyclesExcessForMemOptim = 0;
  /// The pass this layer corresponds to.
  Pass pass = Pass::NONE;
  poplar::Type partialsType = poplar::FLOAT;
  poplar::Type interTilePartialsType = poplar::FLOAT;
  poplar::Type interIpuPartialsType = poplar::FLOAT;
};

inline bool operator<(const ConvOptions &a, const ConvOptions &b) {
  return std::tie(a.weightUpdateMethod,
                  a.useWinograd,
                  a.winogradPatchSize,
                  a.percentageCyclesExcessForMemOptim,
                  a.pass,
                  a.partialsType,
                  a.interTilePartialsType,
                  a.interIpuPartialsType) <
           std::tie(b.weightUpdateMethod,
                    b.useWinograd,
                    b.winogradPatchSize,
                    b.percentageCyclesExcessForMemOptim,
                    b.pass,
                    b.partialsType,
                    b.interTilePartialsType,
                    b.interIpuPartialsType);
}

} // end namespace popconv

#endif // popconv_internal_ConvOptions_hpp