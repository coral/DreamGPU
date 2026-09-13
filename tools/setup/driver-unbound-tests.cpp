// SPDX-License-Identifier: GPL-2.0-or-later
#include "driver-oem-test-win32.h"
#include "driver-unbound.h"
using setup::driver::UnboundStore;
constexpr const char *Root = "C:\\WINDOWS\\DreamGPU";
constexpr const char *Id = "PCI\\VEN_1234&DEV_1113&SUBSYS_00000000\\1";
static void put(const char *p, const char *bytes) {
    auto &n = fake_win32::files[fake_win32::canon(p)];
    n.bytes.assign(bytes, bytes + strlen(bytes));
    n.flushed = true;
}
static void init(UnboundStore &s) {
    assert(s.init(Root, "C:\\WINDOWS", "C:\\WINDOWS\\SYSTEM32", Id));
}
static void reset() {
    fake_win32::reset();
    devices = {{Id, "", false, true}};
    driver_fake::compatible.clear();
    removes = 0;
    lost_response = pending_remove = false;
    put("C:\\WINDOWS\\INF\\OEM0.INF", "unrelated original");
    put("C:\\WINDOWS\\INF\\OEM0.PNF", "original compiled");
}
static setup::driver::Node publish() {
    devices[0].bound = true;
    devices[0].inf = "oem1.inf";
    put("C:\\WINDOWS\\INF\\OEM1.INF", "owned exact INF");
    put("C:\\WINDOWS\\INF\\OEM1.PNF", "owned compiled INF");
    setup::driver::Node desired;
    strcpy(desired.device, Id);
    setup::Sha256 hash;
    const char text[] = "owned exact INF";
    hash.update(reinterpret_cast<const BYTE *>(text), sizeof(text) - 1);
    hash.finish(desired.inf_sha);
    return desired;
}
static SP_DEVINFO_DATA own() {
    SP_DEVINFO_DATA d{};
    d.cbSize = sizeof(d);
    d.DevInst = 1;
    return d;
}
static void package_upgrade(bool reused, bool new_pnf = false, bool regenerated = false) {
    reset();
    auto previous = publish();
    strcpy(previous.inf, "C:\\WINDOWS\\INF\\OEM1.INF");
    driver_fake::compatible.push_back(previous);
    // Windows2000 can retain an older compatible package beside the bound INF.
    auto older = previous;
    strcpy(older.inf, "C:\\WINDOWS\\INF\\OEM0.INF");
    driver_fake::compatible.push_back(older);
    const auto old_inf = fake_win32::files.at("c:\\windows\\inf\\oem0.inf").bytes;
    const auto old_pnf = fake_win32::files.at("c:\\windows\\inf\\oem0.pnf").bytes;
    if (new_pnf)
        fake_win32::files.erase("c:\\windows\\inf\\oem1.pnf");
    UnboundStore package;
    assert(package.init(Root, "C:\\WINDOWS", "C:\\WINDOWS\\SYSTEM32", Id, true));
    assert(package.capture_before(&previous));
    assert(package.package_restored(previous, true)); // Interrupted before publication.
    assert(package.cleanup_package(previous));
    auto device = own();
    auto desired = previous;
    if (new_pnf)
        put("C:\\WINDOWS\\INF\\OEM1.PNF", "newly compiled borrowed INF");
    if (!reused) {
        devices[0].inf = "oem2.inf";
        put("C:\\WINDOWS\\INF\\OEM2.INF", "new upgraded INF");
        put("C:\\WINDOWS\\INF\\OEM2.PNF", "new compiled INF");
        const auto &bytes = fake_win32::files.at("c:\\windows\\inf\\oem2.inf").bytes;
        setup::Sha256 hash;
        hash.update(bytes.data(), bytes.size());
        hash.finish(desired.inf_sha);
    }
    assert(package.capture_published(2, device, desired));
    if (regenerated)
        put("C:\\WINDOWS\\INF\\OEM1.PNF", "Windows regenerated cache for the same borrowed INF");
    const auto borrowed_cache = fake_win32::files.at("c:\\windows\\inf\\oem1.pnf").bytes;
    assert(!package.remove(2, device) && removes == 0);
    if (!reused)
        assert(!package.cleanup_package(previous)); // Current device still consumes it.
    devices[0].inf = "oem1.inf";                    // Prior real binding restored.
    if (!reused) {
        devices.push_back({"PCI\\OTHER", "oem2.inf", true, false});
        assert(!package.cleanup_package(previous)); // Phantom consumer.
        devices.pop_back();
    }
    assert(package.cleanup_package(previous));
    auto writes = fake_win32::mutation;
    assert(package.package_restored(previous, true) && fake_win32::mutation == writes);
    assert(fake_win32::files.contains("c:\\windows\\inf\\oem1.inf"));
    assert(!fake_win32::files.contains("c:\\windows\\inf\\oem2.inf"));
    assert(removes == 0);
    assert(fake_win32::files.at("c:\\windows\\inf\\oem0.inf").bytes == old_inf);
    assert(fake_win32::files.at("c:\\windows\\inf\\oem0.pnf").bytes == old_pnf);
    assert(fake_win32::files.at("c:\\windows\\inf\\oem1.pnf").bytes == borrowed_cache);
    if (reused) {
        fake_win32::files.erase("c:\\windows\\inf\\oem1.pnf");
        assert(package.package_restored(previous, true));
        put("C:\\WINDOWS\\INF\\OEM1.INF", "foreign replacement of borrowed INF");
        assert(!package.package_restored(previous, true));
    }
}
int main() {
    package_upgrade(false);
    package_upgrade(true);
    package_upgrade(true, true);
    package_upgrade(true, false, true);
    package_upgrade(true, true, true);
    reset();
    UnboundStore store;
    init(store);
    assert(store.capture_before());
    auto desired = publish();
    auto device = own();
    assert(store.capture_published(2, device, desired));
    assert(store.remove(2, device));
    assert(store.original_verified());
    assert(removes == 1);
    assert(store.reconcile_removed());
    assert(removes == 1);
    assert(fake_win32::files.contains("c:\\windows\\inf\\oem0.inf"));
    // Interrupted initial capture can restart only before a published baseline exists.
    for (unsigned fail = 1; fail <= 5; ++fail) {
        reset();
        UnboundStore a;
        init(a);
        fake_win32::fail = fail;
        (void)a.capture_before();
        fake_win32::fail = 0;
        UnboundStore b;
        init(b);
        assert(b.capture_before());
    }
    // A failed publication receipt never authorizes an in-memory-only transition.
    for (unsigned fail = 1; fail <= 3; ++fail) {
        reset();
        UnboundStore a;
        init(a);
        assert(a.capture_before());
        desired = publish();
        fake_win32::mutation = 0;
        fake_win32::fail = fail;
        (void)a.capture_published(2, device, desired);
        fake_win32::fail = 0;
        assert(a.capture_published(2, device, desired));
        assert(a.remove(2, device));
        assert(a.original_verified());
    }
    // A compatible preexisting OEM package would automatically rebind after removal.
    reset();
    setup::driver::Node compatible;
    strcpy(compatible.inf, "C:\\WINDOWS\\INF\\OEM0.INF");
    driver_fake::compatible.push_back(compatible);
    UnboundStore borrowed;
    init(borrowed);
    assert(!borrowed.capture_before());
    assert(!fake_win32::files.contains("c:\\windows\\dreamgpu\\unbound.bin"));
    // Newly bound actual INF must be absent from the baseline and match desired bytes.
    reset();
    UnboundStore bad;
    init(bad);
    assert(bad.capture_before());
    desired = publish();
    devices[0].inf = "OEM0.INF";
    assert(!bad.capture_published(2, device, desired));
    reset();
    UnboundStore changed;
    init(changed);
    assert(changed.capture_before());
    desired = publish();
    put("C:\\WINDOWS\\INF\\OEM1.INF", "foreign");
    assert(!changed.capture_published(2, device, desired));
    // Phantom consumers block removal just like present consumers.
    for (bool present : {false, true}) {
        reset();
        UnboundStore shared;
        init(shared);
        assert(shared.capture_before());
        desired = publish();
        assert(shared.capture_published(2, device, desired));
        devices.push_back({"OTHER\\DEVICE", "oem1.inf", true, present});
        assert(!shared.remove(2, device));
        assert(!removes && fake_win32::files.contains("c:\\windows\\inf\\oem1.inf"));
    }
    // A lost successful class-installer response is reconciled, never replayed.
    reset();
    UnboundStore lost;
    init(lost);
    assert(lost.capture_before());
    desired = publish();
    assert(lost.capture_published(2, device, desired));
    lost_response = true;
    assert(!lost.remove(2, device));
    assert(removes == 1);
    UnboundStore recovered;
    init(recovered);
    assert(recovered.reconcile_removed());
    assert(recovered.original_verified());
    assert(removes == 1);
    // A pre-call failure leaves intent but cannot authorize deleting a still-bound package.
    reset();
    UnboundStore uncertain;
    init(uncertain);
    assert(uncertain.capture_before());
    desired = publish();
    assert(uncertain.capture_published(2, device, desired));
    fake_win32::mutation = 0;
    fake_win32::fail = 4;
    assert(!uncertain.remove(2, device));
    fake_win32::fail = 0;
    assert(!uncertain.reconcile_removed());
    assert(!removes);
    // An owned INF authenticates its OS-regenerated cache; borrowed caches above
    // are preserved, while an unrelated INF never authorizes cache deletion.
    reset();
    UnboundStore replaced;
    init(replaced);
    assert(replaced.capture_before());
    desired = publish();
    assert(replaced.capture_published(2, device, desired));
    put("C:\\WINDOWS\\INF\\OEM1.PNF", "Windows regenerated owned INF cache");
    assert(replaced.remove(2, device));
    assert(replaced.original_verified());
    assert(!fake_win32::files.contains("c:\\windows\\inf\\oem1.pnf"));
    reset();
    UnboundStore foreign_inf;
    init(foreign_inf);
    assert(foreign_inf.capture_before());
    desired = publish();
    assert(foreign_inf.capture_published(2, device, desired));
    put("C:\\WINDOWS\\INF\\OEM1.INF", "foreign replacement INF");
    put("C:\\WINDOWS\\INF\\OEM1.PNF", "its unrelated cache");
    assert(!foreign_inf.remove(2, device));
    assert(!removes && fake_win32::files.contains("c:\\windows\\inf\\oem1.pnf"));
    // Without an existing exact INF anchor, changed cache bytes cannot be
    // attributed to this package after an interrupted remove operation.
    reset();
    UnboundStore missing_anchor;
    init(missing_anchor);
    assert(missing_anchor.capture_before());
    desired = publish();
    assert(missing_anchor.capture_published(2, device, desired));
    lost_response = true;
    assert(!missing_anchor.remove(2, device) && removes == 1);
    fake_win32::files.erase("c:\\windows\\inf\\oem1.inf");
    put("C:\\WINDOWS\\INF\\OEM1.PNF", "unattributable cache after INF removal");
    assert(!missing_anchor.reconcile_removed());
    assert(removes == 1 && fake_win32::files.contains("c:\\windows\\inf\\oem1.pnf"));
    reset();
    UnboundStore reuse;
    init(reuse);
    assert(reuse.capture_before());
    desired = publish();
    assert(reuse.capture_published(2, device, desired));
    assert(reuse.remove(2, device));
    put("C:\\WINDOWS\\INF\\OEM1.INF", "new unrelated publication");
    assert(!reuse.reconcile_removed());
    assert(!reuse.original_verified());
    assert(fake_win32::handles.empty() && fake_win32::key_handles.empty());
    puts("PASS actual unbound OEM ownership: baseline, compatible/phantom conflicts, exact "
         "publication, scoped removal, interrupted recovery");
}
