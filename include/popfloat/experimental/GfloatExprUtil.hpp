// Copyright (c) 2020 Graphcore Ltd. All rights reserved.

#ifndef _popfloat_gfloat_expr_util_hpp_
#define _popfloat_gfloat_expr_util_hpp_

#include <popfloat/experimental/GfloatExpr.hpp>

#include <poputil/VertexTemplates.hpp>

namespace popfloat {
namespace experimental {

std::string roundTypeToString(RoundType rmode);
std::string formatTypeToString(FormatType fmt);
std::string srDensityTypeToString(SRDensityType dist);
std::string specTypeToString(SpecType specType);
poplar::Type specTypeToPoplarType(SpecType specType);

} // end namespace experimental
} // end namespace popfloat

// Specialize vertex template stringification for expr ops
namespace poputil {
template <> struct VertexTemplateToString<popfloat::experimental::RoundType> {
  static std::string to_string(const popfloat::experimental::RoundType &rmode) {
    return "popfloat::experimental::RoundType::" +
           popfloat::experimental::roundTypeToString(rmode);
  }
};

template <> struct VertexTemplateToString<popfloat::experimental::FormatType> {
  static std::string to_string(const popfloat::experimental::FormatType &fmt) {
    return "popfloat::experimental::FormatType::" +
           popfloat::experimental::formatTypeToString(fmt);
  }
};

template <>
struct VertexTemplateToString<popfloat::experimental::SRDensityType> {
  static std::string
  to_string(const popfloat::experimental::SRDensityType &dist) {
    return "popfloat::experimental::SRDensityType::" +
           popfloat::experimental::srDensityTypeToString(dist);
  }
};

template <> struct VertexTemplateToString<popfloat::experimental::SpecType> {
  static std::string to_string(const popfloat::experimental::SpecType &spec) {
    return "popfloat::experimental::SpecType::" +
           popfloat::experimental::specTypeToString(spec);
  }
};
} // end namespace poputil

#endif // _popfloat_gfloat_expr_hpp_
