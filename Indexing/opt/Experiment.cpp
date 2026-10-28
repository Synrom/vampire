/*
 * This file is part of Vampire. See https://vprover.github.io/license.html
 * and the source directory for licence information.
 */
#include "Experiment.hpp"

namespace Indexing::OrderExperiment {
Config::Config() {
  auto v=std::getenv("CT_ORDER_VARIANT"); variant=v ? v : "cheap";
  auto r=std::getenv("CT_ORDER_RUN"); run=r ? r : "paired";
  if (variant=="off") {}
  else if (variant=="used") used=true;
  else if (variant=="greater") ordering=true;
  else if (variant=="both") ordering=both=true;
  else if (variant=="eager") ordering=eager=true;
  else if (variant=="capacity") ordering=capacity=true;
  else if (variant=="hybrid") ordering=used=true;
  else if (variant=="full") ordering=both=eager=capacity=used=true;
  else if (variant=="cached_used") used=cache=true;
  else if (variant=="cached_greater") ordering=used=cache=true;
  else if (variant=="cached_both") ordering=both=used=cache=true;
  else if (variant=="cached_full") ordering=both=eager=capacity=used=cache=true;
  else if (variant=="header") header=true;
  else if (variant=="cheap") header=used=cache=terminal=true;
  else if (variant=="balanced") header=ordering=both=used=cache=terminal=true;
  else if (variant=="terminal") terminal=true;
  else std::abort();
  if (run!="paired" && run!="baseline" && run!="optimized") std::abort();
  auto sort=std::getenv("CT_ORDER_SORT");
  weightSort=sort && std::string(sort)=="weight";
  if (sort && std::string(sort)!="weight" && std::string(sort)!="sharing") std::abort();
  }
}
