// 
//     Minotaur -- It's only 1/2 bull
// 
//     (C)opyright 2008 - 2025 The Minotaur Team.
// 


#ifndef MINOTAURCHOL_H
#define MINOTAURCHOL_H

#include "LinearFunction.h"
#include "QuadraticFunction.h"

namespace Minotaur {


  // /**
  // The CholCalculator class is used to calculate Cholesky factorization 
  //  of given quadratic function
  // */
  class CholCalculator {
    public:

      /// default constructor
      CholCalculator();

      // /**
      // Construct using a quadratic function.
      // */
      CholCalculator(ConstQuadraticFunctionPtr qf);

      // /**
      // Destroy
      // */
      ~CholCalculator() {};

    private:

      // /**
      // The quadratic function for whose Hessian we wish to find the eigen values.
      // */
      ConstQuadraticFunctionPtr qf_;

      // /**
      // Dimension of the square matrix
      // */
      UInt n_;

      // /**
      // The square matrix is stored as a single array. The element A[i,j] can
      // be accessed at A_[i+j*n_]. And element A_[i] = A[i mod n_, i/n_].
      // */
      double *A_;

      ///
      double abstol_;

  };


}
#endif

