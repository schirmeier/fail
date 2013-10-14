#include "X86Architecture.hpp"
#include "../Register.hpp"
#include <sstream>

namespace fail {

X86Architecture::X86Architecture()
{
	// -------------------------------------
	// Add the general purpose register:
#ifdef SIM_SUPPORT_64
	// -- 64 bit register --
	const  std::string names[] = { "RAX", "RCX", "RDX", "RBX", "RSP", "RBP", "RSI", "RDI", "R8",
		"R9", "R10", "R11", "R12", "R13", "R14", "R15" };
	for (unsigned short i = 0; i < 16; i++) {
		Register* pReg = new Register(i, 64);
		pReg->setName(names[i]);
		m_addRegister(pReg, RT_GP);
	}
#else
	// -- 32 bit register --
	const std::string names[] = { "EAX", "ECX", "EDX", "EBX", "ESP", "EBP", "ESI", "EDI" };
	for (unsigned short i = 0; i < 8; i++) {
		Register* pReg = new Register(i, 32);
		pReg->setName(names[i]);
		m_addRegister(pReg, RT_GP);
	}
#endif // SIM_SUPPORT_64
	// -------------------------------------
	// Add the program counter (PC) register:
#ifdef SIM_SUPPORT_64
	Register* pPCReg = new Register(RID_PC, 64);
	pPCReg->setName("RIP");
#else
	Register* pPCReg = new Register(RID_PC, 32);
	pPCReg->setName("EIP");
#endif // SIM_SUPPORT_64
	m_addRegister(pPCReg, RT_IP);
	// -------------------------------------
	// Add the status register (EFLAGS):
	Register* pFlagReg = new Register(RID_FLAGS, 32);
	pFlagReg->setName("EFLAGS");
	m_addRegister(pFlagReg, RT_ST);

	// Registers used for extended tracing:
	size_t ids[] = {RID_CAX, RID_CBX, RID_CCX, RID_CDX, RID_CSI, RID_CDI, RID_CSP, RID_CBP, RID_FLAGS};
	for (size_t i = 0; i < sizeof(ids)/sizeof(*ids); ++i) {
		m_addRegister(getRegister(ids[i]), RT_TRACE);
	}
}

X86Architecture::~X86Architecture()
{
	for (std::vector<Register*>::iterator it = m_Registers.begin(); it != m_Registers.end(); it++)
		delete *it;
	m_Registers.clear();
}

} // end-of-namespace: fail
