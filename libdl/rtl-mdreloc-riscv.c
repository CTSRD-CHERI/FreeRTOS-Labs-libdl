/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2019 Hesham Almatary
 *
 * This software was developed by SRI International and the University of
 * Cambridge Computer Laboratory (Department of Computer Science and
 * Technology) under DARPA contract HR0011-18-C-0016 ("ECATS"), as part of the
 * DARPA SSITH research programme.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/* Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
 * See https://llvm.org/LICENSE.txt for license information.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#include <sys/cdefs.h>

#include <errno.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>

#include <rtl/rtl.h>
#include "rtl-elf.h"
#include "rtl-error.h"
#include <rtl/rtl-trace.h>
#include "rtl-unwind.h"
#include "rtl-unwind-dw2.h"

#include <FreeRTOSConfig.h>
#include <FreeRTOS.h>
#include "list.h"

#include <rtl/rtl-freertos-compartments.h>

#ifdef __CHERI_PURE_CAPABILITY__
#include <cheri/cheri-utility.h>
#endif

#define RTL_RISCV_HI20_HASHTABLE_BUCKETS 32

typedef struct rela_hi20 {
  ListItem_t  node;
  Elf_Word    symvalue;
  Elf_Word    hi20_tramp;
  intptr_t    hi20_pc;
} hi20_reloc_t;

static List_t hi20_relocs;
static int hi20_init = 0;

typedef struct rela_hi20_table {
  List_t      *buckets;
  size_t      nbuckets;
} hi20_relocs_t;

static hi20_relocs_t hi20_relocs_table;

static uint_fast32_t
rtems_hi20_hash (const intptr_t hi20_pc)
{
  return ((uint_fast32_t) hi20_pc) & 0xffffffff;
}

static void
rtems_rtl_hi20_insert (hi20_relocs_t*     relocs,
                       hi20_reloc_t*      reloc)
{
  uint_fast32_t hash = rtems_hi20_hash (reloc->hi20_pc);
  vListInsertEnd (&relocs->buckets[hash % relocs->nbuckets],
               &reloc->node);
}

static hi20_reloc_t*
rtems_rtl_hi20_find (intptr_t hi20_pc)
{
  hi20_relocs_t*       relocs;
  uint_fast32_t        hash;
  List_t*              bucket;
  ListItem_t*          node;

  relocs = &hi20_relocs_table;

  hash = rtems_hi20_hash (hi20_pc);
  bucket = &relocs->buckets[hash % relocs->nbuckets];
  node = listGET_HEAD_ENTRY (bucket);

  while (listGET_END_MARKER (bucket) != node)
  {
    hi20_reloc_t* reloc = (hi20_reloc_t*) node;

    if ((uint32_t) reloc->hi20_pc == (uint32_t) hi20_pc) {
      return reloc;
    }

    node = listGET_NEXT (node);
  }

  return NULL;
}

static hi20_reloc_t*
rtems_rtl_hi20_find_symvalue (Elf_Word symvalue)
{
  hi20_relocs_t*       relocs;
  List_t*              bucket;
  ListItem_t*          node;

  relocs = &hi20_relocs_table;
  size_t nbuckets = hi20_relocs_table.nbuckets;


  for (int i = 0; i < nbuckets; i ++) {
    bucket = &relocs->buckets[i];
    node = listGET_HEAD_ENTRY (bucket);

    while (listGET_END_MARKER (bucket) != node)
    {
      hi20_reloc_t* reloc = (hi20_reloc_t*) node;

      if ((uint32_t) reloc->symvalue == (uint32_t) symvalue) {
        return reloc;
      }

      node = listGET_NEXT (node);
    }
  }

  return NULL;
}

static bool
rtems_rtl_hi20_table_open (hi20_relocs_t      *relocs,
                           size_t             buckets)
{
  relocs->buckets = rtems_rtl_alloc_new (RTEMS_RTL_ALLOC_SYMBOL,
                                          buckets * sizeof (List_t),
                                          true);
  if (!relocs->buckets)
  {
    rtems_rtl_set_error (ENOMEM, "no memory for hi20 relocs table");
    return false;
  }
  relocs->nbuckets = buckets;

  for (buckets = 0; buckets < relocs->nbuckets; ++buckets)
    vListInitialise (&relocs->buckets[buckets]);

  return true;
}

#if 0
static void
rtems_rtl_elf_relocate_riscv_hi20_add (Elf_Word symvalue, Elf_Word *pc, bool is_cap, Elf_Word cap_addr, bool is_func) {
  hi20_reloc_t *hi20_reloc =  rtems_rtl_alloc_new (RTEMS_RTL_ALLOC_OBJECT, sizeof(hi20_reloc_t), true);
  vListInitialiseItem(&hi20_reloc->node);

  hi20_reloc->symvalue = symvalue;
  hi20_reloc->hi20_pc = pc;

#ifdef __CHERI_PURE_CAPABILITY__
  hi20_reloc->is_cap = is_cap;
  hi20_reloc->cap_addr = cap_addr;
#endif

  vListInsertEnd(&hi20_relocs, hi20_reloc);
}

static void
rtems_rtl_elf_relocate_riscv_hi20_del (hi20_reloc_t *hi20_reloc) {
  uxListRemove(&hi20_reloc->node);
  rtems_rtl_alloc_del (RTEMS_RTL_ALLOC_OBJECT, hi20_reloc);
}

static bool
rtems_rtl_elf_relocate_riscv_hi20_find (Elf_Word *hi20_pc, hi20_reloc_t** ret_reloc) {
  ListItem_t *node = listGET_HEAD_ENTRY (&hi20_relocs);

  while (listGET_END_MARKER (&hi20_relocs) != node)
  {
    hi20_reloc_t* reloc = (hi20_reloc_t*) node;

    if (reloc->hi20_pc == hi20_pc) {
      *ret_reloc = reloc;
      return true;
    }

    node = listGET_NEXT (node);
  }

  return false;
}

static bool
rtems_rtl_elf_relocate_riscv_hi20_table_free (void) {
  ListItem_t *node = listGET_HEAD_ENTRY (&hi20_relocs);

  while (listGET_END_MARKER (&hi20_relocs) != node)
  {
    hi20_reloc_t* reloc = (hi20_reloc_t*) node;
    rtems_rtl_elf_relocate_riscv_hi20_del(reloc);

    node = listGET_NEXT (node);
  }
}
#endif

uint32_t
rtems_rtl_elf_section_flags (const rtems_rtl_obj* obj,
                             const Elf_Shdr*      shdr) {
  return 0;
}

uint32_t
rtems_rtl_elf_arch_parse_section (const rtems_rtl_obj* obj,
                                  int                  section,
                                  const char*          name,
                                  const Elf_Shdr*      shdr,
                                  const uint32_t       flags) {
  (void) obj;
  (void) section;
  (void) name;
  (void) shdr;

  if ((flags & RTEMS_RTL_OBJ_SECT_RELA) == RTEMS_RTL_OBJ_SECT_RELA) {
    if (!hi20_init) {
      rtems_rtl_hi20_table_open(&hi20_relocs_table, RTL_RISCV_HI20_HASHTABLE_BUCKETS);
      //vListInitialise (&hi20_relocs);
      hi20_init = 1;
    }
  }

  return flags;
}

bool
rtems_rtl_elf_arch_section_alloc (const rtems_rtl_obj* obj,
                                  rtems_rtl_obj_sect*  sect) {
  (void) obj;
  (void) sect;
  return false;
}

bool
rtems_rtl_elf_arch_section_free (const rtems_rtl_obj* obj,
                                 rtems_rtl_obj_sect*  sect) {
  (void) obj;
  (void) sect;
  return false;
}

bool
rtems_rtl_elf_rel_resolve_sym (Elf_Word type) {
  return true;
}

size_t
rtems_rtl_elf_relocate_tramp_max_size (void) {
  /*
   * Disable by returning 0.
   */
  return 0;
}

rtems_rtl_elf_rel_status
rtems_rtl_elf_relocate_rel_tramp (rtems_rtl_obj*            obj,
                                  const Elf_Rel*            rel,
                                  const rtems_rtl_obj_sect* sect,
                                  const char*               symname,
                                  const Elf_Byte            syminfo,
                                  const Elf_Word            symvalue) {
  (void) obj;
  (void) rel;
  (void) sect;
  (void) symname;
  (void) syminfo;
  (void) symvalue;
  return rtems_rtl_elf_rel_no_error;
}

// Extract bits V[Begin:End], where range is inclusive, and Begin must be < 63.
static uint32_t extractBits(uint64_t v, uint32_t begin, uint32_t end) {
  return (v & ((1ULL << (begin + 1)) - 1)) >> end;
}

static int64_t SignExtend64(uint64_t val, unsigned bits) {
  return (int64_t )(((int64_t) (val << (64 - bits))) >> (64 - bits));
}

static void write16le(void *loc, uint16_t val) {
  *((uint16_t *) loc) = val;
}

static void write32le(void *loc, uint32_t val) {
  //*((uint32_t *) loc) = val;
  write16le(loc, val & 0xffff);
  write16le(loc + 2, (val >> 16) & 0xffff);
}

static void write64le(void *loc, uint64_t val) {
  *((uint64_t *) loc) = val;
}

static uint16_t read16le(void *loc) {
  return *((uint16_t *) loc);
}

static uint32_t read32le(void *loc) {
  //return *((uint32_t *) loc);
  return ((uint32_t) read16le(loc + 2) << 16) | (uint32_t) read16le(loc);
}

static uint64_t read64le(void *loc) {
  return *((uint64_t *) loc);
}

static rtems_rtl_elf_rel_status
rtems_rtl_elf_reloc_rela (rtems_rtl_obj*            obj,
                          const Elf_Rela*           rela,
                          const rtems_rtl_obj_sect* sect,
                          const char*               symname,
                          const Elf_Word            syminfo,
                          Elf_Word                  symvalue,
                          Elf_Word                  type,
                          const bool parsing) {
  Elf_Word *where;
  Elf_Word  tmp;
  Elf_Word addend = (Elf_Word) rela->r_addend;
  Elf_Word local = 0;

  char bits = (sizeof(Elf_Word) * 8);
  where = (Elf_Word *)(sect->base + rela->r_offset);
  symvalue += addend;

  // Final PCREL value
  Elf_Word pcrel_val = symvalue - ((intptr_t) where);

  if (ELF_ST_TYPE (syminfo >> 16) == STT_SECTION) {
    local = 1;
    return rtems_rtl_elf_rel_no_error;
  }

  if (parsing) {
    return rtems_rtl_elf_rel_no_error;
  }

  if (type) {
    if (ELF_R_TYPE(rela->r_info) == R_TYPE(PCREL_LO12_I)) {
      uint64_t hi;
      uint64_t lo;
      hi20_reloc_t *ret_reloc;
      Elf_Word return_sym;
      intptr_t hi20_rela_pc =  ((intptr_t) where) + pcrel_val;

      ret_reloc = rtems_rtl_hi20_find(hi20_rela_pc);
      if (!ret_reloc) {
        rtems_rtl_set_error (EINVAL,
                           "%s: Failed to find HI20 relocation for type %d",
                           sect->name, (uint32_t) ELF_R_TYPE(rela->r_info));
        return rtems_rtl_elf_rel_failure;
          return rtems_rtl_elf_rel_no_error;
      } else {
        return_sym = ret_reloc->symvalue;
        pcrel_val = return_sym - ((Elf_Word) hi20_rela_pc);
      }

      hi = (pcrel_val + 0x800) >> 12;
      lo = pcrel_val - (hi << 12);
      write32le(where, (read32le(where) & 0xFFFFF) | ((lo & 0xFFF) << 20));

      return rtems_rtl_elf_rel_no_error;
    } else if (ELF_R_TYPE(rela->r_info) == R_TYPE(PCREL_LO12_S)) {
      uint64_t hi;
      uint64_t lo;
      hi20_reloc_t *ret_reloc;
      Elf_Word return_sym;
      intptr_t hi20_rela_pc =  ((intptr_t) where) + pcrel_val;

      ret_reloc = rtems_rtl_hi20_find(hi20_rela_pc);
      if (!ret_reloc) {
        rtems_rtl_set_error (EINVAL,
                           "%s: Failed to find HI20 relocation for type %d",
                           sect->name, (uint32_t) ELF_R_TYPE(rela->r_info));
        return rtems_rtl_elf_rel_failure;
      } else {
        return_sym = ret_reloc->symvalue;
        pcrel_val = return_sym - ((Elf_Word) hi20_rela_pc);
      }

      hi = (pcrel_val + 0x800) >> 12;
      lo = pcrel_val - (hi << 12);

      uint32_t imm11_5 = extractBits(lo, 11, 5) << 25;
      uint32_t imm4_0 = extractBits(lo, 4, 0) << 7;
      write32le(where, (read32le(where) & 0x1FFF07F) | imm11_5 | imm4_0);

      return rtems_rtl_elf_rel_no_error;
    } else {
      rtems_rtl_set_error (EINVAL, "Can only selectively relocate LO12 types\n");
      return rtems_rtl_elf_rel_failure;
    }
  }

  switch (ELF_R_TYPE(rela->r_info)) {
  case R_TYPE(NONE):
    break;

  case R_TYPE(RVC_BRANCH): {
    uint16_t insn = (read16le(where) & 0xFFFF) & 0xE383;
    uint16_t imm8 = extractBits(pcrel_val, 8, 8) << 12;
    uint16_t imm4_3 = extractBits(pcrel_val, 4, 3) << 10;
    uint16_t imm7_6 = extractBits(pcrel_val, 7, 6) << 5;
    uint16_t imm2_1 = extractBits(pcrel_val, 2, 1) << 3;
    uint16_t imm5 = extractBits(pcrel_val, 5, 5) << 2;
    insn |= imm8 | imm4_3 | imm7_6 | imm2_1 | imm5;

    write16le(where, insn);
  }
  break;

  case R_TYPE(RVC_JUMP): {
    uint16_t insn = (read16le(where) & 0xFFFF) & 0xE003;
    uint16_t imm11 = extractBits(pcrel_val, 11, 11) << 12;
    uint16_t imm4 = extractBits(pcrel_val, 4, 4) << 11;
    uint16_t imm9_8 = extractBits(pcrel_val, 9, 8) << 9;
    uint16_t imm10 = extractBits(pcrel_val, 10, 10) << 8;
    uint16_t imm6 = extractBits(pcrel_val, 6, 6) << 7;
    uint16_t imm7 = extractBits(pcrel_val, 7, 7) << 6;
    uint16_t imm3_1 = extractBits(pcrel_val, 3, 1) << 3;
    uint16_t imm5 = extractBits(pcrel_val, 5, 5) << 2;
    insn |= imm11 | imm4 | imm9_8 | imm10 | imm6 | imm7 | imm3_1 | imm5;

    write16le(where, insn);
  }
  break;

  case R_TYPE(RVC_LUI): {
    int64_t imm = SignExtend64(symvalue + 0x800, bits) >> 12;
    if (imm == 0) { // `c.lui rd, 0` is illegal, convert to `c.li rd, 0`
      write16le(where, (read16le(where) & 0x0F83) | 0x4000);
    } else {
      uint16_t imm17 = extractBits(symvalue + 0x800, 17, 17) << 12;
      uint16_t imm16_12 = extractBits(symvalue + 0x800, 16, 12) << 2;
      write16le(where, (read16le(where) & 0xEF83) | imm17 | imm16_12);
    }
  }
  break;

  case R_TYPE(JAL): {
    uint32_t insn = read32le(where) & 0xFFF;
    uint32_t imm20 = extractBits(pcrel_val, 20, 20) << 31;
    uint32_t imm10_1 = extractBits(pcrel_val, 10, 1) << 21;
    uint32_t imm11 = extractBits(pcrel_val, 11, 11) << 20;
    uint32_t imm19_12 = extractBits(pcrel_val, 19, 12) << 12;
    insn |= imm20 | imm10_1 | imm11 | imm19_12;

    write32le(where, insn);
  }
  break;

  case R_TYPE(BRANCH): {

    uint32_t insn = read32le(where) & 0x1FFF07F;
    uint32_t imm12 = extractBits(pcrel_val, 12, 12) << 31;
    uint32_t imm10_5 = extractBits(pcrel_val, 10, 5) << 25;
    uint32_t imm4_1 = extractBits(pcrel_val, 4, 1) << 8;
    uint32_t imm11 = extractBits(pcrel_val, 11, 11) << 7;
    insn |= imm12 | imm10_5 | imm4_1 | imm11;

    write32le(where, insn);
  }
  break;

  case R_TYPE(64): {
#if configMPU_COMPARTMENTALIZATION
        rtems_rtl_obj_sym* rtl_sym = rtems_rtl_symbol_obj_find(obj, symname);
        if (rtl_sym) {

          if (ELF_ST_TYPE(rtl_sym->data >> 16) == STT_FUNC) {
            // Find the owner compartment of this symbol
            rtems_rtl_obj* owner_obj = rtems_rtl_find_obj_with_symbol(symname);
            if (owner_obj == NULL) {
              // It is a local static function
              owner_obj = obj;
            }

            // Get the symbol/capability from the owner's obj/captable
            rtl_sym = rtems_rtl_symbol_obj_find(owner_obj, symname);
            if (rtl_sym == NULL) {
              return rtems_rtl_elf_rel_failure;
            }

            void* tramp_cap = rtl_cherifreertos_compartments_setup_ecall((void*) symvalue, rtl_cherifreertos_compartment_get_compid(owner_obj));
            if (tramp_cap == NULL) {
              return rtems_rtl_elf_rel_failure;
            } else {
              symvalue = (size_t) tramp_cap;
            }
        }
      }
#endif
    write64le(where, symvalue);
  }
  break;
  case R_TYPE(32): {
#if configMPU_COMPARTMENTALIZATION
        rtems_rtl_obj_sym* rtl_sym = rtems_rtl_symbol_obj_find(obj, symname);
        if (rtl_sym) {

          if (ELF_ST_TYPE(rtl_sym->data >> 16) == STT_FUNC) {
            // Find the owner compartment of this symbol
            rtems_rtl_obj* owner_obj = rtems_rtl_find_obj_with_symbol(symname);
            if (owner_obj == NULL) {
              // It is a local static function
              owner_obj = obj;
            }

            // Get the symbol/capability from the owner's obj/captable
            rtl_sym = rtems_rtl_symbol_obj_find(owner_obj, symname);
            if (rtl_sym == NULL) {
              return rtems_rtl_elf_rel_failure;
            }

            void* tramp_cap = rtl_cherifreertos_compartments_setup_ecall((void*) symvalue, rtl_cherifreertos_compartment_get_compid(owner_obj));
            if (tramp_cap == NULL) {
              return rtems_rtl_elf_rel_failure;
            } else {
              symvalue = (size_t) tramp_cap;
            }
        }
      }
#endif
    write32le(where, symvalue);
  }
  break;

  case R_TYPE(SET6):
    *((uint8_t *) where) = (read32le(where) & 0xc0) | (symvalue & 0x3f);
    break;
  case R_TYPE(SET8):
    *((uint8_t *) where) = symvalue;
    break;
  case R_TYPE(SET16):
    write16le(where, symvalue);
    break;
  case R_TYPE(SET32):
    write32le(where, symvalue);
    break;

  case R_TYPE(ADD8):
    *((uint8_t *) where) = *((uint8_t *) where) + symvalue;
    break;
  case R_TYPE(ADD16):
    write16le(where, read16le(where) + symvalue);
    break;
  case R_TYPE(ADD32):
    write32le(where, read32le(where) + symvalue);
    break;
  case R_TYPE(ADD64):
    write64le(where, read64le(where) + symvalue);
    break;

  case R_TYPE(SUB6):
    *((uint8_t *) where) = (read32le(where) & 0xc0) | (((read32le(where) & 0x3f) - symvalue) & 0x3f);
    break;
  case R_TYPE(SUB8):
    *((uint8_t *) where) = *((uint8_t *) where) - symvalue;
    break;
  case R_TYPE(SUB16):
    write16le(where, read16le(where) - symvalue);
    break;
  case R_TYPE(SUB32):
    write32le(where, read32le(where) - symvalue);
    break;
  case R_TYPE(SUB64):
    write64le(where, read64le(where) - symvalue);
    break;

  case R_TYPE(32_PCREL):
    write32le(where, pcrel_val);
  break;

  case R_TYPE(PCREL_HI20): {

#if configMPU_COMPARTMENTALIZATION
    rtems_rtl_obj_sym* rtl_sym = rtems_rtl_symbol_obj_find(obj, symname);
    if (rtl_sym) {

      if (ELF_ST_TYPE(rtl_sym->data >> 16) == STT_FUNC) {
        // Find the owner compartment of this symbol
        rtems_rtl_obj* owner_obj = rtems_rtl_find_obj_with_symbol(symname);
        if (owner_obj == NULL) {
          // It is a local static function
          owner_obj = obj;
        }

        // Get the symbol/capability from the owner's obj/captable
        rtl_sym = rtems_rtl_symbol_obj_find(owner_obj, symname);
        if (rtl_sym == NULL) {
          return rtems_rtl_elf_rel_failure;
        }

        void* tramp_cap = rtl_cherifreertos_compartments_setup_ecall((void*) symvalue, rtl_cherifreertos_compartment_get_compid(owner_obj));
        if (tramp_cap == NULL) {
          return rtems_rtl_elf_rel_failure;
        } else {
          symvalue = (size_t) tramp_cap;
          pcrel_val = (intptr_t) tramp_cap - ((intptr_t) where);
        }
    }
  }
#endif

    int64_t hi = SignExtend64(pcrel_val + 0x800, bits); //pcrel_val + 0x800;
    write32le(where, (read32le(where) & 0xFFF) | (hi & 0xFFFFF000));

    hi20_reloc_t *hi20_reloc =  rtems_rtl_alloc_new (RTEMS_RTL_ALLOC_OBJECT, sizeof(hi20_reloc_t), true);
    vListInitialiseItem(&hi20_reloc->node);

    hi20_reloc->symvalue = symvalue;
    hi20_reloc->hi20_pc = (intptr_t) where;

    rtems_rtl_hi20_insert(&hi20_relocs_table, hi20_reloc);

  }
  break;

  case R_TYPE(GOT_HI20):
  case R_TYPE(HI20): {

#if configMPU_COMPARTMENTALIZATION
    rtems_rtl_obj_sym* rtl_sym = rtems_rtl_symbol_obj_find(obj, symname);
    if (rtl_sym) {

      if (ELF_ST_TYPE(rtl_sym->data >> 16) == STT_FUNC) {
        // Find the owner compartment of this symbol
        rtems_rtl_obj* owner_obj = rtems_rtl_find_obj_with_symbol(symname);
        if (owner_obj == NULL) {
          // It is a local static function
          owner_obj = obj;
        }

        // Get the symbol/capability from the owner's obj/captable
        rtl_sym = rtems_rtl_symbol_obj_find(owner_obj, symname);
        if (rtl_sym == NULL) {
          return rtems_rtl_elf_rel_failure;
        }

        void* tramp_cap = rtl_cherifreertos_compartments_setup_ecall((void*) symvalue, rtl_cherifreertos_compartment_get_compid(owner_obj));
        if (tramp_cap == NULL) {
          return rtems_rtl_elf_rel_failure;
        } else {
          // printf("Detected a function pointer %s\n", symname);
          hi20_reloc_t *hi20_reloc =  rtems_rtl_alloc_new (RTEMS_RTL_ALLOC_OBJECT, sizeof(hi20_reloc_t), true);
          vListInitialiseItem(&hi20_reloc->node);

          hi20_reloc->symvalue = symvalue;
          hi20_reloc->hi20_tramp = tramp_cap;
          hi20_reloc->hi20_pc = (intptr_t) where;

          rtems_rtl_hi20_insert(&hi20_relocs_table, hi20_reloc);

          symvalue = (size_t) tramp_cap;
        }
    }
  }
#endif

    uint64_t hi = symvalue + 0x800;
    write32le(where, (read32le(where) & 0xFFF) | (hi & 0xFFFFF000));
  }
  break;

  case R_TYPE(LO12_I): {

#if configMPU_COMPARTMENTALIZATION
    rtems_rtl_obj_sym* rtl_sym = rtems_rtl_symbol_obj_find(obj, symname);
    if (rtl_sym) {

      if (ELF_ST_TYPE(rtl_sym->data >> 16) == STT_FUNC) {
        hi20_reloc_t *ret_reloc = rtems_rtl_hi20_find_symvalue(symvalue);
        if (ret_reloc) {
          //printf("LO12_I function pointer detected %s \n", symname);
          symvalue = ret_reloc->hi20_tramp;
        } else {
          //printf("Failed LO12_I function pointer detected %s\n", symname);
          return rtems_rtl_elf_rel_failure;
        }
      }
   }
#endif

    uint64_t hi = (symvalue + 0x800) >> 12;
    uint64_t lo = symvalue - (hi << 12);
    write32le(where, (read32le(where) & 0xFFFFF) | ((lo & 0xFFF) << 20));

  }
  break;

  case R_TYPE(LO12_S): {
#if configMPU_COMPARTMENTALIZATION
    rtems_rtl_obj_sym* rtl_sym = rtems_rtl_symbol_obj_find(obj, symname);
    if (rtl_sym) {
      if (ELF_ST_TYPE(rtl_sym->data >> 16) == STT_FUNC) {
        hi20_reloc_t *ret_reloc = rtems_rtl_hi20_find_symvalue(symvalue);
        if (ret_reloc) {
          //printf("LO12_S function pointer detected %s\n", symname);
          symvalue = ret_reloc->hi20_tramp;
        } else {
          //printf("Failed LO12_S function pointer detected %s \n", symname);
          return rtems_rtl_elf_rel_failure;
        }
      }
   }
#endif
    uint64_t hi = (symvalue + 0x800) >> 12;
    uint64_t lo = symvalue - (hi << 12);
    uint32_t imm11_5 = extractBits(lo, 11, 5) << 25;
    uint32_t imm4_0 = extractBits(lo, 4, 0) << 7;
    write32le(where, (read32le(where) & 0x1FFF07F) | imm11_5 | imm4_0);
  }
  break;

  /* Skip LO12 relocations as they will be handeled in another pass after HI20
   * ones have been added
   */
  case R_TYPE(PCREL_LO12_I):
  case R_TYPE(PCREL_LO12_S): {
    if (type) {
      /* Should have been handeled earlier, not here */
      return rtems_rtl_elf_rel_failure;
    }
  }
  break;

  case R_TYPE(CALL_PLT):
  case R_TYPE(CALL): {
#if configMPU_COMPARTMENTALIZATION
  if (rtl_cherifreertos_is_inter_compartment(obj, symname) && rtems_rtl_esymbol_obj_find(obj, symname)) {

    // Find the owner compartment of this symbol
    rtems_rtl_obj* owner_obj = rtems_rtl_find_obj_with_symbol(symname);
    if (owner_obj == NULL) {
      return rtems_rtl_elf_rel_failure;
    }

    void* tramp_cap = rtl_cherifreertos_compartments_setup_ecall((void*) symvalue, rtl_cherifreertos_compartment_get_compid(owner_obj));
    if (tramp_cap == NULL) {
      return rtems_rtl_elf_rel_failure;
    } else {
      pcrel_val = (intptr_t) tramp_cap - ((intptr_t) where);
    }
  }
#endif
    int64_t hi = SignExtend64(pcrel_val + 0x800, bits);
    write32le(where, (read32le(where) & 0xFFF) | (hi & 0xFFFFF000));
    int64_t hi20 = SignExtend64(pcrel_val + 0x800, bits);
    int64_t lo = pcrel_val - (hi20 << 12);
    write32le(((char *) where) + 4, (read32le(((char *) where) + 4) & 0xFFFFF) | ((lo & 0xFFF) << 20));
  }
  break;

#if configCHERI_COMPARTMENTALIZATION
  case R_TYPE(CHERI_CAPABILITY): {
    rtems_rtl_obj_sym *rtl_sym = rtems_rtl_symbol_obj_find(obj, symname);

    if (rtl_sym) {
       void** captable = rtl_cherifreertos_compartment_obj_get_captable(obj);

       *((__uintcap_t *) where) = (__uintcap_t) captable[rtl_sym->capability];
       return rtems_rtl_elf_rel_no_error;
    } else {
      rtems_rtl_set_error (EINVAL, "Couldn't find the symbol with CHERI_CAPABILITY reloc\n");
      return rtems_rtl_elf_rel_failure;
    }
  }
  break;

  case R_TYPE(CHERI_CAPTAB_PCREL_HI20): {
    rtems_rtl_set_error (EINVAL, "CheriFreeRTOS compartment is invalid, re-compile with gprel ABI flag\n");
    return rtems_rtl_elf_rel_failure;
  }

  case R_TYPE(CHERI_CAPTAB_FREERTOS_GPREL): {
    rtems_rtl_obj_sym *rtl_sym;
    void* tramp_cap;
    size_t cap_offset = 0;

    rtl_sym = rtems_rtl_symbol_obj_find(obj, symname);
    if (rtl_sym) {

      if (ELF_ST_TYPE(rtl_sym->data >> 16) == STT_FUNC) {
        // Find the owner compartment of this symbol
        rtems_rtl_obj* owner_obj = rtems_rtl_find_obj_with_symbol(symname);
        if (owner_obj == NULL) {
          // It is a local static function
          owner_obj = obj;
        }

        // Get the symbol/capability from the owner's obj/captable
        rtl_sym = rtems_rtl_symbol_obj_find(owner_obj, symname);
        if (rtl_sym == NULL) {
          return rtems_rtl_elf_rel_failure;
        }

        // Get the source function capability
        void** captable = rtl_cherifreertos_compartment_obj_get_captable(owner_obj);
        void* func_cap = captable[rtl_sym->capability];
        tramp_cap = rtl_cherifreertos_compartments_setup_ecall(func_cap, rtl_cherifreertos_compartment_get_compid(owner_obj));
        if (tramp_cap == NULL) {
          return rtems_rtl_elf_rel_failure;
        } else {
          // Install the new trampoline into the caller's captable
          cap_offset = rtl_cherifreertos_captable_install_new_cap(obj, tramp_cap);
        }
      } else {
        cap_offset = rtl_sym->capability;
      }

      size_t gp_rel_val = (size_t) ((Elf_Word) cap_offset) * sizeof(void *);

      int64_t hi = (gp_rel_val + 0x800) >> 12;
      int64_t lo = (gp_rel_val - (hi << 12)) & 0xFFF;

      short tmpreg = (read16le(where) & 0xf80) >> 7;
      uint32_t cincoff = read32le(where + 1);

      // lui gpr, #hi20(cap_addr)
      write32le(where, (hi << 12) | read32le(where));
      // cincoffset ctmpreg, cgp, gpr
      // lc ctmpreg, #lo12(cap_addr)(ctmpreg)
      write32le((where + 2), (lo << 20) | read32le(where + 2) & 0x000fffff);

      return rtems_rtl_elf_rel_no_error;
    } else {
      rtems_rtl_set_error (EINVAL, "Couldn't find the %s symbol for FREERTOS_GPREL reloc\n", symname);
      return rtems_rtl_elf_rel_failure;
    }
  }
  break;

  case R_TYPE(CHERI_CCALL_FREERTOS_GPREL): {
    rtems_rtl_obj_sym *rtl_sym;
    void* tramp_cap;
    size_t cap_offset = 0;

    rtl_sym = rtems_rtl_symbol_obj_find(obj, symname);
    if (rtl_sym) {

      // Check if it is an inter-compartment call and emit a trampoline to performs a comp-switch
      if (rtl_cherifreertos_is_inter_compartment(obj, symname) && rtems_rtl_esymbol_obj_find(obj, symname)) {

        // Find the owner compartment of this symbol
        rtems_rtl_obj* owner_obj = rtems_rtl_find_obj_with_symbol(symname);
        if (owner_obj == NULL) {
          return rtems_rtl_elf_rel_failure;
        }

        // Get the symbol/capability from the owner's obj/captable
        rtl_sym = rtems_rtl_symbol_obj_find(owner_obj, symname);
        if (rtl_sym == NULL) {
          return rtems_rtl_elf_rel_failure;
        }

        // Get the source function capability
        void** captable = rtl_cherifreertos_compartment_obj_get_captable(owner_obj);
        void* func_cap = captable[rtl_sym->capability];

        // Emit a trampoline to perform a compartment switch
        tramp_cap = rtl_cherifreertos_compartments_setup_ecall(func_cap, rtl_cherifreertos_compartment_get_compid(owner_obj));
        if (tramp_cap == NULL) {
          return rtems_rtl_elf_rel_failure;
        } else {
          // Install the new trampoline into the caller's captable
          cap_offset = rtl_cherifreertos_captable_install_new_cap(obj, tramp_cap);
        }
      } else {
        cap_offset = rtl_sym->capability;
      }

      size_t gp_rel_val = (size_t) ((Elf_Word) cap_offset) * sizeof(void *);

      int64_t hi = (gp_rel_val + 0x800) >> 12;
      int64_t lo = (gp_rel_val - (hi << 12)) & 0xFFF;

      short tmpreg = (read16le(where) & 0xf80) >> 7;
      uint32_t cincoff = read32le(where + 1);

      // lui gpr, #hi20(cap_addr)
      write32le(where, (hi << 12) | read32le(where));
      // cincoffset ctmpreg, cgp, gpr
      // lc ctmpreg, #lo12(cap_addr)(ctmpreg)
      write32le((where + 2), (lo << 20) | read32le(where + 2) & 0x000fffff);

      return rtems_rtl_elf_rel_no_error;
    } else {
      rtems_rtl_set_error (EINVAL, "Couldn't find the %s symbol for FREERTOS_GPREL reloc\n", symname);
      return rtems_rtl_elf_rel_failure;
    }
  }
  break;

  case R_TYPE(CHERI_CJAL):
  case R_TYPE(CHERI_RVC_CJUMP):
  case R_TYPE(CHERI_CCALL): {
    printf("Warning: Unimplemented CHERI call/jump %d reloc\n", (int) ELF_R_TYPE(rela->r_info));
    return rtems_rtl_elf_rel_no_error;
  }

  break;
#endif
  default:
    rtems_rtl_set_error (EINVAL,
                         "%s: Unsupported relocation type %u "
                         "in non-PLT relocations",
                         sect->name, (uint32_t) ELF_R_TYPE(rela->r_info));
    return rtems_rtl_elf_rel_failure;
  }

  return rtems_rtl_elf_rel_no_error;
}

rtems_rtl_elf_rel_status
rtems_rtl_elf_relocate_rela (rtems_rtl_obj*            obj,
                             const Elf_Rela*           rela,
                             const rtems_rtl_obj_sect* sect,
                             const char*               symname,
                             const Elf_Word            syminfo,
                             const Elf_Word            symvalue,
                             Elf_Word                  type) {
  return rtems_rtl_elf_reloc_rela (obj,
                                   rela,
                                   sect,
                                   symname,
                                   syminfo,
                                   symvalue,
                                   type,
                                   false);
}

rtems_rtl_elf_rel_status
rtems_rtl_elf_relocate_rela_tramp (rtems_rtl_obj*            obj,
                                   const Elf_Rela*           rela,
                                   const rtems_rtl_obj_sect* sect,
                                   const char*               symname,
                                   const Elf_Byte            syminfo,
                                   const Elf_Word            symvalue) {
  return rtems_rtl_elf_reloc_rela (obj,
                                   rela,
                                   sect,
                                   symname,
                                   syminfo,
                                   symvalue,
                                   0,
                                   true);
}

rtems_rtl_elf_rel_status
rtems_rtl_elf_relocate_rel (rtems_rtl_obj*      obj,
                            const Elf_Rel*            rel,
                            const rtems_rtl_obj_sect* sect,
                            const char*               symname,
                            const Elf_Byte            syminfo,
                            const Elf_Word            symvalue) {
  rtems_rtl_set_error (EINVAL, "rel type record not supported");
  return rtems_rtl_elf_rel_failure;
}

bool
rtems_rtl_elf_unwind_parse (const rtems_rtl_obj* obj,
                            const char*          name,
                            uint32_t             flags) {
  return rtems_rtl_elf_unwind_dw2_parse (obj, name, flags);
}

bool
rtems_rtl_elf_unwind_register (rtems_rtl_obj* obj) {
  return rtems_rtl_elf_unwind_dw2_register (obj);
}

bool
rtems_rtl_elf_unwind_deregister (rtems_rtl_obj* obj) {
  return rtems_rtl_elf_unwind_dw2_deregister (obj);
}
