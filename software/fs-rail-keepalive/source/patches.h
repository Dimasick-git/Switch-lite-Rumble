/*
 * patches.h - signature table for the fs power-gate runtime patcher.
 *
 * Each entry locates a power-gate branch in the fs .text by a BYTE PATTERN and
 * writes a patch (an ARM64 NOP) at a relative offset from the match.
 *
 * WHY PATTERNS, NOT OFFSETS
 * -------------------------
 * Absolute offsets are tied to one HOS build of fs and break on every system
 * update. A byte pattern anchored on stable surrounding instructions keeps
 * matching across versions, as long as that code is unchanged - this is what
 * makes the patcher version-agnostic (same approach as sys-patch).
 *
 * PATTERN SYNTAX
 * --------------
 *   - Space-separated hex byte pairs:           "f3 0f 1e f8"
 *   - ".." is a single wildcard byte (any value): "f3 .. .. f8"
 *   - Case-insensitive, spaces optional.
 *
 * Sub-byte note: ARM64 branch immediates (BL imm26, CBNZ imm19) are NOT byte
 * aligned, so a BL or CBNZ cannot be pinned by fixed bytes - only its opcode
 * byte (e.g. 0xB5 for CBNZ X) is stable. Author signatures to anchor on nearby
 * FULLY-FIXED instructions (STP/LDP/MOV/ADRP with constant fields), then set
 * patch_offset to land on the branch you actually want to NOP. Use
 * tools/find_offsets.py --emit-signature on a real binary to draft these.
 *
 * SAFETY
 * ------
 * All entries below are placeholders with enabled=false, because authoring real
 * signatures requires a real fs binary. With every entry disabled the module
 * patches NOTHING and only logs - it cannot brick anything. Fill in a pattern
 * from your dumped fs, flip enabled=true, and test on hardware.
 */
#pragma once

#include <switch.h>
#include <stdbool.h>

typedef struct {
    const char *name;          /* human label, also logged                      */
    const char *pattern;       /* byte pattern with ".." wildcards               */
    int         patch_offset;  /* signed byte offset from match start to patch   */
    const char *patch;         /* hex bytes to write (default ARM64 NOP)         */
    bool        enabled;       /* false = skip (placeholder / not yet verified)  */
} FsRailPatch;

/* ARM64 NOP = 1F 20 03 D5. */
#define ARM64_NOP "1F2003D5"

/*
 * The three power-gate sites (see docs/FS-LOTUS3.md section 1 and section 3).
 * Patterns are EMPTY/disabled until extracted from a real fs binary.
 */
static const FsRailPatch g_fs_rail_patches[] = {
    {
        .name         = "IsGameCardInserted_false_branch",
        .pattern      = "",          /* TODO: extract from fs .text            */
        .patch_offset = 0,
        .patch        = ARM64_NOP,
        .enabled      = false,
    },
    {
        .name         = "GetGameCardHandle_power_gate_branch",
        .pattern      = "",          /* TODO: extract from fs .text            */
        .patch_offset = 0,
        .patch        = ARM64_NOP,
        .enabled      = false,
    },
    {
        .name         = "GetGameCardAttribute_fail_branch",
        .pattern      = "",          /* TODO: extract from fs .text            */
        .patch_offset = 0,
        .patch        = ARM64_NOP,
        .enabled      = false,
    },
};

#define FS_RAIL_PATCH_COUNT (sizeof(g_fs_rail_patches) / sizeof(g_fs_rail_patches[0]))
