// SPDX-FileCopyrightText: 2012-2020 houndthe <cgkajm@gmail.com>
// SPDX-FileCopyrightText: 2024 Billow <billow.fun@gmail.com>
// SPDX-License-Identifier: LGPL-3.0-only

#include <rz_util.h>
#include <rz_type.h>
#include <rz_analysis.h>
#include <rz_bin_dwarf.h>
#include <string.h>
#include "analysis_private.h"

typedef struct dwarf_parse_context_t {
	RzAnalysis *analysis;
	RzBinDwarfCompUnit *unit;
	RzBinDWARF *dw;
} Context;

static RZ_OWN RzType *type_parse_from_offset_internal(
	RZ_BORROW RZ_IN RZ_NONNULL Context *ctx,
	ut64 offset,
	RZ_BORROW RZ_OUT RZ_NULLABLE ut64 *size,
	RZ_BORROW RZ_IN RZ_NONNULL SetU *visited);

static RZ_OWN RzType *type_parse_from_offset(
	RZ_BORROW RZ_IN RZ_NONNULL Context *ctx,
	ut64 offset,
	RZ_BORROW RZ_OUT RZ_NULLABLE ut64 *size);

static bool enum_children_parse(
	RZ_BORROW RZ_IN RZ_NONNULL Context *ctx,
	RZ_BORROW RZ_IN RZ_NONNULL const RzBinDwarfDie *die,
	RZ_BORROW RZ_OUT RZ_NONNULL RzBaseType *base_type);

static bool struct_union_children_parse(
	RZ_BORROW RZ_IN RZ_NONNULL Context *ctx,
	RZ_BORROW RZ_IN RZ_NONNULL const RzBinDwarfDie *die,
	RZ_BORROW RZ_OUT RZ_NONNULL RzBaseType *base_type);

static bool function_from_die(
	RZ_BORROW RZ_IN RZ_NONNULL Context *ctx,
	RZ_BORROW RZ_IN RZ_NONNULL const RzBinDwarfDie *die);

static void die_parse(Context *ctx, RzBinDwarfDie *die);

/* For some languages linkage name is more informative like C++,
   but for Rust it's rubbish and the normal name is fine */
static bool prefer_linkage_name(DW_LANG lang) {
	switch (lang) {
	case DW_LANG_Rust:
	case DW_LANG_Ada83:
	case DW_LANG_Ada95:
	case DW_LANG_Ada2005:
	case DW_LANG_Ada2012:
		return false;
	default:
		return true;
	}
}

/// DWARF Register Number Mapping
static const char *map_dwarf_register_dummy(ut32 reg_num) {
	switch (reg_num) {
	case 0: return "reg0";
	case 1: return "reg1";
	case 2: return "reg2";
	case 3: return "reg3";
	case 4: return "reg4";
	case 5: return "reg5";
	case 6: return "reg6";
	case 7: return "reg7";
	case 8: return "reg8";
	case 9: return "reg9";
	case 10: return "reg10";
	case 11: return "reg11";
	case 12: return "reg12";
	case 13: return "reg13";
	case 14: return "reg14";
	case 15: return "reg15";
	case 16: return "reg16";
	case 17: return "reg17";
	case 18: return "reg18";
	case 19: return "reg19";
	case 20: return "reg20";
	case 21: return "reg21";
	case 22: return "reg22";
	case 23: return "reg23";
	case 24: return "reg24";
	case 25: return "reg25";
	case 26: return "reg26";
	case 27: return "reg27";
	case 28: return "reg28";
	case 29: return "reg29";
	case 30: return "reg30";
	case 31: return "reg31";
	case 32: return "reg32";
	case 33: return "reg33";
	case 34: return "reg34";
	case 35: return "reg35";
	case 36: return "reg36";
	case 37: return "reg37";
	case 38: return "reg38";
	case 39: return "reg39";
	case 40: return "reg40";
	case 41: return "reg41";
	case 42: return "reg42";
	case 43: return "reg43";
	case 44: return "reg44";
	case 45: return "reg45";
	case 46: return "reg46";
	case 47: return "reg47";
	case 48: return "reg48";
	case 49: return "reg49";
	case 50: return "reg50";
	case 51: return "reg51";
	case 52: return "reg52";
	case 53: return "reg53";
	case 54: return "reg54";
	case 55: return "reg55";
	case 56: return "reg56";
	case 57: return "reg57";
	case 58: return "reg58";
	case 59: return "reg59";
	case 60: return "reg60";
	case 61: return "reg61";
	case 62: return "reg62";
	case 63: return "reg63";
	default:
		rz_warn_if_reached();
		return "unsupported_reg";
	}
}

/* x86_64 https://software.intel.com/sites/default/files/article/402129/mpx-linux64-abi.pdf */
static const char *map_dwarf_reg_to_x86_64_reg(ut32 reg_num) {
	switch (reg_num) {
	case 0: return "rax";
	case 1: return "rdx";
	case 2: return "rcx";
	case 3: return "rbx";
	case 4: return "rsi";
	case 5: return "rdi";
	case 6: return "rbp";
	case 7: return "rsp";
	case 8: return "r8";
	case 9: return "r9";
	case 10: return "r10";
	case 11: return "r11";
	case 12: return "r12";
	case 13: return "r13";
	case 14: return "r14";
	case 15: return "r15";
	case 17: return "xmm0";
	case 18: return "xmm1";
	case 19: return "xmm2";
	case 20: return "xmm3";
	case 21: return "xmm4";
	case 22: return "xmm5";
	case 23: return "xmm6";
	case 24: return "xmm7";
	default:
		return "unsupported_reg";
	}
}

/* x86 https://01.org/sites/default/files/file_attach/intel386-psabi-1.0.pdf */
static const char *map_dwarf_reg_to_x86_reg(ut32 reg_num) {
	switch (reg_num) {
	case 0: /* fall-thru */
	case 8: return "eax";
	case 1: return "edx";
	case 2: return "ecx";
	case 3: return "ebx";
	case 4: return "esp";
	case 5: return "ebp";
	case 6: return "esi";
	case 7: return "edi";
	case 9: return "EFLAGS";
	case 11: return "st0";
	case 12: return "st1";
	case 13: return "st2";
	case 14: return "st3";
	case 15: return "st4";
	case 16: return "st5";
	case 17: return "st6";
	case 18: return "st7";
	case 21: return "xmm0";
	case 22: return "xmm1";
	case 23: return "xmm2";
	case 24: return "xmm3";
	case 25: return "xmm4";
	case 26: return "xmm5";
	case 27: return "xmm6";
	case 28: return "xmm7";
	case 29: return "mm0";
	case 30: return "mm1";
	case 31: return "mm2";
	case 32: return "mm3";
	case 33: return "mm4";
	case 34: return "mm5";
	case 35: return "mm6";
	case 36: return "mm7";
	case 40: return "es";
	case 41: return "cs";
	case 42: return "ss";
	case 43: return "ds";
	case 44: return "fs";
	case 45: return "gs";
	default:
		rz_warn_if_reached();
		return "unsupported_reg";
	}
}

/*
 * Most of the registers comes from the PPC ELF ABI v1
 * https://refspecs.linuxfoundation.org/ELF/ppc64/PPC-elf64abi-1.9.html#DW-REG
 *
 * But there are some different mapping in the PPC ELF ABI v2
 * https://ftp.rtems.org/pub/rtems/people/sebh/ABI64BitOpenPOWERv1.1_16July2015_pub.pdf
 */
static const char *map_dwarf_reg_to_ppc64_reg(ut32 reg_num) {
	switch (reg_num) {
	// General Register
	case 0: return "r0";
	case 1: return "r1";
	case 2: return "r2";
	case 3: return "r3";
	case 4: return "r4";
	case 5: return "r5";
	case 6: return "r6";
	case 7: return "r7";
	case 8: return "r8";
	case 9: return "r9";
	case 10: return "r10";
	case 11: return "r11";
	case 12: return "r12";
	case 13: return "r13";
	case 14: return "r14";
	case 15: return "r15";
	case 16: return "r16";
	case 17: return "r17";
	case 18: return "r18";
	case 19: return "r19";
	case 20: return "r20";
	case 21: return "r21";
	case 22: return "r22";
	case 23: return "r23";
	case 24: return "r24";
	case 25: return "r25";
	case 26: return "r26";
	case 27: return "r27";
	case 28: return "r28";
	case 29: return "r29";
	case 30: return "r30";
	case 31: return "r31";
	// Floating Register
	case 32: return "f0";
	case 33: return "f1";
	case 34: return "f2";
	case 35: return "f3";
	case 36: return "f4";
	case 37: return "f5";
	case 38: return "f6";
	case 39: return "f7";
	case 40: return "f8";
	case 41: return "f9";
	case 42: return "f10";
	case 43: return "f11";
	case 44: return "f12";
	case 45: return "f13";
	case 46: return "f14";
	case 47: return "f15";
	case 48: return "f16";
	case 49: return "f17";
	case 50: return "f18";
	case 51: return "f19";
	case 52: return "f20";
	case 53: return "f21";
	case 54: return "f22";
	case 55: return "f23";
	case 56: return "f24";
	case 57: return "f25";
	case 58: return "f26";
	case 59: return "f27";
	case 60: return "f28";
	case 61: return "f29";
	case 62: return "f30";
	case 63: return "f31";
	// Special Register
	case 64: return "cr"; // Condition Register
	case 65: return "fpscr"; // Floating-Point Status and Control Register
	case 66: return "msr"; // Machine State Register
	case 70: return "sr0"; // Segment Register 0
	case 71: return "sr1"; // Segment Register 1
	case 72: return "sr2"; // Segment Register 2
	case 73: return "sr3"; // Segment Register 3
	case 74: return "sr4"; // Segment Register 4
	case 75: return "sr5"; // Segment Register 5
	case 76: return "sr6"; // Segment Register 6
	case 77: return "sr7"; // Segment Register 7
	case 78: return "sr8"; // Segment Register 8
	case 79: return "sr9"; // Segment Register 9
	case 80: return "sr10"; // Segment Register 10
	case 81: return "sr11"; // Segment Register 11
	case 82: return "sr12"; // Segment Register 12
	case 83: return "sr13"; // Segment Register 13
	case 84: return "sr14"; // Segment Register 14
	case 85: return "sr15"; // Segment Register 15
	case 99:
		return "acc"; // Accumulator Register
	// SPRs 100–1123
	case 100: return "mq"; // MQ Register
	case 101: return "xer"; // Fixed-Point Exception Register
	case 104: return "rtcu"; // Real Time Clock Upper Register
	case 105: return "rtcl"; // Real Time Clock Lower Register
	case 108: return "lr"; // Link Register
	case 109: return "ctr"; // Count Register
	case 118: return "dsisr"; // Data Storage Interrupt Status Register
	case 119: return "dar"; // Data Address Register
	case 122: return "dec"; // Decrement Register
	case 125: return "sdr1"; // Storage Description Register 1
	case 126: return "srr0"; // Machine Status Save/Restore Register 0
	case 127: return "srr1"; // Machine Status Save/Restore Register 1
	case 356: return "vrsave"; // Vector Save/Restore Register
	case 372: return "sprg0"; // Software-use Special Purpose Register 0
	case 373: return "sprg1"; // Software-use Special Purpose Register 1
	case 374: return "sprg2"; // Software-use Special Purpose Register 2
	case 375: return "sprg3"; // Software-use Special Purpose Register 3
	case 380: return "asr"; // Address Space Register
	case 382: return "ear"; // External Access Register
	case 384: return "tb"; // Time Base
	case 385: return "tbu"; // Time Base Upper
	case 387: return "pvr"; // Processor Version Register
	case 612: return "spefscr"; // Signal processing and embedded floating-point status and control register
	case 628: return "ibat0u"; // Instruction BAT Upper Register 0
	case 629: return "ibat0l"; // Instruction BAT Lower Register 0
	case 630: return "ibat1u"; // Instruction BAT Upper Register 1
	case 631: return "ibat1l"; // Instruction BAT Lower Register 1
	case 632: return "ibat2u"; // Instruction BAT Upper Register 2
	case 633: return "ibat2l"; // Instruction BAT Lower Register 2
	case 634: return "ibat3u"; // Instruction BAT Upper Register 3
	case 635: return "ibat3l"; // Instruction BAT Lower Register 3
	case 636: return "dbat0u"; // Data BAT Upper Register 0
	case 637: return "dbat0l"; // Data BAT Lower Register 0
	case 638: return "dbat1u"; // Data BAT Upper Register 1
	case 639: return "dbat1l"; // Data BAT Lower Register 1
	case 640: return "dbat2u"; // Data BAT Upper Register 2
	case 641: return "dbat2l"; // Data BAT Lower Register 2
	case 642: return "dbat3u"; // Data BAT Upper Register 3
	case 643: return "dbat3l"; // Data BAT Lower Register 3
	case 1108: return "hid0"; // Hardware Implementation Register 0
	case 1109: return "hid1"; // Hardware Implementation Register 1
	case 1110: return "hid2"; // Hardware Implementation Register 2
	case 1113: return "hid5"; // Hardware Implementation Register 5
	case 1123:
		return "hid15"; // Hardware Implementation Register 15
	// AltiVec registers 1124–1155
	case 1124: return "vr0"; // Vector Registers 0
	case 1125: return "vr1"; // Vector Registers 1
	case 1126: return "vr2"; // Vector Registers 2
	case 1127: return "vr3"; // Vector Registers 3
	case 1128: return "vr4"; // Vector Registers 4
	case 1129: return "vr5"; // Vector Registers 5
	case 1130: return "vr6"; // Vector Registers 6
	case 1131: return "vr7"; // Vector Registers 7
	case 1132: return "vr8"; // Vector Registers 8
	case 1133: return "vr9"; // Vector Registers 9
	case 1134: return "vr10"; // Vector Registers 10
	case 1135: return "vr11"; // Vector Registers 11
	case 1136: return "vr12"; // Vector Registers 12
	case 1137: return "vr13"; // Vector Registers 13
	case 1138: return "vr14"; // Vector Registers 14
	case 1139: return "vr15"; // Vector Registers 15
	case 1140: return "vr16"; // Vector Registers 16
	case 1141: return "vr17"; // Vector Registers 17
	case 1142: return "vr18"; // Vector Registers 18
	case 1143: return "vr19"; // Vector Registers 19
	case 1144: return "vr20"; // Vector Registers 20
	case 1145: return "vr21"; // Vector Registers 21
	case 1146: return "vr22"; // Vector Registers 22
	case 1147: return "vr23"; // Vector Registers 23
	case 1148: return "vr24"; // Vector Registers 24
	case 1149: return "vr25"; // Vector Registers 25
	case 1150: return "vr26"; // Vector Registers 26
	case 1151: return "vr27"; // Vector Registers 27
	case 1152: return "vr28"; // Vector Registers 28
	case 1153: return "vr29"; // Vector Registers 29
	case 1154: return "vr30"; // Vector Registers 30
	case 1155:
		return "vr31"; // Vector Registers 31
	// From ABI v1
	// Reserved 1156–1199
	// Most-significant 32 bits of gpr r0-r31 1200-1231
	// Reserved 1232-2047
	// Device control registers 3072–4095 DCRs
	// Performance monitor registers 4096-5120 PMRs
	default:
		rz_warn_if_reached();
		return "unsupported_reg";
	}
}

/// 4.5.1 DWARF Register Numbers https://www.infineon.com/dgdl/Infineon-TC2xx_EABI-UM-v02_09-EN.pdf?fileId=5546d46269bda8df0169ca1bfc7d24ab
static const char *map_dwarf_reg_to_tricore_reg(ut32 reg_num) {
	switch (reg_num) {
	case 0: return "d0";
	case 1: return "d1";
	case 2: return "d2";
	case 3: return "d3";
	case 4: return "d4";
	case 5: return "d5";
	case 6: return "d6";
	case 7: return "d7";
	case 8: return "d8";
	case 9: return "d9";
	case 10: return "d10";
	case 11: return "d11";
	case 12: return "d12";
	case 13: return "d13";
	case 14: return "d14";
	case 15: return "d15";
	case 16: return "a0";
	case 17: return "a1";
	case 18: return "a2";
	case 19: return "a3";
	case 20: return "a4";
	case 21: return "a5";
	case 22: return "a6";
	case 23: return "a7";
	case 24: return "a8";
	case 25: return "a9";
	case 26: return "a10";
	case 27: return "a11";
	case 28: return "a12";
	case 29: return "a13";
	case 30: return "a14";
	case 31: return "a15";
	case 32: return "e0";
	case 33: return "e2";
	case 34: return "e4";
	case 35: return "e6";
	case 36: return "e8";
	case 37: return "e10";
	case 38: return "e12";
	case 39: return "e14";
	case 40: return "psw";
	case 41: return "pcxi";
	case 42: return "pc";
	case 43: return "pcx";
	case 44: return "lcx";
	case 45: return "isp";
	case 46: return "icr";
	case 47: return "pipn";
	case 48: return "biv";
	case 49: return "btv";
	default:
		rz_warn_if_reached();
		return "unsupported_reg";
	}
}

#define KASE(_num, _reg) \
	case _num: return #_reg;

/// 4.1 https://github.com/ARM-software/abi-aa/blob/2982a9f3b512a5bfdc9e3fea5d3b298f9165c36b/aadwarf32/aadwarf32.rst
static const char *map_dwarf_reg_to_arm32(ut32 reg_num) {
	switch (reg_num) {
		KASE(0, r0);
		KASE(1, r1);
		KASE(2, r2);
		KASE(3, r3);
		KASE(4, r4);
		KASE(5, r5);
		KASE(6, r6);
		KASE(7, r7);
		KASE(8, r8);
		KASE(9, r9);
		KASE(10, r10);
		KASE(11, r11);
		KASE(12, r12);
		KASE(13, r13);
		KASE(14, r14);
		KASE(15, r15);
		/*16-63 None*/
		KASE(64, s0);
		KASE(65, s1);
		KASE(66, s2);
		KASE(67, s3);
		KASE(68, s4);
		KASE(69, s5);
		KASE(70, s6);
		KASE(71, s7);
		KASE(72, s8);
		KASE(73, s9);
		KASE(74, s10);
		KASE(75, s11);
		KASE(76, s12);
		KASE(77, s13);
		KASE(78, s14);
		KASE(79, s15);
		KASE(80, s16);
		KASE(81, s17);
		KASE(82, s18);
		KASE(83, s19);
		KASE(84, s20);
		KASE(85, s21);
		KASE(86, s22);
		KASE(87, s23);
		KASE(88, s24);
		KASE(89, s25);
		KASE(90, s26);
		KASE(91, s27);
		KASE(92, s28);
		KASE(93, s29);
		KASE(94, s30);
		KASE(95, s31);
		KASE(96, f0);
		KASE(97, f1);
		KASE(98, f2);
		KASE(99, f3);
		KASE(100, f4);
		KASE(101, f5);
		KASE(102, f6);
		KASE(103, f7);
		KASE(104, wCGR0);
		KASE(105, wCGR1);
		KASE(106, wCGR2);
		KASE(107, wCGR3);
		KASE(108, wCGR4);
		KASE(109, wCGR5);
		KASE(110, wCGR6);
		KASE(111, wCGR7);
		KASE(112, wR0);
		KASE(113, wR1);
		KASE(114, wR2);
		KASE(115, wR3);
		KASE(116, wR4);
		KASE(117, wR5);
		KASE(118, wR6);
		KASE(119, wR7);
		KASE(120, wR8);
		KASE(121, wR9);
		KASE(122, wR10);
		KASE(123, wR11);
		KASE(124, wR12);
		KASE(125, wR13);
		KASE(126, wR14);
		KASE(127, wR15);
		KASE(128, SPSR);
		KASE(129, SPSR_FIQ);
		KASE(130, SPSR_IRQ);
		KASE(131, SPSR_ABT);
		KASE(132, SPSR_UND);
		KASE(133, SPSR_SVC);
		/*134-142 None*/
		KASE(143, RA_AUTH_CODE);
		KASE(144, R8_USR);
		KASE(145, R9_USR);
		KASE(146, R10_USR);
		KASE(147, R11_USR);
		KASE(148, R12_USR);
		KASE(149, R13_USR);
		KASE(150, R14_USR);
		KASE(151, R8_FIQ);
		KASE(152, R9_FIQ);
		KASE(153, R10_FIQ);
		KASE(154, R11_FIQ);
		KASE(155, R12_FIQ);
		KASE(156, R13_FIQ);
		KASE(157, R14_FIQ);
		KASE(158, R13_IRQ);
		KASE(159, R14_IRQ);
		KASE(160, R13_ABT);
		KASE(161, R14_ABT);
		KASE(162, R13_UND);
		KASE(163, R14_UND);
		KASE(164, R13_SVC);
		KASE(165, R14_SVC);
		/*166-191 None*/
		KASE(192, wC0);
		KASE(193, wC1);
		KASE(194, wC2);
		KASE(195, wC3);
		KASE(196, wC4);
		KASE(197, wC5);
		KASE(198, wC6);
		KASE(199, wC7);
		/*288-319 None*/
		KASE(320, TPIDRURO);
		KASE(321, TPIDRURW);
		KASE(322, TPIDPR);
		KASE(323, HTPIDPR);
		/*324-8191 None*/
	case 8192: return "Vendor co-processor";
	default:
		rz_warn_if_reached();
		return "unsupported_reg";
	}
}

/// 4.1 https://github.com/ARM-software/abi-aa/blob/2982a9f3b512a5bfdc9e3fea5d3b298f9165c36b/aadwarf64/aadwarf64.rst
static const char *map_dwarf_reg_to_arm64(ut32 reg_num) {
	switch (reg_num) {
		KASE(0, X0);
		KASE(1, X1);
		KASE(2, X2);
		KASE(3, X3);
		KASE(4, X4);
		KASE(5, X5);
		KASE(6, X6);
		KASE(7, X7);
		KASE(8, X8);
		KASE(9, X9);
		KASE(10, X10);
		KASE(11, X11);
		KASE(12, X12);
		KASE(13, X13);
		KASE(14, X14);
		KASE(15, X15);
		KASE(16, X16);
		KASE(17, X17);
		KASE(18, X18);
		KASE(19, X19);
		KASE(20, X20);
		KASE(21, X21);
		KASE(22, X22);
		KASE(23, X23);
		KASE(24, X24);
		KASE(25, X25);
		KASE(26, X26);
		KASE(27, X27);
		KASE(28, X28);
		KASE(29, X29);
		KASE(30, X30);
		KASE(31, SP);
		KASE(32, PC);
		KASE(33, ELR_mode);
		KASE(34, RA_SIGN_STATE);
		KASE(35, TPIDRRO_ELO);
		KASE(36, TPIDR_ELO);
		KASE(37, TPIDR_EL1);
		KASE(38, TPIDR_EL2);
		KASE(39, TPIDR_EL3);
	case 40:
	case 41:
	case 42:
	case 43:
	case 44:
		KASE(45, Reserved);
		KASE(46, VG);
		KASE(47, FFR);
		KASE(48, P0);
		KASE(49, P1);
		KASE(50, P2);
		KASE(51, P3);
		KASE(52, P4);
		KASE(53, P5);
		KASE(54, P6);
		KASE(55, P7);
		KASE(56, P8);
		KASE(57, P9);
		KASE(58, P10);
		KASE(59, P11);
		KASE(60, P12);
		KASE(61, P13);
		KASE(62, P14);
		KASE(63, P15);
		KASE(64, V0);
		KASE(65, V1);
		KASE(66, V2);
		KASE(67, V3);
		KASE(68, V4);
		KASE(69, V5);
		KASE(70, V6);
		KASE(71, V7);
		KASE(72, V8);
		KASE(73, V9);
		KASE(74, V10);
		KASE(75, V11);
		KASE(76, V12);
		KASE(77, V13);
		KASE(78, V14);
		KASE(79, V15);
		KASE(80, V16);
		KASE(81, V17);
		KASE(82, V18);
		KASE(83, V19);
		KASE(84, V20);
		KASE(85, V21);
		KASE(86, V22);
		KASE(87, V23);
		KASE(88, V24);
		KASE(89, V25);
		KASE(90, V26);
		KASE(91, V27);
		KASE(92, V28);
		KASE(93, V29);
		KASE(94, V30);
		KASE(95, V31);
		KASE(96, Z0);
		KASE(97, Z1);
		KASE(98, Z2);
		KASE(99, Z3);
		KASE(100, Z4);
		KASE(101, Z5);
		KASE(102, Z6);
		KASE(103, Z7);
		KASE(104, Z8);
		KASE(105, Z9);
		KASE(106, Z10);
		KASE(107, Z11);
		KASE(108, Z12);
		KASE(109, Z13);
		KASE(110, Z14);
		KASE(111, Z15);
		KASE(112, Z16);
		KASE(113, Z17);
		KASE(114, Z18);
		KASE(115, Z19);
		KASE(116, Z20);
		KASE(117, Z21);
		KASE(118, Z22);
		KASE(119, Z23);
		KASE(120, Z24);
		KASE(121, Z25);
		KASE(122, Z26);
		KASE(123, Z27);
		KASE(124, Z28);
		KASE(125, Z29);
		KASE(126, Z30);
		KASE(127, Z31);
	default:
		rz_warn_if_reached();
		return "unsupported_reg";
	}
}

#include "hexagon_dwarf_reg_num_table.inc"
#include "librz/analysis/arch/v850/v850_dwarf_reg_num_table.h"

/**
 * \brief Returns a function that maps a DWARF register number to a register name
 * \param arch The architecture name
 * \param bits The architecture bitness
 * \return The function that maps a DWARF register number to a register name
 */
static DWARF_RegisterMapping dwarf_register_mapping_query(RZ_NONNULL char *arch, int bits) {
	if (RZ_STR_EQ(arch, "x86")) {
		if (bits == 64) {
			return map_dwarf_reg_to_x86_64_reg;
		} else {
			return map_dwarf_reg_to_x86_reg;
		}
	}
	if (RZ_STR_EQ(arch, "ppc") && bits == 64) {
		return map_dwarf_reg_to_ppc64_reg;
	}
	if (RZ_STR_EQ(arch, "tricore")) {
		return map_dwarf_reg_to_tricore_reg;
	}
	if (RZ_STR_EQ(arch, "arm")) {
		if (bits == 64) {
			return map_dwarf_reg_to_arm64;
		} else if (bits <= 32) {
			return map_dwarf_reg_to_arm32;
		}
	}
	if (RZ_STR_EQ(arch, "hexagon")) {
		return map_dwarf_reg_to_hexagon_reg;
	}
	if (RZ_STR_EQ(arch, "v850e3v5")) {
		return v850e3v5_register_name;
	}
	if (RZ_STR_EQ(arch, "v850e2")) {
		return v850e2_register_name;
	}
	if (RZ_STR_EQ(arch, "v850e")) {
		return v850e_register_name;
	}
	if (RZ_STR_EQ(arch, "v850")) {
		return v850_register_name;
	}
	RZ_LOG_ERROR("No DWARF register mapping function defined for %s %d bits\n", arch, bits);
	return map_dwarf_register_dummy;
}

static void variable_fini(RzAnalysisDwarfVariable *var) {
	rz_bin_dwarf_location_free(var->location);
	var->location = NULL;
	RZ_FREE(var->name);
	RZ_FREE(var->link_name);
	rz_type_free(var->type);
}

static char *attr_string(const RzBinDwarfAttr *attr, Context *ctx) {
	if (!attr) {
		return NULL;
	}
	return rz_bin_dwarf_attr_string(attr, ctx->dw, ctx->unit->str_offsets_base);
}

static char *anonymous_name(const char *k, ut64 offset) {
	return rz_str_newf("anonymous_%s_0x%" PFMT64x, k, offset);
}

static char *anonymous_type_name(RzBaseTypeKind k, ut64 offset) {
	return anonymous_name(rz_type_base_type_kind_as_string(k), offset);
}

/**
 * \brief Get the DIE name or create unique one from its offset
 * \return char* DIEs name or NULL if error
 */
static char *die_name(const RzBinDwarfDie *die, Context *ctx) {
	RzBinDwarfAttr *attr = rz_bin_dwarf_die_get_attr(die, DW_AT_name);
	if (attr) {
		return attr_string(attr, ctx);
	}
	attr = rz_bin_dwarf_die_get_attr(die, DW_AT_specification);
	RzBinDwarfDie *spec = attr ? ht_up_find(ctx->dw->info->die_by_offset, rz_bin_dwarf_attr_udata(attr), NULL) : NULL;
	if (!spec) {
		return NULL;
	}
	attr = rz_bin_dwarf_die_get_attr(spec, DW_AT_name);
	if (!attr) {
		return NULL;
	}
	return attr_string(attr, ctx);
}

static RzPVector /*<RzBinDwarfDie *>*/ *die_children(const RzBinDwarfDie *die, RzBinDWARF *dw) {
	RzPVector /*<RzBinDwarfDie *>*/ *vec = rz_pvector_new(NULL);
	if (!vec) {
		return NULL;
	}
	RzBinDwarfCompUnit *unit = ht_up_find(dw->info->unit_by_offset, die->unit_offset, NULL);
	if (!unit) {
		goto err;
	}

	for (size_t i = die->index + 1; i < rz_vector_len(&unit->dies); ++i) {
		RzBinDwarfDie *child_die = rz_vector_index_ptr(&unit->dies, i);
		if (child_die->depth >= die->depth + 1) {
			rz_pvector_push(vec, child_die);
		} else if (child_die->depth == die->depth) {
			break;
		}
	}

	return vec;
err:
	rz_pvector_free(vec);
	return NULL;
}

/**
 * \brief Get the DIE size in bits
 * \return ut64 size in bits or 0 if not found
 */
static ut64 die_bits_size(const RzBinDwarfDie *die) {
	RzBinDwarfAttr *attr = rz_bin_dwarf_die_get_attr(die, DW_AT_byte_size);
	if (attr) {
		return rz_bin_dwarf_attr_udata(attr) * CHAR_BIT;
	}

	attr = rz_bin_dwarf_die_get_attr(die, DW_AT_bit_size);
	if (attr) {
		return rz_bin_dwarf_attr_udata(attr);
	}

	return 0;
}

static bool RzBaseType_eq(const RzBaseType *a, const RzBaseType *b) {
	if (a == NULL || b == NULL) {
		return a == NULL && b == NULL;
	}
	return a->kind == b->kind && a->attrs == b->attrs && RZ_STR_EQ(a->name, b->name);
}

#define RzBaseType_NEW_CHECKED(x, k) \
	(x) = rz_type_base_type_new((k)); \
	if (!(x)) { \
		goto err; \
	}

static RzBaseType *RzBaseType_from_die(Context *ctx, const RzBinDwarfDie *die) {
	RzBaseType *btype = ht_up_find(ctx->analysis->debug_info->base_type_by_offset, die->offset, NULL);
	if (btype) {
		return btype;
	}

	switch (die->tag) {
	case DW_TAG_union_type:
		RzBaseType_NEW_CHECKED(btype, RZ_BASE_TYPE_KIND_UNION);
		if (!struct_union_children_parse(ctx, die, btype)) {
			goto err;
		}
		break;
	case DW_TAG_class_type:
	case DW_TAG_structure_type:
		RzBaseType_NEW_CHECKED(btype, RZ_BASE_TYPE_KIND_STRUCT);
		if (!struct_union_children_parse(ctx, die, btype)) {
			goto err;
		}
		break;
	case DW_TAG_unspecified_type:
	case DW_TAG_base_type:
		RzBaseType_NEW_CHECKED(btype, RZ_BASE_TYPE_KIND_ATOMIC);
		break;
	case DW_TAG_enumeration_type:
		RzBaseType_NEW_CHECKED(btype, RZ_BASE_TYPE_KIND_ENUM);
		if (!enum_children_parse(ctx, die, btype)) {
			goto err;
		}
		break;
	case DW_TAG_typedef:
		RzBaseType_NEW_CHECKED(btype, RZ_BASE_TYPE_KIND_TYPEDEF);
		break;
	default:
		return NULL;
	}

	RzBinDwarfAttr *attr = NULL;
	rz_vector_foreach(&die->attrs, attr) {
		switch (attr->at) {
		case DW_AT_specification: {
			RzBinDwarfDie *decl = ht_up_find(ctx->dw->info->die_by_offset, rz_bin_dwarf_attr_udata(attr), NULL);
			if (!decl) {
				goto err;
			}
			btype->name = die_name(decl, ctx);
			break;
		}
		case DW_AT_name:
			btype->name = attr_string(attr, ctx);
			break;
		case DW_AT_byte_size:
			btype->size = rz_bin_dwarf_attr_udata(attr) * CHAR_BIT;
			break;
		case DW_AT_bit_size:
			btype->size = rz_bin_dwarf_attr_udata(attr);
			break;
		case DW_AT_type:
			btype->type = type_parse_from_offset(ctx, rz_bin_dwarf_attr_udata(attr), &btype->size);
			if (!btype->type) {
				goto err;
			}
			break;
		default: break;
		}
	}

	if (!btype->name) {
		btype->name = anonymous_type_name(btype->kind, die->offset);
	}

	if (!btype->type &&
		(btype->kind == RZ_BASE_TYPE_KIND_TYPEDEF ||
			btype->kind == RZ_BASE_TYPE_KIND_ATOMIC ||
			btype->kind == RZ_BASE_TYPE_KIND_ENUM)) {
		btype->type = rz_type_identifier_of_base_type_str(ctx->analysis->typedb, "void");
	}

	if (!ht_up_insert(ctx->analysis->debug_info->base_type_by_offset, die->offset, btype)) {
		RZ_LOG_WARN("Failed to save base type %s [0x%" PFMT64x "]\n",
			btype->name, die->offset);
	}

	RzPVector *btypes = ht_pp_find(ctx->analysis->debug_info->base_types_by_name, btype->name, NULL);
	if (!btypes) {
		btypes = rz_pvector_new(NULL);
		ht_pp_insert(ctx->analysis->debug_info->base_types_by_name, btype->name, btypes);
		rz_pvector_push(btypes, btype);
	} else {
		void **it;
		rz_pvector_foreach (btypes, it) {
			RzBaseType *b = *it;
			if (RzBaseType_eq(btype, b)) {
				goto ok;
			}
		}
		rz_pvector_push(btypes, btype);
	}
ok:
	return btype;
err:
	rz_type_base_type_free(btype);
	return NULL;
}

/**
 * \brief Parse and return the count of an array or 0 if not found/not defined
 */
static ut64 array_count_parse(Context *ctx, RzBinDwarfDie *die) {
	if (!die->has_children) {
		return 0;
	}
	RzPVector *children = die_children(die, ctx->dw);
	if (!children) {
		return 0;
	}

	void **it;
	rz_pvector_foreach (children, it) {
		RzBinDwarfDie *child_die = *it;
		if (child_die->tag != DW_TAG_subrange_type) {
			continue;
		}
		RzBinDwarfAttr *attr;
		rz_vector_foreach(&child_die->attrs, attr) {
			switch (attr->at) {
			case DW_AT_upper_bound:
			case DW_AT_count:
				rz_pvector_free(children);
				return rz_bin_dwarf_attr_udata(attr) + 1;
			default:
				break;
			}
		}
	}
	rz_pvector_free(children);
	return 0;
}

/**
 * \brief Parse type from a DWARF DIE and write the size to \p size if not NULL
 * \param ctx the context
 * \param die the DIE to parse
 * \param allow_void whether to return a void type instead of NULL if there is no type defined
 * \param size pointer to write the size to or NULL
 * \return return RzType* or NULL if \p type_idx == -1
 */
static RzType *type_parse_from_die_internal(
	Context *ctx,
	RzBinDwarfDie *die,
	bool allow_void,
	RZ_NULLABLE ut64 *size,
	RZ_NONNULL SetU *visited) {
	RzBinDwarfAttr *attr = rz_bin_dwarf_die_get_attr(die, DW_AT_type);
	if (!attr) {
		if (!allow_void) {
			return NULL;
		}
		return rz_type_identifier_of_base_type_str(ctx->analysis->typedb, "void");
	}
	return type_parse_from_offset_internal(ctx, rz_bin_dwarf_attr_udata(attr), size, visited);
}

static void RzType_from_base_type(RzType *t, RzBaseType *b) {
	rz_return_if_fail(t && b);
	t->kind = RZ_TYPE_KIND_IDENTIFIER;
	free(t->identifier.name);
	t->identifier.name = rz_str_dup(b->name);
	switch (b->kind) {
	case RZ_BASE_TYPE_KIND_STRUCT:
		t->identifier.kind = RZ_TYPE_IDENTIFIER_KIND_STRUCT;
		break;
	case RZ_BASE_TYPE_KIND_UNION:
		t->identifier.kind = RZ_TYPE_IDENTIFIER_KIND_UNION;
		break;
	case RZ_BASE_TYPE_KIND_ENUM:
		t->identifier.kind = RZ_TYPE_IDENTIFIER_KIND_ENUM;
		break;
	case RZ_BASE_TYPE_KIND_TYPEDEF:
	case RZ_BASE_TYPE_KIND_ATOMIC:
		t->identifier.kind = RZ_TYPE_IDENTIFIER_KIND_UNSPECIFIED;
		break;
	}
}

/**
 * \brief Recursively parses type entry of a certain offset and saves type size into *size
 *
 * \param ctx the context
 * \param offset offset of the type entry
 * \param size ptr to size of a type to fill up (can be NULL if unwanted)
 * \return the parsed RzType or NULL on failure
 */
static RZ_OWN RzType *type_parse_from_offset_internal(
	RZ_BORROW RZ_IN RZ_NONNULL Context *ctx,
	ut64 offset,
	RZ_BORROW RZ_OUT RZ_NULLABLE ut64 *size,
	RZ_BORROW RZ_IN RZ_NONNULL SetU *visited) {
	RzType *type = ht_up_find(ctx->analysis->debug_info->type_by_offset, offset, NULL);
	if (type) {
		return rz_type_clone(type);
	}

	if (set_u_contains(visited, offset)) {
		return NULL;
	}
	set_u_add(visited, offset);

	RzBinDwarfDie *die = ht_up_find(ctx->dw->info->die_by_offset, offset, NULL);
	if (!die) {
		return NULL;
	}

	// get size of first type DIE that has size
	if (size && *size == 0) {
		*size = die_bits_size(die);
	}
	switch (die->tag) {
	// this should be recursive search for the type until you find base/user defined type
	case DW_TAG_pointer_type:
	case DW_TAG_reference_type: // C++ references are just pointers to us
	case DW_TAG_rvalue_reference_type:
	case DW_TAG_ptr_to_member_type: {
		RzType *pointee = type_parse_from_die_internal(ctx, die, true, size, visited);
		if (!pointee) {
			goto end;
		}
		type = rz_type_pointer_of_type(ctx->analysis->typedb, pointee, false);
		if (!type) {
			rz_type_free(pointee);
			goto end;
		}
		break;
	}
	// We won't parse them as a complete type, because that will already be done
	// so just a name now
	case DW_TAG_typedef:
	case DW_TAG_base_type:
	case DW_TAG_structure_type:
	case DW_TAG_enumeration_type:
	case DW_TAG_union_type:
	case DW_TAG_class_type:
	case DW_TAG_unspecified_type: {
		type = RZ_NEW0(RzType);
		if (!type) {
			goto end;
		}
		RzBaseType *ref = ht_up_find(ctx->analysis->debug_info->base_type_by_offset, offset, NULL);
		if (ref) {
			RzType_from_base_type(type, ref);
			break;
		}
		RzBaseTypeKind k = -1;
		switch (die->tag) {
		case DW_TAG_base_type:
			k = RZ_BASE_TYPE_KIND_ATOMIC;
			break;
		case DW_TAG_structure_type:
		case DW_TAG_class_type:
			type->identifier.kind = RZ_TYPE_IDENTIFIER_KIND_STRUCT;
			k = RZ_BASE_TYPE_KIND_STRUCT;
			break;
		case DW_TAG_union_type:
			type->identifier.kind = RZ_TYPE_IDENTIFIER_KIND_UNION;
			k = RZ_BASE_TYPE_KIND_UNION;
			break;
		case DW_TAG_enumeration_type:
			type->identifier.kind = RZ_TYPE_IDENTIFIER_KIND_ENUM;
			k = RZ_BASE_TYPE_KIND_ENUM;
			break;
		case DW_TAG_unspecified_type:
		default:
			type->identifier.kind = RZ_TYPE_IDENTIFIER_KIND_UNSPECIFIED;
			break;
		}
		type->kind = RZ_TYPE_KIND_IDENTIFIER;
		char *name = die_name(die, ctx);
		type->identifier.name = name ? name
					     : (k != -1 ? anonymous_type_name(k, die->offset)
							: anonymous_name("unspecified", die->offset));
		break;
	}
	case DW_TAG_inlined_subroutine:
	case DW_TAG_subroutine_type: {
		RzCallable *callable = ht_up_find(ctx->analysis->debug_info->callable_by_offset, die->offset, NULL);
		if (!callable) {
			if (!function_from_die(ctx, die)) {
				goto end;
			}
			callable = ht_up_find(ctx->analysis->debug_info->callable_by_offset, die->offset, NULL);
			if (!callable) {
				goto end;
			}
		}
		type = rz_type_callable(callable);
		break;
	}
	case DW_TAG_array_type: {
		RzType *subtype = type_parse_from_die_internal(ctx, die, false, size, visited);
		if (!subtype) {
			goto end;
		}
		ut64 count = array_count_parse(ctx, die);
		type = rz_type_array_of_type(ctx->analysis->typedb, subtype, count);
		if (!type) {
			rz_type_free(subtype);
		}
		break;
	}
	case DW_TAG_const_type: {
		type = type_parse_from_die_internal(ctx, die, true, size, visited);
		if (type) {
			switch (type->kind) {
			case RZ_TYPE_KIND_IDENTIFIER:
				type->identifier.is_const = true;
				break;
			case RZ_TYPE_KIND_POINTER:
				type->pointer.is_const = true;
				break;
			default:
				// const not supported yet for other kinds
				break;
			}
		}
		break;
	}
	case DW_TAG_volatile_type:
	case DW_TAG_restrict_type:
		// TODO: volatile and restrict attributes not supported in RzType
		type = type_parse_from_die_internal(ctx, die, true, size, visited);
		break;
	default:
		break;
	}

	RzType *copy = type ? rz_type_clone(type) : NULL;
	if (copy && ht_up_insert(ctx->analysis->debug_info->type_by_offset, offset, copy)) {
#if RZ_BUILD_DEBUG
		char *tstring = rz_type_as_string(ctx->analysis->typedb, type);
		RZ_LOG_DEBUG("Insert RzType [%s] into type_by_offset\n", tstring);
		free(tstring);
#endif
	} else {
		RZ_LOG_ERROR("Failed to insert RzType [0x%" PFMT64x "] into type_by_offset\n", offset);
		rz_type_free(copy);
	}

end:
	set_u_delete(visited, offset);
	return type;
}

static RZ_OWN RzType *type_parse_from_offset(
	RZ_BORROW RZ_IN RZ_NONNULL Context *ctx,
	ut64 offset,
	RZ_BORROW RZ_OUT RZ_NULLABLE ut64 *size) {
	SetU *visited = set_u_new();
	if (!visited) {
		return NULL;
	}
	RzType *type = type_parse_from_offset_internal(ctx, offset, size, visited);
	set_u_free(visited);
	if (!type) {
		RZ_LOG_VERBOSE("DWARF Type failed at 0x%" PFMT64x "\n", offset);
	}
	return type;
}

static inline const char *select_name(const char *demangle_name, const char *link_name, const char *name, DW_LANG lang) {
	return prefer_linkage_name(lang) ? (demangle_name ? demangle_name : (link_name ? link_name : name)) : name;
}

static RzType *type_parse_from_abstract_origin(Context *ctx, ut64 offset, char **name_out) {
	RzBinDwarfDie *die = ht_up_find(ctx->dw->info->die_by_offset, offset, NULL);
	if (!die) {
		return NULL;
	}
	ut64 size = 0;
	char *name = NULL;
	char *linkname = NULL;
	RzType *type = NULL;
	const RzBinDwarfAttr *attr;
	rz_vector_foreach(&die->attrs, attr) {
		switch (attr->at) {
		case DW_AT_name:
			name = attr_string(attr, ctx);
			break;
		case DW_AT_linkage_name:
		case DW_AT_MIPS_linkage_name:
			linkname = attr_string(attr, ctx);
			break;
		case DW_AT_type:
			type = type_parse_from_offset(ctx, rz_bin_dwarf_attr_udata(attr), &size);
		default:
			break;
		}
	}
	if (!type) {
		goto beach;
	}
	const char *prefer_name = select_name(NULL, linkname, name, ctx->unit->language);
	if (prefer_name && name_out) {
		*name_out = rz_str_dup(prefer_name);
	}
beach:
	free(name);
	free(linkname);
	return type;
}

/**
 * \brief Parses structured entry into *result RzTypeStructMember
 * https://www.dwarfstd.org/doc/DWARF4.pdf#page=102
 */
static RzTypeStructMember *struct_member_parse(
	Context *ctx,
	RzBinDwarfDie *die,
	RzTypeStructMember *result) {
	rz_return_val_if_fail(result, NULL);
	char *name = NULL;
	RzType *type = NULL;
	ut64 offset = 0;
	ut64 size = 0;
	RzBinDwarfAttr *attr = NULL;
	rz_vector_foreach(&die->attrs, attr) {
		switch (attr->at) {
		case DW_AT_name:
			name = attr_string(attr, ctx);
			break;
		case DW_AT_type:
			type = type_parse_from_offset(ctx, rz_bin_dwarf_attr_udata(attr), &size);
			break;
		case DW_AT_data_member_location:
			/*
				2 cases, 1.: If val is integer, it offset in bytes from
				the beginning of containing entity. If containing entity has
				a bit offset, member has that bit offset aswell
				2.: value is a location description
				https://www.dwarfstd.org/doc/DWARF4.pdf#page=39
			*/
			offset = rz_bin_dwarf_attr_udata(attr);
			break;
		// If the size of a data member is not the same as the
		//  size of the type given for the data member
		case DW_AT_byte_size:
			size = rz_bin_dwarf_attr_udata(attr) * CHAR_BIT;
			break;
		case DW_AT_bit_size:
			size = rz_bin_dwarf_attr_udata(attr);
			break;
		case DW_AT_accessibility: // private, public etc.
		case DW_AT_mutable: // flag is it is mutable
		case DW_AT_data_bit_offset:
			/*
				int that specifies the number of bits from beginning
				of containing entity to the beginning of the data member
			*/
		case DW_AT_containing_type:
		default:
			break;
		}
	}

	if (!name) {
		name = anonymous_name("member", die->offset);
	}
	if (!type) {
		RZ_LOG_WARN("DWARF [0x%" PFMT64x "] struct member missing type\n",
			die->offset);
		goto cleanup;
	}
	result->name = name;
	result->type = type;
	result->offset = offset;
	result->size = size;
	return result;

cleanup:
	free(name);
	rz_type_free(type);
	return NULL;
}

/**
 * \brief  Parses a structured entry (structs, classes, unions) into
 *         RzBaseType and saves it using rz_analysis_save_base_type ()
 */
// https://www.dwarfstd.org/doc/DWARF4.pdf#page=102
static bool struct_union_children_parse(
	RZ_BORROW RZ_IN RZ_NONNULL Context *ctx,
	RZ_BORROW RZ_IN RZ_NONNULL const RzBinDwarfDie *die,
	RZ_BORROW RZ_OUT RZ_NONNULL RzBaseType *base_type) {
	if (!die->has_children) {
		return true;
	}
	RzPVector *children = die_children(die, ctx->dw);
	if (!children) {
		return false;
	}

	void **it;
	rz_pvector_foreach (children, it) {
		RzBinDwarfDie *child_die = *it;
		// we take only direct descendats of the structure
		if (!(child_die->depth == die->depth + 1 &&
			    child_die->tag == DW_TAG_member)) {
			die_parse(ctx, child_die);
			continue;
		}
		RzTypeStructMember member = { 0 };
		RzTypeStructMember *result = struct_member_parse(ctx, child_die, &member);
		if (!result) {
			goto err;
		}
		void *element = rz_vector_push(&base_type->struct_data.members, &member);
		if (!element) {
			rz_type_free(result->type);
			goto err;
		}
	}
	rz_pvector_free(children);
	return true;
err:
	rz_pvector_free(children);
	return false;
}

/**
 * \brief  Parses enum entry into *result RzTypeEnumCase
 * https://www.dwarfstd.org/doc/DWARF4.pdf#page=110
 */
static RzTypeEnumCase *enumerator_parse(Context *ctx, RzBinDwarfDie *die, RzTypeEnumCase *result) {
	RzBinDwarfAttr *val_attr = rz_bin_dwarf_die_get_attr(die, DW_AT_const_value);
	if (!val_attr) {
		return NULL;
	}
	st64 val = rz_bin_dwarf_attr_sdata(val_attr);
	// ?? can be block, sdata, data, string w/e
	// TODO solve the encoding, I don't know in which union member is it store

	result->name = die_name(die, ctx);
	if (!result->name) {
		result->name = anonymous_name("enumerator", die->offset);
	}
	result->val = val;
	return result;
}

static bool enum_children_parse(
	RZ_BORROW RZ_IN RZ_NONNULL Context *ctx,
	RZ_BORROW RZ_IN RZ_NONNULL const RzBinDwarfDie *die,
	RZ_BORROW RZ_OUT RZ_NONNULL RzBaseType *base_type) {
	if (!die->has_children) {
		return true;
	}
	RzPVector *children = die_children(die, ctx->dw);
	if (!children) {
		return false;
	}

	void **it;
	rz_pvector_foreach (children, it) {
		RzBinDwarfDie *child_die = *it;
		if (!(child_die->depth == die->depth + 1 &&
			    child_die->tag == DW_TAG_enumerator)) {
			die_parse(ctx, child_die);
			continue;
		}
		RzTypeEnumCase cas = { 0 };
		RzTypeEnumCase *result = enumerator_parse(ctx, child_die, &cas);
		if (!result) {
			goto err;
		}
		void *element = rz_vector_push(&base_type->enum_data.cases, &cas);
		if (!element) {
			rz_type_base_enum_case_free(result, NULL);
			goto err;
		}
	}
	rz_pvector_free(children);
	return true;
err:
	rz_pvector_free(children);
	return false;
}

static void function_apply_specification(Context *ctx, const RzBinDwarfDie *die, RzAnalysisDwarfFunction *fn) {
	RzBinDwarfAttr *attr = NULL;
	rz_vector_foreach(&die->attrs, attr) {
		switch (attr->at) {
		case DW_AT_name:
			if (fn->name) {
				break;
			}
			fn->name = attr_string(attr, ctx);
			break;
		case DW_AT_linkage_name:
		case DW_AT_MIPS_linkage_name:
			if (fn->link_name) {
				break;
			}
			fn->link_name = attr_string(attr, ctx);
			break;
		case DW_AT_type: {
			if (fn->ret_type) {
				break;
			}
			ut64 size = 0;
			fn->ret_type = type_parse_from_offset(ctx, rz_bin_dwarf_attr_udata(attr), &size);
			break;
		}
		default:
			break;
		}
	}
}

static void RzBinDwarfBlock_log(Context *ctx, const RzBinDwarfBlock *block, ut64 offset, const RzBinDwarfRange *range) {
	RzBinDWARFDumpOption dump_opt = {
		.loclist_indent = "",
		.loclist_sep = ",\t",
	};
	char *expr_str = rz_bin_dwarf_expression_to_string(&ctx->unit->hdr.encoding, block, &dump_opt);
	if (RZ_STR_ISNOTEMPTY(expr_str)) {
		if (!range) {
			RZ_LOG_VERBOSE("Location parse failed: 0x%" PFMT64x " [%s]\n", offset, expr_str);
		} else {
			RZ_LOG_VERBOSE("Location parse failed: 0x%" PFMT64x " (0x%" PFMT64x ", 0x%" PFMT64x ") [%s]\n",
				offset, range->begin, range->end, expr_str);
		}
	}
	free(expr_str);
}

static RzBinDwarfLocation *RzBinDwarfLocation_with_kind(RzBinDwarfLocationKind k) {
	RzBinDwarfLocation *location = RZ_NEW0(RzBinDwarfLocation);
	if (!location) {
		return NULL;
	}
	location->kind = k;
	return location;
}

static RzBinDwarfLocation *location_list_parse(
	Context *ctx, RzBinDwarfLocList *loclist, const RzBinDwarfDie *fn) {
	RzBinDwarfLocation *location = RzBinDwarfLocation_with_kind(RzBinDwarfLocationKind_LOCLIST);
	if (!location) {
		return NULL;
	}
	if (loclist->has_location) {
		location->loclist = loclist;
		return location;
	}

	void **it;
	rz_pvector_foreach (&loclist->entries, it) {
		RzBinDwarfLocListEntry *entry = *it;
		if (entry->location) {
			continue;
		}
		if (rz_bin_dwarf_block_empty(entry->expression)) {
			entry->location = RzBinDwarfLocation_with_kind(RzBinDwarfLocationKind_EMPTY);
			continue;
		}
		if (!rz_bin_dwarf_block_valid(entry->expression)) {
			entry->location = RzBinDwarfLocation_with_kind(RzBinDwarfLocationKind_DECODE_ERROR);
			continue;
		}
		entry->location = rz_bin_dwarf_location_from_block(entry->expression, ctx->dw, ctx->unit, fn);
		if (!entry->location) {
			RzBinDwarfBlock_log(ctx, entry->expression, loclist->offset, entry->range);
			entry->location = RzBinDwarfLocation_with_kind(RzBinDwarfLocationKind_DECODE_ERROR);
			continue;
		}
	}
	loclist->has_location = true;
	location->loclist = loclist;
	return location;
}

static RzBinDwarfLocation *location_parse(
	Context *ctx, const RzBinDwarfDie *die, const RzBinDwarfAttr *attr, const RzBinDwarfDie *fn) {
	/* Loclist offset is usually CONSTANT or REFERENCE at older DWARF versions, new one has LocListPtr for that */
	if (attr->value.kind == RzBinDwarfAttr_Block) {
		return rz_bin_dwarf_location_from_block(rz_bin_dwarf_attr_block(attr), ctx->dw, ctx->unit, fn);
	}

	if (attr->value.kind == RzBinDwarfAttr_LoclistPtr ||
		attr->value.kind == RzBinDwarfAttr_Reference ||
		attr->value.kind == RzBinDwarfAttr_UConstant ||
		attr->value.kind == RzBinDwarfAttr_SecOffset) {
		if (!ctx->dw->loclists) {
			RZ_LOG_VERBOSE("loclists is NULL\n");
			return NULL;
		}
		ut64 offset = rz_bin_dwarf_attr_udata(attr);
		RzBinDwarfLocList *loclist = rz_bin_dwarf_loclists_get(ctx->dw->loclists, ctx->dw->addr, ctx->unit, offset);
		if (!loclist) { /* for some reason offset isn't there, wrong parsing or malformed dwarf */
			goto err_find;
		}
		if (rz_pvector_len(&loclist->entries) > 1) {
			return location_list_parse(ctx, loclist, fn);
		}
		if (rz_pvector_len(&loclist->entries) == 1) {
			RzBinDwarfLocListEntry *entry = rz_pvector_at(&loclist->entries, 0);
			return rz_bin_dwarf_location_from_block(entry->expression, ctx->dw, ctx->unit, fn);
		}
		RzBinDwarfLocation *loc = RZ_NEW0(RzBinDwarfLocation);
		if (!loc) {
			return NULL;
		}
		loc->kind = RzBinDwarfLocationKind_EMPTY;
		loc->encoding = ctx->unit->hdr.encoding;
		return loc;
	err_find:
		RZ_LOG_ERROR("Location parse failed 0x%" PFMT64x " <Cannot find loclist>\n", offset);
		return NULL;
	}
	RZ_LOG_ERROR("Location parse failed 0x%" PFMT64x " <Unsupported form: %s>\n", die->offset, rz_bin_dwarf_form(attr->form))
	return NULL;
}

static bool function_var_parse(
	Context *ctx,
	RzAnalysisDwarfFunction *f,
	const RzBinDwarfDie *fn_die,
	RzAnalysisDwarfVariable *v,
	const RzBinDwarfDie *var_die,
	bool *has_unspecified_parameters) {
	v->offset = var_die->offset;
	switch (var_die->tag) {
	case DW_TAG_formal_parameter:
		v->kind = RZ_ANALYSIS_VAR_KIND_FORMAL_PARAMETER;
		break;
	case DW_TAG_variable:
		v->kind = RZ_ANALYSIS_VAR_KIND_VARIABLE;
		break;
	case DW_TAG_unspecified_parameters:
		if (f) {
			f->has_unspecified_parameters = true;
		}
		if (has_unspecified_parameters) {
			*has_unspecified_parameters = true;
		}
		return true;
	default:
		return false;
	}

	bool has_location = false;
	const RzBinDwarfAttr *attr;
	rz_vector_foreach(&var_die->attrs, attr) {
		switch (attr->at) {
		case DW_AT_name:
			v->name = attr_string(attr, ctx);
			break;
		case DW_AT_linkage_name:
		case DW_AT_MIPS_linkage_name:
			v->link_name = attr_string(attr, ctx);
			break;
		case DW_AT_type: {
			RzType *type = type_parse_from_offset(ctx, rz_bin_dwarf_attr_udata(attr), NULL);
			if (type) {
				rz_type_free(v->type);
				v->type = type;
			}
		} break;
		// abstract origin is supposed to have omitted information
		case DW_AT_abstract_origin: {
			RzType *type = type_parse_from_abstract_origin(ctx, rz_bin_dwarf_attr_udata(attr), &v->name);
			if (type) {
				rz_type_free(v->type);
				v->type = type;
			}
		} break;
		case DW_AT_location:
			v->location = location_parse(ctx, var_die, attr, fn_die);
			has_location = true;
			break;
		default:
			break;
		}
	}

	if (!has_location) {
		v->location = RzBinDwarfLocation_with_kind(RzBinDwarfLocationKind_EMPTY);
	} else if (!v->location) {
		v->location = RzBinDwarfLocation_with_kind(RzBinDwarfLocationKind_DECODE_ERROR);
	}

	v->prefer_name = select_name(NULL, v->link_name, v->name, ctx->unit->language);
	if (!v->prefer_name) {
		v->prefer_name = v->name = anonymous_name("var", var_die->offset);
	}
	return true;
}

static bool function_children_parse(
	Context *ctx, const RzBinDwarfDie *die, RzCallable *callable, RzAnalysisDwarfFunction *fn) {
	if (!die->has_children) {
		return false;
	}
	RzPVector *children = die_children(die, ctx->dw);
	if (!children) {
		return false;
	}
	void **it;
	rz_pvector_foreach (children, it) {
		RzBinDwarfDie *child_die = *it;
		if (child_die->depth != die->depth + 1) {
			die_parse(ctx, child_die);
			continue;
		}
		RzAnalysisDwarfVariable v = { 0 };
		bool has_unspecified_parameters = false;
		if (!function_var_parse(ctx, fn, die, &v, child_die, &has_unspecified_parameters)) {
			goto loop_end;
		}
		if (has_unspecified_parameters) {
			callable->has_unspecified_parameters = true;
			goto loop_end;
		}
		if (!v.type) {
			RZ_LOG_ERROR("DWARF function %s variable %s failed\n",
				fn->prefer_name, v.prefer_name);
			goto loop_end;
		}
		if (v.kind == RZ_ANALYSIS_VAR_KIND_FORMAL_PARAMETER) {
			RzCallableArg *arg = rz_type_callable_arg_new(
				ctx->analysis->typedb, v.prefer_name, rz_type_clone(v.type));
			rz_type_callable_arg_add(callable, arg);
		}
		rz_vector_push(&fn->variables, &v);
		ht_up_insert(ctx->analysis->debug_info->variable_by_offset, v.offset, &v);
		continue;
	loop_end:
		variable_fini(&v);
	}
	rz_pvector_free(children);
	return true;
}

static void function_free(RzAnalysisDwarfFunction *f) {
	if (!f) {
		return;
	}
	free(f->name);
	free(f->demangle_name);
	free(f->link_name);
	rz_vector_fini(&f->variables);
	rz_type_free(f->ret_type);
	free(f);
}

/**
 * \brief Parse function,it's arguments, variables and
 *        save the information into the Sdb
 */
static bool function_from_die(
	RZ_BORROW RZ_IN RZ_NONNULL Context *ctx,
	RZ_BORROW RZ_IN RZ_NONNULL const RzBinDwarfDie *die) {
	if (ht_up_find(ctx->analysis->debug_info->function_by_offset, die->offset, NULL)) {
		return true;
	}

	if (rz_bin_dwarf_die_get_attr(die, DW_AT_declaration)) {
		return true; /* just declaration skip */
	}
	RzAnalysisDwarfFunction *fcn = RZ_NEW0(RzAnalysisDwarfFunction);
	if (!fcn) {
		goto cleanup;
	}
	fcn->offset = die->offset;
	RZ_LOG_DEBUG("DWARF function parsing [0x%" PFMT64x "]\n", die->offset);
	RzBinDwarfAttr *attr;
	rz_vector_foreach(&die->attrs, attr) {
		switch (attr->at) {
		case DW_AT_name:
			fcn->name = attr_string(attr, ctx);
			break;
		case DW_AT_linkage_name:
		case DW_AT_MIPS_linkage_name:
			fcn->link_name = attr_string(attr, ctx);
			break;
		case DW_AT_low_pc:
			fcn->low_pc = rz_bin_dwarf_attr_addr(
				attr, ctx->dw, ctx->unit->hdr.encoding.address_size, ctx->unit->addr_base);
			break;
		case DW_AT_high_pc:
			fcn->high_pc = rz_bin_dwarf_attr_addr(
				attr, ctx->dw, ctx->unit->hdr.encoding.address_size, ctx->unit->addr_base);
			break;
		case DW_AT_entry_pc:
			fcn->entry_pc = rz_bin_dwarf_attr_addr(
				attr, ctx->dw, ctx->unit->hdr.encoding.address_size, ctx->unit->addr_base);
			break;
		case DW_AT_specification: /* u64 to declaration DIE with more info */
		{
			RzBinDwarfDie *spec = ht_up_find(ctx->dw->info->die_by_offset, rz_bin_dwarf_attr_udata(attr), NULL);
			if (!spec) {
				RZ_LOG_ERROR("DWARF cannot find specification DIE at 0x%" PFMT64x " f.offset=0x%" PFMT64x "\n",
					rz_bin_dwarf_attr_udata(attr), die->offset);
				break;
			}
			function_apply_specification(ctx, spec, fcn);
			break;
		}
		case DW_AT_type:
			rz_type_free(fcn->ret_type);
			fcn->ret_type = type_parse_from_offset(ctx, rz_bin_dwarf_attr_udata(attr), NULL);
			break;
		case DW_AT_virtuality:
			fcn->is_method = true; /* method specific attr */
			fcn->is_virtual = true;
			break;
		case DW_AT_object_pointer:
			fcn->is_method = true;
			break;
		case DW_AT_vtable_elem_location:
			fcn->is_method = true;
			fcn->vtable_addr = 0; /* TODO we might use this information */
			break;
		case DW_AT_accessibility:
			fcn->is_method = true;
			fcn->access = (ut8)rz_bin_dwarf_attr_udata(attr);
			break;
		case DW_AT_external:
			fcn->is_external = true;
			break;
		case DW_AT_trampoline:
			fcn->is_trampoline = true;
			break;
		case DW_AT_ranges:
		default:
			break;
		}
	}
	if (fcn->link_name) {
		fcn->demangle_name =
			ctx->analysis->binb.demangle(ctx->analysis->binb.bin,
				rz_bin_dwarf_lang_for_demangle(ctx->unit->language), fcn->link_name);
	}
	fcn->prefer_name = select_name(fcn->demangle_name, fcn->link_name, fcn->name, ctx->unit->language);
	if (!fcn->prefer_name) {
		fcn->prefer_name = fcn->name = anonymous_name("fcn", die->offset);
	}

	RzCallable *callable = rz_type_callable_new(fcn->prefer_name);
	callable->ret = fcn->ret_type ? rz_type_clone(fcn->ret_type) : NULL;
	rz_vector_init(&fcn->variables, sizeof(RzAnalysisDwarfVariable), (RzVectorFree)variable_fini, NULL);
	function_children_parse(ctx, die, callable, fcn);

	RZ_LOG_DEBUG("DWARF function saving %s 0x%" PFMT64x " [0x%" PFMT64x "]\n",
		fcn->prefer_name, fcn->low_pc, die->offset);
	if (!ht_up_update(ctx->analysis->debug_info->callable_by_offset, die->offset, callable)) {
		RZ_LOG_ERROR("DWARF callable saving failed [0x%" PFMT64x "]\n", die->offset);
		goto cleanup;
	}
	if (!ht_up_update(ctx->analysis->debug_info->function_by_offset, die->offset, fcn)) {
		RZ_LOG_ERROR("DWARF function saving failed [0x%" PFMT64x "]\n", fcn->low_pc);
		goto cleanup;
	}
	if (fcn->low_pc > 0) {
		if (!ht_up_update(ctx->analysis->debug_info->function_by_addr, fcn->low_pc, fcn)) {
			RZ_LOG_ERROR("DWARF function saving failed with addr: [0x%" PFMT64x "]\n",
				fcn->low_pc);
			goto cleanup;
		}
	}
	return true;
cleanup:
	RZ_LOG_ERROR("Failed to parse function %s at 0x%" PFMT64x "\n", fcn->prefer_name, die->offset);
	function_free(fcn);
	return false;
}

static bool variable_exist_global(RzAnalysis *a, RzAnalysisDwarfVariable *v) {
	RzAnalysisVarGlobal *existing_glob = NULL;
	if ((existing_glob = rz_analysis_var_global_get_byaddr_in(a, v->location->address))) {
		return true;
	}
	if ((existing_glob = rz_analysis_var_global_get_byname(a, v->prefer_name))) {
		return true;
	}
	return false;
}

static bool variable_from_die(
	RZ_BORROW RZ_IN RZ_NONNULL Context *ctx,
	RZ_BORROW RZ_IN RZ_NONNULL const RzBinDwarfDie *die) {
	RzAnalysisDwarfVariable v = { 0 };
	if (!function_var_parse(ctx, NULL, NULL, &v, die, NULL)) {
		return false;
	}
	if (!(v.type && v.location->kind == RzBinDwarfLocationKind_ADDRESS)) {
		return false;
	}

	if (variable_exist_global(ctx->analysis, &v)) {
		return false;
	}

	bool result = rz_analysis_var_global_create(
		ctx->analysis, v.prefer_name, v.type, v.location->address);

	v.type = NULL;
	variable_fini(&v);
	return result;
}

static void die_parse(Context *ctx, RzBinDwarfDie *die) {
	if (set_u_contains(ctx->analysis->debug_info->visited, die->offset)) {
		return;
	}
	set_u_add(ctx->analysis->debug_info->visited, die->offset);
	switch (die->tag) {
	case DW_TAG_structure_type:
	case DW_TAG_union_type:
	case DW_TAG_class_type:
	case DW_TAG_enumeration_type:
	case DW_TAG_typedef:
	case DW_TAG_unspecified_type:
	case DW_TAG_base_type: {
		RzBaseType_from_die(ctx, die);
		break;
	}
	case DW_TAG_entry_point:
	case DW_TAG_subprogram:
		function_from_die(ctx, die);
		break;
	case DW_TAG_variable:
		variable_from_die(ctx, die);
		break;
	default:
		break;
	}
}

static RzBinDwarfDie *die_next(RzBinDwarfDie *die, RzBinDWARF *dw) {
	return (die->sibling > die->offset)
		? ht_up_find(dw->info->die_by_offset, die->sibling, NULL)
		: die + 1;
}

/**
 * \brief Parses type and function information out of DWARF entries
 *        and stores them to analysis->debug_info
 * \param analysis RzAnalysis pointer
 * \param dw RzBinDwarf pointer
 */
RZ_API void rz_analysis_dwarf_preprocess_info(
	RZ_NONNULL RZ_BORROW RzAnalysis *analysis,
	RZ_NONNULL RZ_BORROW RzBinDWARF *dw) {
	rz_return_if_fail(analysis && dw);
	if (!dw->info) {
		return;
	}
	analysis->debug_info->dwarf_register_mapping = dwarf_register_mapping_query(analysis->cpu, analysis->bits);
	Context ctx = {
		.analysis = analysis,
		.dw = dw,
		.unit = NULL,
	};
	RzBinDwarfCompUnit *unit;
	rz_vector_foreach(&dw->info->units, unit) {
		if (rz_vector_empty(&unit->dies)) {
			continue;
		}
		ctx.unit = unit;
		for (RzBinDwarfDie *die = rz_vector_head(&unit->dies);
			die && (ut8 *)die < (ut8 *)unit->dies.a + unit->dies.len * unit->dies.elem_size;
			die = die_next(die, dw)) {
			die_parse(&ctx, die);
		}
	}
}

#define SWAP(T, a, b) \
	do { \
		T temp = a; \
		a = b; \
		b = temp; \
	} while (0)

static inline void update_base_type(const RzTypeDB *typedb, RzBaseType *type) {
	RzBaseType *t = rz_type_db_get_base_type(typedb, type->name);
	if (t && t == type) {
		return;
	}
	rz_type_db_update_base_type(typedb, rz_base_type_clone(type));
}

static void db_save_renamed(RzTypeDB *db, RzBaseType *b, char *name) {
	if (!name) {
		rz_warn_if_reached();
		return;
	}
	RzBaseType *t = rz_type_db_get_base_type(db, b->name);
	if (t == b) {
		return;
	}
	free(b->name);
	b->name = name;
	rz_type_db_update_base_type(db, b);
}

static bool store_base_type(void *u, const void *k, const void *v) {
	RzAnalysis *analysis = u;
	const char *name = k;
	RzPVector *types = (RzPVector *)v;
	const ut32 len = rz_pvector_len(types);
	if (len == 0) {
		RZ_LOG_WARN("BaseType %s has nothing", name);
	} else if (len == 1) {
		RzBaseType *t = rz_pvector_head(types);
		update_base_type(analysis->typedb, t);
	} else if (len == 2) {
		RzBaseType *a = rz_pvector_head(types);
		RzBaseType *b = rz_pvector_tail(types);
		if (a->kind != RZ_BASE_TYPE_KIND_TYPEDEF) {
			SWAP(RzBaseType *, a, b);
		}
		if (a->kind != RZ_BASE_TYPE_KIND_TYPEDEF) {
			update_base_type(analysis->typedb, a);
			db_save_renamed(analysis->typedb, rz_base_type_clone(b), rz_str_newf("%s_0", name));
			goto beach;
		}
		if (a->type->kind != RZ_TYPE_KIND_IDENTIFIER) {
			RZ_LOG_WARN("BaseType: type of typedef [%s] is not RZ_TYPE_KIND_IDENTIFIER\n", name);
			goto beach;
		}
		if (RZ_STR_NE(a->type->identifier.name, name)) {
			RZ_LOG_WARN("BaseType: type name [%s] of typedef [%s] is not valid\n",
				a->type->identifier.name, name);
			goto beach;
		}
		free(a->type->identifier.name);
		char *newname = rz_str_newf("%s_0", name);
		a->type->identifier.name = rz_str_dup(newname);
		update_base_type(analysis->typedb, a);

		db_save_renamed(analysis->typedb, rz_base_type_clone(b), newname);
	} else {
		RZ_LOG_WARN("BaseType: same name [%s] type count is more than 3\n", name);
	}
beach:
	return true;
}

static bool store_callable(void *u, ut64 k, const void *v) {
	RzAnalysis *analysis = u;
	RzCallable *c = (RzCallable *)v;
	if (!rz_type_func_update(analysis->typedb, rz_type_callable_clone(c))) {
		RZ_LOG_WARN("DWARF callable [%s] saving failed with offset: [0x%" PFMT64x "]\n",
			c->name, k);
	}
	return true;
}

/**
 * \brief Parses type and function information out of DWARF entries
 *        and stores them to analysis->debug_info and analysis->typedb
 * \param analysis RzAnalysis pointer
 * \param dw RzBinDwarf pointer
 */
RZ_API void rz_analysis_dwarf_process_info(RzAnalysis *analysis, RzBinDWARF *dw) {
	rz_return_if_fail(analysis && dw);
	rz_analysis_dwarf_preprocess_info(analysis, dw);
	ht_pp_foreach(analysis->debug_info->base_types_by_name, store_base_type, (void *)analysis);
	ht_up_foreach(analysis->debug_info->callable_by_offset, store_callable, (void *)analysis);
}

static bool fixup_regoff_to_stackoff(RzAnalysis *a, RzAnalysisFunction *f,
	RzAnalysisDwarfVariable *dw_var, const char *reg_name, RzAnalysisVar *var) {
	if (dw_var->location->kind != RzBinDwarfLocationKind_REGISTER_OFFSET) {
		return false;
	}
	ut16 reg = dw_var->location->register_number;
	st64 off = dw_var->location->offset;
	if (RZ_STR_EQ(a->cpu, "x86")) {
		if (a->bits == 64) {
			if (reg == 6) { // 6 = rbp
				rz_analysis_var_storage_init_stack(&var->storage, off - f->bp_off);
				return true;
			}
			if (reg == 7) { // 7 = rsp
				rz_analysis_var_storage_init_stack(&var->storage, off);
				return true;
			}
		} else {
			if (reg == 4) { // 4 = esp
				rz_analysis_var_storage_init_stack(&var->storage, off);
				return true;
			}
			if (reg == 5) { // 5 = ebp
				rz_analysis_var_storage_init_stack(&var->storage, off - f->bp_off);
				return true;
			}
		}
	} else if (RZ_STR_EQ(a->cpu, "ppc")) {
		if (reg == 1) { // 1 = r1
			rz_analysis_var_storage_init_stack(&var->storage, off);
			return true;
		}
	} else if (RZ_STR_EQ(a->cpu, "tricore")) {
		if (reg == 30) { // 30 = a14
			rz_analysis_var_storage_init_stack(&var->storage, off);
			return true;
		}
	}
	const char *SP = rz_reg_get_name(a->reg, RZ_REG_NAME_SP);
	if (SP && RZ_STR_EQ(SP, reg_name)) {
		rz_analysis_var_storage_init_stack(&var->storage, off);
		return true;
	}
	const char *BP = rz_reg_get_name(a->reg, RZ_REG_NAME_BP);
	if (BP && RZ_STR_EQ(BP, reg_name)) {
		rz_analysis_var_storage_init_stack(&var->storage, off - f->bp_off);
		return true;
	}
	return false;
}

static RzBinDwarfLocation *location_by_biggest_range(const RzBinDwarfLocList *loclist) {
	if (!loclist) {
		return NULL;
	}
	ut64 biggest_range = 0;
	RzBinDwarfLocation *biggest_range_loc = NULL;
	void **it;
	rz_pvector_foreach (&loclist->entries, it) {
		RzBinDwarfLocListEntry *entry = *it;
		ut64 range = entry->range->begin - entry->range->end;
		if (range > biggest_range && entry->location &&
			(entry->location->kind == RzBinDwarfLocationKind_REGISTER_OFFSET ||
				entry->location->kind == RzBinDwarfLocationKind_REGISTER ||
				entry->location->kind == RzBinDwarfLocationKind_CFA_OFFSET ||
				entry->location->kind == RzBinDwarfLocationKind_COMPOSITE)) {
			biggest_range = range;
			biggest_range_loc = entry->location;
		}
	}
	return biggest_range_loc;
}

static bool RzBinDwarfLocation_as_RzAnalysisVarStorage(
	RzAnalysis *a, RzAnalysisFunction *f,
	RzAnalysisDwarfVariable *dw_var, RzBinDwarfLocation *loc,
	RzAnalysisVar *var, RzAnalysisVarStorage *storage) {
	storage->type = RZ_ANALYSIS_VAR_STORAGE_EVAL_PENDING;
	var->origin.dw_var = dw_var;
	switch (loc->kind) {
	case RzBinDwarfLocationKind_REGISTER: {
		rz_analysis_var_storage_init_reg(storage, a->debug_info->dwarf_register_mapping(loc->register_number));
		break;
	}
	case RzBinDwarfLocationKind_REGISTER_OFFSET: {
		// Convert some register offset to stack offset
		if (fixup_regoff_to_stackoff(a, f, dw_var, a->debug_info->dwarf_register_mapping(loc->register_number), var)) {
			break;
		}
		break;
	}
	case RzBinDwarfLocationKind_ADDRESS: {
		if (variable_exist_global(a, dw_var)) {
			return false;
		}
		rz_analysis_var_global_create(a, dw_var->prefer_name,
			rz_type_clone(dw_var->type), loc->address);
		rz_analysis_var_fini(var);
		return false;
	}
	case RzBinDwarfLocationKind_EMPTY:
	case RzBinDwarfLocationKind_DECODE_ERROR:
	case RzBinDwarfLocationKind_VALUE:
	case RzBinDwarfLocationKind_BYTES:
	case RzBinDwarfLocationKind_IMPLICIT_POINTER:
	case RzBinDwarfLocationKind_EVALUATION_WAITING:
		break;
	case RzBinDwarfLocationKind_COMPOSITE:
		rz_analysis_var_storage_init_composite(storage);
		if (!storage->composite) {
			return false;
		}
		RzBinDwarfPiece *piece = NULL;
		rz_vector_foreach(loc->composite, piece) {
			RzAnalysisVarStorage *sto = RZ_NEW0(RzAnalysisVarStorage);
			if (!sto) {
				goto clean_composite;
			}
			RzBinDwarfLocation_as_RzAnalysisVarStorage(a, f, dw_var, piece->location, var, sto);
			RzAnalysisVarStoragePiece p = {
				.offset_in_bits = piece->bit_offset,
				.size_in_bits = piece->size_in_bits,
				.storage = sto,
			};
			rz_vector_push(storage->composite, &p);
		}
		break;
	clean_composite:
		rz_analysis_var_storage_fini(storage);
		return false;
	case RzBinDwarfLocationKind_CFA_OFFSET:
		// TODO: The following is only an educated guess. There is actually more involved in calculating the
		//       CFA correctly.
		rz_analysis_var_storage_init_stack(storage, loc->offset + a->bits / 8);
		break;
	case RzBinDwarfLocationKind_FB_OFFSET:
		rz_analysis_var_storage_init_stack(storage, loc->offset);
		break;
	case RzBinDwarfLocationKind_LOCLIST: {
		RzBinDwarfLocation *biggest_range_loc = location_by_biggest_range(loc->loclist);
		if (!biggest_range_loc) {
			break;
		}
		if (RzBinDwarfLocation_as_RzAnalysisVarStorage(a, f, dw_var, biggest_range_loc, var, storage)) {
			break;
		}
		break;
	}
	}
	return true;
}

static bool RzAnalysisDwarfVariable_as_RzAnalysisVar(RzAnalysis *a, RzAnalysisFunction *f, RzAnalysisDwarfVariable *DW_var, RzAnalysisVar *var) {
	RzBinDwarfLocation *loc = DW_var->location;
	if (!loc) {
		return false;
	}
	var->type = DW_var->type ? rz_type_clone(DW_var->type) : rz_type_new_default(a->typedb);
	var->name = strdup(DW_var->prefer_name ? DW_var->prefer_name : "");
	var->kind = DW_var->kind;
	var->fcn = f;
	var->origin.kind = RZ_ANALYSIS_VAR_ORIGIN_DWARF;
	return RzBinDwarfLocation_as_RzAnalysisVarStorage(a, f, DW_var, loc, var, &var->storage);
}

static bool dwarf_integrate_function(void *user, const ut64 k, const void *value) {
	RzAnalysis *analysis = user;
	const RzAnalysisDwarfFunction *dw_fn = value;
	RzAnalysisFunction *fn = rz_analysis_get_function_at(analysis, dw_fn->low_pc);
	if (!fn) {
		return true;
	}

	if (dw_fn->prefer_name && !rz_str_startswith(dw_fn->prefer_name, "anonymous")) {
		char *dwf_name = rz_str_newf("dbg.%s", dw_fn->prefer_name);
		rz_analysis_function_rename((RzAnalysisFunction *)fn, dwf_name);
		free(dwf_name);
	}

	RzAnalysisDwarfVariable *dw_var;
	rz_vector_foreach(&dw_fn->variables, dw_var) {
		RzAnalysisVar *var = RZ_NEW0(RzAnalysisVar);
		rz_analysis_var_init(var);
		if (!RzAnalysisDwarfVariable_as_RzAnalysisVar(analysis, fn, dw_var, var)) {
			free(var);
			continue;
		}
		rz_analysis_function_add_var(fn, var);
	}

	fn->has_debuginfo = true;
	fn->is_variadic = dw_fn->has_unspecified_parameters;
	if (dw_fn->high_pc && fn->meta._max < dw_fn->high_pc) {
		fn->meta._max = dw_fn->high_pc;
	}

	return true;
}

/**
 * \brief Use parsed DWARF function info in the function analysis
 * \param analysis The analysis
 * \param flags The flags
 */
RZ_API void rz_analysis_dwarf_integrate_functions(RzAnalysis *analysis, RzFlag *flags) {
	rz_return_if_fail(analysis && analysis->debug_info);
	ht_up_foreach(analysis->debug_info->function_by_addr, dwarf_integrate_function, analysis);
}

#define Ht_FREE_IMPL(V, T, f) \
	static void Ht##V##_##T##_free(Ht##V##Kv *kv) { \
		f(kv->value); \
	}

Ht_FREE_IMPL(UP, RzType, rz_type_free);
Ht_FREE_IMPL(UP, RzBaseType, rz_type_base_type_free);
Ht_FREE_IMPL(UP, RzAnalysisDwarfFunction, function_free);
Ht_FREE_IMPL(UP, RzCallable, rz_type_callable_free);
Ht_FREE_IMPL(PP, RzPVector, rz_pvector_free);

/**
 * \brief Create a new debug info
 * \return RzAnalysisDebugInfo pointer
 */
RZ_API RzAnalysisDebugInfo *rz_analysis_debug_info_new() {
	RzAnalysisDebugInfo *debug_info = RZ_NEW0(RzAnalysisDebugInfo);
	if (!debug_info) {
		return NULL;
	}
	debug_info->function_by_offset = ht_up_new(NULL, HtUP_RzAnalysisDwarfFunction_free, NULL);
	debug_info->function_by_addr = ht_up_new0();
	debug_info->variable_by_offset = ht_up_new0();
	debug_info->type_by_offset = ht_up_new(NULL, HtUP_RzType_free, NULL);
	debug_info->callable_by_offset = ht_up_new(NULL, HtUP_RzCallable_free, NULL);
	debug_info->base_type_by_offset = ht_up_new(NULL, HtUP_RzBaseType_free, NULL);
	debug_info->base_types_by_name = ht_pp_new(NULL, HtPP_RzPVector_free, NULL);
	debug_info->visited = set_u_new();
	return debug_info;
}

/**
 * \brief Free a debug info
 * \param debuginfo RzAnalysisDebugInfo pointer
 */
RZ_API void rz_analysis_debug_info_free(RzAnalysisDebugInfo *debuginfo) {
	if (!debuginfo) {
		return;
	}
	ht_up_free(debuginfo->function_by_offset);
	ht_up_free(debuginfo->function_by_addr);
	ht_up_free(debuginfo->variable_by_offset);
	ht_up_free(debuginfo->type_by_offset);
	ht_up_free(debuginfo->callable_by_offset);
	ht_up_free(debuginfo->base_type_by_offset);
	ht_pp_free(debuginfo->base_types_by_name);
	rz_bin_dwarf_free(debuginfo->dw);
	set_u_free(debuginfo->visited);
	free(debuginfo);
}
