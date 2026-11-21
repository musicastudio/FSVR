// patchfiles/container_fsvr.cpp - FSVR's own .fsvr preset: XML whose <sysex> holds the stream in base64. Ours.
//
// Export Preset writes it (plugin.cpp): <fsvr version="1"> with <name>, <category> and <sysex encoding="base64">,
// the performance, its voices and its Fseq as the unit dumps them. Only the sysex is read here; the name is
// already the performance's own.
#include "reading.h"
#include <cstring>

namespace fsvr::patchfiles {

static const char kBase64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

// Anything outside the alphabet (line breaks, padding) is passed over.
static void unbase64(const char* p, const char* end, Bytes& out) {
    uint32_t v = 0;
    int bits = 0;
    for (; p < end; ++p) {
        const char* at = *p ? std::strchr(kBase64, *p) : nullptr;
        if (!at) continue;
        v = v << 6 | (uint32_t)(at - kBase64);
        if ((bits += 6) >= 8) out.push_back((uint8_t)(v >> (bits -= 8)));
    }
}

bool fsvrStream(const Bytes& file, Bytes& out, Report& report) {
    // Text that is not XML is turned away on its first bytes, before the file is copied.
    size_t at = file.size() >= 3 && file[0] == 0xEF && file[1] == 0xBB && file[2] == 0xBF ? 3 : 0;   // a UTF-8 byte order mark
    while (at < file.size() && (file[at] == ' ' || file[at] == '\t' || file[at] == '\r' || file[at] == '\n')) ++at;
    if (at == file.size() || file[at] != '<') return false;
    const std::string doc(file.begin(), file.end());
    const size_t root = doc.find("<fsvr");
    if (root == std::string::npos || root + 5 >= doc.size() || !std::strchr(" \t\r\n>/", doc[root + 5])) return false;
    size_t a = doc.find("<sysex", root), e = std::string::npos;
    if (a != std::string::npos) a = doc.find('>', a);
    if (a != std::string::npos) e = doc.find("</sysex>", ++a);
    if (e != std::string::npos) unbase64(doc.data() + a, doc.data() + e, out);
    if (out.empty()) report.skipped.push_back("an FSVR preset with no sysex in it");
    return true;
}

} // namespace fsvr::patchfiles
