#include <iostream>
#include <fstream>
//#include <string>

// getpid
#include <sys/types.h>
#include <unistd.h>


#include "experiment.hpp"
#include "experimentInfo.hpp"
#include "campaign.hpp"
#include "sal/SALConfig.hpp"
#include "sal/SALInst.hpp"
#include "sal/Memory.hpp"
#include "sal/bochs/BochsListener.hpp"
#include "sal/Listener.hpp"
#include "util/ElfReader.hpp"
#include "util/WallclockTimer.hpp"
#include "util/gzstream/gzstream.h"
#include "config/FailConfig.hpp"
#include "util/CommandLine.hpp"

// You need to have the tracing plugin enabled for this
#include "../plugins/tracing/TracingPlugin.hpp"

#define LOCAL 0

#ifndef PREREQUISITES
  #error Configure experimentInfo.hpp properly!
#endif

// create/use multiple snapshots to speed up long experiments
// FIXME: doesn't work properly, trace changes! (reason unknown; incorrectly restored serial timers?)
#define MULTIPLE_SNAPSHOTS 0
#define MULTIPLE_SNAPSHOTS_DISTANCE 1000000

#define VIDEOMEM_START 0xb8000
#define VIDEOMEM_SIZE  (80*25*2 *2) // two text mode screens
#define VIDEOMEM_END   (VIDEOMEM_START + VIDEOMEM_SIZE)

using namespace std;
using namespace fail;

#if PREREQUISITES
bool EcosKernelTestExperiment::retrieveGuestAddresses(guest_address_t addr_finish, guest_address_t addr_data_start, guest_address_t addr_data_end) {
#if BASELINE_ASSESSMENT || STACKPROTECTION
	log << "STEP 0: creating memory map spanning all of DATA and BSS" << endl;
	MemoryMap mm;
	mm.add(addr_data_start, addr_data_end - addr_data_start);
	mm.writeToFile(EcosKernelTestCampaign::filename_memorymap(m_variant, m_benchmark).c_str());
#else
	log << "STEP 0: record memory map with addresses of 'interesting' objects" << endl;

	// run until func_finish is reached
	BPSingleListener bp;
	bp.setWatchInstructionPointer(addr_finish);

	// memory map serialization
	// FIXME: use MemoryMap::writeToFile()
	ofstream mm(EcosKernelTestCampaign::filename_memorymap(m_variant, m_benchmark).c_str(), ios::out);
	if (!mm.is_open()) {
		log << "failed to open " << EcosKernelTestCampaign::filename_memorymap() << endl;
		return false;
	}

	GuestListener g;
	string *str = new string; // buffer for guest listeners' data
	unsigned number_of_guest_events = 0;

	while (simulator.addListenerAndResume(&g) == &g) {
		if (g.getData() == '\t') {
			// addr complete?
			//cout << "full: " << *str << "sub: " << str->substr(str->find_last_of('x') - 1) << endl;
			// interpret the string obtained by the guest listeners as address in hex
			unsigned guest_addr;
			stringstream converter(str->substr(str->find_last_of('x') + 1));
			converter >> hex >> guest_addr;
			mm << guest_addr << '\t';
			str->clear();
		} else if (g.getData() == '\n') {
			// len complete?
			// interpret the string obtained by the guest listeners as length in decimal
			unsigned guest_len;
			stringstream converter(*str);
			converter >> dec >> guest_len;
			mm << guest_len << '\n';
			str->clear();
			number_of_guest_events++;
		} else if (g.getData() == 'Q') {
			// when the guest system triggers the guest event 'Q',
			// we can assume that we are in protected mode
			simulator.addListener(&bp);
		} else {
			str->push_back(g.getData());
		}
	}
	assert(number_of_guest_events > 0);
	log << "Breakpoint at func_finish reached: created memory map (" << number_of_guest_events << " entries)" << endl;
	delete str;

	// close serialized mm
	mm.close();
#endif

	return true;
}

bool EcosKernelTestExperiment::establishState(guest_address_t addr_entry, guest_address_t addr_finish, guest_address_t addr_errors_corrected) {
	log << "STEP 1: run until interesting function starts, and save state" << endl;

	GuestListener g;

	while (true) {
		simulator.addListenerAndResume(&g);
		if(g.getData() == 'Q') {
		  log << "Guest system triggered: " << g.getData() << endl;
		  break;
		}
	}

	BPSingleListener bp;
	bp.setWatchInstructionPointer(addr_entry);
	simulator.addListenerAndResume(&bp);
	log << "test function entry reached, saving state" << endl;
	log << "EIP = " << hex << bp.getTriggerInstructionPointer() << endl;
	//log << "error_corrected = " << dec << ((int)simulator.getMemoryManager().getByte(addr_errors_corrected)) << endl;

	// run until 'ECOS_FUNC_FINISH' is reached
	BPSingleListener finish;
	finish.setWatchInstructionPointer(addr_finish);

	// one save every MULTIPLE_SNAPSHOTS_DISTANCE instructions
	BPSingleListener step;
	step.setWatchInstructionPointer(ANY_ADDR);
	step.setCounter(MULTIPLE_SNAPSHOTS_DISTANCE);

	for (unsigned i = 0; ; ++i) {
		log << "saving state at offset " << dec << (i * MULTIPLE_SNAPSHOTS_DISTANCE) << endl;
		simulator.save(EcosKernelTestCampaign::filename_state(i * MULTIPLE_SNAPSHOTS_DISTANCE, m_variant, m_benchmark));
#if MULTIPLE_SNAPSHOTS
		simulator.restore(EcosKernelTestCampaign::filename_state(i * MULTIPLE_SNAPSHOTS_DISTANCE, m_variant, m_benchmark));

		simulator.addListener(&step);
		simulator.addListener(&finish);

		if (simulator.resume() == &finish) {
			break;
		}
#else
		break;
#endif
	}

	return true;
}

bool EcosKernelTestExperiment::performTrace(guest_address_t addr_entry, guest_address_t addr_finish) {
	log << "STEP 2: record trace for fault-space pruning" << endl;

	log << "restoring state" << endl;
	simulator.restore(EcosKernelTestCampaign::filename_state(0, m_variant, m_benchmark));
	log << "EIP = " << hex << simulator.getCPU(0).getInstructionPointer() << endl;
	assert(simulator.getCPU(0).getInstructionPointer() == addr_entry);

	log << "enabling tracing" << endl;
	TracingPlugin tp;

	// restrict memory access logging to injection target
	MemoryMap mm;
	mm.readFromFile(EcosKernelTestCampaign::filename_memorymap(m_variant, m_benchmark).c_str());

	tp.restrictMemoryAddresses(&mm);

	// record trace
	ogzstream of(EcosKernelTestCampaign::filename_trace(m_variant, m_benchmark).c_str());
	tp.setTraceFile(&of);
	// this must be done *after* configuring the plugin:
	simulator.addFlow(&tp);

	// again, run until 'ECOS_FUNC_FINISH' is reached
	BPSingleListener bp;
	bp.setWatchInstructionPointer(addr_finish);
	simulator.addListener(&bp);

	// on the way, count instructions // FIXME add SAL functionality for this?
	BPSingleListener ev_count(ANY_ADDR);
	simulator.addListener(&ev_count);
	unsigned instr_counter = 0;

	// measure elapsed time
	simtime_t time_start = simulator.getTimerTicks();

	// on the way, record lowest and highest memory address accessed
	MemAccessListener ev_mem(ANY_ADDR, MemAccessEvent::MEM_READWRITE);
	simulator.addListener(&ev_mem);
	// range for mem accesses < 1M
	unsigned mem1_low = 0xFFFFFFFFUL;
	unsigned mem1_high = 0;
	// range for mem accesses >= 1M
	unsigned mem2_low = 0xFFFFFFFFUL;
	unsigned mem2_high = 0;

	// do the job, 'till the end
	BaseListener* ev = simulator.resume();
	while(ev != &bp) {
		if(ev == &ev_count) {
			if(instr_counter++ == 0xFFFFFFFFU) {
				log << "ERROR: instr_counter overflowed" << endl;
				return false;
			}
			simulator.addListener(&ev_count);
		}
		else if(ev == &ev_mem) {
			unsigned lo = ev_mem.getTriggerAddress();
			unsigned hi = lo + ev_mem.getTriggerWidth() - 1;

			if (lo < VIDEOMEM_START || lo >= VIDEOMEM_END) {
				if (hi < 1024*1024) { // < 1M
					if (hi > mem1_high) { mem1_high = hi; }
					if (lo < mem1_low)  { mem1_low = lo; }
				} else { // >= 1M
					if (hi > mem2_high) { mem2_high = hi; }
					if (lo < mem2_low)  { mem2_low = lo; }
				}
			}
			simulator.addListener(&ev_mem);
		}
		ev = simulator.resume();
	}

	unsigned long long estimated_timeout_overflow_check =
		simulator.getTimerTicks() - time_start + 55000; // 1s/18.2
	unsigned estimated_timeout =
		(unsigned) (estimated_timeout_overflow_check * 1000000 / simulator.getTimerTicksPerSecond());

	log << dec << "tracing finished after " << instr_counter  << " instructions" << endl;
	log << hex << "all memory accesses within [0x" << mem1_low << ", 0x" << mem1_high << "] u [0x" << mem2_low << ", 0x" << mem2_high << "] (ignoring VGA mem)" << endl;
	log << dec << "elapsed simulated time (plus safety margin): " << (estimated_timeout / 1000000.0) << "s" << endl;

	// sanitize memory ranges
	if (mem1_low > mem1_high) {
		mem1_low = mem1_high = 0;
	}
	if (mem2_low > mem2_high) {
		mem2_low = mem2_high = 1024*1024;
	}

	// save these values for experiment STEP 3
	EcosKernelTestCampaign::writeTraceInfo(instr_counter, estimated_timeout,
		mem1_low, mem1_high, mem2_low, mem2_high, m_variant, m_benchmark);

	simulator.removeFlow(&tp);

	// serialize trace to file
	if (of.fail()) {
		log << "failed to write " << EcosKernelTestCampaign::filename_trace(m_variant, m_benchmark) << endl;
		return false;
	}
	of.close();
	log << "trace written to " << EcosKernelTestCampaign::filename_trace(m_variant, m_benchmark) << endl;
	
	return true;
}

#else // !PREREQUISITES
bool EcosKernelTestExperiment::faultInjection() {
	log << "STEP 3: The actual experiment." << endl;

	// trace info
	unsigned instr_counter, estimated_timeout, mem1_low, mem1_high, mem2_low, mem2_high;
	// ELF symbol addresses
	guest_address_t addr_entry, addr_finish, addr_test_output, addr_errors_corrected,
	                addr_panic, addr_text_start, addr_text_end,
	                addr_data_start, addr_data_end;

	BPSingleListener bp;
	
#if !LOCAL
	for (int experiments = 0;
		experiments < 500 || (m_jc.getNumberOfUndoneJobs() != 0); ) { // stop after ~500 experiments to prevent swapping
	// 50 exp ~ 0.5GB RAM usage per instance (linearly increasing)
#endif

	// get an experiment parameter set
	log << "asking job server for experiment parameters" << endl;
	EcosKernelTestExperimentData param;
#if !LOCAL
	if (!m_jc.getParam(param)) {
		log << "Dying." << endl;
		// communicate that we were told to die
		simulator.terminate(1);
	}
#else
	// XXX debug
	param.msg.set_variant(m_variant);
	param.msg.set_benchmark(m_benchmark);
	param.msg.set_instr2_offset(7462);
	//param.msg.set_instr_address(12345);
	param.msg.set_mem_addr(44540);
#endif

	WallclockTimer timer;
	timer.startTimer();

	int id = param.getWorkloadID();
	m_variant = param.msg.variant();
	m_benchmark = param.msg.benchmark();
	int instr_offset = param.msg.instr2_offset();
	int mem_addr = param.msg.mem_addr();

	EcosKernelTestCampaign::readTraceInfo(instr_counter, estimated_timeout,
		mem1_low, mem1_high, mem2_low, mem2_high, m_variant, m_benchmark);
	readELFSymbols(addr_entry, addr_finish, addr_test_output,
		addr_errors_corrected, addr_panic, addr_text_start, addr_text_end,
		addr_data_start, addr_data_end);

	int state_instr_offset = instr_offset - (instr_offset % MULTIPLE_SNAPSHOTS_DISTANCE);
	string statename;
#if MULTIPLE_SNAPSHOTS
	if (access(EcosKernelTestCampaign::filename_state(state_instr_offset, m_variant, m_benchmark).c_str(), R_OK) == 0) {
		statename = EcosKernelTestCampaign::filename_state(state_instr_offset, m_variant, m_benchmark);
		log << "using state at offset " << state_instr_offset << endl;
		instr_offset -= state_instr_offset;
	} else { // fallback
#endif
		statename = EcosKernelTestCampaign::filename_state(0, m_variant, m_benchmark);
		state_instr_offset = 0;
		log << "using state at offset 0 (fallback)" << endl;
#if MULTIPLE_SNAPSHOTS
	}
#endif

	// for each job with the SINGLEBITFLIP fault model we're actually doing *8*
	// experiments (one for each bit)
	for (int bit_offset = 0; bit_offset < 8; ++bit_offset) {
		++experiments;

		// 8 results in one job
		EcosKernelTestProtoMsg_Result *result = param.msg.add_result();
		result->set_bit_offset(bit_offset);
		log << dec << "job " << id << " " << m_variant << "/" << m_benchmark
		    << " instr " << (instr_offset + state_instr_offset)
		    << " mem " << mem_addr << "+" << bit_offset << endl;

		log << "restoring state" << endl;
		simulator.restore(statename);

		// XXX debug
/*
		stringstream fname;
		fname << "job." << ::getpid();
		ofstream job(fname.str().c_str());
		job << "job " << id << " instr " << instr_offset << " (" << param.msg.instr_address() << ") mem " << mem_addr << "+" << bit_offset << endl;
		job.close();
*/

		// reaching finish() could happen before OR after FI
		BPSingleListener func_finish(addr_finish);
		simulator.addListener(&func_finish);

		// no need to wait if offset is 0
		if (instr_offset > 0) {
			// XXX could be improved with intermediate states (reducing runtime until injection)
			bp.setWatchInstructionPointer(ANY_ADDR);
			bp.setCounter(instr_offset);
			simulator.addListener(&bp);

			// finish() before FI?
			if (simulator.resume() == &func_finish) {
				log << "experiment reached finish() before FI" << endl;

				// wait for bp
				simulator.resume();
			}
		}

		// --- fault injection ---
		MemoryManager& mm = simulator.getMemoryManager();
		byte_t data = mm.getByte(mem_addr);
		byte_t newdata;
		if (param.msg.has_faultmodel() && param.msg.faultmodel() == param.msg.BURST) {
			newdata = data ^ 0xff;
			bit_offset = 8; // enforce loop termination
		} else if (!param.msg.has_faultmodel() || param.msg.faultmodel() == param.msg.SINGLEBITFLIP) {
			newdata = data ^ (1 << bit_offset);
		} else {
			// Won't happen with current campaign implementation.  Keeps
			// compiler happy.
			newdata = data;
		}
		mm.setByte(mem_addr, newdata);
		// note at what IP we did it
		int32_t injection_ip = simulator.getCPU(0).getInstructionPointer();
		param.msg.set_injection_ip(injection_ip);
		log << "fault injected @ ip " << injection_ip
			<< " 0x" << hex << ((int)data) << " -> 0x" << ((int)newdata) << endl;
		// sanity check
		if (param.msg.has_instr2_address() &&
			injection_ip != param.msg.instr2_address()) {
			stringstream ss;
			ss << "SANITY CHECK FAILED: " << injection_ip
			   << " != " << param.msg.instr2_address();
			log << ss.str() << endl;
			result->set_resulttype(result->UNKNOWN);
			result->set_latest_ip(injection_ip);
			result->set_ecos_test_result(result->FAIL);
			result->set_details(ss.str());

			continue;
		}
		if (param.msg.has_instr2_address()) {
			log << "Absolute IP sanity check OK" << endl;
		}

		// --- aftermath ---
		// possible outcomes:
		// - trap, "crash"
		// - jump outside text segment
		// - (XXX unaligned jump inside text segment)
		// - (XXX weird instructions?)
		// - (XXX results displayed?)
		// - reaches THE END
		// - error detected, stop
		// additional info:
		// - #loop iterations before/after FI
		// - (XXX "sane" display?)

		// catch traps as "extraordinary" ending
		TrapListener ev_trap(ANY_TRAP);
		simulator.addListener(&ev_trap);

		// jump outside text segment
		BPRangeListener ev_below_text(ANY_ADDR, addr_text_start - 1);
		BPRangeListener ev_beyond_text(addr_text_end + 1, ANY_ADDR);
		simulator.addListener(&ev_below_text);
		simulator.addListener(&ev_beyond_text);

		// memory access outside of bound determined in the golden run
		// [mem1_low, mem1_high] u [mem2_low, mem2_high]
		// video memory accesses are OK, too
		// FIXME: It would be nice to have a MemAccessListener that accepts a
		// MemoryMap, to have MemoryMaps that store addresses in a compact way,
		// and that are invertible.
		assert(mem1_low < mem1_high && mem1_high < VIDEOMEM_START && VIDEOMEM_END < mem2_low && mem2_low < mem2_high);
		MemAccessListener ev_mem_outside1(0x0, MemAccessEvent::MEM_READWRITE);
		ev_mem_outside1.setWatchWidth(mem1_low);
		MemAccessListener ev_mem_outside2(mem1_high + 1, MemAccessEvent::MEM_READWRITE);
		ev_mem_outside2.setWatchWidth(VIDEOMEM_START - (mem1_high + 1));
		MemAccessListener ev_mem_outside3(VIDEOMEM_END, MemAccessEvent::MEM_READWRITE);
		ev_mem_outside3.setWatchWidth(mem2_low - VIDEOMEM_END);
		MemAccessListener ev_mem_outside4(mem2_high + 1, MemAccessEvent::MEM_READWRITE);
		ev_mem_outside4.setWatchWidth(0xFFFFFFFFU - (mem2_high + 1));
		simulator.addListener(&ev_mem_outside1);
		simulator.addListener(&ev_mem_outside2);
		simulator.addListener(&ev_mem_outside3);
		simulator.addListener(&ev_mem_outside4);

		// timeout (e.g., stuck in a HLT instruction)
		TimerListener ev_timeout(estimated_timeout);
		simulator.addListener(&ev_timeout);

		// remaining instructions until "normal" ending
		// number of instructions that are executed additionally for error corrections
		//BPSingleListener ev_end(ANY_ADDR);
		//ev_end.setCounter(instr_counter - instr_offset + ECOS_RECOVERYINSTR);
		//simulator.addListener(&ev_end);
		
		// eCos' test output function, which will show if the test PASSed or FAILed
		BPSingleListener func_test_output(addr_test_output);
		simulator.addListener(&func_test_output);

		// function called by ecc aspects, when an uncorrectable error is detected
		BPSingleListener func_ecc_panic(addr_panic);
		if (addr_panic != ADDR_INV) {
			simulator.addListener(&func_ecc_panic);
		}

#if LOCAL && 0
		// XXX debug
		log << "enabling tracing" << endl;
		TracingPlugin tp;
		tp.setLogIPOnly(true);
		tp.setOstream(&cout);
		// this must be done *after* configuring the plugin:
		simulator.addFlow(&tp);
#endif

		// the outcome of ecos' test case
		bool ecos_test_passed = false;
		bool ecos_test_failed = false;

		BaseListener* ev;

		// wait until experiment-terminating event occurs
		while (true) {
			ev = simulator.resume();
			if (ev == &func_test_output) {
				// re-add this listener
				simulator.addListener(&func_test_output);

				// 1st argument of cyg_test_output shows what has happened (FAIL or PASS)
				address_t stack_ptr = simulator.getCPU(0).getStackPointer(); // esp
				int32_t cyg_test_output_argument = simulator.getMemoryManager().getByte(stack_ptr + 4); // 1st argument is at esp+4

				log << "cyg_test_output_argument (#1): " << cyg_test_output_argument << endl;

				/*
				typedef enum {
					CYGNUM_TEST_FAIL,
					CYGNUM_TEST_PASS,
					CYGNUM_TEST_EXIT,
					CYGNUM_TEST_INFO,
					CYGNUM_TEST_GDBCMD,
					CYGNUM_TEST_NA
				} Cyg_test_code;
				*/

				if (cyg_test_output_argument == 0) {
					ecos_test_failed = true;
				} else if (cyg_test_output_argument == 1) {
					ecos_test_passed = true;
				}

			// special case: except1 and clockcnv actively generate traps
			} else if (ev == &ev_trap
			        && ((m_benchmark == "except1" && ev_trap.getTriggerNumber() == 13)
					 || (m_benchmark == "clockcnv" && ev_trap.getTriggerNumber() == 7))) {
				// re-add this listener
				simulator.addListener(&ev_trap);
			} else {
				// in any other case, the experiment is finished
				break;
			}
		}

		// record latest IP regardless of result
		result->set_latest_ip(simulator.getCPU(0).getInstructionPointer());

		// record error_corrected regardless of result
		if (addr_errors_corrected != ADDR_INV) {
			int32_t error_corrected = simulator.getMemoryManager().getByte(addr_errors_corrected);
			result->set_error_corrected(error_corrected);
		} else {
			result->set_error_corrected(0);
		}
		
		// record ecos_test_result
		if ( (ecos_test_passed == true) && (ecos_test_failed == false) ) {
			result->set_ecos_test_result(result->PASS);
			log << "Ecos Test PASS" << endl;
		} else {
			result->set_ecos_test_result(result->FAIL);
			log << "Ecos Test FAIL" << endl;
		}

		if (ev == &func_finish) {
			// do we reach finish?
			log << "experiment finished ordinarily" << endl;
			result->set_resulttype(result->FINISHED);
		} else if (ev == &ev_timeout /*|| ev == &ev_end*/) {
			log << "Result TIMEOUT" << endl;
			result->set_resulttype(result->TIMEOUT);
		} else if (ev == &ev_below_text || ev == &ev_beyond_text) {
			log << "Result OUTSIDE" << endl;
			result->set_resulttype(result->OUTSIDE);
		} else if (ev == &ev_mem_outside1 || ev == &ev_mem_outside2
		        || ev == &ev_mem_outside3 || ev == &ev_mem_outside4) {
			log << "Result MEMORYACCESS" << endl;
			result->set_resulttype(result->MEMORYACCESS);
		} else if (ev == &ev_trap) {
			log << dec << "Result TRAP #" << ev_trap.getTriggerNumber() << endl;
			result->set_resulttype(result->TRAP);

			stringstream ss;
			ss << ev_trap.getTriggerNumber();
			result->set_details(ss.str());
		} else if (ev == &func_ecc_panic) {
			log << "ECC Panic: uncorrectable error" << endl;
			result->set_resulttype(result->DETECTED); // DETECTED <=> ECC_PANIC <=> reboot
		} else {
			log << "Result WTF?" << endl;
			result->set_resulttype(result->UNKNOWN);

			stringstream ss;
			ss << "event addr " << ev << " EIP " << simulator.getCPU(0).getInstructionPointer();
			result->set_details(ss.str());
		}
	}
	// sanity check: do we have exactly 8 results?
	if ((!param.msg.has_faultmodel() || param.msg.faultmodel() == param.msg.SINGLEBITFLIP)
	 && param.msg.result_size() != 8) {
		log << "WTF? param.msg.result_size() != 8" << endl;
	} else {
		param.msg.set_runtime(timer);
#if !LOCAL
		m_jc.sendResult(param);
#endif
	}

#if !LOCAL
	}
#endif
	return true;
}
#endif // PREREQUISITES

bool EcosKernelTestExperiment::readELFSymbols(
	fail::guest_address_t& entry,
	fail::guest_address_t& finish,
	fail::guest_address_t& test_output,
	fail::guest_address_t& errors_corrected,
	fail::guest_address_t& panic,
	fail::guest_address_t& text_start,
	fail::guest_address_t& text_end,
	fail::guest_address_t& data_start,
	fail::guest_address_t& data_end)
{
	ElfReader elfreader(EcosKernelTestCampaign::filename_elf(m_variant, m_benchmark).c_str());
	entry            = elfreader.getSymbol("cyg_start").getAddress();
	finish           = elfreader.getSymbol("cyg_test_exit").getAddress();
	test_output      = elfreader.getSymbol("cyg_test_output").getAddress();
	errors_corrected = elfreader.getSymbol("errors_corrected").getAddress();
	panic            = elfreader.getSymbol("_Z9ecc_panicv").getAddress();
	text_start       = elfreader.getSymbol("_stext").getAddress();
	text_end         = elfreader.getSymbol("_etext").getAddress();
	data_start       = elfreader.getSymbol("__ram_data_start").getAddress();
	data_end         = elfreader.getSymbol("__bss_end").getAddress();

	// it's OK if errors_corrected or ecc_panic are missing
	if (entry == ADDR_INV || finish == ADDR_INV || test_output == ADDR_INV ||
	    text_start == ADDR_INV || text_end == ADDR_INV ||
	    data_start == ADDR_INV || data_end == ADDR_INV) {
		return false;
	}
	return true;
}

void EcosKernelTestExperiment::parseOptions()
{
	CommandLine &cmd = CommandLine::Inst();
	cmd.addOption("", "", Arg::None, "USAGE: fail-client -Wf,[option] -Wf,[option] ... <BochsOptions...>");
	CommandLine::option_handle HELP =
		cmd.addOption("h", "help", Arg::None, "-h,--help \tPrint usage and exit");
	CommandLine::option_handle VARIANT =
		cmd.addOption("", "variant", Arg::Required, "--variant v \texperiment variant");
	CommandLine::option_handle BENCHMARK =
		cmd.addOption("", "benchmark", Arg::Required, "--benchmark b \tbenchmark");

	if (!cmd.parse()) {
		cerr << "Error parsing arguments." << endl;
		simulator.terminate(1);
	} else if (cmd[HELP]) {
		cmd.printUsage();
		simulator.terminate(0);
	}

	if (cmd[VARIANT].count() > 0 && cmd[BENCHMARK].count() > 0) {
		m_variant = std::string(cmd[VARIANT].first()->arg);
		m_benchmark = std::string(cmd[BENCHMARK].first()->arg);
	} else {
		cerr << "Please supply parameters for --variant and --benchmark." << endl;
		simulator.terminate(1);
	}
}

bool EcosKernelTestExperiment::run()
{
	log << "startup" << endl;

#if PREREQUISITES || LOCAL
	parseOptions();
#endif

#if PREREQUISITES
	log << "retrieving ELF symbol addresses ..." << endl;
	guest_address_t entry, finish, test_output, errors_corrected,
	                panic, text_start, text_end, data_start, data_end;
	if (!readELFSymbols(entry, finish, test_output, errors_corrected,
	               panic, text_start, text_end, data_start, data_end)) {
		log << "failed, essential symbols are missing!" << endl;
		simulator.terminate(1);
	}

	// step 0
	if (retrieveGuestAddresses(finish, data_start, data_end)) {
		log << "STEP 0 finished: rebooting ..." << endl;
		simulator.reboot();
	} else { return false; }

	// step 1
	if (establishState(entry, finish, errors_corrected)) {
		log << "STEP 1 finished: proceeding ..." << endl;
	} else { return false; }

	// step 2
	if (performTrace(entry, finish)) {
		log << "STEP 2 finished: terminating ..." << endl;
	} else { return false; }

#else // !PREREQUISITES
	// step 3
	faultInjection();
#endif // PREREQUISITES

	// Explicitly terminate, or the simulator will continue to run.
	simulator.terminate();
	return true;
}
