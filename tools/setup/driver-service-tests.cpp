// SPDX-License-Identifier: GPL-2.0-or-later
#include "driver-scm-test-win32.h"
#include "driver-service.h"
using setup::driver::ServiceStore;
static constexpr const char *Id = "PCI\\VEN_1234&DEV_1113&SUBSYS_00000000\\1";
static void reset() {
    fake_win32::reset();
    devices = {{Id, "oem1.inf", false, true}};
    exists_service = marked = malformed_config = marked_openable = false;
    service_state = SERVICE_RUNNING;
    service_start = SERVICE_SYSTEM_START;
    deletes = 0;
    binary = "\\SystemRoot\\System32\\drivers\\dgpumini.sys";
}
static void init(ServiceStore &s) {
    assert(s.init("C:\\WINDOWS\\DreamGPU", "C:\\WINDOWS\\SYSTEM32", Id));
}
static void publish() {
    exists_service = true;
    fake_win32::keys[ServiceKey] = {};
}
static void reboot() {
    assert(marked);
    exists_service = false;
    fake_win32::keys.erase(ServiceKey);
}
int main() {
    {
        reset();
        ServiceStore s;
        init(s);
        assert(s.capture_before());
        publish();
        assert(s.capture_published());
        service_start = SERVICE_DISABLED;
        assert(!s.remove(true)); // Disabled is not an allowed publication change.
        service_start = SERVICE_SYSTEM_START;
        marked_openable = true; // Actual XP marked service remains queryable.
        assert(s.remove(true));
        assert(marked && service_start == SERVICE_DISABLED && deletes == 1);
        assert(s.remove(true) && deletes == 1);
        binary = "C:\\foreign.sys";
        assert(!s.remove(true));
        binary = "\\SystemRoot\\System32\\drivers\\dgpumini.sys";
        reboot();
        assert(s.original_verified());
    }
    {
        reset();
        ServiceStore s;
        init(s);
        assert(s.capture_before());
        publish();
        service_start = SERVICE_DEMAND_START;
        binary = "system32\\DRIVERS\\dgpumini.sys";
        assert(s.capture_published());
        service_start = SERVICE_SYSTEM_START;
        assert(!s.remove(true)); // Captured configuration remains immutable.
        service_start = SERVICE_DEMAND_START;
        assert(s.remove(true));
        reboot();
        assert(s.original_verified());
    }
    {
        reset();
        ServiceStore s;
        init(s);
        assert(s.capture_before());
        publish();
        assert(s.capture_published());
        exists_service = false;
        fake_win32::keys.erase(ServiceKey);
        assert(!s.remove(false));
        assert(s.remove(true));
        assert(s.original_verified() && deletes == 0);
    }
    {
        reset();
        ServiceStore s;
        init(s);
        assert(s.capture_before());
        publish();
        assert(s.capture_published());
        assert(!s.remove(false));
        assert(!deletes);
        assert(s.remove(true));
        assert(deletes == 1 && marked && service_state == SERVICE_RUNNING);
        assert(s.removal_pending());
        assert(!s.original_verified());
        assert(s.remove(true));
        assert(deletes == 1);
        reboot();
        assert(s.original_verified());
        assert(!s.removal_pending());
    }
    {
        reset();
        publish();
        ServiceStore s;
        init(s);
        assert(!s.capture_before());
        assert(!fake_win32::files.contains("c:\\windows\\dreamgpu\\service.bin"));
    }
    {
        reset();
        fake_win32::keys[ServiceKey] = {};
        ServiceStore s;
        init(s);
        assert(!s.capture_before());
    }
    for (bool present : {false, true}) {
        reset();
        ServiceStore s;
        init(s);
        assert(s.capture_before());
        publish();
        assert(s.capture_published());
        devices.push_back({"OTHER\\DEVICE", "other.inf", true, present});
        assert(!s.remove(true));
        assert(!deletes);
    }
    {
        reset();
        ServiceStore s;
        init(s);
        assert(s.capture_before());
        publish();
        assert(s.capture_published());
        binary = "C:\\foreign.sys";
        assert(!s.remove(true));
        assert(!deletes);
    }
    {
        reset();
        ServiceStore s;
        init(s);
        assert(s.capture_before());
        publish();
        malformed_config = true;
        assert(!s.capture_published());
        assert(!deletes);
    }
    for (unsigned fail = 1; fail <= 5; ++fail) {
        reset();
        ServiceStore s;
        init(s);
        assert(s.capture_before());
        publish();
        assert(s.capture_published());
        fake_win32::mutation = 0;
        fake_win32::fail = fail;
        (void)s.remove(true);
        fake_win32::fail = 0;
        ServiceStore recovered;
        init(recovered);
        assert(recovered.remove(true));
        assert(marked && deletes == 1);
        reboot();
        assert(recovered.original_verified());
    }
    {
        reset();
        ServiceStore s;
        init(s);
        assert(s.capture_before());
        publish();
        assert(s.capture_published());
        service_state = SERVICE_STOPPED;
        assert(s.remove(true));
        assert(!exists_service);
        assert(s.original_verified());
    }
    assert(!manager_handles && !service_handles && fake_win32::handles.empty() &&
           fake_win32::key_handles.empty());
    puts("PASS actual owned service lifecycle: absent-before, exact config, phantom users, running "
         "deletion pending, reboot absence, failure recovery");
}
