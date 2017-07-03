#define BOOST_TEST_MODULE ConvUtilTest
#include <boost/test/unit_test.hpp>
#include <popconv/ConvUtil.hpp>

BOOST_AUTO_TEST_CASE(getInputRangeLargeKernel) {
  auto params = popconv::ConvParams("float",
                                    {1, 3, 1, 1},
                                    {5, 1, 1, 1},
                                    {1, 1},
                                    {0, 0}, {0, 0}, {1, 1},
                                    {0, 0}, {0, 0}, {1, 1});
  auto inRange = popconv::getInputRange(0, {0, 2}, 1, params);
  BOOST_CHECK_EQUAL(inRange.first, 0);
  BOOST_CHECK_EQUAL(inRange.second, 2);
}