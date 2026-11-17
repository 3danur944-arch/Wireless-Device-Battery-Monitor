// Exercise the JBL Quantum 350 Wireless path of the real driver with a simulated transport; no hardware is touched.
// The collections and the reply come from a USB capture of JBL QuantumENGINE: HID interface 3 of 0ECB:206B,
// feature report 0x49 answers [49][4B] while the app shows 75 %.
#include "../src/drivers/drivers.h"

#include <cassert>
#include <cstdio>

namespace {
std::vector<BYTE> reply;
bool featureOk = true, openOk = true;
int opens = 0, getCalls = 0;

HidColl Collection(USHORT page, USHORT usage, USHORT in, USHORT feat) {
    HidColl c;
    c.vid = 0x0ECB;
    c.pid = 0x206B;
    c.iface = 3;
    c.path = L"hid";
    c.caps.UsagePage = page;
    c.caps.Usage = usage;
    c.caps.InputReportByteLength = in;
    c.caps.FeatureReportByteLength = feat;
    return c;
}

PollContext Context() {
    opens = getCalls = 0;
    featureOk = openOk = true;
    reply = { 0x49, 0x4B };
    PollContext ctx;
    ctx.colls = { Collection(0x000C, 0x0001, 3, 0), Collection(0x0001, 0x0000, 2, 64) };  // as in `--dump`
    return ctx;
}
}  // namespace

bool HidDev::Open(const HidColl&) { assert(false); return false; }  // only feature access is allowed
bool HidDev::OpenFeatureOnly(const HidColl& c) {
    assert(c.featLen() == 64);  // never the consumer-control collection
    c_ = c;
    ++opens;
    return openOk;
}
void HidDev::Close() {}
void HidDev::Flush() {}
bool HidDev::SetFeature(const std::vector<BYTE>&) { assert(false); return false; }  // read-only driver
int HidDev::Read(std::vector<BYTE>&, int) { assert(false); return -1; }
bool HidDev::GetFeature(BYTE reportId, std::vector<BYTE>& out) {
    ++getCalls;
    assert(reportId == 0x49);
    out.assign(c_.featLen(), 0);  // the real layer pads to the feature report length
    for (size_t i = 0; i < reply.size() && i < out.size(); ++i) out[i] = reply[i];
    return featureOk;
}
std::wstring Hex4(USHORT v) {
    wchar_t text[8];
    swprintf(text, 8, L"%04X", v);
    return text;
}

int main() {
    auto ctx = Context();
    PollJbl(ctx);
    assert(ctx.out.size() == 1 && ctx.IsClaimed(0x0ECB, 0x206B));
    const auto& s = ctx.out[0];
    assert(s.key == L"jbl:206B" && s.name == L"JBL Quantum 350 Wireless");
    assert(s.kind == DeviceKind::Headset && s.via == L"2.4 GHz receiver");
    assert(s.percent == 75 && !s.charging && s.connected && !s.asleep);
    assert(opens == 1 && getCalls == 1);
    PollJbl(ctx);
    assert(getCalls == 1 && ctx.out.size() == 1);  // already claimed

    ctx = Context();
    reply = { 0x49, 100 };
    PollJbl(ctx);
    assert(ctx.out.size() == 1 && ctx.out[0].percent == 100 && !ctx.out[0].charging);
    ctx = Context();
    reply = { 0x49, 0 };
    PollJbl(ctx);
    assert(ctx.out.size() == 1 && ctx.out[0].percent == 0);

    ctx = Context();
    reply = { 0x49, 0x80 | 75 };  // bit 7 = charging (convention of the other JBL models, unverified here)
    PollJbl(ctx);
    assert(ctx.out.size() == 1 && ctx.out[0].percent == 75 && ctx.out[0].charging);

    // Wrong report id, impossible values, failed read and failed open all mean "no device", never 0 %.
    ctx = Context();
    reply = { 0x48, 0x4B };
    PollJbl(ctx);
    assert(ctx.out.empty() && ctx.claimed.empty());
    ctx = Context();
    reply = { 0x49, 0x80 | 101 };
    PollJbl(ctx);
    assert(ctx.out.empty() && ctx.claimed.empty());
    ctx = Context();
    featureOk = false;
    PollJbl(ctx);
    assert(ctx.out.empty() && ctx.claimed.empty() && getCalls == 1);
    ctx = Context();
    openOk = false;
    PollJbl(ctx);
    assert(ctx.out.empty() && ctx.claimed.empty() && getCalls == 0);

    // A different product id of the same vendor is not touched.
    ctx = Context();
    for (auto& c : ctx.colls) c.pid = 0x2125;
    PollJbl(ctx);
    assert(ctx.out.empty() && opens == 0 && getCalls == 0);

    std::puts("jbl_quantum350_test: ok");
    return 0;
}
