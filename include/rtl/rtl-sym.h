/*
 *  COPYRIGHT (c) 2012-2014, 2018 Chris Johns <chrisj@rtems.org>
 *
 *  The license and distribution terms for this file may be
 *  found in the file LICENSE in this distribution or at
 *  http://www.rtems.org/license/LICENSE.
 */
/**
 * @file
 *
 * @ingroup rtems_rtl
 *
 * @brief RTEMS Run-Time Linker Object File Symbol Table.
 */

#if !defined (_RTEMS_RTL_SYM_H_)
#define _RTEMS_RTL_SYM_H_

#include <stdbool.h>
#include <FreeRTOS.h>
#include "list.h"
#include "rtl-obj-fwd.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * An object file symbol.
 */
typedef struct rtems_rtl_obj_sym
{
  ListItem_t       node;    /**< The node's link in the chain. */
  const char*      name;    /**< The symbol's name. */
  uintptr_t        value;   /**< The value of the symbol. */
  size_t           size;    /**< The size of the symbol. */
  uint32_t         data;    /**< Format specific data. */
#ifdef __CHERI_PURE_CAPABILITY__
  uint32_t         capability;  /**< A potential cap index for that symbol */
#endif
} rtems_rtl_obj_sym;

/**
 * Table of symbols stored in a hash table.
 */
typedef struct rtems_rtl_symbols
{
  List_t*              buckets;
  size_t               nbuckets;
} rtems_rtl_symbols;

typedef enum interface_symbol_type {
  RTL_INTERFACE_SYMBOL_EMPTY,
  RTL_INTERFACE_SYMBOL_ALL_GLOBALS,
  RTL_INTERFACE_SYMBOL_SUBSET_GLOBALS
} isymbol_table_mode_t;

static inline bool rtems_rtl_symbol_name_valid (const char* name) {
  return ((name != NULL) && name[0]);
}

/**
 * Open a symbol table with the specified number of buckets.
 *
 * @param symbols The symbol table to open.
 * @param buckets The number of buckets in the hash table.
 * @retval true The symbol is open.
 * @retval false The symbol table could not created. The RTL
 *               error has the error.
 */
bool rtems_rtl_symbol_table_open (rtems_rtl_symbols* symbols,
                                  size_t             buckets);

/**
 * Close the table and erase the hash table.
 *
 * @param symbols Close the symbol table.
 */
void rtems_rtl_symbol_table_close (rtems_rtl_symbols* symbols);

/**
 * Add a table of exported symbols to the symbol table.
 *
 * The export table is a series of symbol records and each record has two
 * fields:
 *
 *  1. label
 *  2. address
 *
 * The 'label' is an ASCIIZ string of variable length. The address is of size
 * of an unsigned long for the target running the link editor. The byte order
 * is defined by the machine type because the table should be built by the
 * target compiler.
 *
 * The table is terminated with a nul string followed by the bytes 0xDE, 0xAD,
 * 0xBE, and 0xEF. This avoids alignments issues.
 *
 * @param obj The object table the symbols are for.
 * @param esyms The exported symbol table.
 * @param size The size of the table in bytes.
 */
bool rtems_rtl_symbol_global_add (rtems_rtl_obj*       obj,
                                  const unsigned char* esyms,
                                  unsigned int         size);

/**
 * Find a symbol given the symbol label in the global symbol table.
 *
 * @param name The name as an ASCIIZ string.
 * @retval NULL No symbol found.
 * @return rtems_rtl_obj_sym* Reference to the symbol.
 */
rtems_rtl_obj_sym* rtems_rtl_symbol_global_find (const char* name);

/**
 * Sort an object file's local and global symbol table. This needs to
 * be done before calling @ref rtems_rtl_symbol_obj_find as it
 * performs a binary search on the tables.
 *
 * @param obj The object file to sort.
 */
void rtems_rtl_symbol_obj_sort (rtems_rtl_obj* obj);

/**
 * Find a symbol given the symbol label in the local object file.
 * This searches all locals, globals, interface, externals and public global
 * list for this object file.
 *
 * @param obj The object file to search.
 * @param name The name as an ASCIIZ string.
 * @retval NULL No symbol found.
 * @return rtems_rtl_obj_sym* Reference to the symbol.
 */
rtems_rtl_obj_sym* rtems_rtl_symbol_obj_find (rtems_rtl_obj* obj,
                                              const char*    name);

/**
 * Find a symbol given the symbol label in the globals list of that object file.
 *
 * @param obj The object file to search.
 * @param name The name as an ASCIIZ string.
 * @retval NULL No symbol found.
 * @return rtems_rtl_obj_sym* Reference to the symbol.
 */
rtems_rtl_obj_sym*
rtems_rtl_gsymbol_obj_find (rtems_rtl_obj* obj, const char* name);

/**
 * Find a symbol given the symbol label in the interface list of that object file.
 *
 * @param obj The object file to search.
 * @param name The name as an ASCIIZ string.
 * @retval NULL No symbol found.
 * @return rtems_rtl_obj_sym* Reference to the symbol.
 */
rtems_rtl_obj_sym*
rtems_rtl_isymbol_obj_find (rtems_rtl_obj* obj, const char* name);

/**
 * Find a symbol given the symbol label in the extenals list of that object file.
 *
 * @param obj The object file to search.
 * @param name The name as an ASCIIZ string.
 * @retval NULL No symbol found.
 * @return rtems_rtl_obj_sym* Reference to the symbol.
 */
rtems_rtl_obj_sym*
rtems_rtl_esymbol_obj_find (rtems_rtl_obj* obj, const char* name);

/**
 * Create a new interface symbol table for an object file.
 *
 * @param obj The object file to create the table for.
 * @param mode The create mode for the interface table.
 * @retval false if failed to create, true if succeeded.
 */
bool rtems_rtl_isymbol_create (rtems_rtl_obj* obj, isymbol_table_mode_t mode);

/**
 * Mint/Copy a symbol from one object's interface table (that owns the symbol)
 * to another object's externals (that requests to access it).
 *
 * @param src_obj The object file that owns the symbol.
 * @param dest_obj The object file that references that external symbol.
 * @retval NULL if failed to mint it, or a newly created symbol that's added to
 * the externals list of the dest_obj if all checks and allocations pass.
 */
rtems_rtl_obj_sym*
rtems_rtl_isymbol_obj_mint (rtems_rtl_obj* src_obj,
                            rtems_rtl_obj* dest_obj,
                            const char* name);

/**
 * Find a symbol given the symbol label and value in the local object file.
 * This should only be used after sections have been relocated to find
 * a unique symbol entry even if the labels are duplicated.
 *
 * @param obj The object file to search.
 * @param name The name as an ASCIIZ string.
 * @param value The symvalue (address) after being relocated.
 * @retval NULL No symbol found.
 * @return rtems_rtl_obj_sym* Reference to the symbol.
 */
rtems_rtl_obj_sym* rtems_rtl_symbol_obj_find_namevalue (rtems_rtl_obj* obj,
                                              const char*    name,
                                              UBaseType_t    value);

rtems_rtl_obj_sym*
rtems_rtl_lsymbol_obj_find(rtems_rtl_obj* obj, const char* name);
/**
 * Find and extract a symbol given the symbol label in the local object file.
 * The symbol is only extracted from the locals list.
 *
 * @param obj The object file to search.
 * @param name The name as an ASCIIZ string.
 * @retval NULL No symbol found.
 * @return rtems_rtl_obj_sym* Reference to the symbol.
 */
rtems_rtl_obj_sym* rtems_rtl_symbol_obj_extract (rtems_rtl_obj* obj,
                                                 const char*    name);

/**
 * Add the object file's symbols to the global table.
 *
 * @param obj The object file the symbols are to be added.
 */
void rtems_rtl_symbol_obj_add (rtems_rtl_obj* obj);

/**
 * Erase the object file's local symbols.
 *
 * @param obj The object file the local symbols are to be erased from.
 */
void rtems_rtl_symbol_obj_erase_local (rtems_rtl_obj* obj);

/**
 * Erase the object file's symbols.
 *
 * @param obj The object file the symbols are to be erased from.
 */
void rtems_rtl_symbol_obj_erase (rtems_rtl_obj* obj);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif
