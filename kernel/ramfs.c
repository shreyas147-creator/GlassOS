#include "ramfs.h"
#include "event.h"
#include "object.h"

static const uint8_t readme[] =
    "Glass RAM filesystem\n"
    "Use ls to list files, cat <path> to read text, and run <path> to load a module.\n";

static const uint8_t about_module[] =
    "# RAMFS shell module\n"
    "print Module loaded through the filesystem interface.\n";

static const uint8_t memory_module[] =
    "# Report allocator state\n"
    "meminfo\n";

static const uint8_t hello_program[] = {
    0xB8, 0x01, 0x00, 0x00, 0x00,       /* mov eax, 1 ; inspect */
    0xBF, 0x40, 0x00, 0x40, 0x00,       /* mov edi, 0x400040 */
    0xCD, 0x80,                         /* int 0x80 */
    0xB8, 0x02, 0x00, 0x00, 0x00,       /* mov eax, 2 ; yield */
    0xCD, 0x80,
    0xB8, 0x03, 0x00, 0x00, 0x00,       /* mov eax, 3 ; config_get */
    0xBF, 0x48, 0x00, 0x40, 0x00,       /* mov edi, 0x400048 */
    0xCD, 0x80,
    0xB8, 0x04, 0x00, 0x00, 0x00,       /* mov eax, 4 ; denied config_set */
    0xBF, 0x48, 0x00, 0x40, 0x00,       /* mov edi, 0x400048 */
    0xCD, 0x80,
    0x48, 0xA1, 0x00, 0x00, 0x60, 0x00, /* mov rax, [0x600000] */
    0x00, 0x00, 0x00, 0x00,
    0x90, 0x90, 0x90, 0x90, 0x90,
    0x90, 0x90, 0x90, 0x90, 0x90,
    0x90,
    's', 'y', 's', 't', 'e', 'm', 0x00, 0x00,
    '/', 'c', 'o', 'n', 'f', 'i', 'g', '/', 't', 'h', 'e', 'm', 'e', 0x00
};

static const struct ramfs_file files[] = {
    { "/README.TXT", readme, sizeof(readme) - 1, RAMFS_FILE_TEXT },
    { "/modules/ABOUT.MOD", about_module, sizeof(about_module) - 1, RAMFS_FILE_MODULE },
    { "/modules/MEMORY.MOD", memory_module, sizeof(memory_module) - 1, RAMFS_FILE_MODULE },
    { "/bin/hello", hello_program, sizeof(hello_program), RAMFS_FILE_EXECUTABLE }
};

int ramfs_init(void) {
    const struct kernel_object *ramfs = object_find("ramfs");
    uint32_t parent = ramfs == 0 ? 0 : ramfs->id;
    for (uint32_t i = 0; i < (uint32_t)(sizeof(files) / sizeof(files[0])); i++) {
        if (files[i].path == 0 || files[i].data == 0 || files[i].size == 0) {
            event_record("ramfs", "ramfs", "initialize", EVENT_FAULT, event_current(), "deny:invalid-file");
            return 0;
        }
        object_register(files[i].type == RAMFS_FILE_MODULE ? "module" :
                        (files[i].type == RAMFS_FILE_EXECUTABLE ? "executable" : "file"),
                        files[i].path, parent, "read-only", event_current());
    }
    event_record("ramfs", "ramfs", "initialize", EVENT_OK, event_current(), "allow:boot-filesystem");
    return 1;
}

uint32_t ramfs_file_count(void) {
    return (uint32_t)(sizeof(files) / sizeof(files[0]));
}

const struct ramfs_file *ramfs_file_at(uint32_t index) {
    return index < ramfs_file_count() ? &files[index] : 0;
}

const struct ramfs_file *ramfs_lookup(const char *path) {
    for (uint32_t i = 0; i < ramfs_file_count(); i++) {
        const char *left = files[i].path;
        const char *right = path;
        while (*left != '\0' && *left == *right) {
            left++;
            right++;
        }
        if (*left == '\0' && *right == '\0') {
            event_record_kind("ramfs", path, "lookup", EVENT_OK, event_current(), "allow:filesystem:lookup", "state");
            return &files[i];
        }
    }
    event_record_kind("ramfs", path, "lookup", EVENT_DENIED, event_current(), "deny:not-found", "state");
    return 0;
}

uint64_t ramfs_read(const struct ramfs_file *file, uint64_t offset,
                    void *buffer, uint64_t buffer_size) {
    if (file == 0 || buffer == 0 || offset >= file->size) {
        return 0;
    }

    uint64_t available = file->size - offset;
    uint64_t bytes = buffer_size < available ? buffer_size : available;
    uint8_t *destination = (uint8_t *)buffer;
    for (uint64_t i = 0; i < bytes; i++) {
        destination[i] = file->data[offset + i];
    }
    return bytes;
}
