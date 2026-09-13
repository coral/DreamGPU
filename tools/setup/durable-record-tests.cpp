// SPDX-License-Identifier: GPL-2.0-or-later
#define DG_SETUP_ADAPTER_TEST
#include "durable-record.h"
#include <cassert>
struct Record {
    uint32_t version = 1, value = 0;
};
int main() {
    using namespace setup;
    using namespace fake_win32;
    constexpr auto path = "C:\\owned\\record.bin";
    auto valid = [](const Record &r) { return r.version == 1; };
    reset();
    Record r{};
    bool exists = true;
    DurableRecord<Record> absent(path);
    assert(absent.load(r, exists, valid, true) && !exists);
    assert(!absent.save(r, valid) && files.empty());
    DurableRecord<Record> writer(path);
    assert(writer.load(r, exists, valid) && !exists);
    r.value = 42;
    assert(writer.save(r, valid));
    const auto full = files.at(canon(path)).bytes;
    files.at(canon(path)).bytes.push_back(123); // Interrupted final append.
    files.at(canon(path)).attributes = FILE_ATTRIBUTE_READONLY;
    auto writes = mutation;
    DurableRecord<Record> reader(path);
    assert(reader.load(r, exists, valid, true) && exists && r.value == 42);
    assert(!reader.save(r, valid));
    assert(mutation == writes && files.at(canon(path)).bytes.size() == full.size() + 1);
    files.at(canon(path)).attributes = FILE_ATTRIBUTE_NORMAL;
    DurableRecord<Record> recovery(path);
    assert(recovery.load(r, exists, valid) && exists);
    assert(files.at(canon(path)).bytes == full);
    r.value = 43;
    assert(recovery.save(r, valid));
    files.at(canon(path)).bytes[0] ^= 1;
    writes = mutation;
    DurableRecord<Record> corrupted(path);
    assert(!corrupted.load(r, exists, valid, true) && mutation == writes);
    puts("PASS durable records: readonly absence, immutable torn tail, no write-after-read, "
         "mutable recovery, corruption");
}
