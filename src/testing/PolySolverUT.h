/* 
 *     Minotaur -- It's only 1/2 bull
 *
 *     (C)opyright 2009 - 2025 The Minotaur Team.
 */

#ifndef POLYSOLVERUT_H
#define POLYSOLVERUT_H

#include <string>

#include <cppunit/TestCase.h>
#include <cppunit/TestCaller.h>
#include <cppunit/TestSuite.h>
#include <cppunit/TestResult.h>
#include <cppunit/extensions/HelperMacros.h>

#include <AMPLInterface.h>
#include <Problem.h>

using namespace MINOTAUR_AMPL;


// read instance using ampl and test:
// Number of variables and their types,
// Number of constraints and their types,
// Function evaluations,
// Gradient evaluations,
// Hessian evaluations.

class PolySolverUT : public CppUnit::TestCase {

public:
  PolySolverUT(std::string name) : TestCase(name) {}
  PolySolverUT() {}

  void setUp();         
  void tearDown();   // need not implement

  void testSize();
  

  CPPUNIT_TEST_SUITE(PolySolverUT);
  CPPUNIT_TEST(testSize);

  CPPUNIT_TEST_SUITE_END();

private:
  Minotaur::EnvPtr env_;
  AMPLInterfacePtr iface_;
  Minotaur::ProblemPtr inst_;
};

#endif

