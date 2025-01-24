//
//     Minotaur -- It's only 1/2 bull
//
//     (C)opyright 2010 - 2024 The Minotaur Team.
//

/**
 * \file CutPool.h
 * \brief Declare class CutPool for storing cuts.
 * \author Ashutosh Mahajan, Argonne National Laboratory
 */

#ifndef MINOTAURCUTPOOL_H
#define MINOTAURCUTPOOL_H

#include "Types.h"

namespace Minotaur {

class Environment;
class Cut;
class Problem;
class Solution;

typedef Environment* EnvironmentPtr;
typedef Cut* CutPtr;
typedef Solution* SolutionPtr;

typedef std::vector<CutPtr> CutVector;
typedef std::deque<CutPtr> CutQ;

/**
 * The CutPool class is meant to store the cutting planes generated by
 * different cut generators and handlers. 
 */
class CutPool {

public:
  /// Default constructor.
  CutPool();

  /// Constructor that loads the relaxation problem. 
  CutManager(EnvPtr env);

  /// Destroy.
  ~CutManager();

  /// Add to pool.
  void addCut(CutPtr cut);

  /// Add to pool.
  void addCuts(CutVector cuts);

  /// Find duplicate.
  CutPtr findDup(CutPtr cut);

  /// Remove a cut.
  void removeCut(CutPtr cut);

  /// Remove a cut.
  void removeCuts(CutVector cuts);

  /// Write to output.
  void write(std::ostream &out) const;

private:
  /// Environment.
  EnvPtr env_;

  /// For logging.
  LoggerPtr logger_;

  /// Globally valid cuts that are in the relaxation. 
  CutQ glAct_;

  /// Globally valid cuts that are not in the relaxation. 
  CutQ glInact_;

  /// For logging.
  const static std::string me_;
};
}
#endif

