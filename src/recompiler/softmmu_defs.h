#ifndef SOFTMMU_DEFS_H
#define SOFTMMU_DEFS_H

#ifndef NEMU
uint8_t REGPARM __ldb_mmu(target_ulong addr, int mmu_idx);
void REGPARM __stb_mmu(target_ulong addr, uint8_t val, int mmu_idx);
uint16_t REGPARM __ldw_mmu(target_ulong addr, int mmu_idx);
void REGPARM __stw_mmu(target_ulong addr, uint16_t val, int mmu_idx);
uint32_t REGPARM __ldl_mmu(target_ulong addr, int mmu_idx);
void REGPARM __stl_mmu(target_ulong addr, uint32_t val, int mmu_idx);
uint64_t REGPARM __ldq_mmu(target_ulong addr, int mmu_idx);
void REGPARM __stq_mmu(target_ulong addr, uint64_t val, int mmu_idx);

uint8_t REGPARM __ldb_cmmu(target_ulong addr, int mmu_idx);
void REGPARM __stb_cmmu(target_ulong addr, uint8_t val, int mmu_idx);
uint16_t REGPARM __ldw_cmmu(target_ulong addr, int mmu_idx);
void REGPARM __stw_cmmu(target_ulong addr, uint16_t val, int mmu_idx);
uint32_t REGPARM __ldl_cmmu(target_ulong addr, int mmu_idx);
void REGPARM __stl_cmmu(target_ulong addr, uint32_t val, int mmu_idx);
uint64_t REGPARM __ldq_cmmu(target_ulong addr, int mmu_idx);
void REGPARM __stq_cmmu(target_ulong addr, uint64_t val, int mmu_idx);
#else /* NEMU */
RTCCUINTREG REGPARM __ldb_mmu(target_ulong addr, int mmu_idx);
void REGPARM __stb_mmu(target_ulong addr, uint8_t val, int mmu_idx);
RTCCUINTREG REGPARM __ldw_mmu(target_ulong addr, int mmu_idx);
void REGPARM __stw_mmu(target_ulong addr, uint16_t val, int mmu_idx);
RTCCUINTREG REGPARM __ldl_mmu(target_ulong addr, int mmu_idx);
void REGPARM __stl_mmu(target_ulong addr, uint32_t val, int mmu_idx);
uint64_t REGPARM __ldq_mmu(target_ulong addr, int mmu_idx);
void REGPARM __stq_mmu(target_ulong addr, uint64_t val, int mmu_idx);

RTCCUINTREG REGPARM __ldb_cmmu(target_ulong addr, int mmu_idx);
void REGPARM __stb_cmmu(target_ulong addr, uint8_t val, int mmu_idx);
RTCCUINTREG REGPARM __ldw_cmmu(target_ulong addr, int mmu_idx);
void REGPARM __stw_cmmu(target_ulong addr, uint16_t val, int mmu_idx);
RTCCUINTREG REGPARM __ldl_cmmu(target_ulong addr, int mmu_idx);
void REGPARM __stl_cmmu(target_ulong addr, uint32_t val, int mmu_idx);
uint64_t REGPARM __ldq_cmmu(target_ulong addr, int mmu_idx);
void REGPARM __stq_cmmu(target_ulong addr, uint64_t val, int mmu_idx);

# ifdef REM_PHYS_ADDR_IN_TLB
RTCCUINTREG REGPARM __ldb_nemu_phys(RTCCUINTREG addr);
RTCCUINTREG REGPARM __ldub_nemu_phys(RTCCUINTREG addr);
void REGPARM __stb_nemu_phys(RTCCUINTREG addr, RTCCUINTREG val);
RTCCUINTREG REGPARM __ldw_nemu_phys(RTCCUINTREG addr);
RTCCUINTREG REGPARM __lduw_nemu_phys(RTCCUINTREG addr);
void REGPARM __stw_nemu_phys(RTCCUINTREG addr, RTCCUINTREG val);
RTCCUINTREG REGPARM __ldl_nemu_phys(RTCCUINTREG addr);
RTCCUINTREG REGPARM __ldul_nemu_phys(RTCCUINTREG addr);
void REGPARM __stl_nemu_phys(RTCCUINTREG addr, RTCCUINTREG val);
uint64_t REGPARM __ldq_nemu_phys(RTCCUINTREG addr);
void REGPARM __stq_nemu_phys(RTCCUINTREG addr, uint64_t val);
# endif

#endif /* NEMU */

#endif
