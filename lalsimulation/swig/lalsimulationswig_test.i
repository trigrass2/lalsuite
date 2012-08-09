// Tests of SWIG interface code
// Author: Karl Wette, 2011, 2012

// Only in debug mode.
#ifndef NDEBUG

// Include LAL test code.
#include <lal/lalswig_test.i>

// Test object parent tracking between modules
typedef struct taglalsimulationswig_test_parent_map {
  lalswig_test_struct s;
} lalsimulationswig_test_parent_map_struct;
lalsimulationswig_test_parent_map_struct lalsimulationswig_test_parent_map;

#endif // !NDEBUG