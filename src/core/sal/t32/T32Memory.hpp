#ifndef __T32_MEMORY_HPP__
  #define __T32_MEMORY_HPP__

#include "../Memory.hpp"

#include <t32.h>

namespace fail {

/**
 * \class T32MemoryManager
 * Represents a concrete implemenation of the abstract
 * MemoryManager to provide access to T32's memory pool.
 */
class T32MemoryManager : public MemoryManager {

  enum ACCESS_CLASS {
    data_access = 0,
    program_access = 1,
    AD_access = 12,
    AP_access = 13,
    USR_access = 15,
  };

public:
	size_t getPoolSize() const { return 0; /* TODO */ }

	host_address_t getStartAddr() const { return 0; }

	byte_t getByte(guest_address_t addr)
	{
    char b;
    getBytes(addr, 1, &b);
    return b;
  }

	void getBytes(guest_address_t addr, size_t cnt, void *dest)
	{
    int access = data_access; // TODO what access class do we need?!
    T32_ReadMemory( addr, access, (byte*)(dest), cnt);
	}

	void setByte(guest_address_t addr, byte_t data)
	{
    setBytes(addr, 1, &data);
	}

	void setBytes(guest_address_t addr, size_t cnt, void const *src)
	{
    int access = data_access; // TODO what access class do we really need?!
    T32_WriteMemory(addr, access, (byte*)(src), cnt);
	}

};

} // end-of-namespace: fail

#endif // __T32_MEMORY_HPP__
