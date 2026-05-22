#pragma once
#include <stddef.h>

// =============================================================================
// scpi_commands.h  —  SCPI command registry interface
//
// The SCPI task calls scpiDispatch() with each incoming line.
// All command definitions live in scpi_commands.cpp.
//
// ─── TO ADD A COMMAND ────────────────────────────────────────────────────────
//   1. Write a static handler in scpi_commands.cpp matching ScpiHandler.
//   2. Add one row to g_scpiRegistry[].
//   Nothing else changes.
// =============================================================================

typedef const char* (*ScpiHandler)(const char* cmd);

struct ScpiCommand {
    const char* command;    // upper-case prefix, e.g. "MEAS:TEMP"
    ScpiHandler handler;
    const char* helpText;
};

extern const ScpiCommand g_scpiRegistry[];

void   scpiDispatch(const char* cmd);
size_t scpiCommandCount();
