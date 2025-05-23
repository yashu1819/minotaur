// 
//     Minotaur -- It's only 1/2 bull
// 
//     (C)opyright 2009 - 2025 The Minotaur Team.
// 

#include "MinotaurConfig.h"
#include "AMPLBqpdUT.h"
#include "AMPLJacobian.h"
#include "AMPLHessian.h"
#include "BranchAndBound.h"
#include "BqpdEngine.h"
#include "Environment.h"
#include "Option.h"

CPPUNIT_TEST_SUITE_REGISTRATION(AMPLBqpdUT);
CPPUNIT_TEST_SUITE_NAMED_REGISTRATION(AMPLBqpdUT, "AMPLBqpdUT");
using namespace MINOTAUR_AMPL;
using namespace std;

void AMPLBqpdUT::testNLP()
{
  Minotaur::EnvPtr env = (Minotaur::EnvPtr) new Minotaur::Environment();
  double solval = 0.0;
  env->setLogLevel(Minotaur::LogNone);
  iface_ = (AMPLInterfacePtr) new AMPLInterface(env);

  // read an instance
  Minotaur::ProblemPtr inst = iface_->readInstance("instances/hs021");

  // set starting point if any
  inst->setInitialPoint(iface_->getInitialPoint());

  inst->calculateSize();

  if (inst->isQuadratic() || inst->isLinear()) { 
    inst->setNativeDer();
  } else {
    // create the jacobian
    Minotaur::JacobianPtr jPtr = (AMPLJacobianPtr) new AMPLJacobian(iface_);
    inst->setJacobian(jPtr);

    // create the hessian
    Minotaur::HessianOfLagPtr hPtr = (AMPLHessianPtr) new AMPLHessian(iface_);
    inst->setHessian(hPtr);
  }

  //create a new engine
  Minotaur::BqpdEnginePtr bqpd_e = (Minotaur::BqpdEnginePtr) 
    new Minotaur::BqpdEngine(env);

  //load the problem
  bqpd_e->load(inst);

  //solve
  bqpd_e->solve();

  // get status
  Minotaur::EngineStatus status = bqpd_e->getStatus();
  CPPUNIT_ASSERT(status==Minotaur::ProvenLocalOptimal);

  // get objvalue
  solval = bqpd_e->getSolutionValue();
  CPPUNIT_ASSERT(fabs(solval+99.96) < 1e-7);

  delete bqpd_e;
  delete inst;
  delete iface_;
  delete env;
}

