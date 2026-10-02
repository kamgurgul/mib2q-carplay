/*
 * altscreen_render — GLES2 YUV420P -> RGB presenter. See video_gles.h.
 *
 * Copyright (c) 2026
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "video_gles.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef PLATFORM_QNX

#include <GLES2/gl2.h>

#ifndef GL_UNPACK_ROW_LENGTH
#define GL_UNPACK_ROW_LENGTH 0x0CF2
#endif

struct altr_gles {
    int sw, sh;
    GLuint prog;
    GLuint texY, texU, texV;
    int yW, yH, uW, uH, vW, vH; /* allocated texture storage; 0 until the first upload */
    int unpack_row;     /* GL_EXT_unpack_subimage */
    uint8_t *packY, *packU, *packV;
    size_t packYcap, packUcap, packVcap;
    GLint aPos, aTex, uY, uU, uV;
};

static const char *VS =
    "attribute vec2 aPos;\n"
    "attribute vec2 aTex;\n"
    "varying vec2 vTex;\n"
    "void main(){ vTex=aTex; gl_Position=vec4(aPos,0.0,1.0); }\n";

/* BT.601 video-range (16-235 / 16-240) YUV -> RGB. CarPlay cluster video is
 * SD-range 4:2:0; if a build ever needs full-range or BT.709, this is the one
 * place to change. */
static const char *FS =
    "precision mediump float;\n"
    "varying vec2 vTex;\n"
    "uniform sampler2D uY;\n"
    "uniform sampler2D uU;\n"
    "uniform sampler2D uV;\n"
    "void main(){\n"
    "  float y=texture2D(uY,vTex).r;\n"
    "  float u=texture2D(uU,vTex).r-0.5;\n"
    "  float v=texture2D(uV,vTex).r-0.5;\n"
    "  y=1.1643*(y-0.0625);\n"
    "  float r=y+1.5958*v;\n"
    "  float g=y-0.39173*u-0.81290*v;\n"
    "  float b=y+2.017*u;\n"
    "  gl_FragColor=vec4(r,g,b,1.0);\n"
    "}\n";

static GLuint compile(GLenum type, const char *src)
{
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512]; GLsizei n = 0;
        glGetShaderInfoLog(s, sizeof(log), &n, log);
        fprintf(stderr, "altr_gles: shader compile failed: %.*s\n", (int)n, log);
        glDeleteShader(s);
        return 0;
    }
    return s;
}

static GLuint mktex(void)
{
    GLuint t = 0;
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return t;
}

altr_gles_t *altr_gles_create(int surface_w, int surface_h)
{
    altr_gles_t *g = (altr_gles_t *)calloc(1, sizeof(*g));
    if (!g) return NULL;
    g->sw = surface_w; g->sh = surface_h;

    GLuint vs = compile(GL_VERTEX_SHADER, VS);
    GLuint fs = compile(GL_FRAGMENT_SHADER, FS);
    if (!vs || !fs) { free(g); return NULL; }
    g->prog = glCreateProgram();
    glAttachShader(g->prog, vs);
    glAttachShader(g->prog, fs);
    glBindAttribLocation(g->prog, 0, "aPos");
    glBindAttribLocation(g->prog, 1, "aTex");
    glLinkProgram(g->prog);
    glDeleteShader(vs); glDeleteShader(fs);
    GLint ok = 0; glGetProgramiv(g->prog, GL_LINK_STATUS, &ok);
    if (!ok) { fprintf(stderr, "altr_gles: link failed\n"); free(g); return NULL; }

    g->aPos = 0; g->aTex = 1;
    g->uY = glGetUniformLocation(g->prog, "uY");
    g->uU = glGetUniformLocation(g->prog, "uU");
    g->uV = glGetUniformLocation(g->prog, "uV");
    g->texY = mktex(); g->texU = mktex(); g->texV = mktex();
    {
        const char *ext = (const char *)glGetString(GL_EXTENSIONS);
        g->unpack_row = ext && strstr(ext, "GL_EXT_unpack_subimage") != NULL;
    }

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    return g;
}

static int ensure_pack(uint8_t **buf, size_t *cap, size_t need)
{
    uint8_t *n;
    if (*cap >= need) return 0;
    n = (uint8_t *)realloc(*buf, need);
    if (!n) return -1;
    *buf = n;
    *cap = need;
    return 0;
}

/* One upload per plane. Storage is allocated only when the size changes.
 * Padded FFmpeg linesizes are packed, or passed through GL_UNPACK_ROW_LENGTH
 * when the driver has GL_EXT_unpack_subimage. */
static int upload_plane(altr_gles_t *g, GLuint tex, int unit, const uint8_t *data,
                        int stride, int w, int h, int *allocW, int *allocH,
                        uint8_t **pack, size_t *packcap)
{
    const uint8_t *src = data;
    int row;
    int use_rowlen = 0;

    if (!data || w <= 0 || h <= 0 || stride < w) return -1;
    if (stride != w) {
        if (g->unpack_row) {
            use_rowlen = 1;
        } else {
            if (ensure_pack(pack, packcap, (size_t)w * (size_t)h) != 0) return -1;
            for (row = 0; row < h; row++)
                memcpy(*pack + (size_t)row * (size_t)w,
                       data + (size_t)row * (size_t)stride, (size_t)w);
            src = *pack;
        }
    }
    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(GL_TEXTURE_2D, tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    if (use_rowlen) glPixelStorei(GL_UNPACK_ROW_LENGTH, stride);
    if (*allocW != w || *allocH != h) {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, w, h, 0,
                     GL_LUMINANCE, GL_UNSIGNED_BYTE, src);
        *allocW = w;
        *allocH = h;
    } else {
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h,
                        GL_LUMINANCE, GL_UNSIGNED_BYTE, src);
    }
    if (use_rowlen) glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    return 0;
}

int altr_gles_draw(altr_gles_t *g, const altr_frame_t *f)
{
    if (!g || !f || f->width <= 0 || f->height <= 0) return -1;
    int cw = f->width  >> f->chroma_shift_w;
    int ch = f->height >> f->chroma_shift_h;

    glViewport(0, 0, g->sw, g->sh);
    glClearColor(0.f, 0.f, 0.f, 1.f);
    glClear(GL_COLOR_BUFFER_BIT);
    glUseProgram(g->prog);

    if (upload_plane(g, g->texY, 0, f->y, f->ystride, f->width, f->height,
                     &g->yW, &g->yH, &g->packY, &g->packYcap) != 0) return -1;
    if (upload_plane(g, g->texU, 1, f->u, f->ustride, cw, ch,
                     &g->uW, &g->uH, &g->packU, &g->packUcap) != 0) return -1;
    if (upload_plane(g, g->texV, 2, f->v, f->vstride, cw, ch,
                     &g->vW, &g->vH, &g->packV, &g->packVcap) != 0) return -1;
    glUniform1i(g->uY, 0);
    glUniform1i(g->uU, 1);
    glUniform1i(g->uV, 2);

    /* Fill the whole cluster surface. Aspect handling (letterbox vs fill) is a
     * geometry decision the cluster integration owns; the default fills. */
    static const GLfloat pos[] = { -1,-1,  1,-1, -1, 1,  1, 1 };
    static const GLfloat tex[] = {  0, 1,  1, 1,  0, 0,  1, 0 };
    glVertexAttribPointer(g->aPos, 2, GL_FLOAT, GL_FALSE, 0, pos);
    glEnableVertexAttribArray(g->aPos);
    glVertexAttribPointer(g->aTex, 2, GL_FLOAT, GL_FALSE, 0, tex);
    glEnableVertexAttribArray(g->aTex);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    return 0;
}

/* Calibration ruler over the video, in surface pixels (origin top-left, as iOS
 * sees the advertised canvas). Horizontal lines every 60 px, each its own colour,
 * so one photo of the VC shows which canvas rows are actually visible. */
static void fill_rect(const altr_gles_t *g, int x, int y, int w, int h,
                      float r, float gr, float b)
{
    glScissor(x, g->sh - y - h, w, h);
    glClearColor(r, gr, b, 1.f);
    glClear(GL_COLOR_BUFFER_BIT);
}

void altr_gles_draw_grid(altr_gles_t *g)
{
    static const float colors[8][3] = {
        {1.f, 0.f, 0.f},   /*  60 red     */
        {1.f, .5f, 0.f},   /* 120 orange  */
        {1.f, 1.f, 0.f},   /* 180 yellow  */
        {0.f, 1.f, 0.f},   /* 240 green   */
        {0.f, 1.f, 1.f},   /* 300 cyan    */
        {0.f, 0.f, 1.f},   /* 360 blue    */
        {1.f, 0.f, 1.f},   /* 420 magenta */
        {1.f, 1.f, 1.f},   /* 480 white   */
    };
    int i, x;
    if (!g) return;
    glEnable(GL_SCISSOR_TEST);
    for (x = 180; x < g->sw; x += 180)
        fill_rect(g, x - 1, 0, 2, g->sh, .5f, .5f, .5f);
    for (i = 0; i < 8 && (i + 1) * 60 < g->sh; i++)
        fill_rect(g, 0, (i + 1) * 60 - 3, g->sw, 6,
                  colors[i][0], colors[i][1], colors[i][2]);
    /* Canvas edges in grey: if the bottom one is missing, the VC crops below it. */
    fill_rect(g, 0, 0, g->sw, 4, .6f, .6f, .6f);
    fill_rect(g, 0, g->sh - 4, g->sw, 4, .6f, .6f, .6f);
    fill_rect(g, 0, 0, 4, g->sh, .6f, .6f, .6f);
    fill_rect(g, g->sw - 4, 0, 4, g->sh, .6f, .6f, .6f);
    glDisable(GL_SCISSOR_TEST);
}

void altr_gles_resize(altr_gles_t *g, int w, int h){ if (g){ g->sw=w; g->sh=h; } }

void altr_gles_destroy(altr_gles_t *g)
{
    if (!g) return;
    if (g->texY) glDeleteTextures(1, &g->texY);
    if (g->texU) glDeleteTextures(1, &g->texU);
    if (g->texV) glDeleteTextures(1, &g->texV);
    if (g->prog) glDeleteProgram(g->prog);
    free(g->packY);
    free(g->packU);
    free(g->packV);
    free(g);
}

#else /* !PLATFORM_QNX — host stubs so decode stays testable */

struct altr_gles { int sw, sh; };
altr_gles_t *altr_gles_create(int w, int h){ (void)w;(void)h; return NULL; }
int altr_gles_draw(altr_gles_t *g, const altr_frame_t *f){ (void)g;(void)f; return -1; }
void altr_gles_draw_grid(altr_gles_t *g){ (void)g; }
void altr_gles_resize(altr_gles_t *g, int w, int h){ (void)g;(void)w;(void)h; }
void altr_gles_destroy(altr_gles_t *g){ (void)g; }

#endif
