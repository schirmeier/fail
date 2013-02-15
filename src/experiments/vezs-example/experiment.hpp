#ifndef __CHECKSUM_OOSTUBS_EXPERIMENT_HPP__
  #define __CHECKSUM_OOSTUBS_EXPERIMENT_HPP__

#include "efw/ExperimentFlow.hpp"
#include "efw/JobClient.hpp"
#include "util/Logger.hpp"

class VEZSExperiment : public fail::ExperimentFlow {

  fail::JobClient m_jc;
  fail::Logger m_log;

public:
  VEZSExperiment() : m_log("VEZS-example", false) {};
	bool run();
};

#endif // __CHECKSUM_OOSTUBS_EXPERIMENT_HPP__
