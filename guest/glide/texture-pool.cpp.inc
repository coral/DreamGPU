// SPDX-License-Identifier: GPL-2.0-or-later
// Address/hash invalidation stays in TexDB::WipeRange. Retired records retain
// only private GL storage; they are never visible to texture lookup.
void TexDB::Retire(Record *r) {
    if (m_upload == r)
        m_upload = NULL;
    unsigned int bytes = 0;
    for (unsigned int i = 0; i < 9; ++i) {
        const Storage &s = r->storage[i];
        if (!s.valid)
            continue;
        // Conservative RGBA8 accounting for all supported Glide formats.
        if (s.width > 2048 || s.height > 2048) {
            delete r;
            return;
        }
        // Include a complete generated chain too. Explicit levels may then
        // over-count, which is intentional; SGIS generation is never hidden.
        unsigned int w = s.width, h = s.height;
        for (;;) {
            bytes += w * h * 4;
            if (w == 1 && h == 1)
                break;
            w = w > 1 ? w / 2 : 1;
            h = h > 1 ? h / 2 : 1;
        }
    }
    if (r->tex2Num || !bytes || m_poolCount >= 64 || bytes > 8 * 1024 * 1024 ||
        m_poolBytes > 8 * 1024 * 1024 - bytes) {
        delete r;
        return;
    }
    r->retainedBytes = bytes;
    r->next = m_pool;
    m_pool = r;
    ++m_poolCount;
    m_poolBytes += bytes;
}

TexDB::Record *TexDB::Acquire(GrTexInfo *info, bool two) {
    if (!two)
        for (Record **p = &m_pool; *p; p = &(*p)->next) {
            Record *r = *p;
            if (r->info.largeLod != info->largeLod || r->info.aspectRatio != info->aspectRatio ||
                r->info.format != info->format)
                continue;
            *p = r->next;
            --m_poolCount;
            m_poolBytes -= r->retainedBytes;
            r->next = NULL;
            return r;
        }
    return new Record(two);
}

void TexDB::ForgetStorage(GLuint name) {
    if (m_upload && m_upload->texNum == name)
        memset(m_upload->storage, 0, sizeof(m_upload->storage));
}

void TexDB::Upload(GLuint name, GLint level, GLint internal, GLsizei width, GLsizei height,
                   GLenum format, GLenum type, const void *data) {
    Record *r = m_upload;
    Storage *s = (r && r->texNum == name && !r->tex2Num && level >= 0 && level < 9)
                     ? &r->storage[level]
                     : NULL;
    if (s && s->valid && s->internal == internal && s->width == width && s->height == height &&
        data && width > 0 && height > 0) {
        glTexSubImage2D(GL_TEXTURE_2D, level, 0, 0, width, height, format, type, data);
        return;
    }
    glTexImage2D(GL_TEXTURE_2D, level, internal, width, height, 0, format, type, data);
    if (s) {
        s->valid = width > 0 && height > 0;
        s->internal = internal;
        s->width = width;
        s->height = height;
    }
}

void TexDB::Parameters(GLuint name, GLint wrapS, GLint wrapT, GLint min, GLint mag) {
    const GLenum pnames[4] = {GL_TEXTURE_WRAP_S, GL_TEXTURE_WRAP_T, GL_TEXTURE_MIN_FILTER,
                              GL_TEXTURE_MAG_FILTER};
    const GLint values[4] = {wrapS, wrapT, min, mag};
    Record *r = m_upload;
    int unit = (r && r->texNum == name) ? 0 : (r && r->tex2Num == name && name) ? 1 : -1;
    for (int i = 0; i < 4; ++i) {
        if (unit >= 0 && r->paramsValid[unit][i] && r->params[unit][i] == values[i])
            continue;
        glTexParameteri(GL_TEXTURE_2D, pnames[i], values[i]);
        if (unit >= 0) {
            r->paramsValid[unit][i] = true;
            r->params[unit][i] = values[i];
        }
    }
}
