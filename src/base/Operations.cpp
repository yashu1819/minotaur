//
//     Minotaur -- It's only 1/2 bull
//
//     (C)opyright 2009 - 2025 The Minotaur Team.
//

/**
 * \file Operations.cpp
 * \brief Define some commonly used functions.
 * \author Ashutosh Mahajan, Argonne National Laboratory
 */

#include <cassert>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <sstream>

#include "MinotaurConfig.h"
#include "Constraint.h"
#include "Operations.h"
#include "Types.h"
#include "Variable.h"

using namespace Minotaur;

double Minotaur::InnerProduct(const VariableGroup& v1, const VariableGroup& v2)
{
  VariableGroup::const_iterator i1, i2, e1, e2;
  VariableGroup::key_compare compare = v1.key_comp();
  i1 = v1.begin();
  e1 = v1.end();
  i2 = v2.begin();
  e2 = v2.end();

  double sum = 0.0;
  while((i1 != e1) && (i2 != e2)) {
    if(compare(i1->first, i2->first)) {
      ++i1;
    } else if(compare(i2->first, i1->first)) {
      ++i2;
    } else {
      assert(i1->first == i2->first);
      sum += i1->second * i2->second;
      ++i1;
      ++i2;
    }
  }
  return sum;
}

//??? Should we just be using CoinPackedVector and such?
//  Ublas sparse vectors?
double Minotaur::InnerProduct(const std::vector<double>& x,
                              const VariableGroup& g)
{
  double sum = 0;
  for(VariableGroup::const_iterator it = g.begin(); it != g.end(); ++it) {
    sum += it->second * x[it->first->getIndex()];
  }
  return sum;
}

double Minotaur::InnerProduct(const double* x, const double* a, int n)
{
  double sum = 0;
  for(int i = 0; i < n; ++i) {
    sum += (*x) * (*a);
    ++x;
    ++a;
  }
  return sum;
}

bool Minotaur::isInt(double v, double tol)
{
  return (fabs(floor(v + 0.5) - v) < tol);
}

void Minotaur::symMatDotV(UInt nz, const double* mat, const UInt* irow,
                          const UInt* jcol, const double* v, double* prod)
{
  for(UInt i = 0; i < nz; ++i) {
    if(irow[i] == jcol[i]) {
      prod[irow[i]] += mat[i] * v[jcol[i]];
    } else {
      prod[irow[i]] += mat[i] * v[jcol[i]];
      prod[jcol[i]] += mat[i] * v[irow[i]];
    }
  }
}

void Minotaur::BoundsOnDiv(double l0, double u0, double l1, double u1,
                           double& lb, double& ub)
{
  double tl, tu;
  BoundsOnRecip(l1, u1, tl, tu);
  BoundsOnProduct(false, l0, u0, tl, tu, lb, ub);
}

void Minotaur::RevBoundsOnMult(double l0, double u0, double l1, double u1,
                               double& lb, double& ub)
{
  if(l0 <= 1e-8 && u0 >= -1e-8 && l1 <= 1e-8 && u1 >= -1e-8) {
    lb = -INFINITY;
    ub = INFINITY;
  } else {
    BoundsOnDiv(l0, u0, l1, u1, lb, ub);
  }
}

void Minotaur::BoundsOnProduct(bool zero_x_inf_zero, ConstVariablePtr x0,
                               ConstVariablePtr x1, double& lb, double& ub)
{
  BoundsOnProduct(zero_x_inf_zero, x0->getLb(), x0->getUb(), x1->getLb(),
                  x1->getUb(), lb, ub);
}

void Minotaur::BoundsOnProduct(bool zero_x_inf_zero, double l0, double u0,
                               double l1, double u1, double& lb, double& ub)
{

  double prod;

  // If l1=u1=0; exchange values of l0,l1 and u0,u1
  // Saves effort on several cases we have to consider
  if(fabs(l1) <= 1e-10 && fabs(u1) <= 1e-10) {
    prod = l1;
    l1 = l0;
    l0 = prod;
    prod = u1;
    u1 = u0;
    u0 = prod;
  }

  if(fabs(l0) <= 1e-10 && fabs(u0) <= 1e-10) {
    if(true == zero_x_inf_zero) {
      lb = 0.0;
      ub = 0.0;
    } else {
      if(l1 == -INFINITY) {
        lb = -INFINITY;
      } else {
        lb = 0.0;
      }
      if(u1 == INFINITY) {
        ub = INFINITY;
      } else {
        ub = 0.0;
      }
    }
  } else if((l1 == -INFINITY && u1 == INFINITY) ||
            (l0 == -INFINITY && u0 == INFINITY)) {
    lb = -INFINITY;
    ub = INFINITY;
  } else {
    prod = l0 * l1;
    if(std::isnan(prod)) {
      prod = -INFINITY;
    }
    lb = prod;
    ub = prod;

    prod = u0 * l1;
    if(std::isnan(prod)) {
      prod = INFINITY;
    }
    lb = std::min(lb, prod);
    ub = std::max(ub, prod);

    prod = u0 * u1;
    if(std::isnan(prod)) {
      prod = -INFINITY;
    }
    lb = std::min(lb, prod);
    ub = std::max(ub, prod);

    prod = l0 * u1;
    if(std::isnan(prod)) {
      prod = INFINITY;
    }
    lb = std::min(lb, prod);
    ub = std::max(ub, prod);
  }
}

void Minotaur::BoundsOnRecip(double l0, double u0, double& lb, double& ub)
{
  /*
   * This code _looks_ OK but returns bad values when (l0,u0) = (-inf,0)
   */
  /*
  if (l0<0 && u0>0) {
    lb = -INFINITY;
    ub = INFINITY;
  } else if {
    lb = 1.0/u0;
    ub = 1.0/l0;
  } 
  */
  if((fabs(u0) < 1e-10) && (fabs(l0) < 1e-10)) {
    lb = -INFINITY;
    ub = INFINITY;
  } else if(l0 < -1e-10 && u0 > 1e-10) {
    lb = -INFINITY;
    ub = INFINITY;
  } else if((fabs(u0) < 1e-10) && l0 < 0) {
    lb = -INFINITY;
    ub = 1.0 / l0;
  } else if((fabs(l0) < 1e-10) && u0 > 0) {
    lb = 1.0 / u0;
    ub = INFINITY;
  } else {
    lb = 1.0 / u0;
    ub = 1.0 / l0;
  }
}

void Minotaur::BoundsOnSquare(ConstVariablePtr x1, double& lb, double& ub)
{
  double l1 = x1->getLb();
  double u1 = x1->getUb();

  if(u1 < 0.) { // both bounds are negative.
    lb = u1 * u1;
    ub = l1 * l1;
  } else if(l1 > 0.) { // both bounds are positive.
    lb = l1 * l1;
    ub = u1 * u1;
  } else { // lb is negative, ub is positive.
    lb = 0.;
    ub = std::max(l1 * l1, u1 * u1);
  }
}

void Minotaur::BoundsOnSquare(const double l1, const double u1, double& lb,
                              double& ub)
{
  if(u1 < 0.) { // both bounds are negative.
    lb = u1 * u1;
    ub = l1 * l1;
  } else if(l1 > 0.) { // both bounds are positive.
    lb = l1 * l1;
    ub = u1 * u1;
  } else { // lb is negative, ub is positive.
    lb = 0.;
    ub = std::max(l1 * l1, u1 * u1);
  }
}

void Minotaur::displayArray(const double* point, UInt n, std::ostream& out)
{
  for(UInt i = 0; i < n; ++i, ++point) {
    out << *point << "\t";
  }
  out << std::endl;
}

bool Minotaur::CompareLen(Constraint *c1, Constraint *c2)
{
  return (c1->getFunction()->getNumVars() < c2->getFunction()->getNumVars());
}

double Minotaur::Gcd(double d1, double d2, const double& etol)
{
  double rem;
  d1 = fabs(d1);
  d2 = fabs(d2);

  if(d2 < d1) {
    rem = d1; // rem used as a temporary variable.
    d1 = d2;
    d2 = rem;
  }

  if(d1 < etol) {
    return d2;
  }

  do {
    rem = fmod(d2, d1);
    d2 = d1;
    d1 = rem;
  } while(rem > etol);

  return d2;
}

double Minotaur::getDistance(const double* Pointa, const double* Pointb, UInt n)
{
  double dist = 0;

  for(UInt i = 0; i < n; ++i, ++Pointa, ++Pointb) {
    dist += pow(*Pointa - *Pointb, 2);
  }
  return sqrt(dist);
}

double Minotaur::minArray(const double* A, UInt n)
{
  double min = A[0];

  for(UInt i = 0; i < n; ++i, ++A) {
    if(min > *A) {
      min = *A;
    }
  }
  return min;
}

void Minotaur::sort(VarVector& vvec, double* x, bool ascend)
{
  if(vvec.size() <= 1) {
    return;
  }

  if(ascend == false) {
    for(int i = vvec.size() - 1; i > -1; --i) {
      x[i] *= -1;
    }
  }

  sortRec(vvec, x, 0, vvec.size() - 1, vvec.size() / 2);

  if(ascend == false) {
    for(int i = vvec.size() - 1; i > -1; --i) {
      x[i] *= -1;
    }
  }
}

void Minotaur::sortRec(VarVector& vvec, double* x, int left, int right,
                       int pivotind)
{
  double pval;
  double tmpval;
  VariablePtr tmpvar;
  int sind;

  // move pivot to the rightmost
  tmpvar = vvec[pivotind];
  vvec[pivotind] = vvec[right];
  vvec[right] = tmpvar;

  pval = x[pivotind];
  x[pivotind] = x[right];
  x[right] = pval;

  // move any value <= pval to the left side.
  sind = left;
  for(int i = left; i < right; ++i) {
    if(x[i] <= pval) {
      tmpvar = vvec[sind];
      vvec[sind] = vvec[i];
      vvec[i] = tmpvar;
      tmpval = x[sind];
      x[sind] = x[i];
      x[i] = tmpval;
      ++sind;
    }
  }

  // move pivot to sind
  tmpvar = vvec[sind];
  vvec[sind] = vvec[right];
  vvec[right] = tmpvar;
  tmpval = x[sind];
  x[sind] = x[right];
  x[right] = tmpval;

  if(sind - left > 1) {
    sortRec(vvec, x, left, sind - 1, (sind + left) / 2);
  }
  if(right - sind > 1) {
    sortRec(vvec, x, sind, right, (sind + right) / 2);
  }
}

void Minotaur::toLowerCase(std::string& str)
{
  int diff = ('z' - 'Z');
  for(UInt i = 0; i < str.size(); ++i) {
    if(str[i] >= 'A' && str[i] <= 'Z') {
      str[i] += diff;
    }
  }
}

std::string Minotaur::toClockTime(double t)
{
  std::string s;
  std::stringstream ss;
  int it = (int)(t * 100);
  ss << std::setfill('0') << std::setw(2) << (int)(it / 360000);
  it = it % (360000);
  ss << ":" << std::setfill('0') << std::setw(2) << (int)(it / 6000);
  it = it % (6000);
  ss << ":" << std::setfill('0') << std::setw(2) << (int)(it / 100);
  it = it % (100);
  ss << ":" << std::setfill('0') << std::setw(2) << it;
  s = ss.str();
  return s;
}

