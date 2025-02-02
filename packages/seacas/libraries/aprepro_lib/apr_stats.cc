// Copyright(C) 1999-2021 National Technology & Engineering Solutions
// of Sandia, LLC (NTESS).  Under the terms of Contract DE-NA0003525 with
// NTESS, the U.S. Government retains certain rights in this software.
//
// See packages/seacas/LICENSE for details

/*******************************************************************
 *      STATS.C: Statistics routines:
 *
 *      newsample(n):   Add a new sample to the mean/average totals.
 *      mean():         Returns the mean of the samples.
 *      deviation():    Returns the standard deviation of the sample.
 *
 ********************************************************************/

#include "apr_stats.h"
#include <cmath>

namespace SEAMS {
  void Stats::newsample(int n)
  {
    // See Knuth, TAOCP vol 2, 3rd edition, page 232
    double TMean = Mean;
    Numnums++;
    Mean = TMean + (n - TMean) / Numnums;

    if (Numnums > 1) {
      StdDev += (n - TMean) * (n - Mean);
    }
  }

  double Stats::mean() const { return Mean; }

  double Stats::variance() const { return (Numnums > 1) ? StdDev / (Numnums - 1) : 0.0; }

  double Stats::deviation() const { return std::sqrt(variance()); }
} // namespace SEAMS
