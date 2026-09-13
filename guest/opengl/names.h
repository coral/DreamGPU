/* SPDX-License-Identifier: GPL-2.0-or-later
 * Bounded shared texture namespace. Zero names are empty slots; a nonzero
 * target on a zero name is a tombstone. Generated names have target zero
 * until first binding. Callers hold the frontend namespace lock.
 */
#define JGL_NAME_SLOTS (DG_GL_MAX_TEXTURES * 2)
#if JGL_NAME_SLOTS & (JGL_NAME_SLOTS - 1)
#error Texture namespace table requires a power-of-two capacity
#endif
typedef struct {
    GLuint Name;
    GLenum Target;
} JGL_NAME;
typedef struct {
    ULONG References, Count, Next;
    JGL_NAME Entries[JGL_NAME_SLOTS];
} JGL_NAMES;

static JGL_NAME *FindName(JGL_NAMES *names, GLuint name) {
    ULONG slot = (name * 2654435761u) & (JGL_NAME_SLOTS - 1), i;
    JGL_NAME *empty = NULL;
    for (i = 0; i < JGL_NAME_SLOTS; ++i) {
        JGL_NAME *entry = &names->Entries[slot];
        if (entry->Name == name)
            return entry;
        if (!entry->Name) {
            if (!empty)
                empty = entry;
            if (!entry->Target)
                return empty;
        }
        slot = (slot + 1) & (JGL_NAME_SLOTS - 1);
    }
    return empty; /* Repeated deletion can leave only tombstones. */
}
