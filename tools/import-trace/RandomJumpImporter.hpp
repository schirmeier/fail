#ifndef __RANDOM_JUMP_IMPORTER_H__
#define __RANDOM_JUMP_IMPORTER_H__

#include <vector>
#include "util/CommandLine.hpp"
#include "Importer.hpp"

#ifndef __puma
#include "util/llvmdisassembler/LLVMDisassembler.hpp"
#endif


class RandomJumpImporter : public Importer {
#ifndef __puma
	llvm::OwningPtr<llvm::object::Binary> binary;
	llvm::OwningPtr<fail::LLVMDisassembler> disas;
#endif

	fail::CommandLine::option_handle FROM, TO;

	fail::MemoryMap *m_mm_from, *m_mm_to;
	std::vector<fail::guest_address_t> m_jump_to_addresses;
public:
	/**
	 * Callback function that can be used to add command line options
	 * to the campaign
	 */
	virtual bool cb_commandline_init();

	virtual bool handle_ip_event(fail::simtime_t curtime, instruction_count_t instr,
								 const Trace_Event &ev);
	virtual bool handle_mem_event(fail::simtime_t curtime, instruction_count_t instr,
								  const Trace_Event &ev) {
		/* ignore on purpose */
		return true;
	}

	virtual void open_unused_ec_intervals() {
		/* empty, Memory Map has a different meaning in this importer */
	}
};

#endif