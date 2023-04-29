/*
 * Copyright (C) 2023-2023 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

#include "pin.H"
#include <iostream>
#include <stdlib.h>
#include "tool_macros.h"

/*
 *  Pintool mode of operation:
 *  o   The pintool instruments two functions: Bar() a second function that is configured via the KnobMode.
 *  o   We expect the analysis routine for the second routine to be called. This is our proof that the instrumentation
 *      for the JMP/CALL worked as expected.  
 *  o   All the functions call Bar(), so we expect the analysis routine for Bar to be called for all the tests.
 *      This is our proof that the second routine operated correctly.
 */

// When this knob is TRUE then the test will not assert on failure but just print.
// This is used for negative tests (that are expected to fail)
// Default: FALSE
static KNOB< BOOL > KnobAllowFailure(KNOB_MODE_WRITEONCE, "pintool", "allow_fail", "0", "Allow failure (do not assert)");

// When this knob is TRUE then the pintool will insert the probe using the PROBE_MODE_ALLOW_POTENTIAL_BRANCH_TARGET mode.
// With this mode, if a potential branch target is detected in the probe area, Pin will still place a probe there.
// Default: FALSE
static KNOB< BOOL > KnobAllowProbePotentialBranchTarget(KNOB_MODE_WRITEONCE, "pintool", "allow_probe_branch_target", "0",
                                                        "Allow probing a potential branch target");

// A number that represents the test mode (which routine is instrumented).
// The application has a mode parameter with identical semantics.
static KNOB< UINT32 > KnobMode(KNOB_MODE_WRITEONCE, "pintool", "mode", "0",
                               "The number represents that function that will be called by the application");

struct RtnInstrumentation
{
    std::string rtnName;
    PROBE_MODE probeMode;
    BOOL probeInRtnHead;
};

#ifdef TARGET_IA32E
//
// 64 bit
//
RtnInstrumentation rtnsToInstrument[] = {
    {"JmpRelAt0", PROBE_MODE_DEFAULT, TRUE},                 // 1
    {"JmpRelAt6", PROBE_MODE_DEFAULT, TRUE},                 // 2
    {"JmpDirectAt0", PROBE_MODE_DEFAULT, TRUE},              // 3
    {"JmpDirectAt6", PROBE_MODE_DEFAULT, TRUE},              // 4
    {"CallRelAt0", PROBE_MODE_DEFAULT, TRUE},                // 5
    {"CallRelAt6", PROBE_MODE_DEFAULT, TRUE},                // 6
    {"CallDirectAt0", PROBE_MODE_DEFAULT, TRUE},             // 7
    {"JmpTargetInProbe", PROBE_MODE_ALLOW_RELOCATION, TRUE}, // 8
    {"JmpDirectToNext", PROBE_MODE_ALLOW_RELOCATION, TRUE},  // 9
    {"CallDirectReg", PROBE_MODE_DEFAULT, FALSE},            // 10
    {"JmpDirectReg", PROBE_MODE_DEFAULT, FALSE}              // 11
};
#else
//
// 32 bit
//
RtnInstrumentation rtnsToInstrument[] = {
    {"CallDirectAt0", PROBE_MODE_DEFAULT, TRUE},             // 1
    {"CallDirectAt5", PROBE_MODE_DEFAULT, TRUE},             // 2
    {"CallDirectReg", PROBE_MODE_DEFAULT, FALSE},            // 3
    {"JmpDirectReg", PROBE_MODE_DEFAULT, FALSE},             // 4
    {"JmpTargetInProbe", PROBE_MODE_ALLOW_RELOCATION, TRUE}, // 5
    {"JmpDirectToNext", PROBE_MODE_ALLOW_RELOCATION, TRUE}   // 6
};
#endif // TARGET_IA32E

VOID BeforeRtn() { std::cout << "Before function " << rtnsToInstrument[KnobMode.Value() - 1].rtnName << std::endl; }

VOID BeforeBar() { std::cout << "Before Bar" << std::endl; }

VOID ImageLoad(IMG img, VOID* v)
{
    if (IMG_IsMainExecutable(img) == FALSE) return;

    // Instrument Bar
    RTN rtnBar = RTN_FindByName(img, "Bar");
    ASSERT(RTN_Valid(rtnBar), "Failed to find routine Bar \n");
    ASSERT(RTN_InsertCallProbed(rtnBar, IPOINT_BEFORE, AFUNPTR(BeforeBar), IARG_END), "Failed to instrument Bar \n");
    std::cout << "Bar instrumented successfully at " << std::hex << RTN_Address(rtnBar) << std::endl;

    // Find the routine to instrument
    UINT32 mode         = KnobMode.Value() - 1;
    std::string rtnName = rtnsToInstrument[mode].rtnName;
    RTN rtn             = RTN_FindByName(img, rtnName.c_str());
    ASSERT(RTN_Valid(rtn), "Failed to find routine " + rtnName + "\n");

    RTN_Open(rtn);
    std::cout << RTN_Name(rtn) << ":" << std::endl;
    for (INS ins = RTN_InsHead(rtn); INS_Valid(ins); ins = INS_Next(ins))
    {
        std::cout << "\t" << std::hex << INS_Address(ins) << " : " << INS_Disassemble(ins) << std::endl;
    }
    RTN_Close(rtn);

    // If probeInRtnHead is TRUE then we should place a probe at the beginning of the routine.
    // If it's FALSE then we should search for the first JMP or CALL, create a new RTN on that instruction
    // and place the probe there.
    // This is used to allow the pintool to skip LEA instructions and probe the JMP/CALL that follows.
    if (rtnsToInstrument[mode].probeInRtnHead == FALSE)
    {
        ADDRINT addr = 0;
        RTN_Open(rtn);
        for (INS ins = RTN_InsHead(rtn); INS_Valid(ins); ins = INS_Next(ins))
        {
            if (INS_IsBranch(ins) || INS_IsCall(ins))
            {
                addr = INS_Address(ins);
                break;
            }
        }
        RTN_Close(rtn);
        ASSERTX(addr != 0);
        RTN newRtn = RTN_CreateAt(addr, "RtnAtBranch");
        ASSERTX(RTN_Valid(newRtn));
        rtn = newRtn;
    }

    if (KnobAllowProbePotentialBranchTarget)
    {
        rtnsToInstrument[mode].probeMode =
            (PROBE_MODE)(rtnsToInstrument[mode].probeMode | PROBE_MODE_ALLOW_POTENTIAL_BRANCH_TARGET);
    }

    // First try to replace with default probe mode.
    // If it fails and PROBE_MODE_ALLOW_RELOCATION/PROBE_MODE_ALLOW_POTENTIAL_BRANCH_TARGET for this test then try to replace using that mode.
    BOOL probed = RTN_InsertCallProbed(rtn, IPOINT_BEFORE, AFUNPTR(BeforeRtn), IARG_END);
    if (!probed && (rtnsToInstrument[mode].probeMode != PROBE_MODE_DEFAULT))
    {
        std::cout << "Failed to instrument " + rtnName + " in default probe mode" << std::endl;
        probed = RTN_InsertCallProbedEx(rtn, IPOINT_BEFORE, rtnsToInstrument[mode].probeMode, AFUNPTR(BeforeRtn), IARG_END);
    }
    if (probed)
    {
        std::cout << rtnName + " instrumented successfully at " << std::hex << RTN_Address(rtn) << std::endl;
    }
    else
    {
        if (KnobAllowFailure)
        {
            std::cout << "Failed to instrument " << rtnName << " at " << std::hex << RTN_Address(rtn) << std::endl;
        }
        else
        {
            ASSERT(probed, "Failed to instrument " + rtnName + " at " + hexstr(RTN_Address(rtn)) + "\n");
        }
    }
}

int main(INT32 argc, CHAR* argv[])
{
    PIN_InitSymbols();
    PIN_Init(argc, argv);

    if ((KnobMode.Value() == 0) || (KnobMode.Value() > sizeof(rtnsToInstrument) / sizeof(rtnsToInstrument[0])))
    {
        std::cerr << "Illegal mode " << std::dec << KnobMode << std::endl;
        PIN_ExitProcess(-1);
    }
    IMG_AddInstrumentFunction(ImageLoad, NULL);
    PIN_StartProgramProbed();
    return 0;
}
