/* ----------------------------------------------------------------------
   SPARTA - Stochastic PArallel Rarefied-gas Time-accurate Analyzer
   http://sparta.sandia.gov
   Steve Plimpton, sjplimp@sandia.gov, Michael Gallis, magalli@sandia.gov
   Sandia National Laboratories

   Copyright (2014) Sandia Corporation.  Under the terms of Contract
   DE-AC04-94AL85000 with Sandia Corporation, the U.S. Government retains
   certain rights in this software.  This software is distributed under 
   the GNU General Public License.

   See the README file in the top-level SPARTA directory.
------------------------------------------------------------------------- */

#ifdef COMMAND_CLASS

CommandStyle(balance_grid,BalanceGrid)

#else

#ifndef SPARTA_BALANCE_GRID_H
#define SPARTA_BALANCE_GRID_H

#include "pointers.h"

namespace SPARTA_NS {

class BalanceGrid : protected Pointers {
 public:
  BalanceGrid(class SPARTA *);
  void command(int, char **);

 private:
  void procs2grid(int, int, int, int &, int &, int &);
};

}

#endif
#endif

/* ERROR/WARNING messages:

*/
