// JBL Quantum 520 / 650 / 850 / 950 (and X variants) headsets, vendor 0ECB, "HIDV3" protocol on the
// vendor collection with usage page FF12 (64-byte reports: input 86, output 87, feature 88).
//   request (feature 88): [88][00 DD][01 00][01][00][02 00][feature lo][feature hi]
//            identifier DD00 = the device itself, command 0001 = get device info, 1 packet, index 0,
//            payload = the 2-byte feature id
//   reply   (input 86):   [86][00 DD][01 00][01][00][len lo][len hi][status lo][status hi]
//                         [feature lo][feature hi][size lo][size hi][value...]
//            status bit 15 set = error
// Battery = feature 000D, one byte: bit 7 charging, bits 0..6 percent.
// JBL Quantum 350 Wireless (VID 0ECB, PID 206B, 2.4 GHz dongle) is NOT a HIDV3 device: its HID interface 3
// (usage 0001:0000, 64-byte feature reports) has no FF12 collection. A USB capture of JBL QuantumENGINE
// showed the battery as numbered feature report 0x49 (GET_REPORT, 2 bytes: [49][percent]); the headset also
// pushes input report 0x08 = [08][percent] when it connects (0x4B = 75 % in both, matching the app).
// Whether bit 7 means "charging" on this model is not verified; it follows the HIDV3 convention.
// Read from JBL QuantumENGINE 2.9.4 (HIDV3Device / PowerFeature.Battery.HIDV3Left) and checked
// against a Quantum 650 Wireless on its 2.4 GHz dongle; the other models use the same code path there.
#include "drivers.h"

namespace {

constexpr USHORT kVid = 0x0ECB;
constexpr USHORT kBatteryFeature = 0x000D;

struct Model { USHORT pid; const wchar_t* name; bool dongle; };
constexpr Model kModels[] = {
    { 0x2125, L"JBL Quantum 650 Wireless", true },  { 0x2126, L"JBL Quantum 650 Wireless", false },
    { 0x213B, L"JBL Quantum 650X Wireless", true }, { 0x213D, L"JBL Quantum 650X Wireless", false },
    { 0x2127, L"JBL Quantum 950 Wireless", true },  { 0x2128, L"JBL Quantum 950 Wireless", false },
    { 0x213E, L"JBL Quantum 950X Wireless", true }, { 0x2140, L"JBL Quantum 950X Wireless", false },
    { 0x218D, L"JBL Quantum 520 Wireless", true },  { 0x218E, L"JBL Quantum 520 Wireless", false },
    { 0x21A7, L"JBL Quantum 520X Wireless", true }, { 0x21A9, L"JBL Quantum 520X Wireless", false },
    { 0x219A, L"JBL Quantum 850 Wireless", true },  { 0x219B, L"JBL Quantum 850 Wireless", false },
};

// With the headset switched off the dongle does not answer at all, so a timeout means "not there".
bool GetBattery(HidDev& d, BYTE& info) {
    d.Flush();
    if (!d.SetFeature({ 0x88, 0x00, 0xDD, 0x01, 0x00, 0x01, 0x00, 0x02, 0x00, (BYTE)kBatteryFeature,
                        (BYTE)(kBatteryFeature >> 8) }))
        return false;
    ULONGLONG end = GetTickCount64() + 800;
    std::vector<BYTE> r;
    while ((LONGLONG)end - (LONGLONG)GetTickCount64() > 0) {
        int left = (int)((LONGLONG)end - (LONGLONG)GetTickCount64());
        if (d.Read(r, left) < 16) return false;
        // Only the reply to "get device info" sent to the device itself.
        if (r[0] != 0x86 || r[1] != 0x00 || r[2] != 0xDD || r[3] != 0x01 || r[4] != 0x00 || r[5] != 0x01) continue;
        if (r[10] & 0x80) return false;  // error status (bit 15)
        if (r[11] != (BYTE)kBatteryFeature || r[12] != (BYTE)(kBatteryFeature >> 8)) continue;
        if (r[13] == 0 && r[14] == 0) continue;  // empty feature value
        info = r[15];
        return (info & 0x7F) <= 100;
    }
    return false;
}


// Quantum 350 Wireless: battery = numbered feature report 0x49 on the HID interface (see the header comment).
constexpr USHORT kQuantum350Pid = 0x206B;
constexpr BYTE kQuantum350BatteryReport = 0x49;

bool GetBattery350(HidDev& d, int& percent, bool& charging) {
    std::vector<BYTE> r;
    if (!d.GetFeature(kQuantum350BatteryReport, r) || r.size() < 2 || r[0] != kQuantum350BatteryReport) return false;
    int v = r[1];
    charging = false;
    if (v > 100) {  // bit 7 as a charging flag (unverified), like the HIDV3 models
        charging = (v & 0x80) != 0;
        v &= 0x7F;
    }
    if (v > 100) return false;
    percent = v;
    return true;
}

void PollJblQuantum350(PollContext& ctx) {
    if (ctx.IsClaimed(kVid, kQuantum350Pid)) return;
    // Feature-report access only, so QuantumENGINE running at the same time is not disturbed.
    for (const HidColl* c : ctx.Find(kVid, kQuantum350Pid, [](const HidColl& c) { return c.featLen() >= 2; })) {
        HidDev d;
        if (!d.OpenFeatureOnly(*c)) continue;
        int percent = 0;
        bool charging = false;
        if (!GetBattery350(d, percent, charging)) continue;
        DeviceStatus s;
        s.key = L"jbl:" + Hex4(kQuantum350Pid);
        s.name = L"JBL Quantum 350 Wireless";
        s.kind = DeviceKind::Headset;
        s.via = L"2.4 GHz receiver";
        s.percent = percent;
        s.charging = charging;
        ctx.Add(s, kVid, kQuantum350Pid);
        break;
    }
}

}  // namespace

void PollJbl(PollContext& ctx) {
    for (const Model& m : kModels) {
        if (ctx.IsClaimed(kVid, m.pid)) continue;
        for (const HidColl* c : ctx.Find(kVid, m.pid, [](const HidColl& c) {
                 return c.page() == 0xFF12 && c.featLen() >= 11 && c.inLen() >= 16;
             })) {
            HidDev d;
            if (!d.Open(*c)) continue;
            BYTE info = 0;
            if (!GetBattery(d, info)) continue;
            DeviceStatus s;
            s.key = L"jbl:" + Hex4(m.pid);
            s.name = m.name;
            s.kind = DeviceKind::Headset;
            s.via = m.dongle ? L"2.4 GHz receiver" : L"USB cable";
            s.percent = info & 0x7F;
            s.charging = (info & 0x80) != 0;
            ctx.Add(s, kVid, m.pid);
            break;
        }
    }
    PollJblQuantum350(ctx);
}
