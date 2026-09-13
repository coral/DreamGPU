// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "win32-lifecycle.h"
#include "plan.h"
#include "durable-record.h"
namespace setup::lifecycle {
// The prior startup value and installer identity are persisted before arming.
// Both OS families use Run: recreating RunOnce from a pending/error callback
// can keep startup executing that callback before Explorer. Historical receipts
// retain their exact key meaning for retirement; they are never relabelled.
enum class ResumeScope { runtime, global, recovery };
class RuntimeResume {
    const char *key_path_ = nullptr;
    uint32_t record_version_ = 1;
    Os os_ = Os::unsupported;
    const char *value_name_ = "DreamGPU.Runtime";
    struct Record {
        uint32_t magic = 0x52474744, version = 1, generation = 0;
        Image before{}, installer{};
    } record_;
    Win32Store &store_;
    char installer_[MAX_PATH]{}, record_path_[MAX_PATH]{};
    Image desired_{};
    bool ready_ = false;
    struct Key {
        HKEY handle = 0;
        Key() = default;
        Key(const Key &) = delete;
        ~Key() {
            if (handle)
                RegCloseKey(handle);
        }
    };
    bool read(HKEY key, Image &image) const {
        image = {};
        DWORD size = sizeof(image.value), type = 0;
        LONG status = RegQueryValueExA(key, value_name_, nullptr, &type, image.value, &size);
        if (status == ERROR_FILE_NOT_FOUND)
            return true;
        if (status != ERROR_SUCCESS || size > sizeof(image.value))
            return false;
        image.exists = 1;
        image.type = type;
        image.size = size;
        Sha256 h;
        h.update(image.value, size);
        h.finish(image.sha);
        return valid_image(image, Kind::registry);
    }
    bool installer_matches() const {
        Image current;
        return store_.inspect_file(installer_, current) && same(current, record_.installer);
    }
    bool validate(const Record &r) const {
        return r.magic == 0x52474744 &&
               (r.version == record_version_ || (os_ == Os::nt5 && r.version == 1)) &&
               r.generation == record_.generation && valid_image(r.before, Kind::registry) &&
               r.installer.exists && valid_image(r.installer, Kind::file);
    }

  public:
    RuntimeResume(Win32Store &store, uint32_t generation, Os os,
                  ResumeScope scope = ResumeScope::runtime)
        : store_(store) {
        if (os != Os::win98 && os != Os::nt5)
            return;
        os_ = os;
        key_path_ = "Software\\Microsoft\\Windows\\CurrentVersion\\Run";
        // Same durable layout; the version binds the captured baseline to its
        // registry key. A legacy Win98 receipt cannot be reinterpreted as Run.
        record_.version = record_version_ = os == Os::win98 ? 2 : 3;
        value_name_ = scope == ResumeScope::global     ? "DreamGPU.Setup"
                      : scope == ResumeScope::recovery ? "DreamGPU.Recovery"
                                                       : "DreamGPU.Runtime";
        record_.generation = generation;
        char suffix[64];
        wsprintfA(suffix,
                  scope == ResumeScope::global     ? "\\G%08lX\\RESUME.JRN"
                  : scope == ResumeScope::recovery ? "\\R%08lX\\RESUME.JRN"
                                                   : "\\T%08lX\\RESUME.JRN",
                  generation);
        if (!store_.private_path(record_path_, suffix))
            return;
        wsprintfA(suffix,
                  scope == ResumeScope::global     ? "\\G%08lX\\setup.exe"
                  : scope == ResumeScope::recovery ? "\\R%08lX\\setup.exe"
                                                   : "\\T%08lX\\setup.exe",
                  generation);
        if (!store_.private_path(installer_, suffix))
            return;
        char command[256] = "\"";
        if (lstrlenA(installer_) > 200)
            return;
        lstrcatA(command, installer_);
        lstrcatA(command, "\" /continue /silent");
        desired_ = string_value(command);
        ready_ = true;
    }
    bool prepare() {
        if (!ready_)
            return false;
        DurableRecord<Record> log(record_path_);
        Record loaded;
        bool exists;
        uint32_t observed = 0;
        if (!log.load(loaded, exists, [this, &observed](const auto &r) {
                if (!validate(r) || (observed && observed != r.version))
                    return false;
                observed = r.version;
                return true;
            }))
            return false;
        if (exists) {
            record_ = loaded;
            if (loaded.version == 1)
                key_path_ = "Software\\Microsoft\\Windows\\CurrentVersion\\RunOnce";
            if (installer_matches())
                return true;
        }
        char self[MAX_PATH];
        DWORD length = GetModuleFileNameA(nullptr, self, sizeof(self));
        Image executing;
        if (!length || length >= sizeof(self) || !store_.inspect_file(self, executing) ||
            !executing.exists)
            return false;
        if (!exists) {
            Key key;
            if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, key_path_, 0, KEY_QUERY_VALUE, &key.handle) !=
                    ERROR_SUCCESS ||
                !read(key.handle, record_.before))
                return false;
            record_.installer = executing;
            // Complete intent is durable before copying the owned executable.
            if (!log.save(record_, [this](const auto &r) { return validate(r); }))
                return false;
        }
        return same(executing, record_.installer) &&
               store_.retain_program(self, installer_, record_.installer);
    }
    bool armed() {
        if (!ready_)
            return false;
        DurableRecord<Record> log(record_path_);
        Record loaded;
        bool exists;
        uint32_t observed = 0;
        if (!log.load(
                loaded, exists,
                [this, &observed](const auto &r) {
                    if (!validate(r) || (observed && observed != r.version))
                        return false;
                    observed = r.version;
                    return true;
                },
                true) ||
            !exists)
            return false;
        record_ = loaded;
        if (record_.version == 1)
            key_path_ = "Software\\Microsoft\\Windows\\CurrentVersion\\RunOnce";
        Key key;
        Image current;
        return installer_matches() &&
               RegOpenKeyExA(HKEY_LOCAL_MACHINE, key_path_, 0, KEY_QUERY_VALUE, &key.handle) ==
                   ERROR_SUCCESS &&
               read(key.handle, current) && same(current, desired_);
    }
    bool arm() {
        // Legacy RunOnce is readable/retirable, never re-created by new code.
        // A changed executor must use authenticated recovery with its own Run receipt.
        if (record_.version == 1)
            return false;
        if (!ready_ || !validate(record_) || !installer_matches())
            return false;
        Key key;
        Image current;
        if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, key_path_, 0, KEY_QUERY_VALUE | KEY_SET_VALUE,
                          &key.handle) != ERROR_SUCCESS ||
            !read(key.handle, current) ||
            (current.exists && !same(current, record_.before) && !same(current, desired_)))
            return false;
        if (!same(current, desired_) &&
            RegSetValueExA(key.handle, value_name_, 0, REG_SZ, desired_.value, desired_.size) !=
                ERROR_SUCCESS)
            return false;
        return read(key.handle, current) && same(current, desired_) &&
               RegFlushKey(key.handle) == ERROR_SUCCESS;
    }
    bool finish() {
        if (!ready_ || !validate(record_))
            return false;
        Key key;
        Image current;
        if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, key_path_, 0, KEY_QUERY_VALUE | KEY_SET_VALUE,
                          &key.handle) != ERROR_SUCCESS ||
            !read(key.handle, current))
            return false;
        if (!same(current, record_.before)) {
            if (current.exists && !same(current, desired_))
                return false;
            LONG status = record_.before.exists
                              ? RegSetValueExA(key.handle, value_name_, 0, record_.before.type,
                                               record_.before.value, record_.before.size)
                              : RegDeleteValueA(key.handle, value_name_);
            if (status != ERROR_SUCCESS &&
                !(status == ERROR_FILE_NOT_FOUND && !record_.before.exists))
                return false;
        }
        return read(key.handle, current) && same(current, record_.before) &&
               RegFlushKey(key.handle) == ERROR_SUCCESS;
    }
};
} // namespace setup::lifecycle
