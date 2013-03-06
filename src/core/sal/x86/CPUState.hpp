#ifndef __X86_CPU_STATE_HPP__
  #define __X86_CPU_STATE_HPP__

#include "../CPU.hpp"
#include "../CPUState.hpp"

namespace fail {

/**
 * \class X86CPUState
 * This class represents the current state of a x86 based CPU. A final CPU class
 * implemention need to implement \c X86CPUState and \c X86Architecture.
 */
class X86CPUState : public CPUState {
public:
	/**
	 * Returns the current content of the base pointer register.
	 * @return the current (e)bp
	 */
	virtual address_t getBasePointer() const = 0;
	/**
	 * Returns the current (E)FLAGS.
	 * @return the current (E)FLAGS processor register content
	 */
	virtual regdata_t getFlagsRegister() const = 0;
};

} // end-of-namespace: fail

#endif // __X86_CPU_STATE_HPP__