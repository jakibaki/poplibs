#include "ExprOpUtil.hpp"
#include "poputil/exceptions.hpp"

namespace popops {
namespace expr {

std::string unaryOpTypeToString(UnaryOpType op) {
  switch(op) {
  case UnaryOpType::ABSOLUTE:
    return "ABSOLUTE";
  case UnaryOpType::BITWISE_NOT:
    return "BITWISE_NOT";
  case UnaryOpType::CEIL:
    return "CEIL";
  case UnaryOpType::COS:
    return "COS";
  case UnaryOpType::COUNT_LEADING_ZEROS:
    return "COUNT_LEADING_ZEROS";
  case UnaryOpType::EXPONENT:
    return "EXPONENT";
  case UnaryOpType::EXPONENT_MINUS_ONE:
    return "EXPONENT_MINUS_ONE";
  case UnaryOpType::FLOOR:
    return "FLOOR";
  case UnaryOpType::INVERSE:
    return "INVERSE";
  case UnaryOpType::IS_FINITE:
    return "IS_FINITE";
  case UnaryOpType::LOGARITHM:
    return "LOGARITHM";
  case UnaryOpType::LOGARITHM_ONE_PLUS:
    return "LOGARITHM_ONE_PLUS";
  case UnaryOpType::LOGICAL_NOT:
    return "LOGICAL_NOT";
  case UnaryOpType::NEGATE:
    return "NEGATE";
  case UnaryOpType::POPCOUNT:
    return "POPCOUNT";
  case UnaryOpType::ROUND:
    return "ROUND";
  case UnaryOpType::SIGNUM:
    return "SIGNUM";
  case UnaryOpType::SIN:
    return "SIN";
  case UnaryOpType::TANH:
    return "TANH";
  case UnaryOpType::SQRT:
    return "SQRT";
  case UnaryOpType::SQUARE:
    return "SQUARE";
  case UnaryOpType::SIGMOID:
    return "SIGMOID";
  case UnaryOpType::RSQRT:
    return "RSQRT";
  }
  throw poputil::poplibs_error("Op not supported");
}

std::string binaryOpTypeToString(BinaryOpType op) {
  switch(op) {
    case BinaryOpType::ADD:
      return "ADD";
    case BinaryOpType::ATAN2:
      return "ATAN2";
    case BinaryOpType::BITWISE_AND:
      return "BITWISE_AND";
    case BinaryOpType::BITWISE_OR:
      return "BITWISE_OR";
    case BinaryOpType::DIVIDE:
      return "DIVIDE";
    case BinaryOpType::EQUAL:
      return "EQUAL";
    case BinaryOpType::GREATER_THAN_EQUAL:
      return "GREATER_THAN_EQUAL";
    case BinaryOpType::GREATER_THAN:
      return "GREATER_THAN";
    case BinaryOpType::LESS_THAN_EQUAL:
      return "LESS_THAN_EQUAL";
    case BinaryOpType::LOGICAL_AND:
      return "LOGICAL_AND";
    case BinaryOpType::LOGICAL_OR:
      return "LOGICAL_OR";
    case BinaryOpType::LESS_THAN:
      return "LESS_THAN";
    case BinaryOpType::MAXIMUM:
      return "MAXIMUM";
    case BinaryOpType::MINIMUM:
      return "MINIMUM";
    case BinaryOpType::MULTIPLY:
      return "MULTIPLY";
    case BinaryOpType::NOT_EQUAL:
      return "NOT_EQUAL";
    case BinaryOpType::POWER:
      return "POWER";
    case BinaryOpType::REMAINDER:
      return "REMAINDER";
    case BinaryOpType::SHIFT_LEFT:
      return "SHIFT_LEFT";
    case BinaryOpType::SHIFT_RIGHT:
      return "SHIFT_RIGHT";
    case BinaryOpType::SHIFT_RIGHT_SIGN_EXTEND:
      return "SHIFT_RIGHT_SIGN_EXTEND";
    case BinaryOpType::SUBTRACT:
      return "SUBTRACT";
  }
  throw poputil::poplibs_error("Op not supported");
}

std::string broadcastOpTypeToString(BroadcastOpType op) {
  switch(op) {
    case BroadcastOpType::ADD:
      return "ADD";
    case BroadcastOpType::INV_STD_DEV_TO_VARIANCE:
      return "INV_STD_DEV_TO_VARIANCE";
    case BroadcastOpType::MULTIPLY:
      return "MULTIPLY";
    case BroadcastOpType::SUBTRACT:
      return "SUBTRACT";
    case BroadcastOpType::VARIANCE_TO_INV_STD_DEV:
      return "VARIANCE_TO_INV_STD_DEV";
  }
  throw poputil::poplibs_error("Op not supported");
}

}} // end namespace popops::expr
