/*
 * Copyright (C) 2007-2023 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

/*! @file
 *  This is an example of the PIN tool that demonstrates some basic PIN APIs
 *  and could serve as the starting point for developing your first PIN tool
 */

#include "pin.H"
#include <iostream>
#include <fstream>
#include <map>
#include <vector>
#include <algorithm>

/* ================================================================== */
// Global variables
/* ================================================================== */

std::map<VOID *, int> dist;
std::map<VOID *, int> reuse;
int line = 0;

UINT64 insCount = 0;    // number of dynamically executed instructions
UINT64 bblCount = 0;    // number of dynamically executed basic blocks
UINT64 threadCount = 0; // total number of threads, including main thread
UINT64 funcCount = 0;   // number of dynamically executed function calls (function invocations)

static UINT64 memAccessCount = 0;     // number of memory accesses recorded
static UINT64 MAX_MEM_ACCESS = 10000; // limit (you can change)

UINT64 totalDistance = 0;
UINT64 distanceCount = 0;

std::ostream *out = &std::cerr;

/* ===================================================================== */
// Command line switches
/* ===================================================================== */
KNOB<std::string> KnobOutputFile(KNOB_MODE_WRITEONCE, "pintool", "o", "", "specify file name for MyPinTool output");

KNOB<BOOL> KnobCount(KNOB_MODE_WRITEONCE, "pintool", "count", "1",
                     "count instructions, basic blocks and threads in the application");

/* ===================================================================== */
// Utilities
/* ===================================================================== */

/*!
 *  Print out help message.
 */
INT32 Usage()
{
    std::cerr << "This tool prints out the number of dynamically executed " << std::endl
              << "instructions, basic blocks and threads in the application." << std::endl
              << std::endl;

    std::cerr << KNOB_BASE::StringKnobSummary() << std::endl;

    return -1;
}

/* ===================================================================== */
// Analysis routines
/* ===================================================================== */

/*!
 * Count function invocations.
 * This function is called every time a function (routine) is entered.
 * It increments the global function call counter.
 */
VOID CountFunc()
{
    funcCount++;
}

/*!
 * Increase counter of the executed basic blocks and instructions.
 * This function is called for every basic block when it is about to be executed.
 * @param[in]   numInstInBbl    number of instructions in the basic block
 * @note use atomic operations for multi-threaded applications
 */
VOID CountBbl(UINT32 numInstInBbl)
{
    bblCount++;
    insCount += numInstInBbl;
}

/*!
 * Record memory access information.
 * This function is called before a memory instruction is executed.
 * It logs the instruction pointer (PC), type of access (Read/Write),
 * and the effective memory address being accessed.
 * @param[in]   ip      instruction pointer (program counter) of the instruction
 * @param[in]   type    type of memory access ('R' for read, 'W' for write)
 * @param[in]   addr    effective memory address accessed by the instruction
 */
VOID RecordMemAccess(VOID *ip, CHAR type, VOID *addr)
{
    if (memAccessCount >= MAX_MEM_ACCESS)
    {
        PIN_ExitApplication(0);
    }

    int distance = -1;

    if (dist.find(addr) != dist.end())
    {
        distance = line - dist[addr];

        totalDistance += distance;
        distanceCount++;
    }

    if (reuse.find(addr) == reuse.end())
        reuse[addr] = 0;
    else
        reuse[addr]++;

    std::string rw = (type == 'R') ? "Read" : "Write";

    *out << std::hex << ip << ", "
         << rw << ", "
         << std::hex << addr << ", "
         << std::dec << distance << ", "
         << reuse[addr] << std::endl;

    dist[addr] = line;

    line++;

    memAccessCount++;
}

/* ===================================================================== */
// Instrumentation callbacks
/* ===================================================================== */

/*!
 * Insert call to the CountBbl() analysis routine before every basic block
 * of the trace.
 * This function is called every time a new trace is encountered.
 * @param[in]   trace    trace to be instrumented
 * @param[in]   v        value specified by the tool in the TRACE_AddInstrumentFunction
 *                       function call
 */
VOID Trace(TRACE trace, VOID *v)
{
    // Visit every basic block in the trace
    for (BBL bbl = TRACE_BblHead(trace); BBL_Valid(bbl); bbl = BBL_Next(bbl))
    {
        // Insert a call to CountBbl() before every basic bloc, passing the number of instructions
        BBL_InsertCall(bbl, IPOINT_BEFORE, (AFUNPTR)CountBbl, IARG_UINT32, BBL_NumIns(bbl), IARG_END);
    }
}

/*!
 * Instrument instructions for memory access tracing.
 * This function is called for every instruction and inserts analysis
 * routines before instructions that perform memory operations.
 * It identifies memory reads and writes and records their addresses.
 * @param[in]   ins     instruction to be instrumented
 * @param[in]   v       value specified by the tool in the
 *                      INS_AddInstrumentFunction function call
 */
VOID Instruction(INS ins, VOID *v)
{
    UINT32 memOperands = INS_MemoryOperandCount(ins);

    for (UINT32 memOp = 0; memOp < memOperands; memOp++)
    {
        // Memory READ
        if (INS_MemoryOperandIsRead(ins, memOp))
        {
            INS_InsertPredicatedCall(
                ins, IPOINT_BEFORE, (AFUNPTR)RecordMemAccess,
                IARG_INST_PTR,
                IARG_UINT32, 'R',
                IARG_MEMORYOP_EA, memOp,
                IARG_END);
        }

        // Memory WRITE
        if (INS_MemoryOperandIsWritten(ins, memOp))
        {
            INS_InsertPredicatedCall(
                ins, IPOINT_BEFORE, (AFUNPTR)RecordMemAccess,
                IARG_INST_PTR,
                IARG_UINT32, 'W',
                IARG_MEMORYOP_EA, memOp,
                IARG_END);
        }
    }
}

/*!
 * Instrument routines (functions) to count function calls.
 * This function is invoked for every routine in the application.
 * It inserts a call to CountFunc() before the routine executes,
 * thereby counting each function invocation.
 * @param[in]   rtn     routine (function) to be instrumented
 * @param[in]   v       value specified by the tool in the
 *                      RTN_AddInstrumentFunction function call
 */
VOID Routine(RTN rtn, VOID *v)
{
    RTN_Open(rtn);

    RTN_InsertCall(
        rtn, IPOINT_BEFORE,
        (AFUNPTR)CountFunc,
        IARG_END);

    RTN_Close(rtn);
}

/*!
 * Increase counter of threads in the application.
 * This function is called for every thread created by the application when it is
 * about to start running (including the root thread).
 * @param[in]   threadIndex     ID assigned by PIN to the new thread
 * @param[in]   ctxt            initial register state for the new thread
 * @param[in]   flags           thread creation flags (OS specific)
 * @param[in]   v               value specified by the tool in the
 *                              PIN_AddThreadStartFunction function call
 */
VOID ThreadStart(THREADID threadIndex, CONTEXT *ctxt, INT32 flags, VOID *v) { threadCount++; }

/*!
 * Print out analysis results.
 * This function is called when the application exits.
 * @param[in]   code            exit code of the application
 * @param[in]   v               value specified by the tool in the
 *                              PIN_AddFiniFunction function call
 */
VOID Fini(INT32 code, VOID *v)
{
    *out << "===============================================" << std::endl;

    double avg = 0.0;
    if (distanceCount != 0)
        avg = (double)totalDistance / distanceCount;

    std::cout << "Average Distance: " << avg << std::endl;

    std::vector<std::pair<VOID *, int>> vec(reuse.begin(), reuse.end());

    std::sort(vec.begin(), vec.end(),
              [](auto &a, auto &b)
              {
                  return a.second > b.second;
              });

    std::cout << "Top 5 Reuse Addresses:" << std::endl;

    for (int i = 0; i < 5 && i < (int)vec.size(); i++)
    {
        std::cout << std::hex << vec[i].first
                  << " -> " << std::dec << vec[i].second << std::endl;
    }

    *out << "===============================================" << std::endl;
}

/*!
 * The main procedure of the tool.
 * This function is called when the application image is loaded but not yet started.
 * @param[in]   argc            total number of elements in the argv array
 * @param[in]   argv            array of command line arguments,
 *                              including pin -t <toolname> -- ...
 */
int main(int argc, char *argv[])
{
    // Initialize PIN library. Print help message if -h(elp) is specified
    // in the command line or the command line is invalid
    if (PIN_Init(argc, argv))
    {
        return Usage();
    }

    std::string fileName = KnobOutputFile.Value();

    if (fileName.empty())
    {
        fileName = "mem.trace";
    }

    out = new std::ofstream(fileName.c_str());

    // Register instruction-level instrumentation (memory tracing)
    INS_AddInstrumentFunction(Instruction, 0);

    // Register function to be called for every routine
    RTN_AddInstrumentFunction(Routine, 0);

    if (KnobCount)
    {
        // Register function to be called to instrument traces
        TRACE_AddInstrumentFunction(Trace, 0);

        // Register function to be called for every thread before it starts running
        PIN_AddThreadStartFunction(ThreadStart, 0);

        // Register function to be called when the application exits
        PIN_AddFiniFunction(Fini, 0);
    }

    std::cerr << "===============================================" << std::endl;
    std::cerr << "This application is instrumented by MyPinTool" << std::endl;
    if (!KnobOutputFile.Value().empty())
    {
        std::cerr << "See file " << KnobOutputFile.Value() << " for analysis results" << std::endl;
    }
    std::cerr << "===============================================" << std::endl;

    // Start the program, never returns
    PIN_StartProgram();

    return 0;
}

/* ===================================================================== */
/* eof */
/* ===================================================================== */
