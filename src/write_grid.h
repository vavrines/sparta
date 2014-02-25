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

CommandStyle(write_grid,WriteGrid)

#else

#ifndef SPARTA_WRITE_GRID_H
#define SPARTA_WRITE_GRID_H

#include "stdio.h"
#include "pointers.h"

namespace SPARTA_NS {

class WriteGrid : protected Pointers {
 public:
  WriteGrid(class SPARTA *);
  void command(int, char **);

 private:
  FILE *fp;

  void header();
  void write_parents();
};

}

#endif
#endif
