// SPDX-License-Identifier: GPL-2.0-or-later
#include "policy.h"
#include "sha256.h"
#include <cassert>
#include <initializer_list>
#include <cstring>
struct Store {
    bool occupied = false, owned = false, published = false, fail_commit = false,
         fail_rollback = false;
    unsigned removed = 0;
    bool begin() {
        if (occupied)
            return false;
        owned = true;
        return true;
    }
    bool commit() {
        if (fail_commit)
            return false;
        published = true;
        return true;
    }
    bool rollback() {
        if (fail_rollback)
            return false;
        assert(owned);
        owned = false;
        removed++;
        return true;
    }
};
int main() {
    using setup::Os;
    assert(setup::select_os(1, 4, 10) == Os::win98);
    assert(setup::select_os(2, 5, 0) == Os::nt5 && setup::select_os(2, 5, 1) == Os::nt5);
    assert(setup::select_os(1, 4, 90) == Os::unsupported &&
           setup::select_os(2, 6, 0) == Os::unsupported);
    assert(setup::pci("PCI\\VEN_1234&DEV_1113&REV_01", 28));
    assert(!setup::pci("PCI\\VEN_1234&DEV_11130", 22));
    assert(!setup::pci("PCI\\VEN_1234&DEV_1113", 3));
    for (auto p : {"../x", "/absolute", "C:/x", "a//b", "a/../b", "a./b", "a/", "a\\b"})
        assert(!setup::safe_path(p));
    assert(setup::safe_path("application/dgpugl.dll"));
    {
        Store s;
        s.occupied = true;
        {
            setup::Transaction tx(s);
            assert(!tx.begin());
        }
        assert(s.removed == 0);
    }
    {
        Store s;
        {
            setup::Transaction tx(s);
            assert(tx.begin());
        }
        assert(s.removed == 1);
    }
    {
        Store s;
        s.fail_commit = true;
        {
            setup::Transaction tx(s);
            assert(tx.begin());
            assert(!tx.commit());
            assert(tx.rollback());
        }
        assert(s.removed == 1);
    }
    {
        Store s;
        {
            setup::Transaction tx(s);
            assert(tx.begin());
            assert(tx.commit());
        }
        assert(s.removed == 0 && s.published);
    }
    {
        Store s;
        {
            setup::Transaction tx(s);
            assert(tx.begin());
            s.fail_rollback = true;
            assert(!tx.rollback());
            s.fail_rollback = false;
        }
        assert(s.removed == 1);
    }
    char out[65];
    setup::Sha256 empty;
    empty.finish(out);
    assert(!strcmp(out, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"));
    setup::Sha256 abc;
    abc.update((const uint8_t *)"abc", 3);
    abc.finish(out);
    assert(!strcmp(out, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
    setup::Sha256 million;
    for (unsigned i = 0; i < 1000000; i++)
        million.update((const uint8_t *)"a", 1);
    million.finish(out);
    assert(!strcmp(out, "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0"));
}
