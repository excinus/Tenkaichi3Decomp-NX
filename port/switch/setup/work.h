// The Switch setup's work (work.cpp), on a thread of its own; it reports in native.cpp's event lines, read with
// Native_Poll / Native_Running (port/setup/native.h).
#ifndef PORT_SWITCH_SETUP_WORK_H
#define PORT_SWITCH_SETUP_WORK_H
#include <string>

struct NxJob {
    bool fromPc;     // the game data from the PC version (pc), or from the disc image (iso, and eu if any)
    std::string iso, eu, pc;
    std::string sd;  // the SD card (its root)
};

void NxSetup_Start(const NxJob &job);
bool NxSetup_HasFiles(void);                       // this program carries the release's game files
bool NxSetup_PcValid(const std::string &pc);       // a PC installation (or its gamedata) with the game data in it
std::string NxSetup_CardNote(const std::string &sd, bool &warn); // what the chosen card looks like

#endif
