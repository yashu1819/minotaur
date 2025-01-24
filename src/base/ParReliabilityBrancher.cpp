//
//    Minotaur -- It's only 1/2 bull
//
//    (C)opyright 2008 - 2024 The Minotaur Team.
//

/**
 * \file ParReliabilityBrancher.cpp
 * \brief Define methods for reliability branching in a parallel framework.
 * \author Prashant Palkar, Meenarli Sharma, Ashutosh Mahajan, IIT Bombay
 */

#include <algorithm>
#include <cmath>
#include <iomanip>

#include "MinotaurConfig.h"
#include "Branch.h"
#include "BrCand.h"
#include "BrVarCand.h"
#include "Engine.h"
#include "Environment.h"
#include "Handler.h"
#include "Logger.h"
#include "Modification.h"
#include "Node.h"
#include "Option.h"
#include "ProblemSize.h"
#include "Relaxation.h"
#include "ParReliabilityBrancher.h"
#include "Solution.h"
#include "SolutionPool.h"
#include "Timer.h"
#include "Variable.h"
#include <omp.h>

//#define SPEW 1

using namespace Minotaur;

const std::string ParReliabilityBrancher::me_ = "reliability brancher: "; 

ParReliabilityBrancher::ParReliabilityBrancher(EnvPtr env, HandlerVector & handlers) 
: engine_(EnginePtr()),             // NULL
  eTol_(1e-6),
  handlers_(handlers),              // Create a copy, the vector is not too big
  init_(false),
  maxDepth_(1000),
  maxIterations_(25),
  maxStrongCands_(20),
  minNodeDist_(50),
  rel_(RelaxationPtr()),            // NULL
  status_(NotModifiedByBrancher),
  thresh_(4),
  trustCutoff_(true),
  x_(0)
{
  timer_ = env->getNewTimer();
  logger_ = env->getLogger();
  stats_ = new ParRelBrStats();
  stats_->calls = 0;
  stats_->engProbs = 0;
  stats_->strBrCalls = 0;
  stats_->bndChange = 0;
  stats_->iters = 0;
  stats_->strTime = 0.0;
}


ParReliabilityBrancher::~ParReliabilityBrancher()
{
  delete stats_;
  delete timer_;
}


BrCandPtr ParReliabilityBrancher::findBestCandidate_(const double objval, 
                                                  double cutoff, NodePtr node,
                                                  DoubleVector pseudoUp,
                                                  DoubleVector pseudoDown,
                                                  UInt nodesProc)
{
  double best_score = -INFINITY;
  double score, change_up, change_down, maxchange;
  UInt cnt, maxcnt;
  EngineStatus status_up, status_down;
  BrCandPtr cand, best_cand = 0;

  // first evaluate candidates that have reliable pseudo costs
  for (BrCandVIter it=relCands_.begin(); it!=relCands_.end(); ++it) {
    getPCScore_(*it, &change_down, &change_up, &score, pseudoUp, pseudoDown);
    if (score > best_score) {
      best_score = score;
      best_cand = *it;
      if (change_up > change_down) {
        best_cand->setDir(DownBranch);
      } else {
        best_cand->setDir(UpBranch);
      }
    }
  }

  maxchange = cutoff-objval;
  // now do strong branching on unreliable candidates
  if (unrelCands_.size()>0) {
    BrCandVIter it;
    engine_->enableStrBrSetup();
    engine_->setIterationLimit(maxIterations_); // TODO: make limit dynamic.
    cnt = 0;
    maxcnt = (node->getDepth()>maxDepth_) ? 0 : maxStrongCands_;
    for (it=unrelCands_.begin(); it!=unrelCands_.end() && 
        cnt < maxcnt; ++it, ++cnt) {
      cand = *it;
      strongBranch_(cand, change_up, change_down, status_up, status_down);
      change_up    = std::max(change_up - objval, 0.0);
      change_down  = std::max(change_down - objval, 0.0);
      useStrongBranchInfo_(cand, maxchange, change_up, change_down, 
          status_up, status_down);
      score = getScore_(change_up, change_down);
      lastStrBranched_[cand->getPCostIndex()] = nodesProc + 1;
      // to also accommodate node resolves, use below
      //lastStrBranched_[cand->getPCostIndex()] = stats_->calls;
#if SPEW
      writeScore_(cand, score, change_up, change_down);
#endif
      if (status_!=NotModifiedByBrancher) {
        break;
      }
      if (score > best_score) {
        best_score = score;
        best_cand = cand;
        if (change_up > change_down) {
          best_cand->setDir(DownBranch);
        } else {
          best_cand->setDir(UpBranch);
        }
      }
    }
    engine_->resetIterationLimit(); 
    engine_->disableStrBrSetup();
    if (NotModifiedByBrancher == status_) {
      // get score of remaining unreliable candidates as well.
      for (;it!=unrelCands_.end(); ++it) {
        getPCScore_(*it, &change_down, &change_up, &score, pseudoUp, pseudoDown);
        if (score > best_score) {
          best_score = score;
          best_cand = *it;
          if (change_up > change_down) {
            best_cand->setDir(DownBranch);
          } else {
            best_cand->setDir(UpBranch);
          }
        }
      }
    }
  }
  return best_cand;
}


Branches ParReliabilityBrancher::findBranches(RelaxationPtr rel, NodePtr node, 
                        ConstSolutionPtr sol, SolutionPoolPtr s_pool, 
                        BrancherStatus & br_status, ModVector &mods,
                        UIntVector timesUp, UIntVector timesDown,
                        DoubleVector pseudoUp, DoubleVector pseudoDown,
                        UInt nodesProc)
{
  Branches branches = 0;
  BrCandPtr br_can = 0; 
  const double *x = sol->getPrimal();

  ++(stats_->calls);
  if (!init_) {
    init_ = true;
    initialize(rel);
  }
  rel_ = rel;
  br_status = NotModifiedByBrancher;
  status_ = NotModifiedByBrancher;
  mods_.clear();

  // make a copy of x, because it is overwritten while strong branching.
  x_.resize(rel->getNumVars());
  std::copy(x, x+rel->getNumVars(), x_.begin());

  findCandidates_(&timesUp, &timesDown, &pseudoUp, &pseudoDown, nodesProc);
  if (status_ == PrunedByBrancher) {
    br_status = status_;
    return 0;
  }

  if (status_ == NotModifiedByBrancher) {
//#pragma omp critical (solPool) // reading best solution value
    br_can = findBestCandidate_(sol->getObjValue(), 
                                s_pool->getBestSolutionValue(), node,
                                pseudoUp, pseudoDown, nodesProc);
  }

  if (status_ == NotModifiedByBrancher) {
    // surrounded by br_can :-)
//#pragma omp critical (solPool) // can be handled at IntVarHandler?
    branches = br_can->getHandler()->getBranches(br_can, x_, rel_, s_pool);
    for (BranchConstIterator br_iter=branches->begin();
        br_iter!=branches->end(); ++br_iter) {
      (*br_iter)->setBrCand(br_can);
    }
#if SPEW
    logger_->msgStream(LogDebug) << me_ << "best candidate = "
      << br_can->getName() << std::endl;
#endif
  } else {
    // we found some modifications that can be done to the node. Send these
    // back to the processor.
    if (mods_.size()>0) {
      mods.insert(mods.end(),mods_.begin(),mods_.end());
    }
    br_status = status_;
#if SPEW
    logger_->msgStream(LogDebug) << me_ << "found modifications"
                                 << std::endl;
    if (mods_.size()>0) {
      for (ModificationConstIterator miter=mods_.begin(); miter!=mods_.end();
           ++miter) {
        (*miter)->write(logger_->msgStream(LogDebug));
      }
    } else if (status_==PrunedByBrancher) {
      logger_->msgStream(LogDebug) << me_ << "Pruned." << std::endl;
    } else {
      logger_->msgStream(LogDebug) << me_ << "unexpected status = "
                                   << status_ << std::endl;
    }
#endif
  }

  freeCandidates_(br_can);
  if (status_ != NotModifiedByBrancher && br_can) {
    delete br_can;
  }
  return branches;
}


void ParReliabilityBrancher::findCandidates_(UIntVector *timesUp,
                                             UIntVector *timesDown,
                                             DoubleVector *pseudoUp,
                                             DoubleVector *pseudoDown,
                                             UInt nodesProc)
{
  VariableIterator v_iter, v_iter2, best_iter;
  VariableConstIterator cv_iter;
  int index;
  bool is_inf = false;   // if true, then node can be pruned.

  BrVarCandSet cands;       // candidates from which to choose one.
  BrVarCandSet cands2;      // Temporary set.
  BrCandVector gencands;
  BrCandVector gencands2;   // Temporary vector.
  double s_wt = 1e-5;
  double i_wt = 1e-6;
  double score;

  assert(relCands_.empty());
  assert(unrelCands_.empty());

  for (HandlerIterator h = handlers_.begin(); h != handlers_.end(); ++h) {
    // ask each handler to give some candidates
    (*h)->getBranchingCandidates(rel_, x_, mods_, cands2, gencands2, is_inf);
    for (BrVarCandIter it = cands2.begin(); it != cands2.end(); ++it) {
      (*it)->setHandler(*h);
    }
    for (BrCandVIter it = gencands2.begin(); it != gencands2.end(); ++it) {
      (*it)->setHandler(*h);
    }
    cands.insert(cands2.begin(), cands2.end());
    gencands.insert(gencands.end(), gencands2.begin(), gencands2.end());
    cands2.clear();
    gencands2.clear();
    if (is_inf || mods_.size() > 0) {
      for (BrVarCandIter it=cands.begin(); it!=cands.end(); ++it) {
        delete *it;
      }
      for (BrCandVIter it=gencands.begin(); it!=gencands.end(); ++it) {
        delete *it;
      }
      if (is_inf) {
        status_ = PrunedByBrancher;
      } else {
        status_ = ModifiedByBrancher;
      }
      return;
    } 
  }

  // visit each candidate in and check if it has reliable pseudo costs.
  for (BrVarCandIter it=cands.begin(); it!=cands.end(); ++it) {
    index = (*it)->getPCostIndex();
    // global update of times and pseudoCost info
    (*timesUp)[index] += timesUp_[index];
    (*timesDown)[index] += timesDown_[index];
    if ((*timesUp)[index]) {
      (*pseudoUp)[index] = ((*pseudoUp)[index] + pseudoUp_[index]*timesUp_[index])/(*timesUp)[index];
    }
    if ((*timesDown)[index]) {
      (*pseudoDown)[index] = ((*pseudoDown)[index] + pseudoDown_[index]*timesDown_[index])/(*timesDown)[index];
    }
    // to also accommodate node resolves, use below commented line
    //if ((minNodeDist_ > fabs(stats_->calls-lastStrBranched_[index])) ||
    if ((minNodeDist_ > fabs(nodesProc-lastStrBranched_[index])) ||
        ((*timesUp)[index] >= thresh_ && (*timesDown)[index] >= thresh_)) {
      relCands_.push_back(*it);
    } else {
      score = (*timesUp)[index] + (*timesDown)[index]
        -s_wt*((*pseudoUp)[index] + (*pseudoDown)[index])
        -i_wt*std::max((*it)->getDDist(), (*it)->getUDist());
      (*it)->setScore(score);
      unrelCands_.push_back(*it);
    }
  }
  // push all general candidates (that are not variables) as reliable
  // candidates
  for (BrCandVIter it=gencands.begin(); it!=gencands.end(); ++it) {
    relCands_.push_back(*it);
  }

  // sort unreliable candidates in the increasing order of their reliability.
  std::sort(unrelCands_.begin(), unrelCands_.end(), CompareScore);

#if SPEW
  logger_->msgStream(LogDebug) << me_
                               << "number of reliable candidates = " 
                               << relCands_.size() << std::endl 
                               << me_
                               << "number of unreliable candidates = " 
                               << unrelCands_.size() << std::endl;
  if (logger_->getMaxLevel() == LogDebug2) {
    writeScores_(logger_->msgStream(LogDebug2));
  }
#endif

  return;
}


void ParReliabilityBrancher::freeCandidates_(BrCandPtr no_del)
{
  for (BrCandVIter it=unrelCands_.begin(); it!=unrelCands_.end(); ++it) {
    if (no_del != *it) {
      delete *it;
    }
  }
  for (BrCandVIter it=relCands_.begin(); it!=relCands_.end(); ++it) {
    if (no_del != *it) {
      delete *it;
    }
  }
  relCands_.clear();
  unrelCands_.clear();
}


bool ParReliabilityBrancher::getTrustCutoff()
{
  return trustCutoff_;
}


UInt ParReliabilityBrancher::getIterLim() 
{
  return maxIterations_;
}


std::string ParReliabilityBrancher::getName() const
{
  return "ParReliabilityBrancher";
}


void ParReliabilityBrancher::getPCScore_(BrCandPtr cand, double *ch_down, 
                                      double *ch_up, double *score,
                                      DoubleVector pseudoUp, DoubleVector pseudoDown) 
{
  int index = cand->getPCostIndex();
  if (index>-1) {
    *ch_down   = cand->getDDist()*pseudoDown[index];
    *ch_up     = cand->getUDist()*pseudoUp[index];
    *score     = getScore_(*ch_up, *ch_down);
  } else {
    *ch_down   = 0.0;
    *ch_up     = 0.0;
    *score     = cand->getScore();
  }
}


double ParReliabilityBrancher::getScore_(const double & up_score, 
                                      const double & down_score)
{
  if (up_score>down_score) {
    return down_score*0.8 + up_score*0.2;
  } else {
    return up_score*0.8 + down_score*0.2;
  }
  return 0.;
}


UInt ParReliabilityBrancher::getThresh() const
{
  return thresh_;
}


void ParReliabilityBrancher::initialize(RelaxationPtr rel)
{
  int n = rel->getNumVars();
  // initialize to zero.
  pseudoUp_ = DoubleVector(n,0.); 
  pseudoDown_ = DoubleVector(n,0.); 
  lastStrBranched_ = UIntVector(n,20000);
  timesUp_ = std::vector<UInt>(n,0); 
  timesDown_ = std::vector<UInt>(n,0); 

  // reserve space.
  relCands_.reserve(n);
  unrelCands_.reserve(n);
  x_.reserve(n);
}


void ParReliabilityBrancher::setTrustCutoff(bool val)
{
  trustCutoff_ = val;
}


void ParReliabilityBrancher::setEngine(EnginePtr engine)
{
  engine_ = engine;
}


void ParReliabilityBrancher::setIterLim(UInt k) 
{
  maxIterations_ = k;
}


void ParReliabilityBrancher::setMaxDepth(UInt k) 
{
  maxDepth_ = k;
}


void ParReliabilityBrancher::setMinNodeDist(UInt k) 
{
  minNodeDist_ = k;
}


void ParReliabilityBrancher::setThresh(UInt k) 
{
  thresh_ = k;
}


bool ParReliabilityBrancher::shouldPrune_(const double &chcutoff, 
                                       const double &change,
                                       const EngineStatus & status,
                                       bool *is_rel)
{
  switch (status) {
   case (ProvenLocalInfeasible):
     return true;
   case (ProvenInfeasible):
     return true;
   case (ProvenObjectiveCutOff):
     return true;
   case (ProvenLocalOptimal):
   case (ProvenOptimal):
     if (trustCutoff_ && change>chcutoff-eTol_) {
       return true;
     }
     // check feasiblity
     break;
   case (EngineUnknownStatus):
     assert (!"engine status is UnknownStatus in reliability branching!");
     break;
   case (EngineIterationLimit):
     break;
   case (ProvenFailedCQFeas):
   case (ProvenFailedCQInfeas):
     logger_->msgStream(LogInfo) << me_ << "Failed CQ." <<
     " Continuing." << std::endl;
     *is_rel = false;
     break;
   default:
     logger_->errStream() << me_ << "unexpected engine status. " 
                          << "status = " << status << std::endl;
     *is_rel = false;
     stats_->engProbs += 1;
     break;
  }
  return false;
}


void ParReliabilityBrancher::strongBranch_(BrCandPtr cand, double & obj_up, 
                                        double & obj_down, 
                                        EngineStatus & status_up, 
                                        EngineStatus & status_down)
{
  HandlerPtr h = cand->getHandler();
  ModificationPtr mod;

  // first do down.
  mod = h->getBrMod(cand, x_, rel_, DownBranch);
  mod->applyToProblem(rel_);

  timer_->start();
  status_down = engine_->solve();
  stats_->strTime += timer_->query();
  timer_->stop();
  ++(stats_->strBrCalls);
  obj_down = engine_->getSolutionValue();
  mod->undoToProblem(rel_);
  delete mod;

  // now go up.
  mod = h->getBrMod(cand, x_, rel_, UpBranch);
  mod->applyToProblem(rel_);

  timer_->start();
  status_up = engine_->solve();
  stats_->strTime += timer_->query();
  timer_->stop();
  ++(stats_->strBrCalls);
  obj_up = engine_->getSolutionValue();
  mod->undoToProblem(rel_);
  delete mod;
}


void ParReliabilityBrancher::updateAfterSolve(NodePtr node, ConstSolutionPtr sol)
{
  const double *x = sol->getPrimal();
  NodePtr parent = node->getParent();
  if (parent) {
    BrCandPtr cand = node->getBranch()->getBrCand();
    int index = cand->getPCostIndex();
    if (index>-1) {
      double oldval = node->getBranch()->getActivity();
      double newval = x[index];
      double cost = (node->getLb()-parent->getLb()) / 
                    (fabs(newval - oldval)+eTol_);
      if (cost < 0. || std::isinf(cost) || std::isnan(cost)) {
        cost = 0.;
      }
      if (newval < oldval) {
        updatePCost_(index, cost, pseudoDown_, timesDown_);
      } else {
        updatePCost_(index, cost, pseudoUp_, timesUp_);
      }
    } 
  }
}


void ParReliabilityBrancher::updatePCost_(const int & i, const double & new_cost, 
                                       DoubleVector & cost, UIntVector & count)
{
  cost[i]   = (cost[i]*count[i] + new_cost)/(count[i]+1);
  count[i] += 1;
}


void ParReliabilityBrancher::useStrongBranchInfo_(BrCandPtr cand, 
                                               const double &chcutoff, 
                                               double &change_up, 
                                               double &change_down,
                                               const EngineStatus & status_up,
                                               const EngineStatus & status_down)
{
  const UInt index        = cand->getPCostIndex();
  bool should_prune_up    = false;
  bool should_prune_down  = false;
  bool is_rel = true;
  double cost;

  should_prune_down = shouldPrune_(chcutoff, change_down, status_down, &is_rel);
  should_prune_up   = shouldPrune_(chcutoff, change_up, status_up, &is_rel);

  if (!is_rel) {
    change_up = 0.;
    change_down = 0.;
  } else if (should_prune_up == true && should_prune_down == true) {
    status_ = PrunedByBrancher;
    stats_->bndChange += 2;
  } else if (should_prune_up) {
    status_ = ModifiedByBrancher;
    mods_.push_back(cand->getHandler()->getBrMod(cand, x_, rel_, DownBranch));
    ++(stats_->bndChange);
  } else if (should_prune_down) {
    status_ = ModifiedByBrancher;
    mods_.push_back(cand->getHandler()->getBrMod(cand, x_, rel_, UpBranch));
    ++(stats_->bndChange);
  } else { 
    cost = fabs(change_down)/(fabs(cand->getDDist())+eTol_);
    updatePCost_(index, cost, pseudoDown_, timesDown_);

    cost = fabs(change_up)/(fabs(cand->getUDist())+eTol_);
    updatePCost_(index, cost, pseudoUp_, timesUp_);
  }
}


void ParReliabilityBrancher::writeScore_(BrCandPtr cand, double score, 
                                      double change_up, double change_down)
{
  logger_->msgStream(LogDebug2) << me_ << "candidate: " << cand->getName() 
                                << " lstStr = " << lastStrBranched_[cand->getPCostIndex()]
                                << " down change = " << change_down
                                << " up change = " << change_up
                                << " score = " << score
                                << std::endl;
}


void ParReliabilityBrancher::writeScores_(std::ostream &out)
{
  out << me_ << "unreliable candidates:" << std::endl;
  for (BrCandVIter it=unrelCands_.begin(); it!=unrelCands_.end(); ++it) {
    if ((*it)->getPCostIndex()>-1) {
      out << std::setprecision(6) << (*it)->getName() << "\t" 
        << timesDown_[(*it)->getPCostIndex()] << "\t"
        << timesUp_[(*it)->getPCostIndex()] << "\t" 
        << pseudoDown_[(*it)->getPCostIndex()] << "\t"
        << pseudoUp_[(*it)->getPCostIndex()] << "\t"
        << x_[(*it)->getPCostIndex()] << "\t"
        << rel_->getVariable((*it)->getPCostIndex())->getLb() << "\t"
        << rel_->getVariable((*it)->getPCostIndex())->getUb() << "\t"
        << std::endl;
    } else {
      out << std::setprecision(6) << (*it)->getName() << "\t" 
                                  << 0 << "\t" << 0 << "\t"
                                  << (*it)->getScore() << "\t"
                                  << (*it)->getScore() << "\t"
                                  << (*it)->getDDist() << "\t"
                                  << 0.0 << "\t"
                                  << 1.0 << "\t" << std::endl;
    }
  }

  out << me_ << "reliable candidates:" << std::endl;
  for (BrCandVIter it=relCands_.begin(); it!=relCands_.end(); ++it) {
    if ((*it)->getPCostIndex()>-1) {
      out << (*it)->getName() << "\t" 
        << timesDown_[(*it)->getPCostIndex()] << "\t"
        << timesUp_[(*it)->getPCostIndex()] << "\t" 
        << pseudoDown_[(*it)->getPCostIndex()] << "\t"
        << pseudoUp_[(*it)->getPCostIndex()] << "\t"
        << x_[(*it)->getPCostIndex()] << "\t"
        << rel_->getVariable((*it)->getPCostIndex())->getLb() << "\t"
        << rel_->getVariable((*it)->getPCostIndex())->getUb() << "\t"
        << std::endl;
    } else {
      out << std::setprecision(6) << (*it)->getName() << "\t" 
          << 0 << "\t" << 0 << "\t"
          << (*it)->getScore() << "\t"
          << (*it)->getScore() << "\t"
          << (*it)->getDDist() << "\t"
          << 0.0 << "\t"
          << 1.0 << "\t" << std::endl;
    }
  }
}


void ParReliabilityBrancher::writeStats(std::ostream &out) const
{
  if (stats_) {
    out << me_ << "times called                = " << stats_->calls 
      << std::endl
      << me_ << "no. of problems in engine   = " << stats_->engProbs
      << std::endl
      << me_ << "times relaxation solved     = " << stats_->strBrCalls
      << std::endl
      << me_ << "times bounds changed        = " << stats_->bndChange
      << std::endl
      << me_ << "time in solving relaxations = " << stats_->strTime
      << std::endl;
  }
}


