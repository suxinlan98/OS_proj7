#include "asm.h"

.section tlbrentry
.globl handle_tlbr
.align 0x4
handle_tlbr:
	csrwr	$t0, LOONGARCH_CSR_TLBRSAVE
	csrrd	$t0, LOONGARCH_CSR_PGD
	lddir	$t0, $t0, 3
	srli.d	$t0, $t0, 12
	slli.d	$t0, $t0, 12
	lddir	$t0, $t0, 2
	srli.d	$t0, $t0, 12
	slli.d	$t0, $t0, 12
	lddir   $t0, $t0, 1
	srli.d	$t0, $t0, 12
	slli.d	$t0, $t0, 12
	ldpte	$t0, 0
	ldpte	$t0, 1
	tlbfill
	csrrd	$t0, LOONGARCH_CSR_TLBRSAVE
	ertn
	
