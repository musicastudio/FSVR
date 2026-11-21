// patchfiles/dx7.h - the DX7 family's voice data: VCED, the packed VMEM record, the AMEM slot and the ACED. Ours,
// from the DX7 and DX7II MIDI data formats.
#pragma once
#include "reading.h"

namespace fsvr::patchfiles {

constexpr size_t VCED_SIZE = 155, ACED_SIZE = 49, RECORD = 128, VMEM_SIZE = 32 * RECORD, AMEM_SLOT = 35, AMEM_SIZE = 32 * AMEM_SLOT;
constexpr size_t VCED_NAME = 145, RECORD_NAME = 118, NAME_LENGTH = 10;

uint8_t checksum(const uint8_t* data, size_t n);   // Yamaha's: the two's complement of the sum, 7 bits
// A data block's checksum against the byte after it; a warning in the report when they differ.
void checkSum(const Message& m, size_t data, size_t n, const char* what, Reading& r);
std::string text(const uint8_t* p, size_t n);      // a name, unprintable bytes as spaces, trailing ones cut

Bytes vcedMessage(const uint8_t* vced);            // F0 43 00 00 01 1B <155> cs F7
Bytes acedMessage(const uint8_t* aced);            // F0 43 00 05 00 31 <49> cs F7
void unpackRecord(const uint8_t* record, uint8_t* vced);
void packRecord(const uint8_t* vced, uint8_t* record);
bool clampVced(uint8_t* vced);                     // to the DX7's ranges and a printable name; true when a byte was not
void initVoice(uint8_t* vced);                     // the DX7's INIT VOICE

// An AMEM slot unpacked to the 49-byte ACED in the layout the FS1R reads (Data List 3.2.2), and whether it
// would change the conversion: the firmware reads only the amp mod senses, PEG range, its velocity switch
// and PEG rate scaling (FUN_00035918, FUN_00035E96), all of them 0 in a DX7 voice's default slot.
void amemToAced(const uint8_t* slot, uint8_t* aced);
bool acedMatters(const uint8_t* aced);

// A DX voice item: its VCED, after its ACED when there is one.
Item dxVoice(const uint8_t* vced, const uint8_t* aced);

} // namespace fsvr::patchfiles
