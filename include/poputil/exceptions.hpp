// Copyright (c) 2017 Graphcore Ltd. All rights reserved.

#ifndef poputil_exceptions_hpp
#define poputil_exceptions_hpp

#include <stdexcept>
#include <string>

namespace poputil {

struct poplibs_error : std::runtime_error {
  std::string type;
  explicit poplibs_error(const std::string &s);
  explicit poplibs_error(const char *s);
};

} // namespace poputil

#endif // poputil_exceptions_hpp
