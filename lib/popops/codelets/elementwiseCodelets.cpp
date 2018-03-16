#include <poplar/Vertex.hpp>
#include <poplar/HalfFloat.hpp>
#include <cmath>

#include "util.hpp"

using namespace poplar;
static constexpr auto ONE_PTR = poplar::VectorLayout::ONE_PTR;

namespace popops {

template <typename InType>
class
[[poplar::constraint("elem(**data) != elem(**deltas)")]]
ScaledAdd : public Vertex {
public:
  Vector<InOut<Vector<InType>>> data;
  Vector<Input<Vector<InType, ONE_PTR>>, ONE_PTR> deltas;

  InType K;
  SimOnlyField<unsigned> dataPathWidth;

  bool compute() {
    for (unsigned i = 0; i < data.size(); ++i) {
      for (unsigned j = 0; j < data[i].size(); ++j) {
        data[i][j] += K * deltas[i][j];
      }
    }
    return true;
  }
};

template class ScaledAdd<float>;
template class ScaledAdd<half>;
template class ScaledAdd<int>;
template class ScaledAdd<unsigned>;


template <typename FPType>
class
[[poplar::constraint("elem(**A) != elem(**B)")]]
HadamardProd : public Vertex {
public:
  Vector<InOut<Vector<FPType>>> A;
  Vector<Input<Vector<FPType, ONE_PTR>>, ONE_PTR> B;

  SimOnlyField<unsigned> dataPathWidth;

  bool compute() {
    for (unsigned i = 0; i < A.size(); ++i) {
      for (unsigned j = 0; j < A[i].size(); ++j) {
        A[i][j] *= B[i][j];
      }
    }
    return true;
  }
};

template class HadamardProd<float>;
template class HadamardProd<half>;



template <typename InType>
class Zero : public Vertex {
public:
  Output<Vector<InType>> out;

  SimOnlyField<unsigned> dataPathWidth;

  bool compute() {
    for (unsigned i = 0; i < out.size(); ++i) {
      out[i] = 0;
    }
    return true;
  }
};

template class Zero<float>;
template class Zero<half>;
template class Zero<int>;
template class Zero<unsigned>;

template <typename FPType>
class Zero2d : public Vertex {
public:
  Vector<Output<Vector<FPType>>> out;

  SimOnlyField<unsigned> dataPathWidth;

  bool compute() {
    for (auto &row : out) {
      for (auto &x : row) {
        x = 0;
      }
    }
    return true;
  }
};

template class Zero2d<float>;
template class Zero2d<half>;


template <typename SrcType, typename DstType>
class
[[poplar::constraint("elem(*src) != elem(*dst)")]]
Cast : public Vertex {
public:
  Input<Vector<SrcType, ONE_PTR>> src;
  Output<Vector<DstType>> dst;
  SimOnlyField<unsigned> dataPathWidth;

  bool compute() {
    for (unsigned i = 0; i < dst.size(); ++i) {
      dst[i] = static_cast<DstType>(src[i]);
    }
    return true;
  }
};

template class Cast<float, float>;
template class Cast<float, half>;
template class Cast<float, int>;
template class Cast<float, bool>;

template class Cast<half, float>;
template class Cast<half, half>;
template class Cast<half, int>;
template class Cast<half, bool>;

template class Cast<int,float>;
template class Cast<int,half>;
template class Cast<int,int>;
template class Cast<int,bool>;

template class Cast<bool,float>;
template class Cast<bool,half>;
template class Cast<bool,int>;
template class Cast<bool,bool>;

template <typename SrcType, typename DstType>
class
[[poplar::constraint("elem(**src) != elem(**dst)")]]
Cast2d : public Vertex {
public:
  Vector<Input<Vector<SrcType, ONE_PTR>>, ONE_PTR> src;
  Vector<Output<Vector<DstType>>> dst;
  SimOnlyField<unsigned> dataPathWidth;

  bool compute() {
    for (unsigned i = 0; i != dst.size(); ++i) {
      for (unsigned j = 0; j != dst[i].size(); ++j) {
        dst[i][j] = static_cast<DstType>(src[i][j]);
      }
    }
    return true;
  }
};

template class Cast2d<float, float>;
template class Cast2d<float, half>;
template class Cast2d<float, int>;
template class Cast2d<float, bool>;

template class Cast2d<half, float>;
template class Cast2d<half, half>;
template class Cast2d<half, int>;
template class Cast2d<half, bool>;

template class Cast2d<int,float>;
template class Cast2d<int,half>;
template class Cast2d<int,int>;
template class Cast2d<int,bool>;

template class Cast2d<bool,float>;
template class Cast2d<bool,half>;
template class Cast2d<bool,int>;
template class Cast2d<bool,bool>;


template <typename InType>
class Absolute : public Vertex {
public:
  Vector<Input<Vector<InType, ONE_PTR>>, ONE_PTR> in;
  Vector<Output<Vector<InType>>> out;
  SimOnlyField<unsigned> dataPathWidth;

  bool compute() {
    for (unsigned i = 0; i != out.size(); ++i) {
      for (unsigned j = 0; j != out[i].size(); ++j) {
        if (in[i][j] >= 0) {
          out[i][j] = in[i][j];
        } else {
          out[i][j] = -in[i][j];
        }
      }
    }
    return true;
  }
};

template class Absolute<float>;
template class Absolute<half>;
template class Absolute<int>;


template <typename InType>
class Atan2 : public Vertex {
public:
  Vector<Input<Vector<InType, ONE_PTR>>, ONE_PTR> in1;
  Vector<Input<Vector<InType, ONE_PTR>>, ONE_PTR> in2;
  Vector<Output<Vector<InType>>> out;
  SimOnlyField<unsigned> dataPathWidth;

  bool compute() {

    for (unsigned i = 0; i != out.size(); ++i) {
      for (unsigned j = 0; j != out[i].size(); ++j) {
        out[i][j] = std::atan2(in1[i][j], in2[i][j]);
      }
    }
    return true;
  }
};

template class Atan2<float>;
template class Atan2<half>;


template <typename InType>
class
[[poplar::constraint("elem(**in1) != elem(**in2)",
                     "upper(**in1) || upper(**in2)")]]
Add : public Vertex {
public:
  Vector<Input<Vector<InType, ONE_PTR>>, ONE_PTR> in1;
  Vector<Input<Vector<InType, ONE_PTR>>, ONE_PTR> in2;
  Vector<Output<Vector<InType>>> out;
  SimOnlyField<unsigned> dataPathWidth;

  bool compute() {

    for (unsigned i = 0; i != out.size(); ++i) {
      for (unsigned j = 0; j != out[i].size(); ++j) {
        out[i][j] = in1[i][j] + in2[i][j];
      }
    }
    return true;
  }
};

template class Add<float>;
template class Add<half>;
template class Add<int>;
template class Add<unsigned>;

template <typename InType>
class
[[poplar::constraint("elem(**in1) != elem(**in2)",
                     "elem(**in2) != elem(**out)",
                     "elem(**in1) != elem(**out)",
                     "upper(**in1) || upper(**in2)")]]

BitwiseAnd : public Vertex {
public:
  Vector<Input<Vector<InType, ONE_PTR>>, ONE_PTR> in1;
  Vector<Input<Vector<InType, ONE_PTR>>, ONE_PTR> in2;
  Vector<Output<Vector<InType>>> out;
  SimOnlyField<unsigned> dataPathWidth;

  bool compute() {
    for (unsigned i = 0; i != out.size(); ++i) {
      for (unsigned j = 0; j != out[i].size(); ++j) {
        out[i][j] = in1[i][j] & in2[i][j];
      }
    }
    return true;
  }
};

template class BitwiseAnd<int>;


template <typename InType>
class
[[poplar::constraint("elem(**in) != elem(**out)")]]
BitwiseNot : public Vertex {
public:
  Vector<Input<Vector<InType, ONE_PTR>>, ONE_PTR> in;
  Vector<Output<Vector<InType>>> out;
  SimOnlyField<unsigned> dataPathWidth;

  bool compute() {
    for (unsigned i = 0; i != out.size(); ++i) {
      for (unsigned j = 0; j != out[i].size(); ++j) {
        out[i][j] = ~in[i][j];
      }
    }
    return true;
  }
};

template class BitwiseNot<int>;


template <typename InType>
class
[[poplar::constraint("elem(**in1) != elem(**in2)",
                     "elem(**in2) != elem(**out)",
                     "elem(**in1) != elem(**out)",
                     "upper(**in1) || upper(**in2)")]]
BitwiseOr : public Vertex {
public:
  Vector<Input<Vector<InType, ONE_PTR>>, ONE_PTR> in1;
  Vector<Input<Vector<InType, ONE_PTR>>, ONE_PTR> in2;
  Vector<Output<Vector<InType>>> out;
  SimOnlyField<unsigned> dataPathWidth;

  bool compute() {
    for (unsigned i = 0; i != out.size(); ++i) {
      for (unsigned j = 0; j != out[i].size(); ++j) {
        out[i][j] = in1[i][j] | in2[i][j];
      }
    }
    return true;
  }
};

template class BitwiseOr<int>;


template <typename InType>
class
[[poplar::constraint("elem(**in) != elem(**out)")]]
Ceil : public Vertex {
public:
  Vector<Input<Vector<InType, ONE_PTR>>, ONE_PTR> in;
  Vector<Output<Vector<InType>>> out;
  SimOnlyField<unsigned> dataPathWidth;

  bool compute() {
    for (unsigned i = 0; i != out.size(); ++i) {
      for (unsigned j = 0; j != out[i].size(); ++j) {
        out[i][j] = std::ceil(in[i][j]);
      }
    }
    return true;
  }
};

template class Ceil<float>;
template class Ceil<half>;

template <typename InType>
class Cos : public Vertex {
public:
  Vector<Input<Vector<InType, ONE_PTR>>, ONE_PTR> in;
  Vector<Output<Vector<InType>>> out;
  SimOnlyField<unsigned> dataPathWidth;

  bool compute() {
    for (unsigned i = 0; i != out.size(); ++i) {
      for (unsigned j = 0; j != out[i].size(); ++j) {
        out[i][j] = std::cos(in[i][j]);
      }
    }
    return true;
  }
};

template class Cos<float>;
template class Cos<half>;

template <typename InType>
class
[[poplar::constraint("elem(**in1) != elem(**in2)",
                     "elem(**in2) != elem(**out)",
                     "elem(**in1) != elem(**out)",
                     "upper(**in1) || upper(**in2)")]]
Divide : public Vertex {
public:
  Vector<Input<Vector<InType, ONE_PTR>>, ONE_PTR> in1;
  Vector<Input<Vector<InType, ONE_PTR>>, ONE_PTR> in2;
  Vector<Output<Vector<InType>>> out;
  SimOnlyField<unsigned> dataPathWidth;

  bool compute() {
    for (unsigned i = 0; i != out.size(); ++i) {
      for (unsigned j = 0; j != out[i].size(); ++j) {
        out[i][j] = in1[i][j] / in2[i][j];
      }
    }
    return true;
  }
};

template class Divide<float>;
template class Divide<half>;
template class Divide<int>;

template <typename InType>
class Equal : public Vertex {
public:
  Vector<Input<Vector<InType, ONE_PTR>>, ONE_PTR> in1;
  Vector<Input<Vector<InType, ONE_PTR>>, ONE_PTR> in2;
  Vector<Output<Vector<bool>>> out;
  SimOnlyField<unsigned> dataPathWidth;

  bool compute() {
    for (unsigned i = 0; i != out.size(); ++i) {
      for (unsigned j = 0; j != out[i].size(); ++j) {
        out[i][j] = in1[i][j] == in2[i][j];
      }
    }
    return true;
  }
};

template class Equal<float>;
template class Equal<half>;
template class Equal<bool>;
template class Equal<int>;


template <typename InType>
class Exponent : public Vertex {
public:
  Vector<Input<Vector<InType, ONE_PTR>>, ONE_PTR> in;
  Vector<Output<Vector<InType>>> out;
  SimOnlyField<unsigned> dataPathWidth;

  bool compute() {
    for (unsigned i = 0; i != out.size(); ++i) {
      for (unsigned j = 0; j != out[i].size(); ++j) {
        out[i][j] = std::exp(in[i][j]);
      }
    }
    return true;
  }
};

template class Exponent<float>;
template class Exponent<half>;

template <typename InType>
class
[[poplar::constraint("elem(**in) != elem(**out)")]]
Floor : public Vertex {
public:
  Vector<Input<Vector<InType, ONE_PTR>>, ONE_PTR> in;
  Vector<Output<Vector<InType>>> out;
  SimOnlyField<unsigned> dataPathWidth;

  bool compute() {
    for (unsigned i = 0; i != out.size(); ++i) {
      for (unsigned j = 0; j != out[i].size(); ++j) {
        out[i][j] = std::floor(in[i][j]);
      }
    }
    return true;
  }
};

template class Floor<float>;
template class Floor<half>;

template <typename InType>
class GreaterThan : public Vertex {
public:
  Vector<Input<Vector<InType, ONE_PTR>>, ONE_PTR> in1;
  Vector<Input<Vector<InType, ONE_PTR>>, ONE_PTR> in2;
  Vector<Output<Vector<bool>>> out;
  SimOnlyField<unsigned> dataPathWidth;

  bool compute() {
    for (unsigned i = 0; i != out.size(); ++i) {
      for (unsigned j = 0; j != out[i].size(); ++j) {
        out[i][j] = in1[i][j] > in2[i][j];
      }
    }
    return true;
  }
};

template class GreaterThan<float>;
template class GreaterThan<half>;
template class GreaterThan<int>;
template class GreaterThan<bool>;

template <typename InType>
class GreaterThanEqual : public Vertex {
public:
  Vector<Input<Vector<InType, ONE_PTR>>, ONE_PTR> in1;
  Vector<Input<Vector<InType, ONE_PTR>>, ONE_PTR> in2;
  Vector<Output<Vector<bool>>> out;
  SimOnlyField<unsigned> dataPathWidth;

  bool compute() {
    for (unsigned i = 0; i != out.size(); ++i) {
      for (unsigned j = 0; j != out[i].size(); ++j) {
        out[i][j] = in1[i][j] >= in2[i][j];
      }
    }
    return true;
  }
};

template class GreaterThanEqual<float>;
template class GreaterThanEqual<half>;
template class GreaterThanEqual<int>;
template class GreaterThanEqual<bool>;

template <typename InType>
class IsFinite : public Vertex {
public:
  Vector<Input<Vector<InType, ONE_PTR>>, ONE_PTR> in;
  Vector<Output<Vector<bool>>> out;
  SimOnlyField<unsigned> dataPathWidth;

  bool compute() {
    for (unsigned i = 0; i != out.size(); ++i) {
      for (unsigned j = 0; j != out[i].size(); ++j) {
        InType v = in[i][j];
        out[i][j] = (v == v) && (std::abs(v) != INFINITY);
      }
    }
    return true;
  }
};

template class IsFinite<float>;
template class IsFinite<half>;

template <typename InType>
class LessThan : public Vertex {
public:
  Vector<Input<Vector<InType, ONE_PTR>>, ONE_PTR> in1;
  Vector<Input<Vector<InType, ONE_PTR>>, ONE_PTR> in2;
  Vector<Output<Vector<bool>>> out;
  SimOnlyField<unsigned> dataPathWidth;

  bool compute() {
    for (unsigned i = 0; i != out.size(); ++i) {
      for (unsigned j = 0; j != out[i].size(); ++j) {
        out[i][j] = in1[i][j] < in2[i][j];
      }
    }
    return true;
  }
};

template class LessThan<float>;
template class LessThan<half>;
template class LessThan<int>;
template class LessThan<bool>;

template <typename InType>
class LessThanEqual : public Vertex {
public:
  Vector<Input<Vector<InType, ONE_PTR>>, ONE_PTR> in1;
  Vector<Input<Vector<InType, ONE_PTR>>, ONE_PTR> in2;
  Vector<Output<Vector<bool>>> out;
  SimOnlyField<unsigned> dataPathWidth;

  bool compute() {
    for (unsigned i = 0; i != out.size(); ++i) {
      for (unsigned j = 0; j != out[i].size(); ++j) {
        out[i][j] = in1[i][j] <= in2[i][j];
      }
    }
    return true;
  }
};

template class LessThanEqual<float>;
template class LessThanEqual<half>;
template class LessThanEqual<int>;
template class LessThanEqual<bool>;

template <typename InType>
class Logarithm : public Vertex {
public:
  Vector<Input<Vector<InType, ONE_PTR>>, ONE_PTR> in;
  Vector<Output<Vector<InType>>> out;
  SimOnlyField<unsigned> dataPathWidth;

  bool compute() {
    for (unsigned i = 0; i != out.size(); ++i) {
      for (unsigned j = 0; j != out[i].size(); ++j) {
        out[i][j] = std::log(in[i][j]);
      }
    }
    return true;
  }
};

template class Logarithm<float>;
template class Logarithm<half>;

template <typename InType>
class LogicalAnd : public Vertex {
public:
  Vector<Input<Vector<InType, ONE_PTR>>, ONE_PTR> in1;
  Vector<Input<Vector<InType, ONE_PTR>>, ONE_PTR> in2;
  Vector<Output<Vector<bool>>> out;
  SimOnlyField<unsigned> dataPathWidth;

  bool compute() {
    for (unsigned i = 0; i != out.size(); ++i) {
      for (unsigned j = 0; j != out[i].size(); ++j) {
        out[i][j] = in1[i][j] && in2[i][j];
      }
    }
    return true;
  }
};

template class LogicalAnd<bool>;


template <typename InType>
class LogicalNot : public Vertex {
public:
  Vector<Input<Vector<InType, ONE_PTR>>, ONE_PTR> in;
  Vector<Output<Vector<InType>>> out;
  SimOnlyField<unsigned> dataPathWidth;

  bool compute() {
    for (unsigned i = 0; i != out.size(); ++i) {
      for (unsigned j = 0; j != out[i].size(); ++j) {
        out[i][j] = !in[i][j];
      }
    }
    return true;
  }
};

template class LogicalNot<bool>;


template <typename InType>
class LogicalOr : public Vertex {
public:
  Vector<Input<Vector<InType, ONE_PTR>>, ONE_PTR> in1;
  Vector<Input<Vector<InType, ONE_PTR>>, ONE_PTR> in2;
  Vector<Output<Vector<bool>>> out;
  SimOnlyField<unsigned> dataPathWidth;

  bool compute() {
    for (unsigned i = 0; i != out.size(); ++i) {
      for (unsigned j = 0; j != out[i].size(); ++j) {
        out[i][j] = in1[i][j] || in2[i][j];
      }
    }
    return true;
  }
};

template class LogicalOr<bool>;

template <typename InType>
class
[[poplar::constraint("elem(**in1) != elem(**in2)",
                     "upper(**in1) || upper(**in2)")]]
Maximum : public Vertex {
public:
  Vector<Input<Vector<InType, ONE_PTR>>, ONE_PTR> in1;
  Vector<Input<Vector<InType, ONE_PTR>>, ONE_PTR> in2;
  Vector<Output<Vector<InType>>> out;
  SimOnlyField<unsigned> dataPathWidth;

  bool compute() {
    for (unsigned i = 0; i != out.size(); ++i) {
      for (unsigned j = 0; j != out[i].size(); ++j) {
        out[i][j] = max(in1[i][j], in2[i][j]);
      }
    }
    return true;
  }
};

template class Maximum<float>;
template class Maximum<half>;
template class Maximum<int>;


template <typename InType>
class
[[poplar::constraint("elem(**in1) != elem(**in2)",
                     "upper(**in1) || upper(**in2)")]]
Minimum : public Vertex {
public:
  Vector<Input<Vector<InType, ONE_PTR>>, ONE_PTR> in1;
  Vector<Input<Vector<InType, ONE_PTR>>, ONE_PTR> in2;
  Vector<Output<Vector<InType>>> out;
  SimOnlyField<unsigned> dataPathWidth;

  bool compute() {
    for (unsigned i = 0; i != out.size(); ++i) {
      for (unsigned j = 0; j != out[i].size(); ++j) {
        out[i][j] = min(in1[i][j], in2[i][j]);
      }
    }
    return true;
  }
};

template class Minimum<float>;
template class Minimum<half>;
template class Minimum<int>;


template <typename InType>
class
[[poplar::constraint("elem(**in1) != elem(**in2)",
                     "upper(**in1) || upper(**in2)")]]
Multiply : public Vertex {
public:
  Vector<Input<Vector<InType, ONE_PTR>>, ONE_PTR> in1;
  Vector<Input<Vector<InType, ONE_PTR>>, ONE_PTR> in2;
  Vector<Output<Vector<InType>>> out;
  SimOnlyField<unsigned> dataPathWidth;

  bool compute() {
    for (unsigned i = 0; i != out.size(); ++i) {
      for (unsigned j = 0; j != out[i].size(); ++j) {
        out[i][j] = in1[i][j] * in2[i][j];
      }
    }
    return true;
  }
};

template class Multiply<float>;
template class Multiply<half>;
template class Multiply<int>;


template <typename InType>
class NotEqual : public Vertex {
public:
  Vector<Input<Vector<InType, ONE_PTR>>, ONE_PTR> in1;
  Vector<Input<Vector<InType, ONE_PTR>>, ONE_PTR> in2;
  Vector<Output<Vector<bool>>> out;
  SimOnlyField<unsigned> dataPathWidth;

  bool compute() {
    for (unsigned i = 0; i != out.size(); ++i) {
      for (unsigned j = 0; j != out[i].size(); ++j) {
        out[i][j] = in1[i][j] != in2[i][j];
      }
    }
    return true;
  }
};

template class NotEqual<float>;
template class NotEqual<half>;
template class NotEqual<int>;
template class NotEqual<bool>;


template <typename InType>
class
[[poplar::constraint("elem(**in) != elem(**out)")]]
Negate : public Vertex {
public:
  Vector<Input<Vector<InType, ONE_PTR>>, ONE_PTR> in;
  Vector<Output<Vector<InType>>> out;
  SimOnlyField<unsigned> dataPathWidth;

  bool compute() {
    for (unsigned i = 0; i != out.size(); ++i) {
      for (unsigned j = 0; j != out[i].size(); ++j) {
        out[i][j] = -in[i][j];
      }
    }
    return true;
  }
};

template class Negate<float>;
template class Negate<half>;
template class Negate<int>;


template <typename InType>
class Power : public Vertex {
public:
  Vector<Input<Vector<InType, ONE_PTR>>, ONE_PTR> in1;
  Vector<Input<Vector<InType, ONE_PTR>>, ONE_PTR> in2;
  Vector<Output<Vector<InType>>> out;
  SimOnlyField<unsigned> dataPathWidth;

  bool compute() {
    for (unsigned i = 0; i != out.size(); ++i) {
      for (unsigned j = 0; j != out[i].size(); ++j) {
        out[i][j] = std::pow(in1[i][j], in2[i][j]);
      }
    }
    return true;
  }
};

template class Power<float>;
template class Power<half>;


template <typename InType>
class
[[poplar::constraint("elem(**in1) != elem(**in2)",
                     "elem(**in1) != elem(**out)",
                     "elem(**in2) != elem(**out)",
                     "upper(**in1) || upper(**in2)")]]
Remainder : public Vertex {
public:
  Vector<Input<Vector<InType, ONE_PTR>>, ONE_PTR> in1;
  Vector<Input<Vector<InType, ONE_PTR>>, ONE_PTR> in2;
  Vector<Output<Vector<InType>>> out;
  SimOnlyField<unsigned> dataPathWidth;

  bool compute() {
    for (unsigned i = 0; i != out.size(); ++i) {
      for (unsigned j = 0; j != out[i].size(); ++j) {
        if (std::is_same<InType, int>::value) {
          int r = in1[i][j] / in2[i][j];
          out[i][j] = in1[i][j] - r * in2[i][j];
        } else {
          out[i][j] = std::fmod(float(in1[i][j]),
                                float(in2[i][j]));
        }
      }
    }
    return true;
  }
};

template class Remainder<float>;
template class Remainder<half>;
template class Remainder<int>;

template <typename InType>
class Round : public Vertex {
public:
  Vector<Input<Vector<InType, ONE_PTR>>, ONE_PTR> in;
  Vector<Output<Vector<InType>>> out;
  SimOnlyField<unsigned> dataPathWidth;

  bool compute() {
    for (unsigned i = 0; i != out.size(); ++i) {
      for (unsigned j = 0; j != out[i].size(); ++j) {
        out[i][j] =  std::round(in[i][j]);
      }
    }
    return true;
  }
};

template class Round<float>;
template class Round<half>;

template <typename InType>
class ShiftLeft : public Vertex {
public:
  Vector<Input<Vector<InType, ONE_PTR>>, ONE_PTR> in1;
  Vector<Input<Vector<InType, ONE_PTR>>, ONE_PTR> in2;
  Vector<Output<Vector<InType>>> out;
  SimOnlyField<unsigned> dataPathWidth;

  bool compute() {
    for (unsigned i = 0; i != out.size(); ++i) {
      for (unsigned j = 0; j != out[i].size(); ++j) {
        out[i][j] = in1[i][j] << in2[i][j];
      }
    }
    return true;
  }
};

template class ShiftLeft<int>;

template <typename InType>
class ShiftRight : public Vertex {
public:
  Vector<Input<Vector<InType, ONE_PTR>>, ONE_PTR> in1;
  Vector<Input<Vector<InType, ONE_PTR>>, ONE_PTR> in2;
  Vector<Output<Vector<InType>>> out;
  SimOnlyField<unsigned> dataPathWidth;

  bool compute() {
    for (unsigned i = 0; i != out.size(); ++i) {
      for (unsigned j = 0; j != out[i].size(); ++j) {
        out[i][j] = (unsigned)in1[i][j] >> in2[i][j];
      }
    }
    return true;
  }
};

template class ShiftRight<int>;

template <typename InType>
class ShiftRightSignExtend : public Vertex {
public:
  Vector<Input<Vector<InType, ONE_PTR>>, ONE_PTR> in1;
  Vector<Input<Vector<InType, ONE_PTR>>, ONE_PTR> in2;
  Vector<Output<Vector<InType>>> out;
  SimOnlyField<unsigned> dataPathWidth;

  bool compute() {
    for (unsigned i = 0; i != out.size(); ++i) {
      for (unsigned j = 0; j != out[i].size(); ++j) {
        out[i][j] = in1[i][j] >> in2[i][j];
      }
    }
    return true;
  }
};

template class ShiftRightSignExtend<int>;

template <typename InType>
class
[[poplar::constraint("elem(**in) != elem(**out)")]]
Signum : public Vertex {
public:
  Vector<Input<Vector<InType, ONE_PTR>>, ONE_PTR> in;
  Vector<Output<Vector<InType>>> out;
  SimOnlyField<unsigned> dataPathWidth;

  bool compute() {
    for (unsigned i = 0; i != out.size(); ++i) {
      for (unsigned j = 0; j != out[i].size(); ++j) {
        out[i][j] =  (0 < in[i][j]) - (in[i][j] < 0);
      }
    }
    return true;
  }
};

template class Signum<float>;
template class Signum<half>;
template class Signum<int>;

template <typename InType>
class Sin : public Vertex {
public:
  Vector<Input<Vector<InType, ONE_PTR>>, ONE_PTR> in;
  Vector<Output<Vector<InType>>> out;
  SimOnlyField<unsigned> dataPathWidth;

  bool compute() {
    for (unsigned i = 0; i != out.size(); ++i) {
      for (unsigned j = 0; j != out[i].size(); ++j) {
        out[i][j] = std::sin(in[i][j]);
      }
    }
    return true;
  }
};

template class Sin<float>;
template class Sin<half>;

template <typename InType>
class
[[poplar::constraint("elem(**in1) != elem(**in2)",
                     "upper(**in1) || upper(**in2)")]]
Subtract : public Vertex {
public:
  Vector<Input<Vector<InType, ONE_PTR>>, ONE_PTR> in1;
  Vector<Input<Vector<InType, ONE_PTR>>, ONE_PTR> in2;
  Vector<Output<Vector<InType>>> out;
  SimOnlyField<unsigned> dataPathWidth;

  bool compute() {
    for (unsigned i = 0; i != out.size(); ++i) {
      for (unsigned j = 0; j != out[i].size(); ++j) {
        out[i][j] = in1[i][j] - in2[i][j];
      }
    }
    return true;
  }
};

template class Subtract<float>;
template class Subtract<half>;
template class Subtract<int>;
template class Subtract<unsigned>;


template <typename InType>
class Tanh : public Vertex {
public:
  Vector<Input<Vector<InType, ONE_PTR>>, ONE_PTR> in;
  Vector<Output<Vector<InType>>> out;
  SimOnlyField<unsigned> dataPathWidth;

  bool compute() {
    for (unsigned i = 0; i != out.size(); ++i) {
      for (unsigned j = 0; j != out[i].size(); ++j) {
        out[i][j] = std::tanh(in[i][j]);
      }
    }
    return true;
  }
};

template class Tanh<float>;
template class Tanh<half>;


template <typename InType>
class Sqrt : public Vertex {
public:
  Vector<Input<Vector<InType, ONE_PTR>>, ONE_PTR> in;
  Vector<Output<Vector<InType>>> out;
  SimOnlyField<unsigned> dataPathWidth;

  bool compute() {
    for (unsigned i = 0; i != out.size(); ++i) {
      for (unsigned j = 0; j != out[i].size(); ++j) {
        out[i][j] = std::sqrt(in[i][j]);
      }
    }
    return true;
  }
};

template class Sqrt<float>;
template class Sqrt<half>;

template <typename InType>
class
[[poplar::constraint("elem(**in) != elem(**out)")]]
Square : public Vertex {
public:
  Vector<Input<Vector<InType, ONE_PTR>>, ONE_PTR> in;
  Vector<Output<Vector<InType>>> out;
  SimOnlyField<unsigned> dataPathWidth;

  bool compute() {
    for (unsigned i = 0; i != out.size(); ++i) {
      for (unsigned j = 0; j != out[i].size(); ++j) {
        out[i][j] = in[i][j] * in[i][j];
      }
    }
    return true;
  }
};

template class Square<float>;
template class Square<half>;

template <typename InType>
class Clamp : public Vertex {
public:
  Vector<Input<Vector<InType, ONE_PTR>>, ONE_PTR> in1;
  Vector<Input<Vector<InType, ONE_PTR>>, ONE_PTR> in2;  // lower bound
  Vector<Input<Vector<InType, ONE_PTR>>, ONE_PTR> in3;  // upper bound
  Vector<Output<Vector<InType>>> out;
  SimOnlyField<unsigned> dataPathWidth;

  bool compute() {
    for (unsigned i = 0; i != out.size(); ++i) {

      for (unsigned j = 0; j != out[i].size(); ++j) {
        out[i][j] = in1[i][j];
        if (out[i][j] < in2[i][j]) {
          out[i][j] = in2[i][j];
        }
        if (out[i][j] > in3[i][j]) {
          out[i][j] = in3[i][j];
        }
      }
    }
    return true;
  }
};

template class Clamp<float>;
template class Clamp<half>;
template class Clamp<int>;

template <typename InType>
class Select : public Vertex {
public:
  Vector<Input<Vector<InType, ONE_PTR>>, ONE_PTR> in1;
  Vector<Input<Vector<InType, ONE_PTR>>, ONE_PTR> in2;
  Vector<Input<Vector<bool>>> in3;
  Vector<Output<Vector<InType>>> out;
  SimOnlyField<unsigned> dataPathWidth;

  bool compute() {
    for (unsigned i = 0; i != out.size(); ++i) {
      for (unsigned j = 0; j != out[i].size(); ++j) {
        out[i][j] = in3[i][j] ? in1[i][j] : in2[i][j];
      }
    }
    return true;
  }
};

template class Select<float>;
template class Select<half>;
template class Select<int>;
template class Select<bool>;

}