#ifndef __ELFREADER_HPP__
#define __ELFREADER_HPP__

#include <string>
#include <ostream>
#include "sal/SALConfig.hpp" // for ADDR_INV
#include "Logger.hpp"
#include "elfinfo/elfinfo.h"
#include <vector>
#include <map>
#include "Demangler.hpp"

namespace fail {

  struct ELF {
    static const std::string NOTFOUND;
  };

  class ElfSymbol {
    std::string name;
    guest_address_t address;
    size_t size;
    int m_type;

    public:
    enum { SECTION = 1, SYMBOL = 2, UNDEFINED = 3, };

    ElfSymbol(const std::string & name = ELF::NOTFOUND, guest_address_t addr = ADDR_INV, size_t size = -1, int type = UNDEFINED)
      : name(name), address(addr), size(size), m_type(type) {};

    const std::string& getName() const { return name; };
    guest_address_t getAddress() const { return address; };
    size_t getSize() const { return size; };
    guest_address_t getStart() const { return getAddress(); }; // alias
    guest_address_t getEnd() const { return address + size; };

    bool isSection() const { return m_type == SECTION; };
    bool isSymbol()  const { return m_type == SYMBOL; };

    bool operator==(const std::string& rhs) const {
      if(rhs == name){
        return true;
      }
      if( rhs == Demangler::demangle(name) ){
        return true;
      }

      return false;
    }

    bool operator==(const guest_address_t rhs) const {
      return rhs == address;
    }

    bool contains(guest_address_t ad) const {
      return (ad >= address) && (ad < address+size);
    }
  };


  /**
   * \class ElfReader
   * Parses an ELF file and provides a list of symbol names
   * and corresponding addresses
   */

  class ElfReader {

    public:

      /**
       * Constructor.
       * @param path Path to the ELF file.
       */
      ElfReader(const char* path);

      /**
       * Constructor.
       * @note The path is guessed from a FAIL_ELF_PATH environment variable
       */
      ElfReader();

      /**
       * Print the list of available mangled symbols
       * @note This includes both C and C++ symbols
       */
       void printMangled();

      /**
       * Print a list of demangled symbols.
       */
       void printDemangled();

      /**
       * Print the list of all available sections.
       */
       void printSections();

      /**
       * Get symbol by address
       * @param address Address within range of the symbol
       * @return The according symbol name if symbol.address <= address < symbol.address + symbol.size , else g_SymbolNotFound
       */
       const ElfSymbol& getSymbol(guest_address_t address);

      /**
       * Get symbol by name
       * @param address Name of the symbol
       * @return The according symbol name if section was found, else g_SymbolNotFound
       */
       const ElfSymbol& getSymbol( const std::string& name );

      /**
       * Get section by address
       * @param address An address to search for a section containing that address.
       * @return The according section name if section was found, else g_SymbolNotFound
       */
       const ElfSymbol& getSection(guest_address_t address);

      /**
       * Get section by name
       * @param name The name of the section
       * @return The according section if section was found, else g_SymbolNotFound
       */
       const ElfSymbol& getSection( const std::string& name );

    private:
      Logger m_log;

      void setup(const char*);
      int process_symboltable(int sect_num, FILE* fp);
      int process_section(Elf32_Shdr *sect_hdr, char* sect_name_buff);

      typedef ElfSymbol entry_t;
      typedef std::vector<entry_t> container_t;

      container_t m_symboltable;
      container_t m_sectiontable;

      guest_address_t getAddress(const std::string& name);
      std::string getName(guest_address_t address);
  };

} // end-of-namespace fail

#endif //__ELFREADER_HPP__
