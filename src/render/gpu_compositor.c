#include "render/gpu_compositor.h"
#include "render/blend.h"
#include <string.h>
#include <stdio.h>

#ifdef _WIN32
#define GLFW_EXPOSE_NATIVE_WIN32
#define GLFW_EXPOSE_NATIVE_WGL
#endif

/* Include GLFW and OpenGL headers */
#include <GLFW/glfw3.h>

#ifdef _WIN32
#include <GL/gl.h>
#include <GL/glext.h>
/* Windows requires loading OpenGL extension functions manually */
typedef void (APIENTRY *PFNGLGENFRAMEBUFFERSPROC)(GLsizei n, GLuint *framebuffers);
typedef void (APIENTRY *PFNGLBINDFRAMEBUFFERPROC)(GLenum target, GLuint framebuffer);
typedef void (APIENTRY *PFNGLFRAMEBUFFERTEXTURE2DPROC)(GLenum target, GLenum attachment, GLenum textarget, GLuint texture, GLint level);
typedef GLenum (APIENTRY *PFNGLCHECKFRAMEBUFFERSTATUSPROC)(GLenum target);
typedef void (APIENTRY *PFNGLDELETEFRAMEBUFFERSPROC)(GLsizei n, const GLuint *framebuffers);
typedef void (APIENTRY *PFNGLGENBUFFERSPROC)(GLsizei n, GLuint *buffers);
typedef void (APIENTRY *PFNGLBINDBUFFERPROC)(GLenum target, GLuint buffer);
typedef void (APIENTRY *PFNGLBUFFERDATAPROC)(GLenum target, GLsizeiptr size, const void *data, GLenum usage);
typedef void (APIENTRY *PFNGLDELETEBUFFERSPROC)(GLsizei n, const GLuint *buffers);
typedef GLuint (APIENTRY *PFNGLCREATESHADERPROC)(GLenum type);
typedef void (APIENTRY *PFNGLSHADERSOURCEPROC)(GLuint shader, GLsizei count, const GLchar *const*string, const GLint *length);
typedef void (APIENTRY *PFNGLCOMPILESHADERPROC)(GLuint shader);
typedef void (APIENTRY *PFNGLGETSHADERIVPROC)(GLuint shader, GLenum pname, GLint *params);
typedef void (APIENTRY *PFNGLGETSHADERINFOLOGPROC)(GLuint shader, GLsizei bufSize, GLsizei *length, GLchar *infoLog);
typedef GLuint (APIENTRY *PFNGLCREATEPROGRAMPROC)(void);
typedef void (APIENTRY *PFNGLATTACHSHADERPROC)(GLuint program, GLuint shader);
typedef void (APIENTRY *PFNGLLINKPROGRAMPROC)(GLuint program);
typedef void (APIENTRY *PFNGLGETPROGRAMIVPROC)(GLuint program, GLenum pname, GLint *params);
typedef void (APIENTRY *PFNGLGETPROGRAMINFOLOGPROC)(GLuint program, GLsizei bufSize, GLsizei *length, GLchar *infoLog);
typedef void (APIENTRY *PFNGLDELETESHADERPROC)(GLuint shader);
typedef void (APIENTRY *PFNGLUSEPROGRAMPROC)(GLuint program);
typedef GLint (APIENTRY *PFNGLGETUNIFORMLOCATIONPROC)(GLuint program, const GLchar *name);
typedef void (APIENTRY *PFNGLUNIFORM1IPROC)(GLint location, GLint v0);
typedef void (APIENTRY *PFNGLUNIFORM1FPROC)(GLint location, GLfloat v0);
typedef void (APIENTRY *PFNGLUNIFORM2FPROC)(GLint location, GLfloat v0, GLfloat v1);
typedef void (APIENTRY *PFNGLDELETEPROGRAMPROC)(GLuint program);
typedef void (APIENTRY *PFNGLACTIVETEXTUREPROC)(GLenum texture);
typedef void (APIENTRY *PFNGLGENVERTEXARRAYSPROC)(GLsizei n, GLuint *arrays);
typedef void (APIENTRY *PFNGLBINDVERTEXARRAYPROC)(GLuint array);
typedef void (APIENTRY *PFNGLDELETEVERTEXARRAYSPROC)(GLsizei n, const GLuint *arrays);
typedef void (APIENTRY *PFNGLENABLEVERTEXATTRIBARRAYPROC)(GLuint index);
typedef void (APIENTRY *PFNGLVERTEXATTRIBPOINTERPROC)(GLuint index, GLint size, GLenum type, GLboolean normalized, GLsizei stride, const void *pointer);

static PFNGLGENFRAMEBUFFERSPROC glGenFramebuffers = NULL;
static PFNGLBINDFRAMEBUFFERPROC glBindFramebuffer = NULL;
static PFNGLFRAMEBUFFERTEXTURE2DPROC glFramebufferTexture2D = NULL;
static PFNGLCHECKFRAMEBUFFERSTATUSPROC glCheckFramebufferStatus = NULL;
static PFNGLDELETEFRAMEBUFFERSPROC glDeleteFramebuffers = NULL;
static PFNGLGENBUFFERSPROC glGenBuffers = NULL;
static PFNGLBINDBUFFERPROC glBindBuffer = NULL;
static PFNGLBUFFERDATAPROC glBufferData = NULL;
static PFNGLDELETEBUFFERSPROC glDeleteBuffers = NULL;
static PFNGLCREATESHADERPROC glCreateShader = NULL;
static PFNGLSHADERSOURCEPROC glShaderSource = NULL;
static PFNGLCOMPILESHADERPROC glCompileShader = NULL;
static PFNGLGETSHADERIVPROC glGetShaderiv = NULL;
static PFNGLGETSHADERINFOLOGPROC glGetShaderInfoLog = NULL;
static PFNGLCREATEPROGRAMPROC glCreateProgram = NULL;
static PFNGLATTACHSHADERPROC glAttachShader = NULL;
static PFNGLLINKPROGRAMPROC glLinkProgram = NULL;
static PFNGLGETPROGRAMIVPROC glGetProgramiv = NULL;
static PFNGLGETPROGRAMINFOLOGPROC glGetProgramInfoLog = NULL;
static PFNGLDELETESHADERPROC glDeleteShader = NULL;
static PFNGLUSEPROGRAMPROC glUseProgram = NULL;
static PFNGLGETUNIFORMLOCATIONPROC glGetUniformLocation = NULL;
static PFNGLUNIFORM1IPROC glUniform1i = NULL;
static PFNGLUNIFORM1FPROC glUniform1f = NULL;
static PFNGLUNIFORM2FPROC glUniform2f = NULL;
static PFNGLDELETEPROGRAMPROC glDeleteProgram = NULL;
static PFNGLACTIVETEXTUREPROC glActiveTexture = NULL;
static PFNGLGENVERTEXARRAYSPROC glGenVertexArrays = NULL;
static PFNGLBINDVERTEXARRAYPROC glBindVertexArray = NULL;
static PFNGLDELETEVERTEXARRAYSPROC glDeleteVertexArrays = NULL;
static PFNGLENABLEVERTEXATTRIBARRAYPROC glEnableVertexAttribArray = NULL;
static PFNGLVERTEXATTRIBPOINTERPROC glVertexAttribPointer = NULL;

#ifndef GL_FRAMEBUFFER
#define GL_FRAMEBUFFER 0x8D40
#endif
#ifndef GL_COLOR_ATTACHMENT0
#define GL_COLOR_ATTACHMENT0 0x8CE0
#endif
#ifndef GL_FRAMEBUFFER_COMPLETE
#define GL_FRAMEBUFFER_COMPLETE 0x8CD5
#endif
#ifndef GL_ARRAY_BUFFER
#define GL_ARRAY_BUFFER 0x8892
#endif
#ifndef GL_STATIC_DRAW
#define GL_STATIC_DRAW 0x88E4
#endif
#ifndef GL_FRAGMENT_SHADER
#define GL_FRAGMENT_SHADER 0x8B30
#endif
#ifndef GL_VERTEX_SHADER
#define GL_VERTEX_SHADER 0x8B31
#endif
#ifndef GL_COMPILE_STATUS
#define GL_COMPILE_STATUS 0x8B81
#endif
#ifndef GL_LINK_STATUS
#define GL_LINK_STATUS 0x8B82
#endif
#ifndef GL_TEXTURE0
#define GL_TEXTURE0 0x84C0
#endif
#ifndef GL_TEXTURE1
#define GL_TEXTURE1 0x84C1
#endif
#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif

#else
/* Linux/Mac - use standard GL headers */
#include <GL/gl.h>
#include <GL/glext.h>
#endif

/* Layer texture cache entry */
typedef struct {
    ImageLayer* layer;        /* Pointer to the layer (for validation) */
    GLuint texture_id;        /* OpenGL texture ID */
    guint width, height;      /* Cached dimensions */
    guint64 last_used;        /* Frame counter when last used */
    guint64 content_version;  /* Layer content version when texture was uploaded */
    gboolean valid;           /* TRUE if texture is valid */
} LayerTextureCache;

/* GPU compositor internal structure */
struct GPUCompositor {
    GLFWwindow* window;         /* Hidden GLFW window for OpenGL context */
    gboolean initialized;       /* TRUE if successfully initialized */
    
    /* GPU device info */
    GPUDeviceInfo active_device;
    
    /* Shader programs */
    GLuint shader_program;      /* Main compositing shader with blend modes */
    GLint u_texture_loc;        /* Source layer texture sampler */
    GLint u_dst_texture_loc;    /* Destination texture sampler (for blend modes) */
    GLint u_opacity_loc;        /* Opacity uniform location */
    GLint u_tex_offset_loc;     /* Texture coordinate offset uniform */
    GLint u_tex_scale_loc;      /* Texture coordinate scale uniform */
    GLint u_blend_mode_loc;     /* Blend mode uniform */
    GLint u_is_first_layer_loc; /* First layer flag (skip blending) */
    GLint u_tile_size_loc;      /* Tile dimensions for UV calculation */
    
    /* Ping-pong framebuffers for blend mode compositing */
    GLuint fbo[2];              /* Two framebuffer objects */
    GLuint fbo_texture[2];      /* Textures attached to FBOs */
    gint fbo_width, fbo_height; /* Current FBO dimensions */
    gint current_fbo;           /* Index of current write FBO (0 or 1) */
    
    /* Vertex array/buffer for fullscreen quad */
    GLuint vao;
    GLuint vbo;
    
    /* Layer texture cache */
    GHashTable* texture_cache;  /* ImageLayer* -> LayerTextureCache* */
    guint64 frame_counter;      /* For LRU cache eviction */
    
    /* Statistics */
    guint64 tiles_composited;
    gsize memory_used;
    
    /* Thread safety */
    GMutex mutex;
};

/* Global GLFW initialization flag */
static gboolean g_glfw_initialized = FALSE;
static gint g_glfw_ref_count = 0;
static GMutex g_glfw_mutex;

/* Vertex shader source with texture coordinate transformation for tile-based rendering */
static const char* VERTEX_SHADER_SOURCE =
    "#version 120\n"
    "attribute vec2 a_position;\n"
    "attribute vec2 a_texcoord;\n"
    "uniform vec2 u_tex_offset;\n"   /* Offset into layer texture (in UV coords) */
    "uniform vec2 u_tex_scale;\n"    /* Scale of tile in layer texture (in UV coords) */
    "varying vec2 v_texcoord;\n"
    "void main() {\n"
    "    gl_Position = vec4(a_position, 0.0, 1.0);\n"
    "    /* Transform texture coords: map tile's portion of layer texture */\n"
    "    v_texcoord = a_texcoord * u_tex_scale + u_tex_offset;\n"
    "}\n";

/* Fragment shader source with full blend mode support
 * 
 * Uses ping-pong rendering: reads from destination texture to apply blend modes,
 * then writes to the alternate FBO. This allows proper Porter-Duff compositing
 * with all Photoshop-compatible blend modes.
 * 
 * Blend mode constants must match BlendMode enum in document.h
 */
static const char* FRAGMENT_SHADER_SOURCE =
    "#version 120\n"
    "uniform sampler2D u_texture;\n"      /* Source layer texture */
    "uniform sampler2D u_dst_texture;\n"  /* Destination (accumulated result) */
    "uniform float u_opacity;\n"
    "uniform int u_blend_mode;\n"
    "uniform bool u_is_first_layer;\n"
    "uniform vec2 u_tile_size;\n"         /* Tile dimensions for UV calculation */
    "varying vec2 v_texcoord;\n"
    "\n"
    "/* Blend mode constants - must match BlendMode enum */\n"
    "const int BLEND_NORMAL = 0;\n"
    "const int BLEND_DISSOLVE = 1;\n"
    "const int BLEND_DARKEN = 2;\n"
    "const int BLEND_MULTIPLY = 3;\n"
    "const int BLEND_COLOR_BURN = 4;\n"
    "const int BLEND_LINEAR_BURN = 5;\n"
    "const int BLEND_DARKER_COLOR = 6;\n"
    "const int BLEND_LIGHTEN = 7;\n"
    "const int BLEND_SCREEN = 8;\n"
    "const int BLEND_COLOR_DODGE = 9;\n"
    "const int BLEND_LINEAR_DODGE = 10;\n"
    "const int BLEND_LIGHTER_COLOR = 11;\n"
    "const int BLEND_OVERLAY = 12;\n"
    "const int BLEND_SOFT_LIGHT = 13;\n"
    "const int BLEND_HARD_LIGHT = 14;\n"
    "const int BLEND_VIVID_LIGHT = 15;\n"
    "const int BLEND_LINEAR_LIGHT = 16;\n"
    "const int BLEND_PIN_LIGHT = 17;\n"
    "const int BLEND_HARD_MIX = 18;\n"
    "const int BLEND_DIFFERENCE = 19;\n"
    "const int BLEND_EXCLUSION = 20;\n"
    "const int BLEND_SUBTRACT = 21;\n"
    "const int BLEND_DIVIDE = 22;\n"
    "const int BLEND_HUE = 23;\n"
    "const int BLEND_SATURATION = 24;\n"
    "const int BLEND_COLOR = 25;\n"
    "const int BLEND_LUMINOSITY = 26;\n"
    "\n"
    "/* Helper: luminosity of RGB */\n"
    "float lum(vec3 c) {\n"
    "    return 0.3 * c.r + 0.59 * c.g + 0.11 * c.b;\n"
    "}\n"
    "\n"
    "/* Helper: saturation of RGB */\n"
    "float sat(vec3 c) {\n"
    "    return max(max(c.r, c.g), c.b) - min(min(c.r, c.g), c.b);\n"
    "}\n"
    "\n"
    "/* Helper: clip color to valid range while preserving luminosity */\n"
    "vec3 clipColor(vec3 c) {\n"
    "    float l = lum(c);\n"
    "    float n = min(min(c.r, c.g), c.b);\n"
    "    float x = max(max(c.r, c.g), c.b);\n"
    "    if (n < 0.0) c = l + (c - l) * l / (l - n);\n"
    "    if (x > 1.0) c = l + (c - l) * (1.0 - l) / (x - l);\n"
    "    return c;\n"
    "}\n"
    "\n"
    "/* Helper: set luminosity */\n"
    "vec3 setLum(vec3 c, float l) {\n"
    "    float d = l - lum(c);\n"
    "    return clipColor(c + d);\n"
    "}\n"
    "\n"
    "/* Helper: set saturation (complex - reorders components) */\n"
    "vec3 setSat(vec3 c, float s) {\n"
    "    float cmin = min(min(c.r, c.g), c.b);\n"
    "    float cmax = max(max(c.r, c.g), c.b);\n"
    "    float cmid;\n"
    "    vec3 result;\n"
    "    if (cmax == cmin) return vec3(0.0);\n"
    "    /* Find and set mid value */\n"
    "    if (c.r == cmax) {\n"
    "        if (c.g == cmin) { cmid = c.b; result = vec3(s, 0.0, (cmid - cmin) * s / (cmax - cmin)); }\n"
    "        else { cmid = c.g; result = vec3(s, (cmid - cmin) * s / (cmax - cmin), 0.0); }\n"
    "    } else if (c.g == cmax) {\n"
    "        if (c.r == cmin) { cmid = c.b; result = vec3(0.0, s, (cmid - cmin) * s / (cmax - cmin)); }\n"
    "        else { cmid = c.r; result = vec3((cmid - cmin) * s / (cmax - cmin), s, 0.0); }\n"
    "    } else {\n"
    "        if (c.r == cmin) { cmid = c.g; result = vec3(0.0, (cmid - cmin) * s / (cmax - cmin), s); }\n"
    "        else { cmid = c.r; result = vec3((cmid - cmin) * s / (cmax - cmin), 0.0, s); }\n"
    "    }\n"
    "    return result;\n"
    "}\n"
    "\n"
    "/* Apply blend mode to get blended RGB (non-premultiplied inputs) */\n"
    "vec3 applyBlendMode(vec3 src, vec3 dst, int mode) {\n"
    "    vec3 result;\n"
    "    \n"
    "    if (mode == BLEND_NORMAL || mode == BLEND_DISSOLVE) {\n"
    "        result = src;\n"
    "    }\n"
    "    /* Darken modes */\n"
    "    else if (mode == BLEND_DARKEN) {\n"
    "        result = min(src, dst);\n"
    "    }\n"
    "    else if (mode == BLEND_MULTIPLY) {\n"
    "        result = src * dst;\n"
    "    }\n"
    "    else if (mode == BLEND_COLOR_BURN) {\n"
    "        result = vec3(\n"
    "            src.r == 0.0 ? 0.0 : max(0.0, 1.0 - (1.0 - dst.r) / src.r),\n"
    "            src.g == 0.0 ? 0.0 : max(0.0, 1.0 - (1.0 - dst.g) / src.g),\n"
    "            src.b == 0.0 ? 0.0 : max(0.0, 1.0 - (1.0 - dst.b) / src.b)\n"
    "        );\n"
    "    }\n"
    "    else if (mode == BLEND_LINEAR_BURN) {\n"
    "        result = max(vec3(0.0), src + dst - 1.0);\n"
    "    }\n"
    "    else if (mode == BLEND_DARKER_COLOR) {\n"
    "        result = lum(src) < lum(dst) ? src : dst;\n"
    "    }\n"
    "    /* Lighten modes */\n"
    "    else if (mode == BLEND_LIGHTEN) {\n"
    "        result = max(src, dst);\n"
    "    }\n"
    "    else if (mode == BLEND_SCREEN) {\n"
    "        result = 1.0 - (1.0 - src) * (1.0 - dst);\n"
    "    }\n"
    "    else if (mode == BLEND_COLOR_DODGE) {\n"
    "        result = vec3(\n"
    "            src.r == 1.0 ? 1.0 : min(1.0, dst.r / (1.0 - src.r)),\n"
    "            src.g == 1.0 ? 1.0 : min(1.0, dst.g / (1.0 - src.g)),\n"
    "            src.b == 1.0 ? 1.0 : min(1.0, dst.b / (1.0 - src.b))\n"
    "        );\n"
    "    }\n"
    "    else if (mode == BLEND_LINEAR_DODGE) {\n"
    "        result = min(vec3(1.0), src + dst);\n"
    "    }\n"
    "    else if (mode == BLEND_LIGHTER_COLOR) {\n"
    "        result = lum(src) > lum(dst) ? src : dst;\n"
    "    }\n"
    "    /* Contrast modes */\n"
    "    else if (mode == BLEND_OVERLAY) {\n"
    "        result = vec3(\n"
    "            dst.r < 0.5 ? 2.0 * src.r * dst.r : 1.0 - 2.0 * (1.0 - src.r) * (1.0 - dst.r),\n"
    "            dst.g < 0.5 ? 2.0 * src.g * dst.g : 1.0 - 2.0 * (1.0 - src.g) * (1.0 - dst.g),\n"
    "            dst.b < 0.5 ? 2.0 * src.b * dst.b : 1.0 - 2.0 * (1.0 - src.b) * (1.0 - dst.b)\n"
    "        );\n"
    "    }\n"
    "    else if (mode == BLEND_SOFT_LIGHT) {\n"
    "        /* W3C formula */\n"
    "        vec3 d = vec3(\n"
    "            dst.r <= 0.25 ? ((16.0 * dst.r - 12.0) * dst.r + 4.0) * dst.r : sqrt(dst.r),\n"
    "            dst.g <= 0.25 ? ((16.0 * dst.g - 12.0) * dst.g + 4.0) * dst.g : sqrt(dst.g),\n"
    "            dst.b <= 0.25 ? ((16.0 * dst.b - 12.0) * dst.b + 4.0) * dst.b : sqrt(dst.b)\n"
    "        );\n"
    "        result = vec3(\n"
    "            src.r <= 0.5 ? dst.r - (1.0 - 2.0 * src.r) * dst.r * (1.0 - dst.r) : dst.r + (2.0 * src.r - 1.0) * (d.r - dst.r),\n"
    "            src.g <= 0.5 ? dst.g - (1.0 - 2.0 * src.g) * dst.g * (1.0 - dst.g) : dst.g + (2.0 * src.g - 1.0) * (d.g - dst.g),\n"
    "            src.b <= 0.5 ? dst.b - (1.0 - 2.0 * src.b) * dst.b * (1.0 - dst.b) : dst.b + (2.0 * src.b - 1.0) * (d.b - dst.b)\n"
    "        );\n"
    "    }\n"
    "    else if (mode == BLEND_HARD_LIGHT) {\n"
    "        result = vec3(\n"
    "            src.r < 0.5 ? 2.0 * src.r * dst.r : 1.0 - 2.0 * (1.0 - src.r) * (1.0 - dst.r),\n"
    "            src.g < 0.5 ? 2.0 * src.g * dst.g : 1.0 - 2.0 * (1.0 - src.g) * (1.0 - dst.g),\n"
    "            src.b < 0.5 ? 2.0 * src.b * dst.b : 1.0 - 2.0 * (1.0 - src.b) * (1.0 - dst.b)\n"
    "        );\n"
    "    }\n"
    "    else if (mode == BLEND_VIVID_LIGHT) {\n"
    "        result = vec3(\n"
    "            src.r <= 0.5 ? (src.r == 0.0 ? 0.0 : max(0.0, 1.0 - (1.0 - dst.r) / (2.0 * src.r))) : (src.r == 1.0 ? 1.0 : min(1.0, dst.r / (2.0 * (1.0 - src.r)))),\n"
    "            src.g <= 0.5 ? (src.g == 0.0 ? 0.0 : max(0.0, 1.0 - (1.0 - dst.g) / (2.0 * src.g))) : (src.g == 1.0 ? 1.0 : min(1.0, dst.g / (2.0 * (1.0 - src.g)))),\n"
    "            src.b <= 0.5 ? (src.b == 0.0 ? 0.0 : max(0.0, 1.0 - (1.0 - dst.b) / (2.0 * src.b))) : (src.b == 1.0 ? 1.0 : min(1.0, dst.b / (2.0 * (1.0 - src.b))))\n"
    "        );\n"
    "    }\n"
    "    else if (mode == BLEND_LINEAR_LIGHT) {\n"
    "        result = clamp(dst + 2.0 * src - 1.0, 0.0, 1.0);\n"
    "    }\n"
    "    else if (mode == BLEND_PIN_LIGHT) {\n"
    "        result = vec3(\n"
    "            src.r > 0.5 ? max(dst.r, 2.0 * (src.r - 0.5)) : min(dst.r, 2.0 * src.r),\n"
    "            src.g > 0.5 ? max(dst.g, 2.0 * (src.g - 0.5)) : min(dst.g, 2.0 * src.g),\n"
    "            src.b > 0.5 ? max(dst.b, 2.0 * (src.b - 0.5)) : min(dst.b, 2.0 * src.b)\n"
    "        );\n"
    "    }\n"
    "    else if (mode == BLEND_HARD_MIX) {\n"
    "        result = vec3(\n"
    "            src.r + dst.r >= 1.0 ? 1.0 : 0.0,\n"
    "            src.g + dst.g >= 1.0 ? 1.0 : 0.0,\n"
    "            src.b + dst.b >= 1.0 ? 1.0 : 0.0\n"
    "        );\n"
    "    }\n"
    "    /* Inversion modes */\n"
    "    else if (mode == BLEND_DIFFERENCE) {\n"
    "        result = abs(src - dst);\n"
    "    }\n"
    "    else if (mode == BLEND_EXCLUSION) {\n"
    "        result = src + dst - 2.0 * src * dst;\n"
    "    }\n"
    "    /* Arithmetic modes */\n"
    "    else if (mode == BLEND_SUBTRACT) {\n"
    "        result = max(vec3(0.0), dst - src);\n"
    "    }\n"
    "    else if (mode == BLEND_DIVIDE) {\n"
    "        result = vec3(\n"
    "            src.r == 0.0 ? 1.0 : min(1.0, dst.r / src.r),\n"
    "            src.g == 0.0 ? 1.0 : min(1.0, dst.g / src.g),\n"
    "            src.b == 0.0 ? 1.0 : min(1.0, dst.b / src.b)\n"
    "        );\n"
    "    }\n"
    "    /* HSL component modes */\n"
    "    else if (mode == BLEND_HUE) {\n"
    "        result = setLum(setSat(src, sat(dst)), lum(dst));\n"
    "    }\n"
    "    else if (mode == BLEND_SATURATION) {\n"
    "        result = setLum(setSat(dst, sat(src)), lum(dst));\n"
    "    }\n"
    "    else if (mode == BLEND_COLOR) {\n"
    "        result = setLum(src, lum(dst));\n"
    "    }\n"
    "    else if (mode == BLEND_LUMINOSITY) {\n"
    "        result = setLum(dst, lum(src));\n"
    "    }\n"
    "    else {\n"
    "        result = src; /* Fallback to normal */\n"
    "    }\n"
    "    \n"
    "    return result;\n"
    "}\n"
    "\n"
    "void main() {\n"
    "    /* Discard fragments outside valid texture range */\n"
    "    if (v_texcoord.x < 0.0 || v_texcoord.x > 1.0 || v_texcoord.y < 0.0 || v_texcoord.y > 1.0) {\n"
    "        discard;\n"
    "    }\n"
    "    \n"
    "    /* Sample source layer (premultiplied alpha from Cairo) */\n"
    "    vec4 src_pm = texture2D(u_texture, v_texcoord);\n"
    "    \n"
    "    /* Convert to straight alpha for blend calculations */\n"
    "    vec3 src_rgb = src_pm.a > 0.0 ? src_pm.rgb / src_pm.a : vec3(0.0);\n"
    "    float src_a = src_pm.a * u_opacity;\n"
    "    \n"
    "    /* For first layer or fully transparent source, just output the source */\n"
    "    if (u_is_first_layer || src_a <= 0.0) {\n"
    "        /* Output premultiplied alpha */\n"
    "        gl_FragColor = vec4(src_rgb * src_a, src_a);\n"
    "        return;\n"
    "    }\n"
    "    \n"
    "    /* Sample destination (premultiplied alpha) */\n"
    "    vec2 dst_uv = gl_FragCoord.xy / u_tile_size;\n"
    "    vec4 dst_pm = texture2D(u_dst_texture, dst_uv);\n"
    "    \n"
    "    /* Convert destination to straight alpha */\n"
    "    vec3 dst_rgb = dst_pm.a > 0.0 ? dst_pm.rgb / dst_pm.a : vec3(0.0);\n"
    "    float dst_a = dst_pm.a;\n"
    "    \n"
    "    /* Apply blend mode to get blended color */\n"
    "    vec3 blended = applyBlendMode(src_rgb, dst_rgb, u_blend_mode);\n"
    "    \n"
    "    /* Porter-Duff OVER compositing with blended color */\n"
    "    /* result_a = src_a + dst_a * (1 - src_a) */\n"
    "    /* result_rgb = (blended * src_a + dst_rgb * dst_a * (1 - src_a)) / result_a */\n"
    "    float result_a = src_a + dst_a * (1.0 - src_a);\n"
    "    vec3 result_rgb;\n"
    "    if (result_a > 0.0) {\n"
    "        result_rgb = (blended * src_a + dst_rgb * dst_a * (1.0 - src_a)) / result_a;\n"
    "    } else {\n"
    "        result_rgb = vec3(0.0);\n"
    "    }\n"
    "    \n"
    "    /* Output premultiplied alpha */\n"
    "    gl_FragColor = vec4(result_rgb * result_a, result_a);\n"
    "}\n";

/* Fullscreen quad vertices (position + texcoord) */
static const float QUAD_VERTICES[] = {
    /* Position (x, y), TexCoord (u, v) */
    -1.0f, -1.0f,  0.0f, 0.0f,
     1.0f, -1.0f,  1.0f, 0.0f,
     1.0f,  1.0f,  1.0f, 1.0f,
    -1.0f, -1.0f,  0.0f, 0.0f,
     1.0f,  1.0f,  1.0f, 1.0f,
    -1.0f,  1.0f,  0.0f, 1.0f,
};

/**
 * Load OpenGL extension functions (Windows only)
 */
#ifdef _WIN32
static gboolean load_gl_extensions(void) {
    glGenFramebuffers = (PFNGLGENFRAMEBUFFERSPROC)glfwGetProcAddress("glGenFramebuffers");
    glBindFramebuffer = (PFNGLBINDFRAMEBUFFERPROC)glfwGetProcAddress("glBindFramebuffer");
    glFramebufferTexture2D = (PFNGLFRAMEBUFFERTEXTURE2DPROC)glfwGetProcAddress("glFramebufferTexture2D");
    glCheckFramebufferStatus = (PFNGLCHECKFRAMEBUFFERSTATUSPROC)glfwGetProcAddress("glCheckFramebufferStatus");
    glDeleteFramebuffers = (PFNGLDELETEFRAMEBUFFERSPROC)glfwGetProcAddress("glDeleteFramebuffers");
    glGenBuffers = (PFNGLGENBUFFERSPROC)glfwGetProcAddress("glGenBuffers");
    glBindBuffer = (PFNGLBINDBUFFERPROC)glfwGetProcAddress("glBindBuffer");
    glBufferData = (PFNGLBUFFERDATAPROC)glfwGetProcAddress("glBufferData");
    glDeleteBuffers = (PFNGLDELETEBUFFERSPROC)glfwGetProcAddress("glDeleteBuffers");
    glCreateShader = (PFNGLCREATESHADERPROC)glfwGetProcAddress("glCreateShader");
    glShaderSource = (PFNGLSHADERSOURCEPROC)glfwGetProcAddress("glShaderSource");
    glCompileShader = (PFNGLCOMPILESHADERPROC)glfwGetProcAddress("glCompileShader");
    glGetShaderiv = (PFNGLGETSHADERIVPROC)glfwGetProcAddress("glGetShaderiv");
    glGetShaderInfoLog = (PFNGLGETSHADERINFOLOGPROC)glfwGetProcAddress("glGetShaderInfoLog");
    glCreateProgram = (PFNGLCREATEPROGRAMPROC)glfwGetProcAddress("glCreateProgram");
    glAttachShader = (PFNGLATTACHSHADERPROC)glfwGetProcAddress("glAttachShader");
    glLinkProgram = (PFNGLLINKPROGRAMPROC)glfwGetProcAddress("glLinkProgram");
    glGetProgramiv = (PFNGLGETPROGRAMIVPROC)glfwGetProcAddress("glGetProgramiv");
    glGetProgramInfoLog = (PFNGLGETPROGRAMINFOLOGPROC)glfwGetProcAddress("glGetProgramInfoLog");
    glDeleteShader = (PFNGLDELETESHADERPROC)glfwGetProcAddress("glDeleteShader");
    glUseProgram = (PFNGLUSEPROGRAMPROC)glfwGetProcAddress("glUseProgram");
    glGetUniformLocation = (PFNGLGETUNIFORMLOCATIONPROC)glfwGetProcAddress("glGetUniformLocation");
    glUniform1i = (PFNGLUNIFORM1IPROC)glfwGetProcAddress("glUniform1i");
    glUniform1f = (PFNGLUNIFORM1FPROC)glfwGetProcAddress("glUniform1f");
    glUniform2f = (PFNGLUNIFORM2FPROC)glfwGetProcAddress("glUniform2f");
    glDeleteProgram = (PFNGLDELETEPROGRAMPROC)glfwGetProcAddress("glDeleteProgram");
    glActiveTexture = (PFNGLACTIVETEXTUREPROC)glfwGetProcAddress("glActiveTexture");
    glGenVertexArrays = (PFNGLGENVERTEXARRAYSPROC)glfwGetProcAddress("glGenVertexArrays");
    glBindVertexArray = (PFNGLBINDVERTEXARRAYPROC)glfwGetProcAddress("glBindVertexArray");
    glDeleteVertexArrays = (PFNGLDELETEVERTEXARRAYSPROC)glfwGetProcAddress("glDeleteVertexArrays");
    glEnableVertexAttribArray = (PFNGLENABLEVERTEXATTRIBARRAYPROC)glfwGetProcAddress("glEnableVertexAttribArray");
    glVertexAttribPointer = (PFNGLVERTEXATTRIBPOINTERPROC)glfwGetProcAddress("glVertexAttribPointer");
    
    /* Check required functions */
    if (!glGenFramebuffers || !glBindFramebuffer || !glCreateShader || !glCreateProgram) {
        g_warning("GPU Compositor: Failed to load required OpenGL extensions");
        return FALSE;
    }
    
    return TRUE;
}
#endif

/**
 * Free a texture cache entry
 */
static void texture_cache_entry_free(gpointer data) {
    LayerTextureCache* entry = (LayerTextureCache*)data;
    if (entry) {
        if (entry->texture_id != 0) {
            glDeleteTextures(1, &entry->texture_id);
        }
        g_free(entry);
    }
}

/**
 * Initialize GLFW if not already initialized
 */
static gboolean ensure_glfw_initialized(void) {
    g_mutex_lock(&g_glfw_mutex);
    
    if (!g_glfw_initialized) {
        if (!glfwInit()) {
            g_warning("GPU Compositor: Failed to initialize GLFW");
            g_mutex_unlock(&g_glfw_mutex);
            return FALSE;
        }
        g_glfw_initialized = TRUE;
    }
    
    g_glfw_ref_count++;
    g_mutex_unlock(&g_glfw_mutex);
    return TRUE;
}

/**
 * Release GLFW reference
 */
static void release_glfw(void) {
    g_mutex_lock(&g_glfw_mutex);
    
    g_glfw_ref_count--;
    if (g_glfw_ref_count <= 0 && g_glfw_initialized) {
        glfwTerminate();
        g_glfw_initialized = FALSE;
        g_glfw_ref_count = 0;
    }
    
    g_mutex_unlock(&g_glfw_mutex);
}

/**
 * Compile a shader
 */
static GLuint compile_shader(GLenum type, const char* source) {
    GLuint shader = glCreateShader(type);
    if (shader == 0) {
        return 0;
    }
    
    glShaderSource(shader, 1, &source, NULL);
    glCompileShader(shader);
    
    GLint status;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
    
    if (status == GL_FALSE) {
        char info_log[512];
        glGetShaderInfoLog(shader, sizeof(info_log), NULL, info_log);
        g_warning("GPU Compositor: Shader compilation failed: %s", info_log);
        glDeleteShader(shader);
        return 0;
    }
    
    return shader;
}

/**
 * Create and link shader program
 */
static GLuint create_shader_program(void) {
    GLuint vertex_shader = compile_shader(GL_VERTEX_SHADER, VERTEX_SHADER_SOURCE);
    if (vertex_shader == 0) {
        return 0;
    }
    
    GLuint fragment_shader = compile_shader(GL_FRAGMENT_SHADER, FRAGMENT_SHADER_SOURCE);
    if (fragment_shader == 0) {
        glDeleteShader(vertex_shader);
        return 0;
    }
    
    GLuint program = glCreateProgram();
    if (program == 0) {
        glDeleteShader(vertex_shader);
        glDeleteShader(fragment_shader);
        return 0;
    }
    
    glAttachShader(program, vertex_shader);
    glAttachShader(program, fragment_shader);
    glLinkProgram(program);
    
    /* Shaders can be deleted after linking */
    glDeleteShader(vertex_shader);
    glDeleteShader(fragment_shader);
    
    GLint status;
    glGetProgramiv(program, GL_LINK_STATUS, &status);
    
    if (status == GL_FALSE) {
        char info_log[512];
        glGetProgramInfoLog(program, sizeof(info_log), NULL, info_log);
        g_warning("GPU Compositor: Shader program linking failed: %s", info_log);
        glDeleteProgram(program);
        return 0;
    }
    
    return program;
}

/**
 * Check if GPU acceleration is available
 */
gboolean gpu_compositor_is_available(void) {
    if (!ensure_glfw_initialized()) {
        return FALSE;
    }
    
    /* Try to create a test window to check OpenGL support */
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 2);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
    
    GLFWwindow* test_window = glfwCreateWindow(1, 1, "", NULL, NULL);
    if (!test_window) {
        release_glfw();
        return FALSE;
    }
    
    glfwDestroyWindow(test_window);
    release_glfw();
    return TRUE;
}

/**
 * Get list of available GPU devices
 */
GPUDeviceInfo* gpu_compositor_get_device_list(gint* count) {
    GPUDeviceInfo* devices = NULL;
    *count = 0;
    
    if (!ensure_glfw_initialized()) {
        return NULL;
    }
    
    /* GLFW doesn't provide direct GPU enumeration, but we can get info 
     * from the default context. For multi-GPU systems, users would need
     * to use platform-specific APIs (CUDA, WGL, etc.) */
    
    /* Create a hidden window to query GPU info */
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 2);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
    
    GLFWwindow* window = glfwCreateWindow(1, 1, "", NULL, NULL);
    if (!window) {
        release_glfw();
        return NULL;
    }
    
    glfwMakeContextCurrent(window);
    
    /* Get GPU info from OpenGL */
    const char* vendor = (const char*)glGetString(GL_VENDOR);
    const char* renderer = (const char*)glGetString(GL_RENDERER);
    
    /* Create single device entry for the default GPU */
    devices = g_new0(GPUDeviceInfo, 1);
    *count = 1;
    
    devices[0].name = g_strdup(renderer ? renderer : "Default GPU");
    devices[0].vendor = g_strdup(vendor ? vendor : "Unknown");
    devices[0].renderer = g_strdup(renderer ? renderer : "Unknown");
    devices[0].index = 0;
    devices[0].is_default = TRUE;
    
    glfwDestroyWindow(window);
    release_glfw();
    
    return devices;
}

/**
 * Free device list
 */
void gpu_compositor_free_device_list(GPUDeviceInfo* devices, gint count) {
    if (!devices) {
        return;
    }
    
    for (gint i = 0; i < count; i++) {
        g_free(devices[i].name);
        g_free(devices[i].vendor);
        g_free(devices[i].renderer);
    }
    g_free(devices);
}

/**
 * Create GPU compositor
 */
GPUCompositor* gpu_compositor_create(const gchar* device_name) {
    GPUCompositor* compositor = g_new0(GPUCompositor, 1);
    
    g_mutex_init(&compositor->mutex);
    
    /* Initialize GLFW */
    if (!ensure_glfw_initialized()) {
        g_free(compositor);
        return NULL;
    }
    
    /* Create hidden window for OpenGL context */
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 2);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
    glfwWindowHint(GLFW_DOUBLEBUFFER, GLFW_FALSE); /* Single-buffered for FBO rendering */
    
    compositor->window = glfwCreateWindow(1, 1, "RasterLab GPU Compositor", NULL, NULL);
    if (!compositor->window) {
        g_warning("GPU Compositor: Failed to create GLFW window");
        release_glfw();
        g_mutex_clear(&compositor->mutex);
        g_free(compositor);
        return NULL;
    }
    
    glfwMakeContextCurrent(compositor->window);
    
#ifdef _WIN32
    /* Load OpenGL extensions on Windows */
    if (!load_gl_extensions()) {
        glfwDestroyWindow(compositor->window);
        release_glfw();
        g_mutex_clear(&compositor->mutex);
        g_free(compositor);
        return NULL;
    }
#endif
    
    /* Get GPU info */
    const char* vendor = (const char*)glGetString(GL_VENDOR);
    const char* renderer = (const char*)glGetString(GL_RENDERER);
    
    compositor->active_device.name = g_strdup(renderer ? renderer : "Unknown GPU");
    compositor->active_device.vendor = g_strdup(vendor ? vendor : "Unknown");
    compositor->active_device.renderer = g_strdup(renderer ? renderer : "Unknown");
    compositor->active_device.index = 0;
    compositor->active_device.is_default = TRUE;
    
    g_message("GPU Compositor: Using %s (%s)", 
              compositor->active_device.name, 
              compositor->active_device.vendor);
    
    /* Create shader program */
    compositor->shader_program = create_shader_program();
    if (compositor->shader_program == 0) {
        g_warning("GPU Compositor: Failed to create shader program");
        glfwDestroyWindow(compositor->window);
        release_glfw();
        g_mutex_clear(&compositor->mutex);
        g_free(compositor->active_device.name);
        g_free(compositor->active_device.vendor);
        g_free(compositor->active_device.renderer);
        g_free(compositor);
        return NULL;
    }
    
    /* Get uniform locations */
    compositor->u_texture_loc = glGetUniformLocation(compositor->shader_program, "u_texture");
    compositor->u_dst_texture_loc = glGetUniformLocation(compositor->shader_program, "u_dst_texture");
    compositor->u_opacity_loc = glGetUniformLocation(compositor->shader_program, "u_opacity");
    compositor->u_tex_offset_loc = glGetUniformLocation(compositor->shader_program, "u_tex_offset");
    compositor->u_tex_scale_loc = glGetUniformLocation(compositor->shader_program, "u_tex_scale");
    compositor->u_blend_mode_loc = glGetUniformLocation(compositor->shader_program, "u_blend_mode");
    compositor->u_is_first_layer_loc = glGetUniformLocation(compositor->shader_program, "u_is_first_layer");
    compositor->u_tile_size_loc = glGetUniformLocation(compositor->shader_program, "u_tile_size");
    
    /* Create VAO and VBO for fullscreen quad */
    if (glGenVertexArrays) {
        glGenVertexArrays(1, &compositor->vao);
        glBindVertexArray(compositor->vao);
    }
    
    glGenBuffers(1, &compositor->vbo);
    glBindBuffer(GL_ARRAY_BUFFER, compositor->vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(QUAD_VERTICES), QUAD_VERTICES, GL_STATIC_DRAW);
    
    /* Set up vertex attributes */
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
    
    /* Create texture cache */
    compositor->texture_cache = g_hash_table_new_full(g_direct_hash, g_direct_equal,
                                                       NULL, texture_cache_entry_free);
    
    /* Create two FBOs for ping-pong rendering (blend modes need to read destination) */
    glGenFramebuffers(2, compositor->fbo);
    glGenTextures(2, compositor->fbo_texture);
    compositor->fbo_width = 0;
    compositor->fbo_height = 0;
    compositor->current_fbo = 0;
    
    compositor->initialized = TRUE;
    compositor->tiles_composited = 0;
    compositor->memory_used = 0;
    
    return compositor;
}

/**
 * Destroy GPU compositor
 */
void gpu_compositor_destroy(GPUCompositor* compositor) {
    if (!compositor) {
        return;
    }
    
    g_mutex_lock(&compositor->mutex);
    
    if (compositor->window) {
        glfwMakeContextCurrent(compositor->window);
        
        /* Clean up OpenGL resources */
        if (compositor->shader_program) {
            glDeleteProgram(compositor->shader_program);
        }
        
        /* Delete both ping-pong FBOs */
        if (compositor->fbo[0] || compositor->fbo[1]) {
            glDeleteFramebuffers(2, compositor->fbo);
        }
        
        if (compositor->fbo_texture[0] || compositor->fbo_texture[1]) {
            glDeleteTextures(2, compositor->fbo_texture);
        }
        
        if (compositor->vao && glDeleteVertexArrays) {
            glDeleteVertexArrays(1, &compositor->vao);
        }
        
        if (compositor->vbo) {
            glDeleteBuffers(1, &compositor->vbo);
        }
        
        /* Free texture cache */
        if (compositor->texture_cache) {
            g_hash_table_destroy(compositor->texture_cache);
        }
        
        glfwDestroyWindow(compositor->window);
        release_glfw();
    }
    
    /* Free device info */
    g_free(compositor->active_device.name);
    g_free(compositor->active_device.vendor);
    g_free(compositor->active_device.renderer);
    
    g_mutex_unlock(&compositor->mutex);
    g_mutex_clear(&compositor->mutex);
    
    g_free(compositor);
}

/**
 * Check if GPU compositor is ready
 */
gboolean gpu_compositor_is_ready(GPUCompositor* compositor) {
    if (!compositor) {
        return FALSE;
    }
    return compositor->initialized && compositor->window != NULL;
}

/**
 * Get active GPU device info
 */
const GPUDeviceInfo* gpu_compositor_get_active_device(GPUCompositor* compositor) {
    if (!compositor || !compositor->initialized) {
        return NULL;
    }
    return &compositor->active_device;
}

/**
 * Ensure both FBOs are the right size for the tile (ping-pong rendering)
 */
static gboolean ensure_fbo_size(GPUCompositor* compositor, gint width, gint height) {
    if (compositor->fbo_width == width && compositor->fbo_height == height) {
        return TRUE;
    }
    
    /* Resize both FBO textures for ping-pong rendering */
    for (int i = 0; i < 2; i++) {
        glBindTexture(GL_TEXTURE_2D, compositor->fbo_texture[i]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_BGRA, GL_UNSIGNED_BYTE, NULL);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        
        /* Attach texture to FBO */
        glBindFramebuffer(GL_FRAMEBUFFER, compositor->fbo[i]);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, compositor->fbo_texture[i], 0);
        
        /* Check FBO status */
        GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
        if (status != GL_FRAMEBUFFER_COMPLETE) {
            g_warning("GPU Compositor: Framebuffer %d incomplete: 0x%x", i, status);
            return FALSE;
        }
    }
    
    compositor->fbo_width = width;
    compositor->fbo_height = height;
    
    return TRUE;
}

/**
 * Get or create texture for a layer
 */
static GLuint get_layer_texture(GPUCompositor* compositor, ImageLayer* layer) {
    LayerTextureCache* cache = g_hash_table_lookup(compositor->texture_cache, layer);
    
    /* Check if cached texture is still valid:
     * - cache exists and is marked valid
     * - dimensions match
     * - content version matches (layer hasn't been modified) */
    if (cache && cache->valid && 
        cache->width == layer->width && cache->height == layer->height &&
        cache->content_version == layer->content_version) {
        cache->last_used = compositor->frame_counter;
        return cache->texture_id;
    }
    
    /* Need to create or update texture */
    if (!layer->surface) {
        return 0;
    }
    
    guint8* layer_data = cairo_image_surface_get_data(layer->surface);
    gint layer_stride = cairo_image_surface_get_stride(layer->surface);
    
    if (!layer_data) {
        return 0;
    }
    
    if (!cache) {
        cache = g_new0(LayerTextureCache, 1);
        cache->layer = layer;
        glGenTextures(1, &cache->texture_id);
        g_hash_table_insert(compositor->texture_cache, layer, cache);
    }
    
    /* Upload texture data */
    glBindTexture(GL_TEXTURE_2D, cache->texture_id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    
    /* Cairo uses BGRA format */
    glPixelStorei(GL_UNPACK_ROW_LENGTH, layer_stride / 4);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, layer->width, layer->height, 0,
                 GL_BGRA, GL_UNSIGNED_BYTE, layer_data);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    
    cache->width = layer->width;
    cache->height = layer->height;
    cache->content_version = layer->content_version;
    cache->last_used = compositor->frame_counter;
    cache->valid = TRUE;
    
    /* Update memory usage estimate */
    compositor->memory_used += layer->width * layer->height * 4;
    
    return cache->texture_id;
}

/**
 * Composite a tile using GPU
 * 
 * NOTE: This implementation only supports NORMAL blend mode correctly.
 * Non-normal blend modes require reading the destination framebuffer content,
 * which would need ping-pong rendering (too complex for current implementation).
 * The tile_worker falls back to CPU compositing for non-normal blend modes.
 */
gboolean gpu_compositor_composite_tile(GPUCompositor* compositor,
                                       ImageDocument* doc,
                                       Tile* tile,
                                       gint tile_x,
                                       gint tile_y) {
    if (!compositor || !compositor->initialized || !doc || !tile || !tile->pixel_buffer) {
        return FALSE;
    }
    
    g_mutex_lock(&compositor->mutex);
    
    glfwMakeContextCurrent(compositor->window);
    
    /* Ensure both FBOs are correct size */
    if (!ensure_fbo_size(compositor, tile->w, tile->h)) {
        g_mutex_unlock(&compositor->mutex);
        return FALSE;
    }
    
    /* Clear both FBOs to transparent */
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    for (int i = 0; i < 2; i++) {
        glBindFramebuffer(GL_FRAMEBUFFER, compositor->fbo[i]);
        glClear(GL_COLOR_BUFFER_BIT);
    }
    
    /* Disable OpenGL blending - we handle blending in the shader */
    glDisable(GL_BLEND);
    
    /* Use shader program */
    glUseProgram(compositor->shader_program);
    
    /* Bind VAO */
    if (compositor->vao && glBindVertexArray) {
        glBindVertexArray(compositor->vao);
    }
    
    /* Set viewport */
    glViewport(0, 0, tile->w, tile->h);
    
    /* Set tile size uniform for destination UV calculation */
    glUniform2f(compositor->u_tile_size_loc, (GLfloat)tile->w, (GLfloat)tile->h);
    
    /* Calculate tile bounds in document coordinates */
    gint tile_left = tile->px;
    gint tile_top = tile->py;
    gint tile_right = tile->px + tile->w;
    gint tile_bottom = tile->py + tile->h;
    
    /* Reset ping-pong state */
    compositor->current_fbo = 0;
    gboolean is_first_visible_layer = TRUE;
    
    /* Composite each visible layer using ping-pong rendering */
    for (GList* iter = doc->layers; iter; iter = iter->next) {
        ImageLayer* layer = (ImageLayer*)iter->data;
        
        if (!layer || !layer->visible || layer->opacity <= 0.0 || !layer->surface) {
            continue;
        }
        
        /* Check if layer intersects tile */
        gint layer_left = layer->offset_x;
        gint layer_top = layer->offset_y;
        gint layer_right = layer_left + layer->width;
        gint layer_bottom = layer_top + layer->height;
        
        if (layer_right <= tile_left || layer_left >= tile_right ||
            layer_bottom <= tile_top || layer_top >= tile_bottom) {
            continue; /* No intersection */
        }
        
        /* Get layer texture */
        GLuint layer_texture = get_layer_texture(compositor, layer);
        if (layer_texture == 0) {
            continue;
        }
        
        /* Calculate which portion of the layer texture corresponds to the tile
         * 
         * The tile covers [tile_left, tile_right) x [tile_top, tile_bottom) in document coords
         * The layer covers [layer_left, layer_right) x [layer_top, layer_bottom) in document coords
         * Layer texture UV is [0,1] x [0,1] mapping to the layer's bounds
         */
        GLfloat tex_offset_x = (GLfloat)(tile_left - layer_left) / (GLfloat)layer->width;
        GLfloat tex_offset_y = (GLfloat)(tile_top - layer_top) / (GLfloat)layer->height;
        GLfloat tex_scale_x = (GLfloat)tile->w / (GLfloat)layer->width;
        GLfloat tex_scale_y = (GLfloat)tile->h / (GLfloat)layer->height;
        
        /* Ping-pong: bind current FBO for writing, previous FBO texture for reading */
        gint write_fbo = compositor->current_fbo;
        gint read_fbo = 1 - write_fbo;
        
        glBindFramebuffer(GL_FRAMEBUFFER, compositor->fbo[write_fbo]);
        
        /* Bind source layer texture to unit 0 */
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, layer_texture);
        glUniform1i(compositor->u_texture_loc, 0);
        
        /* Bind destination (previous result) texture to unit 1 */
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, compositor->fbo_texture[read_fbo]);
        glUniform1i(compositor->u_dst_texture_loc, 1);
        
        /* Set texture coordinate transformation */
        glUniform2f(compositor->u_tex_offset_loc, tex_offset_x, tex_offset_y);
        glUniform2f(compositor->u_tex_scale_loc, tex_scale_x, tex_scale_y);
        
        /* Set opacity */
        glUniform1f(compositor->u_opacity_loc, (GLfloat)layer->opacity);
        
        /* Set blend mode (first layer always uses NORMAL internally) */
        glUniform1i(compositor->u_blend_mode_loc, is_first_visible_layer ? 0 : (gint)layer->blend_mode);
        glUniform1i(compositor->u_is_first_layer_loc, is_first_visible_layer ? 1 : 0);
        
        /* Draw fullscreen quad */
        glDrawArrays(GL_TRIANGLES, 0, 6);
        
        /* Swap FBOs for next layer */
        compositor->current_fbo = read_fbo;
        is_first_visible_layer = FALSE;
    }
    
    /* Result is in the FBO we last wrote to (which is now current_fbo after the swap) */
    gint result_fbo = 1 - compositor->current_fbo;
    
    /* Bind result FBO for reading */
    glBindFramebuffer(GL_FRAMEBUFFER, compositor->fbo[result_fbo]);
    
    /* Read back pixels to tile buffer
     * 
     * OpenGL coordinate flow:
     * - glTexImage2D places Cairo row 0 at texture V=0
     * - At screen bottom (vertex y=-1, texcoord v=0), we sample V=tex_offset
     * - For tile at document top (tex_offset=0), screen bottom gets image top
     * - FBO pixel y=0 (bottom) contains what was rendered at screen bottom = image top
     * - glReadPixels writes FBO y=0 to buffer[0]
     * - So buffer[0] = image top, which is correct for Cairo (no flip needed)
     */
    glPixelStorei(GL_PACK_ROW_LENGTH, tile->stride / 4);
    glReadPixels(0, 0, tile->w, tile->h, GL_BGRA, GL_UNSIGNED_BYTE, tile->pixel_buffer);
    glPixelStorei(GL_PACK_ROW_LENGTH, 0);
    
    /* Unbind FBO */
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    
    compositor->tiles_composited++;
    compositor->frame_counter++;
    
    g_mutex_unlock(&compositor->mutex);
    
    return TRUE;
}

/**
 * Upload a layer's surface to GPU texture cache
 */
gboolean gpu_compositor_upload_layer(GPUCompositor* compositor, ImageLayer* layer) {
    if (!compositor || !compositor->initialized || !layer || !layer->surface) {
        return FALSE;
    }
    
    g_mutex_lock(&compositor->mutex);
    glfwMakeContextCurrent(compositor->window);
    
    GLuint texture = get_layer_texture(compositor, layer);
    
    g_mutex_unlock(&compositor->mutex);
    
    return texture != 0;
}

/**
 * Invalidate a layer's GPU texture cache
 */
void gpu_compositor_invalidate_layer(GPUCompositor* compositor, ImageLayer* layer) {
    if (!compositor || !layer) {
        return;
    }
    
    g_mutex_lock(&compositor->mutex);
    
    LayerTextureCache* cache = g_hash_table_lookup(compositor->texture_cache, layer);
    if (cache) {
        cache->valid = FALSE;
    }
    
    g_mutex_unlock(&compositor->mutex);
}

/**
 * Clear all GPU texture caches
 */
void gpu_compositor_clear_cache(GPUCompositor* compositor) {
    if (!compositor) {
        return;
    }
    
    g_mutex_lock(&compositor->mutex);
    
    if (compositor->window) {
        glfwMakeContextCurrent(compositor->window);
    }
    
    if (compositor->texture_cache) {
        g_hash_table_remove_all(compositor->texture_cache);
    }
    
    compositor->memory_used = 0;
    
    g_mutex_unlock(&compositor->mutex);
}

/**
 * Get GPU compositor statistics
 */
void gpu_compositor_get_stats(GPUCompositor* compositor,
                              guint64* tiles_composited,
                              guint* textures_cached,
                              gsize* memory_used) {
    if (!compositor) {
        if (tiles_composited) *tiles_composited = 0;
        if (textures_cached) *textures_cached = 0;
        if (memory_used) *memory_used = 0;
        return;
    }
    
    g_mutex_lock(&compositor->mutex);
    
    if (tiles_composited) {
        *tiles_composited = compositor->tiles_composited;
    }
    if (textures_cached && compositor->texture_cache) {
        *textures_cached = g_hash_table_size(compositor->texture_cache);
    }
    if (memory_used) {
        *memory_used = compositor->memory_used;
    }
    
    g_mutex_unlock(&compositor->mutex);
}
