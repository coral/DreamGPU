// SPDX-License-Identifier: GPL-2.0-or-later
// Included by the actual Win32Store lifecycle sanitizer test.
static void setup_legacy_win98() {
    setup_case(Os::win98, false);
    driver_fake::node_changed = [](const Node &node) {
        const bool desired = !strcmp(node.provider, "DreamGPU");
        const char *drv = desired ? "dgpumini.drv" : "qemumini.drv";
        const char *mini = desired ? "dgpumini.vxd" : "qemumini.vxd";
        auto &key = fake_win32::keys["default"];
        key["drv"] = {REG_SZ, {drv, drv + strlen(drv)}};
        key["minivdd"] = {REG_SZ, {mini, mini + strlen(mini)}};
    };
    Node original = driver_fake::current;
    strcpy(original.inf, "C:\\WINDOWS\\INF\\OTHER\\JUKEJR~1.INF");
    strcpy(original.provider, "Juke");
    strcpy(original.section, "Jrg");
    strcpy(original.description, "Juke Retro GPU");
    file(original.inf, "legacy exact compatible INF");
    strcpy(original.inf_sha, digest(fake_win32::files[fake_win32::canon(original.inf)].bytes).c_str());
    driver_fake::compatible[0] = original;
    driver_fake::set_node(original);
    for (const char *name : {"infpath", "infsection", "driverdesc", "providername"})
        fake_win32::keys["driver"][name].bytes.pop_back();
    file("C:\\WINDOWS\\SYSTEM\\qemumini.drv", "legacy display before-image");
    file("C:\\WINDOWS\\SYSTEM\\qemumini.vxd", "legacy miniport before-image");
    fake_win32::files[fake_win32::canon("C:\\WINDOWS\\SYSTEM\\qemumini.drv")].attributes =
        FILE_ATTRIBUTE_READONLY | 32;
}

static unsigned legacy_failure(unsigned stage, unsigned failure) {
    setup_legacy_win98();
    auto original = fake_win32::files;
    Win32Store store;
    Journal j;
    assert(store.init(Os::win98, root));
    if (stage == 0) {
        fake_win32::mutation = 0;
        fake_win32::fail = failure;
    }
    bool captured = store.capture(j, payloads);
    unsigned calls = fake_win32::mutation;
    fake_win32::fail = 0;
    if (!captured) {
        assert(stage == 0);
        for (const auto &[path, before] : original)
            assert(fake_win32::files[path].bytes == before.bytes &&
                   fake_win32::files[path].attributes == before.attributes);
        assert(driver_fake::binds == 0 && driver_fake::launches == 0);
        if (!store.load(j))
            return calls; // No complete ownership record: no public writes occurred.
        if (j.phase == Phase::capturing)
            assert(store.complete_capture(j));
    }
    assert(j.version == 5 && j.original_binding == Binding::legacy_win98 && valid(j));
    assert(store.unchanged_originals(j));
    auto old = j;
    old.version = 3;
    assert(!valid(old));
    old = j;
    old.os = Os::nt5;
    assert(!valid(old));
    old = j;
    old.files[2].replaced = 1;
    assert(!valid(old));
    execution(j);
    Engine engine(store, j);
    if (stage == 1) {
        fake_win32::mutation = 0;
        fake_win32::fail = failure;
    }
    auto result = engine.install();
    if (stage == 1)
        calls = fake_win32::mutation;
    fake_win32::fail = 0;
    assert(store.load(j));
    result = engine.resume();
    if (result == Result::pending_reboot)
        result = engine.resume();
    assert(result == Result::verified);
    assert(store.verify_readonly(j, false));
    if (stage == 2) {
        fake_win32::mutation = 0;
        fake_win32::fail = failure;
    }
    result = engine.rollback();
    if (stage == 2)
        calls = fake_win32::mutation;
    fake_win32::fail = 0;
    // Cold continuation uses only recorded identities, not an in-memory plan.
    Win32Store cold;
    assert(cold.init(Os::win98, root) && cold.load(j));
    Engine recovery(cold, j);
    result = j.phase == Phase::verified ? recovery.rollback() : recovery.resume();
    if (result == Result::pending_reboot)
        result = recovery.resume();
    assert(result == Result::restored && cold.verify_readonly(j, true));
    for (unsigned n = 0; n < j.count; ++n) {
        const auto &f = j.files[n];
        const auto path = fake_win32::canon(f.path);
        if (!f.exists)
            assert(!fake_win32::files.count(path));
        else {
            assert(fake_win32::files[path].bytes == original[path].bytes);
            assert(fake_win32::files[path].attributes == original[path].attributes);
            assert(fake_win32::files[fake_win32::canon((std::string(root) + f.backup).c_str())].bytes ==
                   original[path].bytes);
        }
    }
    return calls;
}

static void legacy_inf_aliases() {
    constexpr const char *short_name = "C:\\WINDOWS\\INF\\OTHER\\JUKEJR~1.INF";
    constexpr const char *long_name = "C:\\WINDOWS\\INF\\OTHER\\Jukejrg9x.inf";
    for (unsigned scenario = 0; scenario < 6; ++scenario) {
        setup_legacy_win98();
        Node expected = driver_fake::current;
        driver_fake::short_paths[fake_win32::canon(long_name)] = short_name;
        driver_fake::detail_inf_override = long_name;
        if (scenario == 1) {
            strcpy(expected.inf, long_name);
            driver_fake::compatible[0] = expected;
            driver_fake::detail_inf_override = short_name;
        }
        if (scenario == 2) {
            // Identical bytes in a different INF are not the same file identity.
            driver_fake::detail_inf_override = "C:\\WINDOWS\\INF\\OTHER\\copy.inf";
            file(driver_fake::detail_inf_override, "legacy exact compatible INF");
        }
        if (scenario == 3)
            driver_fake::short_paths.clear(); // Unresolvable reported name.
        if (scenario == 4) {
            const std::string oversized(300, 'x');
            driver_fake::short_paths[fake_win32::canon(long_name)] = oversized;
            file(oversized, "invalid canonical path");
        }
        if (scenario == 5)
            driver_fake::compatible.push_back(expected); // Still exactly one node required.
        SP_DEVINFO_DATA device{};
        DriverList list(1, device);
        assert(list.select(expected, true) == (scenario < 2));
        assert(driver_fake::binds == 0 && driver_fake::launches == 0);
    }
}

static void legacy_win98_tests() {
    legacy_inf_aliases();
    unsigned failures = 0;
    for (unsigned stage = 0; stage < 3; ++stage) {
        unsigned count = legacy_failure(stage, 0);
        for (unsigned n = 1; n <= count; ++n) {
            legacy_failure(stage, n);
            ++failures;
        }
    }
    for (unsigned scenario = 0; scenario < 7; ++scenario) {
        setup_legacy_win98();
        if (scenario == 0)
            fake_win32::files.erase(fake_win32::canon("C:\\WINDOWS\\SYSTEM\\qemumini.vxd"));
        if (scenario == 1)
            file("C:\\WINDOWS\\SYSTEM\\dgpumini.drv", "foreign destination");
        if (scenario == 2)
            fake_win32::keys["default"]["minivdd"].bytes = {'x'};
        Win32Store store;
        Journal j;
        assert(store.init(Os::win98, root));
        if (scenario < 3) {
            assert(!store.capture(j, payloads));
            assert(driver_fake::binds == 0 && driver_fake::launches == 0);
            continue;
        }
        assert(store.capture(j, payloads));
        const char *changed[] = {
            "C:\\WINDOWS\\SYSTEM\\qemumini.drv",
            "C:\\WINDOWS\\DreamGPU\\driver-backup\\qemumini.vxd",
            "C:\\WINDOWS\\DreamGPU\\driver-backup\\original.inf"};
        if (scenario < 6)
            file(changed[scenario - 3], "foreign edit");
        else
            fake_win32::keys["default"]["minivdd"].bytes = {'x'};
        Engine engine(store, j);
        assert(engine.install() == Result::conflict);
        assert(driver_fake::binds == 0 && driver_fake::launches == 0);
    }
    printf("PASS legacy Win98 migration: exact pair, preserved before-images, cold rollback, %u mutation failures\n", failures);
}
