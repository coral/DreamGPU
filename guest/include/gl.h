/* SPDX-License-Identifier: GPL-2.0-or-later
 * DreamGPU-authored protocol. Canonical file: guest/include/gl.h;
 * mirrored into vendor/qemu/include/standard-headers/dreamgpu/gl.h.
 */
#ifndef DREAMGPU_GL_H
#define DREAMGPU_GL_H

#define DG_CAP_GL_TRANSPORT 0x100
#define DG_CAP_GL_FRONT_BUFFERS 0x200
#define DG_CAP_GL_PRESENT_BOUNDS 0x400
#define DG_CAP_GL_BULK_READBACK 0x800
#define DG_CAP_GL_DEPTH_STENCIL_READBACK 0x1000
/* Existing DATA records with complete scalar/proxy/empty image semantics. */
#define DG_CAP_GL_TEXTURE_IMAGES 0x2000
/* Supplied border texels participate in sampling, not just storage/readback. */
#define DG_CAP_GL_TEXTURE_BORDERS 0x4000
/* Fixed-function multitexture: ARB_multitexture with combine and dot3
 * environments. ActiveTexture (one word) and MultiTexCoord4f (target and four
 * float words; legal inside Begin/End and in display lists) are ordinary CALL
 * records. The host answers ACTIVE_TEXTURE and MAX_TEXTURE_UNITS. Bindings,
 * enables, environments, coordinate generation and the texture matrix follow
 * the active unit; TEXTURE_BIT push/pop covers every unit's bindings and the
 * active unit. Client arrays for units 1-7 use DrawArraysEXT, below. */
#define DG_CAP_GL_MULTITEXTURE 0x8000
#define DG_GL_MAX_TEXTURE_UNITS 8
/* Fixed-function state: BlendColor, BlendEquation(Separate), BlendFuncSeparate,
 * StencilFunc/OpSeparate (core GL 2.0 face form), PointParameterf(v),
 * TEXTURE_MAX_ANISOTROPY_EXT, TEXTURE_FILTER_CONTROL LOD bias and POINT_SPRITE
 * COORD_REPLACE, plus their state queries, as ordinary validated records. */
#define DG_CAP_GL_FIXED_STATE 0x10000
/* Texture control: ATI_texture_env_combine3 COMBINE modes, ATI_texture_mirror_once
 * wrap modes and SGIS_generate_mipmap (TexParameter GENERATE_MIPMAP, its hint and
 * queries). Values travel in existing records; native generation of levels below
 * a defined image is charged with that image. */
#define DG_CAP_GL_TEXTURE_CONTROL 0x20000
/* Compressed textures: ARB_texture_compression with EXT_texture_compression_s3tc.
 * CompressedTexImage2D (DATA, seven words: target, level, internalformat, width,
 * height, border, imageSize) carries either no payload (zero-defined storage) or
 * exactly imageSize bytes. CompressedTexSubImage2D (DATA, eight words: target,
 * level, xoffset, yoffset, width, height, format, imageSize) carries exactly
 * imageSize bytes. GetCompressedTexImage is a QUERY of target, level | words<<16,
 * first word, returning RESULT_INT words of the native compressed image (zero
 * words count keeps 128). TexImage2D additionally accepts the S3TC and generic
 * COMPRESSED_* internal formats. Only TEXTURE_2D (and its proxy) is compressed,
 * and cube map faces with DG_CAP_GL_TEXTURE_CUBE_MAP. */
#define DG_CAP_GL_COMPRESSED_TEXTURES 0x40000
#define DG_GL_COMPRESSED_BLOCK 4
/* Fog coordinates: EXT_fog_coord. FogCoordf (one float word) is an ordinary
 * CALL, legal inside Begin/End and display lists. FOG_COORDINATE_SOURCE is a
 * Fog parameter. DrawArraysEXT slot 14 (DG_GL_ARRAY_FOG_COORD) carries one FLOAT
 * or DOUBLE fog coordinate per vertex; the host answers FOG_COORDINATE_SOURCE
 * and CURRENT_FOG_COORDINATE. */
#define DG_CAP_GL_FOG_COORD 0x80000
/* Cube maps: ARB_texture_cube_map (EXT_texture_cube_map). TEXTURE_CUBE_MAP is a
 * BindTexture/Enable/TexParameter target (adding TEXTURE_WRAP_R) with its own
 * binding per texture unit, covered by TEXTURE_BIT; a name keeps the target of
 * its first bind. The six face targets replace TEXTURE_2D in the existing
 * TexImage2D/TexSubImage2D, CopyTexImage2D/CopyTexSubImage2D, S3TC compressed,
 * GetTexImage, GetCompressedTexImage and GetTexLevelParameter records, and
 * PROXY_TEXTURE_CUBE_MAP in their proxies. Face images are square, borderless
 * and use the 2D level limits; each face tracks its own levels. TexGen accepts
 * NORMAL_MAP and REFLECTION_MAP. The host answers TEXTURE_BINDING_CUBE_MAP and
 * MAX_CUBE_MAP_TEXTURE_SIZE (DG_GL_MAX_TEXTURE_DIMENSION). */
#define DG_CAP_GL_TEXTURE_CUBE_MAP 0x100000
/* Fragment programs: ARB_fragment_program, target FRAGMENT_PROGRAM_ARB only.
 * BindProgramARB (target, name) and ProgramEnvParameter4fARB/
 * ProgramLocalParameter4fARB (target, index, four float words) are CALL records
 * and compile into display lists. ProgramStringARB is DATA (target, format,
 * length) carrying exactly length bytes, or none for a nonzero length the
 * frontend cannot transfer (over DG_GL_MAX_PROGRAM_BYTES or its packet); it
 * compiles into lists. The host fails a load with INVALID_OPERATION, setting
 * the error position and string, for an untransferred string (position length),
 * a byte outside 1-127 or a texture image unit/coordinate set reference beyond
 * the reported limits (its offset), and parses into a private object first so a
 * rejected string never changes the bound program. DeleteProgramsARB is DATA
 * (count, at most DG_GL_MAX_PROGRAMS, then names) and executes immediately.
 * QUERY records: GenProgramsARB (count 1..DG_GL_PROGRAM_NAME_BATCH) returns that
 * many RESULT_INT names, all zero on failure; IsProgramARB (name) RESULT_BOOL;
 * GetProgramivARB (target, pname) one RESULT_INT; GetProgramEnvParameterfvARB
 * and GetProgramLocalParameterfvARB (target, index) four RESULT_FLOAT;
 * GetProgramStringARB (target, words, first word) RESULT_INT words of the string,
 * zero-padded; GetString(PROGRAM_ERROR_STRING_ARB) one NUL-terminated string of
 * printable ASCII (other bytes mapped), at most DG_GL_MAX_RESULT_BYTES with its
 * terminator. Enable/IsEnabled(FRAGMENT_PROGRAM_ARB), MATRIXi_ARB matrix
 * modes and the CURRENT_MATRIX_ARB, PROGRAM_ERROR_POSITION_ARB, program matrix
 * and MAX_TEXTURE_COORDS/IMAGE_UNITS_ARB state use existing records; the host
 * clamps coordinate sets and image units to its texture units. Program names and
 * objects, including the default object, share the texture namespace. */
#define DG_CAP_GL_FRAGMENT_PROGRAM 0x200000
#define DG_GL_MAX_PROGRAM_BYTES 0xff00
#define DG_GL_MAX_PROGRAMS 4096
#define DG_GL_PROGRAM_NAME_BATCH 128
/* Vertex programs: ARB_vertex_program. The program records above also accept
 * target VERTEX_PROGRAM_ARB (with either capability the program functions
 * exist): both targets share one namespace, a name keeps the target of its
 * first bind, and each target has its own binding and default object. A vertex
 * program load also fails on the host for a vertex.attrib[n] reference with n
 * at or beyond the reported MAX_VERTEX_ATTRIBS_ARB (position its offset).
 * PROGRAM_ERROR_POSITION/STRING belong to the last load of either target.
 * GetProgramivARB of the vertex target adds the address register names and
 * excludes the fragment-only ALU/TEX/indirection names. VertexAttrib4fARB
 * (index below DG_GL_MAX_VERTEX_ATTRIBS, four float words) is a CALL record,
 * legal inside Begin/End, where index 0 provokes a vertex like Vertex4f, and in
 * display lists. QUERY GetVertexAttribfvARB (index 1..DG_GL_MAX_VERTEX_ATTRIBS-1,
 * CURRENT_VERTEX_ATTRIB_ARB) returns four RESULT_FLOAT; attribute array state is
 * client state. Enable/IsEnabled of VERTEX_PROGRAM_ARB, VERTEX_PROGRAM_POINT_SIZE
 * and VERTEX_PROGRAM_TWO_SIDE (ENABLE_BIT) and MAX_VERTEX_ATTRIBS_ARB (at most
 * DG_GL_MAX_VERTEX_ATTRIBS) use existing records. Generic attribute arrays use
 * the attribute draw below. Generic and conventional attributes do not alias,
 * except that generic attribute 0 is the vertex position: its array supersedes
 * the vertex array and provokes drawing. */
#define DG_CAP_GL_VERTEX_PROGRAM 0x400000
#define DG_GL_MAX_VERTEX_ATTRIBS 16
/* 3D textures: EXT_texture3D with the TexSubImage3DEXT and CopyTexSubImage3DEXT
 * forms. TEXTURE_3D is a BindTexture/Enable/TexParameter target with its own
 * binding per texture unit, covered by TEXTURE_BIT; a name keeps the target of
 * its first bind. TEXTURE_WRAP_R is a TexParameter of every target. Records use
 * the core function numbers. TexImage3D is DATA with nine words (target, level,
 * internalformat, width, height, depth, border, format, type) carrying exactly
 * width*height*depth pixels, or none for zero-defined storage; PROXY_TEXTURE_3D
 * carries none. TexSubImage3D is DATA with ten words (target, level, xoffset,
 * yoffset, zoffset, width, height, depth, format, type) carrying exactly its
 * pixels. Pixels are tight rows, then images, in the TexImage2D wire formats
 * and types; the frontend resolves UNPACK_IMAGE_HEIGHT and UNPACK_SKIP_IMAGES.
 * CopyTexSubImage3D is a CALL of nine words (target, level, xoffset, yoffset,
 * zoffset, x, y, width, height). Images use the uncompressed internal formats,
 * each dimension at most DG_GL_MAX_3D_TEXTURE_SIZE >> level; a border of 1 is
 * GL_INVALID_OPERATION (its proxy is rejected). Offsets outside the defined
 * level are GL errors, not record faults. GetTexImage of TEXTURE_3D numbers
 * the first pixel in x, then y, then z order; GetTexLevelParameter accepts
 * TEXTURE_3D, PROXY_TEXTURE_3D and TEXTURE_DEPTH. The host answers
 * TEXTURE_BINDING_3D and MAX_3D_TEXTURE_SIZE (DG_GL_MAX_3D_TEXTURE_SIZE or a
 * smaller native limit). Residency charges every image of a level. */
#define DG_CAP_GL_TEXTURE_3D 0x800000
#define DG_GL_MAX_3D_TEXTURE_SIZE 256
/* Vertex buffer objects: ARB_vertex_buffer_object, targets ARRAY_BUFFER and
 * ELEMENT_ARRAY_BUFFER. Buffer names and objects share the texture namespace
 * (wglShareLists). Records name their buffers: bindings, array pointers and
 * mappings are frontend client state, and the host holds no binding.
 * BufferDataARB is DATA (name, size at most DG_GL_MAX_BUFFER_BYTES, usage:
 * any of the nine STREAM/STATIC/DYNAMIC DRAW/READ/COPY hints) carrying exactly
 * size bytes, or none for zero-defined storage; the first one creates the
 * object. BufferSubDataARB is DATA (name, offset) storing its payload at
 * offset. DeleteBuffersARB is DATA (count, at most DG_GL_MAX_BUFFERS, then
 * names). All three execute immediately and are never compiled into display
 * lists. A subdata range beyond the buffer (or an unknown name) is
 * GL_INVALID_VALUE; a buffer table or resource budget exhaustion is
 * GL_OUT_OF_MEMORY and leaves the buffer empty. Storage is charged against the
 * resource budget at its size.
 * The buffer draw is DATA function DrawRangeElements with ten scalar words:
 * mode, base, count, the DrawArraysEXT mask with ARRAY_RAW, the generic mask
 * (both as in the attribute draw), the buffer mask (bit s: descriptor slot s,
 * an enabled slot, reads a buffer), vertices, index type (zero for an array
 * draw, else UNSIGNED_BYTE, UNSIGNED_SHORT or UNSIGNED_INT), element buffer
 * (zero: inline indices) and element offset (a multiple of the index size;
 * zero without an element buffer). The payload has the attribute draw's
 * thirty-two descriptors under its rules, then one reference of
 * DG_GL_BUFFER_REFERENCE_BYTES (buffer name, nonzero; byte offset; stride at
 * most DG_GL_MAX_BUFFER_STRIDE, zero for tight; offset and stride multiples
 * of the component size) per buffer slot in slot order, then a client section
 * of `vertices` vertices for every other enabled slot under the RAW alignment
 * and padding rules, then any inline indices at their size alignment, then
 * zero padding to a word. Client sections hold vertices base..base+vertices-1.
 * An array draw draws vertices base..base+count-1, and vertices is count with
 * a client section, else zero. An indexed draw draws count indices; with a
 * client section vertices is 1..DG_GL_MAX_VERTICES and every index lies in
 * base..base+vertices-1, without one base and vertices are zero. Count is at
 * most DG_GL_MAX_BUFFER_ELEMENTS, and DG_GL_MAX_INDICES for inline indices;
 * base+count-1 and base+vertices-1 fit 32 bits. A missing buffer, an element
 * range beyond its buffer, an attribute read beyond its buffer or an
 * element-buffer index outside the client vertices is GL_INVALID_OPERATION
 * and draws nothing, as is a buffer draw received while compiling a display
 * list: the frontend dereferences buffers into client draws there, as the
 * extension requires. Native GL converts buffer attributes of every
 * descriptor type.
 * With this capability the frontend also sends indexed draws of client arrays
 * alone as buffer draws with a zero buffer mask: client sections of the
 * indexed range base..base+vertices-1 (each array once, at its wire type) and
 * inline indices, instead of one expanded vertex per index, whenever that is
 * no larger and fits one packet; the record and its admission are unchanged
 * (every inline index within the client vertices, else the batch fails, so
 * the frontend sends only indices it scanned). It also offers
 * EXT_compiled_vertex_array: LockArraysEXT/UnlockArraysEXT are frontend client
 * state only, never recorded, and a locked draw submits the records of the
 * same draw unlocked. Display lists keep capturing the expansion. */
#define DG_CAP_GL_VERTEX_BUFFER_OBJECT 0x1000000
#define DG_GL_MAX_BUFFERS 16384
#define DG_GL_MAX_BUFFER_BYTES 0x08000000
#define DG_GL_MAX_BUFFER_STRIDE 2048
#define DG_GL_MAX_BUFFER_ELEMENTS 0x01000000
#define DG_GL_BUFFER_REFERENCE_BYTES 12
/* Framebuffer objects: EXT_framebuffer_object with EXT_framebuffer_blit and
 * EXT_packed_depth_stencil. Records use the core function numbers. Framebuffer
 * names and objects belong to one context; renderbuffer names and objects share
 * the texture namespace (wglShareLists). Name zero is the context's window
 * framebuffer (its drawable), which keeps its own draw and read buffers; a
 * framebuffer object starts with COLOR_ATTACHMENT0 for both. The frontend
 * reserves names (Gen) and answers Is locally; an object exists from its first
 * bind. Every command below executes immediately and is never compiled into a
 * display list, except BlitFramebuffer, which compiles. None is legal inside
 * Begin/End.
 * CALL records: BindFramebuffer (target, name) with target FRAMEBUFFER (both
 * bindings), DRAW_FRAMEBUFFER or READ_FRAMEBUFFER; BindRenderbuffer
 * (RENDERBUFFER, name); RenderbufferStorage (RENDERBUFFER, internalformat,
 * width, height, each at most DG_GL_MAX_RENDERBUFFER_SIZE) with RGB, RGBA,
 * R3_G3_B2, RGB4..RGB16, RGBA2..RGBA16 (0x804f-0x805b), DEPTH_COMPONENT(16/24/
 * 32), STENCIL_INDEX(1/4/8/16), DEPTH_STENCIL or DEPTH24_STENCIL8, whose
 * contents are zero-defined; FramebufferTexture1D/2D (target, attachment,
 * textarget, texture, level) and FramebufferTexture3D (the same and zoffset,
 * below DG_GL_MAX_3D_TEXTURE_SIZE), textarget TEXTURE_1D, TEXTURE_2D or a
 * cube map face, and TEXTURE_3D respectively, level at most
 * DG_GL_MAX_TEXTURE_LEVEL; FramebufferRenderbuffer (target, attachment,
 * RENDERBUFFER, renderbuffer); GenerateMipmap (TEXTURE_1D, TEXTURE_2D,
 * TEXTURE_CUBE_MAP or TEXTURE_3D: the active unit's binding); BlitFramebuffer
 * (srcX0, srcY0, srcX1, srcY1, dstX0, dstY0, dstX1, dstY1 as signed words, mask
 * of COLOR/DEPTH/STENCIL_BUFFER_BIT, filter NEAREST or LINEAR, LINEAR only for
 * colour alone). Attachments are COLOR_ATTACHMENT0..DG_GL_MAX_COLOR_ATTACHMENTS-1,
 * DEPTH_ATTACHMENT and STENCIL_ATTACHMENT; texture or renderbuffer zero
 * detaches. DATA records: DeleteFramebuffers (count, at most
 * DG_GL_MAX_FRAMEBUFFERS, then names) and DeleteRenderbuffers (count, at most
 * DG_GL_MAX_RENDERBUFFERS, then names); zero and unknown names are ignored.
 * QUERY records, one RESULT_INT each: CheckFramebufferStatus (target);
 * GetFramebufferAttachmentParameteriv (target, attachment, pname OBJECT_TYPE,
 * OBJECT_NAME, TEXTURE_LEVEL, TEXTURE_CUBE_MAP_FACE or TEXTURE_3D_ZOFFSET);
 * GetRenderbufferParameteriv (RENDERBUFFER, pname WIDTH, HEIGHT,
 * INTERNAL_FORMAT or RED..STENCIL_SIZE). Get*v answers FRAMEBUFFER_BINDING
 * (the draw binding), READ_FRAMEBUFFER_BINDING, RENDERBUFFER_BINDING,
 * MAX_COLOR_ATTACHMENTS and MAX_RENDERBUFFER_SIZE (the protocol limits or
 * smaller native ones). DrawBuffer and ReadBuffer also take
 * COLOR_ATTACHMENTn, and ReadBuffer NONE.
 * Everything that depends on GL state is a GL error, never a record fault: an
 * attachment command, a query of an attachment or a buffer name the bound
 * framebuffer cannot take (COLOR_ATTACHMENTn for the window, a window buffer
 * for an object) is INVALID_OPERATION, as are a nonzero texture that is not a
 * texture object of textarget's target, a renderbuffer that is not an object,
 * RenderbufferStorage or a renderbuffer query without a bound renderbuffer and
 * GenerateMipmap of a compressed or bordered level; a level beyond its
 * target's limit or a renderbuffer beyond the native size is INVALID_VALUE;
 * table or budget exhaustion is OUT_OF_MEMORY, leaving a binding unchanged and
 * a renderbuffer without storage. Completeness is the native GL's: attachments of
 * different sizes or colour formats may be complete, so INCOMPLETE_DIMENSIONS
 * and INCOMPLETE_FORMATS are not reported. Drawing, clearing, reading or
 * blitting with an incomplete framebuffer is INVALID_FRAMEBUFFER_OPERATION
 * (0x0506), as is any read (ReadPixels, CopyPixels, CopyTex*, a blit source) of
 * a texture level the host could not initialize. Deleting a bound framebuffer
 * rebinds the window; deleting a bound or attached renderbuffer or texture
 * unbinds it and detaches it from this context's bound framebuffers only, and
 * an object attached elsewhere lives until detached. Rendering into a texture
 * that is also sampled is undefined but harmless. Renderbuffer storage is
 * charged against the resource budget.
 * EXT_packed_depth_stencil and the depth attachments: with this capability
 * TexImage2D/TexSubImage2D and CopyTexImage2D/CopyTexSubImage2D of TEXTURE_2D
 * (not 1D, cube map or 3D) also take the internal formats DEPTH_COMPONENT(16/24/
 * 32), DEPTH_STENCIL and DEPTH24_STENCIL8 without a border; images carry format
 * DEPTH_COMPONENT with BYTE..FLOAT (one component) or DEPTH_STENCIL with
 * UNSIGNED_INT_24_8 (four bytes), as does DrawPixels. ARB_depth_texture is not
 * claimed by this capability (DG_CAP2_GL_DEPTH_TEXTURE claims it, below): GetTexImage
 * of a depth level is not supported without it. ReadPixels DEPTH and
 * STENCIL modes read a framebuffer object's depth and stencil attachments.
 * With a framebuffer object bound for reading, a ReadPixels rectangle need not
 * fit the drawable: x and y stay below DG_GL_MAX_DIMENSION, the host reads the
 * part inside every attachment and returns zero elsewhere, and an
 * INVALID_FRAMEBUFFER_OPERATION read returns zeros. The scalar PointParameterf
 * record takes POINT_SPRITE_COORD_ORIGIN (GL 2.0) to native GL as well; a
 * renderer drawing upside down into objects keeps point sprites upright with
 * it, and objects and the window share one native orientation. */
#define DG_CAP_GL_FRAMEBUFFER_OBJECT 0x2000000
#define DG_GL_MAX_FRAMEBUFFERS 1024
#define DG_GL_MAX_RENDERBUFFERS 4096
#define DG_GL_MAX_COLOR_ATTACHMENTS 4
#define DG_GL_MAX_RENDERBUFFER_SIZE 4096
/* BGRA vertex arrays: ARB_vertex_array_bgra, size BGRA (UNSIGNED_BYTE only)
 * for colour, secondary colour and generic attribute arrays. The frontend
 * reorders client memory into the RGBA sections of the existing array records,
 * so their layouts and admission are unchanged. A draw that reads a BGRA array
 * from a buffer object uses the BGRA buffer draw, DATA function
 * DrawRangeElementsEXT: the buffer draw's ten scalar words, payload and rules,
 * except that a buffer slot's descriptor may add DG_GL_ARRAY_BGRA on the
 * colour (slot 1), secondary colour (slot 4) and generic attribute slots
 * (16-31) as exactly UNSIGNED_BYTE with four components, plus
 * DG_GL_ATTRIB_NORMALIZED (required) for a generic attribute. Native GL reads
 * that buffer slot with size BGRA; its element is four bytes for every bound.
 * Client-section descriptors never carry the flag. A distinct function keeps
 * the buffer draw admission of older kernels and hosts unchanged. */
#define DG_CAP_GL_VERTEX_ARRAY_BGRA 0x4000000
#define DG_GL_ARRAY_BGRA 0x02000000U
/* Occlusion queries and fences: ARB_occlusion_query (target SAMPLES_PASSED)
 * and NV_fence (condition ALL_COMPLETED_NV). Query and fence names and objects
 * belong to one context and the host owns them: a query object exists from its
 * first BeginQuery, a fence from its first SetFenceNV, of any nonzero name.
 * Query records use the core function numbers, fence records the NV ones. None
 * is legal inside Begin/End.
 * CALL records, compiled into display lists as both extensions require:
 * BeginQuery (SAMPLES_PASSED, id), EndQuery (SAMPLES_PASSED) and SetFenceNV
 * (fence, ALL_COMPLETED_NV). DATA records, executed immediately: DeleteQueries
 * and DeleteFencesNV (count, at most DG_GL_MAX_QUERIES, then names; zero and
 * unknown names are ignored). QUERY records: GenQueries and GenFencesNV (count
 * 1..DG_GL_QUERY_NAME_BATCH) return that many unused RESULT_INT names, all zero
 * on failure; IsQuery and IsFenceNV (name) one RESULT_BOOL; GetQueryiv
 * (SAMPLES_PASSED, CURRENT_QUERY or QUERY_COUNTER_BITS) one RESULT_INT, at most
 * 32 bits; TestFenceNV and FinishFenceNV (fence), and GetFenceivNV (fence,
 * FENCE_STATUS_NV or FENCE_CONDITION_NV), RESULT_INT one, then the status
 * (FinishFenceNV after waiting) or the condition.
 * GetQueryObjectuiv (id, QUERY_RESULT or QUERY_RESULT_AVAILABLE, reports at most
 * DG_GL_QUERY_REPORTS) serves both pnames and both signednesses with RESULT_INT
 * words: a state (DG_GL_QUERY_PENDING or DG_GL_QUERY_AVAILABLE; QUERY_RESULT
 * waits for the result), the result (zero while pending; saturated at
 * 2^32-1), then at most `reports` (name, result) pairs of other queries of the
 * context whose last EndQuery has become available, oldest first. Each EndQuery
 * is reported once, as the queried id or as a pair; BeginQuery or deletion of
 * the name retires it, and the queried id is always answered. A GL error
 * (GetQueryObjectuiv of a name that is not a query object or is active, a
 * fence record of a name that is not a fence) answers a single zero word: the
 * frontend leaves params unchanged and TestFenceNV returns TRUE, as NV_fence
 * requires. Results stay asynchronous on the host: a pending answer and a
 * pending TestFenceNV flush native GL, so every result arrives in finite time.
 * The frontend keeps what the host reported (results, signalled fences) until
 * the name is begun, set or deleted or a display list executes, so polling
 * availability then the result costs one round trip and polling several ended
 * queries usually one.
 * Everything that depends on GL state is a GL error, never a record fault:
 * BeginQuery of id zero or while a query is active, EndQuery without one, and
 * GenQueries or DeleteQueries while one is active (ARB_occlusion_query, which
 * leaves the names untouched) are INVALID_OPERATION, as are SetFenceNV of name
 * zero and the fence records of a name that is not a fence; table or budget
 * exhaustion is OUT_OF_MEMORY.
 * Each object is charged against the resource budget. */
#define DG_CAP_GL_OCCLUSION_QUERY 0x8000000
#define DG_GL_MAX_QUERIES 4096
#define DG_GL_QUERY_NAME_BATCH 128
#define DG_GL_QUERY_REPORTS 63
#define DG_GL_QUERY_PENDING 1
#define DG_GL_QUERY_AVAILABLE 2
/* Rectangle textures: ARB_texture_rectangle (NV_texture_rectangle), the
 * GeForce FX's conditional non-power-of-two textures. TEXTURE_RECTANGLE is a
 * BindTexture/Enable/TexParameter target with its own binding per texture unit,
 * covered by TEXTURE_BIT; a name keeps the target of its first bind. Enable
 * precedence is cube map > 3D > rectangle > 2D > 1D. TexImage2D/TexSubImage2D,
 * CopyTexImage2D/CopyTexSubImage2D, GetTexImage and GetTexLevelParameter take
 * TEXTURE_RECTANGLE in their existing records, TexImage2D and
 * GetTexLevelParameter also PROXY_TEXTURE_RECTANGLE. A rectangle image has
 * level zero only, no border, an uncompressed colour internal format (no S3TC,
 * generic compressed or, without DG_CAP2_GL_DEPTH_TEXTURE, depth format) and
 * dimensions of any size up to
 * DG_GL_MAX_TEXTURE_DIMENSION; any other record is a fault, so the frontend
 * reports INVALID_VALUE (level, border, size, depth format) or INVALID_ENUM
 * (compressed format, compressed records, GetCompressedTexImage, GenerateMipmap)
 * locally. GENERATE_MIPMAP is no rectangle parameter. FramebufferTexture2D takes
 * textarget TEXTURE_RECTANGLE; a level other than zero is INVALID_VALUE.
 * Texture coordinates are unnormalized (0..width, 0..height), also through
 * ARB_fragment_program's RECT texture target. Native GL judges parameter
 * values: REPEAT, MIRRORED_REPEAT and the mirror-once wraps and mipmap
 * minification filters are INVALID_ENUM, a nonzero BASE_LEVEL is
 * INVALID_OPERATION; a new object starts with CLAMP_TO_EDGE wraps and a LINEAR
 * minification filter. The host answers TEXTURE_BINDING_RECTANGLE and
 * MAX_RECTANGLE_TEXTURE_SIZE (DG_GL_MAX_TEXTURE_DIMENSION or a smaller native
 * limit). Residency charges the image like a 2D level. */
#define DG_CAP_GL_TEXTURE_RECTANGLE 0x10000000
#define DG_IRQ_GL_COMPLETION 0x02
#define DG_GL_REG_VERSION 0x1100
#define DG_GL_REG_ADDR_LO 0x1104
#define DG_GL_REG_ADDR_HI 0x1108
#define DG_GL_REG_BYTES 0x110c
#define DG_GL_REG_SEQUENCE 0x1110
#define DG_GL_REG_GENERATION 0x1114
#define DG_GL_REG_SUBMIT 0x1118
#define DG_GL_REG_STATUS 0x111c
#define DG_GL_REG_COMPLETED 0x1120
#define DG_GL_REG_ERROR 0x1124
#define DG_GL_REG_MAX_BYTES 0x1128
#define DG_GL_REG_MAX_RECORDS 0x112c
#define DG_GL_REG_QUERY_FUNCTION 0x1130
#define DG_GL_REG_FUNCTION_WORDS 0x1134
#define DG_GL_REG_PRESENT_SLOT 0x1140
#define DG_GL_REG_PRESENT_EPOCH_LO 0x1144
#define DG_GL_REG_PRESENT_EPOCH_HI 0x1148
#define DG_GL_REG_PRESENT_FRAME_LO 0x114c
#define DG_GL_REG_PRESENT_FRAME_HI 0x1150
#define DG_GL_REG_PRESENT_CLIENT 0x1154
#define DG_GL_REG_PRESENT_DRAWABLE 0x1158
#define DG_GL_REG_RESULT_ADDR_LO 0x115c
#define DG_GL_REG_RESULT_ADDR_HI 0x1160
#define DG_GL_REG_RESULT_CAPACITY 0x1164
#define DG_GL_REG_RESULT_BYTES 0x1168
#define DG_GL_REG_RESULT_TYPE 0x116c
#define DG_GL_REG_FAULT_STOP 0x1170
#define DG_GL_REG_FAULT_OPERATION 0x1174
#define DG_GL_REG_FAULT_SEQUENCE 0x1178
#define DG_GL_VERSION 0x00010000
#define DG_GL_MAX_BYTES 0x01010000
#define DG_GL_MAX_RECORDS 4096
#define DG_GL_MAX_CONTEXTS 32
#define DG_GL_MAX_DRAWABLES 32
#define DG_GL_MAX_DIMENSION 4096
#define DG_GL_MAX_IMAGE_BYTES 0x20000000
#define DG_GL_EXPORT_SLOTS 3
/* Queued submission. QUEUE_VERSION reads zero on older hosts, which have only
 * the single-batch registers above; this host keeps them unchanged for older
 * drivers. A driver writes a 4 KiB-aligned guest RAM page to QUEUE_ADDR_LO/HI,
 * zeroes its guest words, then writes the current engine GENERATION to
 * QUEUE_CONTROL (zero disables an idle queue). The device clears the page's
 * device words, writes GENERATION then STATE ACTIVE, and QUEUE_CONTROL reads
 * one. Engine reset disables the queue: unfinished batches are discarded
 * without completion and STATE becomes zero. While enabled, legacy SUBMIT
 * completes with DG_GL_ERROR_BATCH and never executes.
 * All counters are free-running 32-bit values compared as signed differences.
 * The driver fills entry PRODUCED % DEPTH, then increments PRODUCED, then
 * writes any value to QUEUE_DOORBELL (an ioeventfd where available, so it need
 * not stop in the device model). The device copies each published entry and
 * its complete command bytes into host memory, validates them exactly as
 * SUBMIT does, and advances CONSUMED: consumed command RAM may be reused
 * immediately, while the entry stays device-owned until it completes. Batches
 * execute and complete in entry order, at most DG_GL_QUEUE_DEPTH unfinished.
 * Completion writes the entry's STATUS (DONE, ERROR) and ERROR, RESULT_BYTES/
 * TYPE after DMA of a QUERY result to the entry's RESULT address (capacity
 * zero for every other batch), the latest PRESENT identity (the PRESENT_*
 * registers), then COMPLETED. DG_IRQ_GL_COMPLETION is raised only while WAIT
 * arms a condition that holds: COMPLETED - WAIT_COMPLETED >= 0 or CONSUMED -
 * WAIT_CONSUMED >= 0. A waiter arms, fences, then rechecks before sleeping.
 * CLIENT/STREAM name an optional fault domain (zero: none). Once a batch of a
 * domain fails, its later batches complete with DG_GL_ERROR_SKIPPED without
 * executing, until engine reset. Records, admission and ordering are those of
 * SUBMIT; only the transport differs. */
#define DG_GL_REG_QUEUE_VERSION 0x1180
#define DG_GL_REG_QUEUE_ADDR_LO 0x1184
#define DG_GL_REG_QUEUE_ADDR_HI 0x1188
#define DG_GL_REG_QUEUE_CONTROL 0x118c
#define DG_GL_REG_QUEUE_DOORBELL 0x1190
#define DG_GL_QUEUE_VERSION 1
#define DG_GL_QUEUE_DEPTH 16
#define DG_GL_QUEUE_PAGE_BYTES 4096
#define DG_GL_QUEUE_ACTIVE 1
/* Device-written page words. Bytes 36-63 are zero. */
#define DG_GL_QUEUE_STATE 0
#define DG_GL_QUEUE_GENERATION 4
#define DG_GL_QUEUE_CONSUMED 8
#define DG_GL_QUEUE_COMPLETED 12
#define DG_GL_QUEUE_PRESENT_SLOT 16
#define DG_GL_QUEUE_PRESENT_EPOCH_LO 20
#define DG_GL_QUEUE_PRESENT_EPOCH_HI 24
#define DG_GL_QUEUE_PRESENT_FRAME_LO 28
#define DG_GL_QUEUE_PRESENT_FRAME_HI 32
/* Guest-written page words. */
#define DG_GL_QUEUE_PRODUCED 64
#define DG_GL_QUEUE_WAIT 68
#define DG_GL_QUEUE_WAIT_COMPLETED 72
#define DG_GL_QUEUE_WAIT_CONSUMED 76
#define DG_GL_QUEUE_WAIT_FOR_COMPLETED 1
#define DG_GL_QUEUE_WAIT_FOR_CONSUMED 2
/* DEPTH entries of ENTRY_BYTES from RING: guest words, then device words. */
#define DG_GL_QUEUE_RING 256
#define DG_GL_QUEUE_ENTRY_BYTES 64
#define DG_GL_ENTRY_ADDR_LO 0
#define DG_GL_ENTRY_ADDR_HI 4
#define DG_GL_ENTRY_BYTES 8
#define DG_GL_ENTRY_SEQUENCE 12
#define DG_GL_ENTRY_GENERATION 16
#define DG_GL_ENTRY_CLIENT 20
#define DG_GL_ENTRY_STREAM 24
#define DG_GL_ENTRY_RESULT_ADDR_LO 28
#define DG_GL_ENTRY_RESULT_ADDR_HI 32
#define DG_GL_ENTRY_RESULT_CAPACITY 36
#define DG_GL_ENTRY_RESERVED0 40
#define DG_GL_ENTRY_RESERVED1 44
#define DG_GL_ENTRY_STATUS 48
#define DG_GL_ENTRY_ERROR 52
#define DG_GL_ENTRY_RESULT_BYTES 56
#define DG_GL_ENTRY_RESULT_TYPE 60
/* Second capability word. DG_REG_CAPS has three bits left (0x20000000,
 * 0x40000000, 0x80000000), kept in reserve; GL features from here on are bits
 * of CAPS2. A register a host does not implement reads zero, so a host
 * without CAPS2 offers none of its features and needs no marker bit: every
 * CAPS2 bit is negotiated exactly like a CAPS bit, and drivers that never
 * read it are unaffected. Like CAPS it is invariant for a realized device and
 * zero without a GL engine; it is read only together with CAPS and changes
 * nothing else. Kernels report it unchanged in the DG_ESCAPE_CAPABILITIES
 * reply's DeviceError (dg-escape.h), which older kernels leave zero, and
 * frontends gate each feature on its bit. */
#define DG_GL_REG_CAPS2 0x1194
/* Depth textures (CAPS2): ARB_depth_texture and ARB_shadow, and
 * ARB_fragment_program_shadow (SHADOW1D, SHADOW2D and SHADOWRECT program
 * targets, native GL parses them) with DG_CAP_GL_FRAGMENT_PROGRAM, as a GeForce
 * FX exposed them. Records use the core function numbers.
 * TexImage1D/TexImage2D and their proxies, TexSubImage1D/TexSubImage2D and
 * CopyTexImage1D/CopyTexImage2D of TEXTURE_1D, TEXTURE_2D and TEXTURE_RECTANGLE
 * take the internal formats DEPTH_COMPONENT(16/24/32), DEPTH_STENCIL and
 * DEPTH24_STENCIL8 without a border, with images in format DEPTH_COMPONENT
 * (BYTE..FLOAT, one component) or DEPTH_STENCIL (UNSIGNED_INT_24_8, four
 * bytes), or none for zero-defined storage, as DG_CAP_GL_FRAMEBUFFER_OBJECT
 * already allows for TEXTURE_2D; cube maps and 3D textures have no depth
 * images (the frontend reports INVALID_VALUE). CopyTex(Sub)Image of a depth
 * level reads the read framebuffer's depth; FramebufferTexture1D/2D attach a
 * depth level as DEPTH_ATTACHMENT (and a DEPTH_STENCIL one as
 * STENCIL_ATTACHMENT too). Display lists capture them as the core records.
 * TexParameter (every target) takes TEXTURE_COMPARE_MODE (NONE or
 * COMPARE_R_TO_TEXTURE), TEXTURE_COMPARE_FUNC (LEQUAL or GEQUAL; there is no
 * EXT_shadow_funcs) and DEPTH_TEXTURE_MODE (LUMINANCE, INTENSITY or ALPHA);
 * native GL accepts more values, so the frontend judges these three and
 * reports INVALID_ENUM. GetTexParameter answers them, GetTexLevelParameter
 * TEXTURE_DEPTH_SIZE and TEXTURE_STENCIL_SIZE. Sampling, fixed-function
 * environments and programs compare natively: R (r/q for projective lookups)
 * against the texel by the function, giving 1.0 or 0.0, filtered (LINEAR
 * averages the results of its texels), then expanded by DEPTH_TEXTURE_MODE.
 * Native fixed function may take a 1D lookup's reference from t (Mesa's does);
 * SHADOW1D programs use r.
 * GetTexImage takes level-word bit DG_GL_TEXTURE_READ_DEPTH: alone it reads a
 * depth level as one float32 depth (RESULT_FLOAT) per pixel, with
 * DG_GL_TEXTURE_READ_FLOAT a DEPTH_STENCIL level as one UNSIGNED_INT_24_8 word
 * (RESULT_INT) per pixel; pixel count and first pixel are as for colour. A
 * colour read of a depth level, or a depth read of another level, is a native
 * error that fails the batch: the frontend first asks TEXTURE_DEPTH_SIZE (and
 * TEXTURE_STENCIL_SIZE for DEPTH_STENCIL) and reports INVALID_OPERATION
 * itself. Residency charges a depth texel like a colour one. Without the
 * capability every rule above is DG_CAP_GL_FRAMEBUFFER_OBJECT's. */
#define DG_CAP2_GL_DEPTH_TEXTURE 0x1
/* Exact R5G6B5 storage (CAPS2): the internal format RGB565 (DG_GL_RGB565,
 * core in GL 4.1 from ARB_ES2_compatibility) in the existing TexImage1D/2D/3D
 * records and their proxies, CopyTexImage1D/2D and RenderbufferStorage, on
 * every target that takes a colour format, without any extension name: no
 * other ARB_ES2_compatibility entry point or token exists. A GeForce FX kept
 * 16-bit RGB textures and targets in 5, 6 and 5 bits; WineD3D stores
 * Direct3D's R5G6B5 as RGB565 when the frontend accepts it (patch 0057). RGB5
 * stays what the native GL makes of it (5/5/5 on Mesa). The host keeps an
 * RGB565 image in native RGB565 where the native GL has it (4.1 or later, or
 * ARB_ES2_compatibility), otherwise in RGB8: either way an image uploaded,
 * cleared, drawn, copied or rendered with 5/6/5-bit component values reads
 * back with exactly those values, as the high bits of each RGBA8 wire
 * component. Level and renderbuffer queries answer the native storage
 * (RGB565 and 5/6/5 bits, or RGB8 and 8/8/8). Without the capability RGB565
 * is an invalid internal format, a local GL error in the frontend. */
#define DG_CAP2_GL_RGB565 0x2
#define DG_GL_RGB565 0x8d62

/* ADDR_LO/HI, BYTES, RESULT_ADDR_LO/HI, RESULT_CAPACITY and the programmed
 * GENERATION persist across submit and
 * completion, and are included in inactive-GL migration. Engine reset clears
 * them and increments REG_GENERATION. A driver can cache DMA programming by
 * that generation; each submit still snapshots the complete command bytes.
 * CAPS and VERSION are invariant for a realized device. STATUS/COMPLETED refer
 * to the accepted operation; DONE without ERROR guarantees REG_ERROR is zero.
 * A pending submission must still check completion before reusing its RAM.
 */

/* Variable records: 32-byte header followed by opcode arguments/data.
 * All integers/IEEE754 float argument words are little endian. SIZE includes
 * header, must be >=32 and multiple of4; concatenated SIZEs equal batch BYTES.
 * CLIENT is a nonzero driver-allocated process token, not the process PID.
 * CONTEXT/DRAWABLE are nonzero per-client IDs when required by the opcode.
 * GENERATION must match the engine generation read before submission.
 * Drivers negotiate min(local limit, REG_MAX_BYTES/REG_MAX_RECORDS) when
 * opening a channel and after a generation change. Older hosts advertise
 * 256 or 1024 records; record layout and ordering are unchanged by a larger
 * limit.
 */
#define DG_GL_OFF_OP 0
#define DG_GL_OFF_SIZE 4
#define DG_GL_OFF_CLIENT 8
#define DG_GL_OFF_CONTEXT 12
#define DG_GL_OFF_DRAWABLE 16
#define DG_GL_OFF_FLAGS 20
#define DG_GL_OFF_RESERVED 24
#define DG_GL_OFF_GENERATION 28
#define DG_GL_HEADER_BYTES 32

#define DG_GL_CREATE_CONTEXT 1 /* one word: share-context ID, zero=none */
#define DG_GL_DESTROY_CONTEXT 2
#define DG_GL_CREATE_DRAWABLE 3 /* two words: width, height */
#define DG_GL_DESTROY_DRAWABLE 4
#define DG_GL_MAKE_CURRENT 5
#define DG_GL_CALL 6 /* function enum, exact argument words */
#define DG_GL_PRESENT 7
#define DG_GL_CLOSE_CLIENT 8
#define DG_GL_DESKTOP 9 /* fixed 64-byte descriptor below */
#define DG_GL_DATA_CALL 10
#define DG_GL_QUERY 11

/* Normal PRESENT exports DRAWABLE, never a complete desktop. EXCLUSIVE must
 * be set explicitly by the privileged display driver and dimensions must
 * match the active VBE primary. No implicit full-desktop takeover occurs.
 */
#define DG_GL_PRESENT_EXCLUSIVE 1
#define DG_GL_PRESENT_RETAIN 2
/* Ordinary PRESENT exchanges logical FRONT/BACK, then exports FRONT.
 * FRONT_ONLY exports the current FRONT without exchange, for explicit flush
 * of front-buffer rendering. Resource identity/retention otherwise match.
 * The kernel window path owns this flag; ordinary user PRESENT stays flags0.
 */
#define DG_GL_PRESENT_FRONT_ONLY 4
/* Occluded normal swap: exchange buffers, publish no image/result identity.
 * May combine only BOUNDED. The window driver retains no export slot. */
#define DG_GL_PRESENT_NO_EXPORT 8
/* Append expected drawable width,height to PRESENT (40 bytes total). The
 * privileged window driver snapshots these while GDI owns the window lock.
 * Mismatch rejects before exchange/export, never enters desktop composition.
 * Combine with normal, FRONT_ONLY or NO_EXPORT presentation semantics. */
#define DG_GL_PRESENT_BOUNDED 16
#define DG_GL_READ_PIXELS_MAX 16384
/* QUERY glReadPixels uses x | mode, y, width | (height << 16).
 * Mode zero preserves the original canonical RGBA8 result (R low byte).
 * With DG_CAP_GL_DEPTH_STENCIL_READBACK, DEPTH returns native float32 depth
 * words (RESULT_FLOAT), STENCIL returns uint32 indices (RESULT_INT).
 * RGBA_FLOAT returns four float32 channels (16 bytes/pixel); other modes
 * return four bytes/pixel in GL bottom-left row order. X occupies
 * low16 only; reserved mode bits/combinations are rejected. Rectangles must
 * fit the selected drawable and contain <=DG_GL_READ_PIXELS_MAX pixels.
 * Frontends tile to negotiated capacity and apply public type/PACK conversion.
 */
#define DG_GL_READ_X_MASK 0xffff
#define DG_GL_READ_DEPTH 0x10000
#define DG_GL_READ_STENCIL 0x20000
#define DG_GL_READ_RGBA_FLOAT 0x30000

#define DG_GL_ERROR_NONE 0
#define DG_GL_ERROR_BATCH 1
#define DG_GL_ERROR_DMA 2
#define DG_GL_ERROR_UNSUPPORTED 3
#define DG_GL_ERROR_CONTEXT 4
#define DG_GL_ERROR_DRAWABLE 5
#define DG_GL_ERROR_HOST 6
#define DG_GL_ERROR_TRANSPORT 7
#define DG_GL_ERROR_GENERATION 8
#define DG_GL_ERROR_LIMIT 9
#define DG_GL_ERROR_DESKTOP 10
#define DG_GL_ERROR_TEXTURE 11
/* A queued batch of a failed fault domain, not executed. */
#define DG_GL_ERROR_SKIPPED 12
/* DESKTOP descriptors follow the usual 32-byte GL record header. The kernel
 * display driver serializes all GDI primary accesses until batch completion.
 * CPU captures/readbacks are serviced in bounded main-loop quanta; no guest
 * writes may race a pending capture. CPU operations always use BGRA32 pixels.
 * Seed/Return cover the active VBE primary; Patch can update a subrectangle.
 * Return requires a full-primary Readback after the last GPU modification.
 * Fill color occupies SRC_X; all unused words must be zero. BLIT/DISCARD use
 * CLIENT/DRAWABLE from the enclosing header and exact PRESENT result IDs.
 * BLIT_FINAL consumes the receiver's retained reference after the final clip.
 */
#define DG_DESKTOP_BYTES 64
#define DG_DESKTOP_MAX_RECORDS 64
#define DG_DESKTOP_MAX_BYTES 0x04000000
#define DG_DESKTOP_SEED 1
#define DG_DESKTOP_PATCH 2
#define DG_DESKTOP_RETURN 3
#define DG_DESKTOP_FILL 4
#define DG_DESKTOP_COPY 5
#define DG_DESKTOP_BLIT 6
#define DG_DESKTOP_READBACK 7
#define DG_DESKTOP_DISCARD 8
#define DG_DESKTOP_BLIT_FINAL 1
#define DG_DESKTOP_OP 0
#define DG_DESKTOP_FLAGS 4
#define DG_DESKTOP_DST_X 8
#define DG_DESKTOP_DST_Y 12
#define DG_DESKTOP_WIDTH 16
#define DG_DESKTOP_HEIGHT 20
#define DG_DESKTOP_SRC_X 24
#define DG_DESKTOP_SRC_Y 28
#define DG_DESKTOP_RESERVED0 32
/* Primary memory format, independent of the 32-bit compositor/export format.
 * Zero preserves the original XRGB8888 packets; 16 selects little-endian RGB565.
 * The device checks this against the active VBE mode before enqueue. */
#define DG_DESKTOP_PRIMARY_BPP 32
#define DG_DESKTOP_RESERVED1 36
#define DG_DESKTOP_SLOT_OR_OFFSET 40
#define DG_DESKTOP_VRAM_STRIDE 44
#define DG_DESKTOP_IMAGE_EPOCH 48
#define DG_DESKTOP_IMAGE_FRAME 56

/* DATA_CALL has function32, byte_count36, fixed scalar arguments40, then an
 * immutable tightly packed byte payload and zero padding to a four-byte size.
 * Guest pointers are never transmitted. The wrapper resolves guest pixel-unpack
 * stride/skips before submission; host uploads use canonical unpack state.
 * QUERY_FUNCTION sets INLINE_DATA in the word count for these functions.
 * TexImage2D/TexSubImage2D omit their pointer argument (eight scalar words).
 * TexImage2D byte_count=0 allocates zero-initialized storage with the same
 * format/dimension/mip bounds; follow it with bounded TexSubImage2D tiles.
 * An empty TexSubImage2D payload is not an allocation request.
 * DeleteTextures has one count word, then that many little-endian guest names.
 */
#define DG_GL_FUNCTION_INLINE_DATA 0x80000000
#define DG_GL_FUNCTION_QUERY 0x40000000
/* Private scalar record: preserve deferred GL_COMPILE errors without executing invalid input. */
#define DG_GL_RECORD_ERROR 4095
#define DG_GL_FUNCTION_KIND_MASK 0xc0000000
#define DG_GL_DATA_FUNCTION 32
#define DG_GL_DATA_BYTES 36
#define DG_GL_DATA_ARGS 40
#define DG_GL_MAX_TEXTURES 4096
#define DG_GL_MAX_TEXTURE_DIMENSION 2048
#define DG_GL_MAX_TEXTURE_BYTES 0x10000000
#define DG_GL_MAX_TEXTURE_LEVEL 11

/* TexImage1D/TexSubImage1D use the same eight-word image/tile DATA shape
 * as their 2D counterparts, with target TEXTURE_1D, height1 and tile yoffset0.
 * Targets bind independent objects; an existing name cannot change targets.
 * GetTexImage QUERY arguments: target, level, first_pixel. Each result is128
 * canonical little-endian RGBA8 pixels (RESULT_INT,512bytes), zero-padded at
 * level end. The public wrapper applies packing and requested color format.
 * First-pixel0 starts a fresh bounded snapshot; writes invalidate its version.
 */

/* Client arrays are copied at draw time, never retained as guest pointers.
 * DrawArrays scalar words: mode, first(0), vertex_count, enabled attributes.
 * DrawElements: mode, index_count, index_type, vertex_count, attributes.
 * Vertex records precede tightly packed U8/U16/U32 indices in Elements.
 * The wrapper normalizes positions/colors/normals/coordinates to float32;
 * all components and indices use little endian. Position is always enabled.
 * DrawArrays snapshots only the requested range, translating first to zero.
 */
#define DG_GL_VERTEX_BYTES 64
#define DG_GL_VERTEX_POSITION 0  /* four floats */
#define DG_GL_VERTEX_COLOR 16    /* four floats */
#define DG_GL_VERTEX_NORMAL 32   /* three floats */
#define DG_GL_VERTEX_TEXCOORD 44 /* four floats */
#define DG_GL_VERTEX_RESERVED 60 /* zero */
#define DG_GL_ARRAY_POSITION 1
#define DG_GL_ARRAY_COLOR 2
#define DG_GL_ARRAY_NORMAL 4
#define DG_GL_ARRAY_TEXCOORD 8
#define DG_GL_ARRAY_SECONDARY 16
#define DG_GL_ARRAY_INDEX 32
#define DG_GL_ARRAY_EDGE 64
#define DG_GL_ARRAY_MASK 127
#define DG_GL_VERTEX_INDEX 80 /* one double */
#define DG_GL_VERTEX_EDGE 88  /* one canonical boolean byte, seven zero bytes */
#define DG_GL_VERTEX_EXTENDED_BYTES 96
/* Existing vertices remain64bytes; secondary color appends RGB and zero pad. */
#define DG_GL_VERTEX_SECONDARY 64
#define DG_GL_VERTEX_SECONDARY_PAD 76
#define DG_GL_VERTEX_SECONDARY_BYTES 80
#define DG_GL_VERTEX_SIZE(mask)                                                                    \
    (((mask) & (DG_GL_ARRAY_INDEX | DG_GL_ARRAY_EDGE))                                             \
         ? DG_GL_VERTEX_EXTENDED_BYTES                                                             \
         : (((mask) & DG_GL_ARRAY_SECONDARY) ? DG_GL_VERTEX_SECONDARY_BYTES : DG_GL_VERTEX_BYTES))
/* Compact DrawArrays retains four scalar words; ARRAY_RAW marks a payload
 * of seven LE descriptor words followed by tight attribute sections in bit
 * order. Descriptor = GL type | (component count <<16); zero when disabled.
 * Each section begins at its component-size alignment relative to payload;
 * inter-section and final four-byte alignment padding is zero. Native GL owns
 * conversion/default-component semantics. No pointers or strides cross ABI.
 * Legacy fixed records above remain accepted by the paired current host. */
#define DG_GL_ARRAY_RAW 0x80000000U
#define DG_GL_ARRAY_DESCRIPTOR_BYTES 28
/* With DG_CAP_GL_MULTITEXTURE, DrawArraysEXT uses the same four scalar words
 * and RAW payload rules with sixteen descriptors. Slots 0-6 match DrawArrays;
 * slots 7-13 are texture coordinates of units 1-7 with the slot-3 rules;
 * slot 14 is the fog coordinate (one FLOAT or DOUBLE, DG_CAP_GL_FOG_COORD);
 * slot 15 is reserved: its mask bit and descriptor must be zero.
 * A distinct function keeps older kernels' seven-slot admission intact. */
#define DG_GL_ARRAY_TEXCOORD1 0x80
#define DG_GL_ARRAY_FOG_COORD 0x4000
#define DG_GL_ARRAY_EXT_MASK 0x7fff
#define DG_GL_ARRAY_EXT_SLOTS 16
#define DG_GL_ARRAY_EXT_DESCRIPTOR_BYTES 64
/* With DG_CAP_GL_VERTEX_PROGRAM, the attribute draw is DATA function
 * VertexAttribPointerARB with five scalar words: mode, first (0), vertex count,
 * the DrawArraysEXT mask with ARRAY_RAW, and a generic attribute mask (bit n
 * for attribute n, DG_GL_ATTRIB_MASK). Its payload has thirty-two descriptors,
 * the sixteen DrawArraysEXT slots then generic attributes 0-15, followed by the
 * enabled sections in slot order under the RAW alignment and padding rules.
 * Slots 0-15 keep the DrawArraysEXT rules, except that the position bit may be
 * clear while generic attribute 0 is enabled (one of them must be). A generic
 * descriptor is a GL type BYTE..DOUBLE (not 2_BYTES..4_BYTES) | component count
 * 1-4 << 16, plus DG_GL_ATTRIB_NORMALIZED for a normalized integer type; native
 * GL converts. A distinct function keeps older kernels' sixteen-slot
 * DrawArraysEXT admission intact. */
#define DG_GL_ATTRIB_NORMALIZED 0x01000000U
#define DG_GL_ATTRIB_MASK 0xffff
#define DG_GL_ATTRIB_SLOTS 32
#define DG_GL_ATTRIB_DESCRIPTOR_BYTES 128
#define DG_GL_MAX_VERTICES 65536
#define DG_GL_MAX_EVAL_ORDER 8
#define DG_GL_MAX_CAPTURE_VALUES 16384
#define DG_GL_MAX_INDICES 262144

/* QUERY must be the sole record in its batch. Payload is function,arg0,arg1,
 * arg2 (16 bytes), with unused arguments zero. Get* uses pname; GetError has
 * no arguments; GetTexParameter/GetTexEnv use target,pname; GetTexLevelParameter
 * uses target,level,pname. QUERY_FUNCTION returns QUERY | scalar argument count.
 * The privileged driver owns a writable RAM DMA result buffer and programs
 * RESULT_ADDR/CAPACITY before submission. These registers are snapshotted;
 * bytes/type become valid only after successful completion of that sequence.
 * Results are packed little-endian values or a NUL-terminated ASCII string.
 */
#define DG_GL_QUERY_BYTES 48
#define DG_GL_MAX_RESULT_BYTES 512
#define DG_GL_MAX_READBACK_BYTES 65536
/* GetTexImage: level low14, FLOAT bit15 requests RGBA float32 (16 bytes/pixel)
 * with DG_CAP_GL_DEPTH_STENCIL_READBACK. Optional pixel count high16; zero
 * keeps legacy128 pixels. FLOAT returns RESULT_FLOAT, default RESULT_INT.
 * RGBA_FLOAT framebuffer mode uses four FLOAT words per pixel as well.
 * DEPTH bit14 (DG_CAP2_GL_DEPTH_TEXTURE) reads depth instead: one float32 per
 * pixel, or with FLOAT one UNSIGNED_INT_24_8 word of a DEPTH_STENCIL level.
 * Levels are at most DG_GL_MAX_TEXTURE_LEVEL, so older hosts never saw bit14. */
#define DG_GL_TEXTURE_READ_FLOAT 0x8000
#define DG_GL_TEXTURE_READ_DEPTH 0x4000
#define DG_GL_TEXTURE_READ_LEVEL_MASK 0x3fff
#define DG_GL_TEXTURE_READ_COUNT_SHIFT 16
#define DG_GL_RESULT_BOOL 1
#define DG_GL_RESULT_INT 2
#define DG_GL_RESULT_FLOAT 3
#define DG_GL_RESULT_DOUBLE 4
#define DG_GL_RESULT_STRING 5

/* A coherence failure cannot return through a void display synchronization
 * callback. Write a reason here, then wait on a nonsignaled kernel event
 * without returning to GDI. Host enters internal-error (QMP cont rejects until
 * system reset), preserves exported canvas resources and emits a fault event.
 * READBACK/RETURN errors also trigger this stop before their completion IRQ.
 * Engine RESET cannot clear the fault; a system reset or checkpoint restore
 * is required. Zero/unknown writes do not clear or replace the first reason.
 */
#define DG_GL_FAULT_HOST_COHERENCE 1
#define DG_GL_FAULT_DRIVER_TIMEOUT 2
#define DG_GL_FAULT_DRIVER_VALIDATE 3
#define DG_GL_FAULT_DRIVER_INTERNAL 4

/* QUERY_FUNCTION selects donor function enum. FUNCTION_WORDS returns exact
 * argument word count (possibly INLINE_DATA) or UINT32_MAX when unsupported.
 * No complete OpenGL version is advertised merely for having a transport.
 */
#endif
