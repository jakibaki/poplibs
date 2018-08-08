// Copyright (c) 2018, Graphcore Ltd, All rights reserved.

#ifndef poplibs_NonLinearityDefUtil_hpp_
#define poplibs_NonLinearityDefUtil_hpp_

#include <popnn/NonLinearityDef.hpp>
#include <poputil/exceptions.hpp>
#include <poputil/VertexTemplates.hpp>

namespace popnn {

inline const char *asString(const popnn::NonLinearityType &type) {
  switch (type) {
  case popnn::NonLinearityType::RELU: return "relu";
  case popnn::NonLinearityType::SIGMOID: return "sigmoid";
  case popnn::NonLinearityType::TANH: return "tanh";
  case popnn::NonLinearityType::SOFTMAX: return "softmax";
  default:
    throw poputil::poplib_error("Unsupported non-linearity type");
  }
}

inline std::ostream &operator<<(std::ostream &os,
                                const popnn::NonLinearityType &type) {
  return os << asString(type);
}

inline std::istream &operator>>(std::istream &in,
                                popnn::NonLinearityType &type) {
  std::string token;
  in >> token;
  if (token == "relu")
    type = popnn::NonLinearityType::RELU;
  else if (token == "sigmoid")
    type = popnn::NonLinearityType::SIGMOID;
  else if (token == "tanh")
    type = popnn::NonLinearityType::TANH;
  else if (token == "softmax")
    type = popnn::NonLinearityType::SOFTMAX;
  else
    throw poputil::poplib_error(
      "Unsupported non-linearity type '" + token + "'");
  return in;
}

} // end namespace popnn

// Specialize vertex template stringification for non-linearity type.
namespace poputil {

template <>
struct VertexTemplateToString<popnn::NonLinearityType> {
  static std::string to_string(const popnn::NonLinearityType &nlType) {
    switch (nlType) {
      case popnn::NonLinearityType::SIGMOID:
        return "popnn::NonLinearityType::SIGMOID";
      case popnn::NonLinearityType::RELU:
        return "popnn::NonLinearityType::RELU";
      case popnn::NonLinearityType::TANH:
        return "popnn::NonLinearityType::TANH";
      case popnn::NonLinearityType::SOFTMAX:
      default:
        throw poputil::poplib_error("Unsupported non-linearity type");
    }
  }
};

} // end namespace poputil

#endif // poplibs_NonLinearityDefUtil_hpp_
