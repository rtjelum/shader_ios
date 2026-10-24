/* Standalone Shadertoy
 * Copyright (C) 2014 Simon Budig <simon@budig.de>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <getopt.h>
#include <poll.h>
#include <math.h>

#ifdef __APPLE__
#include <objc/runtime.h>
#include <objc/message.h>
#endif

#include <sys/types.h>
#include <sys/stat.h>
#include <libgen.h>
#include <sys/socket.h>
#include <netinet/in.h>

#ifdef __APPLE__
#define GL_SILENCE_DEPRECATION
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl.h>
#include <OpenGL/glext.h>
#include <GLUT/glut.h>
#else
#include <GL/glew.h>
#include <GL/glut.h>
#include <GL/freeglut_ext.h>
#endif

#include <gdk-pixbuf/gdk-pixbuf.h>

/* width, height, x0, y0 (top left) */
static double geometry[4] = { 0, };

/* x, y, x_press, y_press  (in target coords) */
static double mouse[4] = { 0, };

static GLint prog = 0;

/* framebuffer pixels per window point (2 on Retina displays) */
static double pixel_scale = 1.0;
static GLenum tex[4];

/* bitmask of iChannels the shader samples as cube maps */
static int cube_channels = 0;

/* bitmask of iChannels given a cube map on the command line */
static int cube_loaded = 0;

/* suppresses compiler error output while probing channel types */
static int quiet = 0;
static int sockfd  = -1;

#ifdef __APPLE__
#define IPC_ADDR (0x7f000001)  /* macOS only configures 127.0.0.1 on lo0 */
#else
#define IPC_ADDR (0x7f000000)
#endif
#define IPC_PORT (4242)

void ipc_socket_handle_message (void);
void ipc_socket_send_message (char *msg);

void
mouse_press_handler (int button, int state, int x, int y)
{
  char msg[1000];
  int x0, y0, height;

  if (button != GLUT_LEFT_BUTTON)
    return;

  if (state == GLUT_DOWN)
    {
      x0     = glutGet (GLUT_WINDOW_X);
      y0     = glutGet (GLUT_WINDOW_Y);
      height = glutGet (GLUT_WINDOW_HEIGHT);

      if (geometry[0] > 0.1 && geometry[1] > 0.1)
        {
          mouse[2] = mouse[0] =               geometry[2] + x0 + x;
          mouse[3] = mouse[1] = geometry[1] - geometry[3] - y0 - y;
        }
      else
        {
          mouse[2] = mouse[0] = x * pixel_scale;
          mouse[3] = mouse[1] = (height - y) * pixel_scale;
        }
    }
  else
    {
      mouse[2] = -1;
      mouse[3] = -1;
    }

  snprintf (msg, sizeof (msg), "iMouse:%.0f,%.0f,%.0f,%.0f",
            mouse[0], mouse[1], mouse[2], mouse[3]);
  ipc_socket_send_message (msg);
}


void
mouse_move_handler (int x, int y)
{
  char msg[1000];
  int x0, y0, height;

      x0     = glutGet (GLUT_WINDOW_X);
      y0     = glutGet (GLUT_WINDOW_Y);
      height = glutGet (GLUT_WINDOW_HEIGHT);

      if (geometry[0] > 0.1 && geometry[1] > 0.1)
        {
          mouse[0] =               geometry[2] + x0 + x;
          mouse[1] = geometry[1] - geometry[3] - y0 - y;
        }
      else
        {
          mouse[0] = x * pixel_scale;
          mouse[1] = (height - y) * pixel_scale;
        }

  snprintf (msg, sizeof (msg), "iMouse:%.0f,%.0f,%.0f,%.0f",
            mouse[0], mouse[1], mouse[2], mouse[3]);
  ipc_socket_send_message (msg);
}


void
keyboard_handler (unsigned char key, int x, int y)
{
  switch (key)
    {
      case '\x1b':  /* Escape */
      case 'q':
      case 'Q':
#ifdef __APPLE__
        exit (0);
#else
        glutLeaveMainLoop ();
#endif
        break;

      case 'f': /* fullscreen */
      case 'F':
#ifdef __APPLE__
        {
          static int fullscreen = 0;

          if (fullscreen)
            glutReshapeWindow (800, 600);
          else
            glutFullScreen ();
          fullscreen = !fullscreen;
        }
#else
        glutFullScreenToggle ();
#endif
        break;

      default:
        break;
    }
}


/* Runs whenever GLUT has nothing else to do: handle sync messages and draw
 * the next frame. The frame rate is limited only by vsync. */
void
idle (void)
{
  struct pollfd udp;
  int ret;

  udp.fd = sockfd;
  udp.events = POLLIN;

  while ((ret = poll (&udp, 1, 0)) >= 1)
    {
      ipc_socket_handle_message ();
    }

  glutPostRedisplay ();
}


void
display (void)
{
  static int frames;
  static long long last_time;
  int x0, y0, width, height;
  long long ticks;
  float shader_time;
  GLint uindex;
  struct timespec ts;

  glUseProgram (prog);

  x0     = glutGet (GLUT_WINDOW_X);
  y0     = glutGet (GLUT_WINDOW_Y);
  width  = glutGet (GLUT_WINDOW_WIDTH);
  height = glutGet (GLUT_WINDOW_HEIGHT);
  width  *= pixel_scale;
  height *= pixel_scale;
  glViewport (0, 0, width, height);
  clock_gettime (CLOCK_MONOTONIC_RAW, &ts);
  ticks  = ts.tv_sec * 1000LL + ts.tv_nsec / 1000000;

  /* The monotonic clock (shared between instances for syncing) counts
   * from boot; as a float that loses sub-frame precision after a few days
   * of uptime, so wrap it to an hour first. */
  shader_time = fmod (ts.tv_sec + ts.tv_nsec / 1e9, 3600.0);

  if (frames == 0)
    last_time = ticks;

  frames++;

  if (ticks - last_time >= 5000)
    {
      fprintf (stderr, "FPS: %.2f\n", 1000.0 * frames / (ticks - last_time));
      frames = 0;
    }

  uindex = glGetUniformLocation (prog, "iGlobalTime");
  if (uindex >= 0)
    glUniform1f (uindex, shader_time);

  uindex = glGetUniformLocation (prog, "iTime");
  if (uindex >= 0)
    glUniform1f (uindex, shader_time);

  uindex = glGetUniformLocation (prog, "time");
  if (uindex >= 0)
    glUniform1f (uindex, shader_time);

  uindex = glGetUniformLocation (prog, "iResolution");
  if (uindex >= 0)
    {
      if (geometry[0] > 0.1 && geometry[1] > 0.1)
        glUniform3f (uindex, geometry[0], geometry[1], 1.0);
      else
        glUniform3f (uindex, width, height, 1.0);
    }

  uindex = glGetUniformLocation (prog, "iOffset");
  if (uindex >= 0)
    {
      if (geometry[0] > 0.1 && geometry[1] > 0.1)
        {
          glUniform2f (uindex,
                       x0 + geometry[2],
                       geometry[1] - (y0 + height) - geometry[3]);
        }
      else
        {
          glUniform2f (uindex, 0.0, 0.0);
        }
    }

  uindex = glGetUniformLocation (prog, "iMouse");
  if (uindex >= 0)
    glUniform4f (uindex, mouse[0],  mouse[1], mouse[2], mouse[3]);


  uindex = glGetUniformLocation (prog, "iChannel0");
  if (uindex >= 0)
    {
      glActiveTexture (GL_TEXTURE0 + 0);
      glBindTexture (cube_channels & (1 << 0) ? GL_TEXTURE_CUBE_MAP : GL_TEXTURE_2D,
                     tex[0]);
      glUniform1i (uindex, 0);
    }

  uindex = glGetUniformLocation (prog, "iChannel1");
  if (uindex >= 0)
    {
      glActiveTexture (GL_TEXTURE0 + 1);
      glBindTexture (cube_channels & (1 << 1) ? GL_TEXTURE_CUBE_MAP : GL_TEXTURE_2D,
                     tex[1]);
      glUniform1i (uindex, 1);
    }

  uindex = glGetUniformLocation (prog, "iChannel2");
  if (uindex >= 0)
    {
      glActiveTexture (GL_TEXTURE0 + 2);
      glBindTexture (cube_channels & (1 << 2) ? GL_TEXTURE_CUBE_MAP : GL_TEXTURE_2D,
                     tex[2]);
      glUniform1i (uindex, 2);
    }

  uindex = glGetUniformLocation (prog, "iChannel3");
  if (uindex >= 0)
    {
      glActiveTexture (GL_TEXTURE0 + 3);
      glBindTexture (cube_channels & (1 << 3) ? GL_TEXTURE_CUBE_MAP : GL_TEXTURE_2D,
                     tex[3]);
      glUniform1i (uindex, 3);
    }

  uindex = glGetUniformLocation (prog, "resolution");
  if (uindex >= 0)
    {
      if (geometry[0] > 0.1 && geometry[1] > 0.1)
        glUniform2f (uindex, geometry[0], geometry[1]);
      else
        glUniform2f (uindex, width, height);
    }

  uindex = glGetUniformLocation (prog, "led_color");
  if (uindex >= 0)
    glUniform3f (uindex, 0.5, 0.3, 0.8);

  glClear (GL_COLOR_BUFFER_BIT);
  glRectf (-1.0, -1.0, 1.0, 1.0);

  glutSwapBuffers ();
  /* wait for the GPU, so frames don't queue up and the FPS count is honest */
  glFinish ();
}


/* Find the image for one cube face (GL order: +X -X +Y -Y +Z -Z).
 * path is either a directory with px/nx/py/ny/pz/nz or posx/negx/...
 * images, or the first of shadertoy's files (name.jpg, name_1.jpg, ...). */
static int
cubemap_face_path (const char *path, int face, char *buf, size_t size)
{
  static const char *short_names[] = { "px", "nx", "py", "ny", "pz", "nz" };
  static const char *long_names[]  = { "posx", "negx", "posy", "negy", "posz", "negz" };
  static const char *exts[] = { "jpg", "jpeg", "png" };
  struct stat st;
  size_t e;

  if (stat (path, &st) == 0 && S_ISDIR (st.st_mode))
    {
      for (e = 0; e < sizeof (exts) / sizeof (exts[0]); e++)
        {
          snprintf (buf, size, "%s/%s.%s", path, short_names[face], exts[e]);
          if (stat (buf, &st) == 0)
            return 1;
          snprintf (buf, size, "%s/%s.%s", path, long_names[face], exts[e]);
          if (stat (buf, &st) == 0)
            return 1;
        }
      return 0;
    }
  else
    {
      const char *dot = strrchr (path, '.');
      int base = dot ? (int) (dot - path) : (int) strlen (path);

      if (face == 0)
        snprintf (buf, size, "%s", path);
      else
        snprintf (buf, size, "%.*s_%d%s", base, path, face, dot ? dot : "");
      return stat (buf, &st) == 0;
    }
}


static int
is_cubemap_path (const char *path)
{
  char face[4096];
  int i;

  for (i = 0; i < 6; i++)
    if (!cubemap_face_path (path, i, face, sizeof (face)))
      return 0;

  return 1;
}


int
load_cubemap (const char *path, GLuint *tex_id)
{
  char filename[4096];
  int face;

  glGenTextures (1, tex_id);
  glBindTexture (GL_TEXTURE_CUBE_MAP, *tex_id);

  for (face = 0; face < 6; face++)
    {
      GdkPixbuf *pixbuf;
      uint8_t *data, *packed;
      int width, height, rowstride, cpp, y;

      cubemap_face_path (path, face, filename, sizeof (filename));
      pixbuf = gdk_pixbuf_new_from_file (filename, NULL);
      if (!pixbuf || gdk_pixbuf_get_bits_per_sample (pixbuf) != 8)
        {
          fprintf (stderr, "failed to load cube face %s\n", filename);
          return 0;
        }

      width     = gdk_pixbuf_get_width (pixbuf);
      height    = gdk_pixbuf_get_height (pixbuf);
      rowstride = gdk_pixbuf_get_rowstride (pixbuf);
      cpp       = gdk_pixbuf_get_n_channels (pixbuf);
      data      = gdk_pixbuf_get_pixels (pixbuf);

      packed = malloc (width * height * cpp);
      for (y = 0; y < height; y++)
        memcpy (packed + y * width * cpp, data + y * rowstride, width * cpp);

      glTexImage2D (GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, 0, GL_RGBA,
                    width, height, 0, cpp == 3 ? GL_RGB : GL_RGBA,
                    GL_UNSIGNED_BYTE, packed);

      free (packed);
      g_object_unref (pixbuf);
    }

  glTexParameteri (GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri (GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
  glTexParameteri (GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri (GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexParameteri (GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
  glGenerateMipmap (GL_TEXTURE_CUBE_MAP);

  fprintf (stderr, "cube map: %s --> id %d\n", path, *tex_id);

  return 1;
}


int
load_texture (char    *filename,
              GLenum   type,
              GLenum  *tex_id,
              char     nearest,
              char     repeat)
{
  GdkPixbuf *pixbuf;
  int width, height;
  uint8_t *data;
  GLfloat *tex_data;
  int rowstride;
  int cpp, bps;
  int x, y, c;

  pixbuf = gdk_pixbuf_new_from_file (filename, NULL);

  width = gdk_pixbuf_get_width (pixbuf);
  height = gdk_pixbuf_get_height (pixbuf);

  data = gdk_pixbuf_get_pixels (pixbuf);
  rowstride = gdk_pixbuf_get_rowstride (pixbuf);
  bps = gdk_pixbuf_get_bits_per_sample (pixbuf);
  cpp = gdk_pixbuf_get_n_channels (pixbuf);

  if (bps != 8 && bps != 16)
    {
      fprintf (stderr, "unexpected bits per sample: %d\n", bps);
      return 0;
    }

  if (cpp != 3 && cpp != 4)
    {
      fprintf (stderr, "unexpected n_channels: %d\n", cpp);
      return 0;
    }

  tex_data = malloc (width * height * cpp * sizeof (GLfloat));
  for (y = 0; y < height; y++)
    {
      uint8_t  *cur_row8  = (uint8_t *)  (data + y * rowstride);
      uint16_t *cur_row16 = (uint16_t *) (data + y * rowstride);

      for (x = 0; x < width; x++)
        {
          for (c = 0; c < cpp; c++)
            {
              if (bps == 8)
                tex_data[(y * width + x) * cpp + c] = ((GLfloat) cur_row8[x * cpp + c]) / 255.0;
              else
                tex_data[(y * width + x) * cpp + c] = ((GLfloat) cur_row16[x * cpp + c]) / 65535.0;
            }
        }
    }

  glGenTextures (1, tex_id);
  glBindTexture (type, *tex_id);
  glTexImage2D (type, 0, GL_RGBA,
                width, height,
                0, cpp == 3 ? GL_RGB : GL_RGBA,
                GL_FLOAT,
                tex_data);
  if (nearest)
    {
      glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
      glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    }
  else
    {
      glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
      glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
      glGenerateMipmap (GL_TEXTURE_2D);
    }

  if (repeat)
    {
      glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
      glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    }
  else
    {
      glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
      glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
    }

  free (tex_data);
  g_object_unref (pixbuf);

  fprintf (stderr, "texture: %s, %dx%d, %d (%d) --> id %d\n",
           filename, width, height, cpp, bps, *tex_id);

  return 1;
}


GLint
compile_shader (const GLenum  shader_type,
                const GLchar *shader_source)
{
  GLuint shader = glCreateShader (shader_type);
  GLint status = GL_FALSE;
  GLint loglen;
  GLchar *error_message;

  glShaderSource (shader, 1, &shader_source, NULL);
  glCompileShader (shader);

  glGetShaderiv (shader, GL_COMPILE_STATUS, &status);
  if (status == GL_TRUE)
    return shader;

  glGetShaderiv (shader, GL_INFO_LOG_LENGTH, &loglen);
  error_message = calloc (loglen, sizeof (GLchar));
  glGetShaderInfoLog (shader, loglen, NULL, error_message);
  if (!quiet)
    fprintf (stderr, "shader failed to compile:\n   %s\n", error_message);
  free (error_message);

  return -1;
}


GLint
link_program (const GLchar *shader_source)
{
  GLint frag, program;
  GLint status = GL_FALSE;
  GLint loglen, n_uniforms;
  GLchar *error_message;
  GLint i;

  GLchar name[80];
  GLsizei namelen;

  frag = compile_shader (GL_FRAGMENT_SHADER, shader_source);
  if (frag < 0)
    return -1;

  program = glCreateProgram ();

  glAttachShader (program, frag);
  glLinkProgram (program);
  // glDeleteShader (frag);

  glGetProgramiv (program, GL_LINK_STATUS, &status);
  if (status != GL_TRUE)
    {
      glGetProgramiv (program, GL_INFO_LOG_LENGTH, &loglen);
      error_message = calloc (loglen, sizeof (GLchar));
      glGetProgramInfoLog (program, loglen, NULL, error_message);
      if (!quiet)
        fprintf (stderr, "program failed to link:\n   %s\n", error_message);
      free (error_message);
      return -1;
    }

  /* diagnostics */
  glGetProgramiv (program, GL_ACTIVE_UNIFORMS, &n_uniforms);
  fprintf (stderr, "%d uniforms:\n", n_uniforms);

  for (i = 0; i < n_uniforms; i++)
    {
      GLint size;
      GLenum type;

      glGetActiveUniform (program, i, 79, &namelen, &size, &type, name);
      name[namelen] = '\0';
      fprintf (stderr, "  %2d: %-20s (type: 0x%04x, size: %d)\n", i, name, type, size);
    }

  return program;
}

#ifdef __APPLE__
/* Ask Cocoa for a full-resolution (Retina) backing surface for the GLUT
 * window; by default it renders at point resolution and gets upscaled. */
void
enable_hidpi (void)
{
  id ctx, view, window;

  ctx = ((id (*) (Class, SEL)) objc_msgSend)
          (objc_getClass ("NSOpenGLContext"), sel_registerName ("currentContext"));
  view = ((id (*) (id, SEL)) objc_msgSend) (ctx, sel_registerName ("view"));
  if (!view)
    return;

  ((void (*) (id, SEL, BOOL)) objc_msgSend)
    (view, sel_registerName ("setWantsBestResolutionOpenGLSurface:"), YES);
  ((void (*) (id, SEL)) objc_msgSend) (ctx, sel_registerName ("update"));

  window = ((id (*) (id, SEL)) objc_msgSend) (view, sel_registerName ("window"));
  if (window)
    pixel_scale = ((double (*) (id, SEL)) objc_msgSend)
                    (window, sel_registerName ("backingScaleFactor"));

  fprintf (stderr, "pixel scale  : %.1f\n", pixel_scale);
}
#endif


void
init_glew (void)
{
#ifdef __APPLE__
  /* no GLEW needed: the legacy context exposes GL 2.1 directly */
  fprintf (stderr,
           "GL_VERSION   : %s\nGL_VENDOR    : %s\nGL_RENDERER  : %s\n"
           "GLSL VERSION : %s\n\n",
           glGetString (GL_VERSION), glGetString (GL_VENDOR),
           glGetString (GL_RENDERER),
           glGetString (GL_SHADING_LANGUAGE_VERSION));
#else
  GLenum status;

  status = glewInit ();

  if (status != GLEW_OK)
    {
      fprintf (stderr, "glewInit error: %s\n", glewGetErrorString (status));
      exit (-1);
    }

  fprintf (stderr,
           "GL_VERSION   : %s\nGL_VENDOR    : %s\nGL_RENDERER  : %s\n"
           "GLEW_VERSION : %s\nGLSL VERSION : %s\n\n",
           glGetString (GL_VERSION), glGetString (GL_VENDOR),
           glGetString (GL_RENDERER), glewGetString (GLEW_VERSION),
           glGetString (GL_SHADING_LANGUAGE_VERSION));

  if (!GLEW_VERSION_2_1)
    {
      fprintf (stderr, "OpenGL 2.1 or better required for GLSL support.");
      exit (-1);
    }
#endif
}


char *
load_file (char *filename)
{
  FILE *f;
  int size;
  char *data;

  f = fopen (filename, "rb");
  if (f == NULL)
    {
      perror ("error opening file");
      return NULL;
    }

  fseek (f, 0, SEEK_END);
  size = ftell (f);
  fseek (f, 0, SEEK_SET);

  data = malloc (size + 1);
  if (fread (data, size, 1, f) < 1)
    {
      fprintf (stderr, "problem reading file %s\n", filename);
      free (data);
      return NULL;
    }
  fclose(f);

  data[size] = '\0';

  return data;
}


/* shadertoy inputs a pasted shader may use without declaring them */
static const struct {
  const char *name;
  const char *decl;
  int         supported;
} shader_inputs[] = {
  { "iResolution",        "uniform vec3      iResolution;",           1 },
  { "iTime",              "uniform float     iTime;",                 1 },
  { "iGlobalTime",        "uniform float     iGlobalTime;",           1 },
  { "iMouse",             "uniform vec4      iMouse;",                1 },
  { "iChannel0",          "uniform sampler2D iChannel0;",             1 },
  { "iChannel1",          "uniform sampler2D iChannel1;",             1 },
  { "iChannel2",          "uniform sampler2D iChannel2;",             1 },
  { "iChannel3",          "uniform sampler2D iChannel3;",             1 },
  { "iTimeDelta",         "uniform float     iTimeDelta;",            0 },
  { "iFrame",             "uniform int       iFrame;",                0 },
  { "iChannelTime",       "uniform float     iChannelTime[4];",       0 },
  { "iChannelResolution", "uniform vec3      iChannelResolution[4];", 0 },
  { "iDate",              "uniform vec4      iDate;",                 0 },
  { "iSampleRate",        "uniform float     iSampleRate;",           0 },
};

/* main() for shadertoy's "Cube A" pass: renders one cube face, with the
 * face's orientation in the cubeFace* uniforms */
static const char shader_cube_main[] =
  "\n"
  "uniform vec3 cubeFaceForward;\n"
  "uniform vec3 cubeFaceRight;\n"
  "uniform vec3 cubeFaceUp;\n"
  "\n"
  "void main (void)\n"
  "{\n"
  "  vec4 color = vec4 (0.0, 0.0, 0.0, 1.0);\n"
  "  vec2 uv = gl_FragCoord.xy / iResolution.xy * 2.0 - 1.0;\n"
  "  vec3 dir = normalize (cubeFaceForward + uv.x * cubeFaceRight + uv.y * cubeFaceUp);\n"
  "  mainCubemap (color, gl_FragCoord.xy, vec3 (0.0), dir);\n"
  "  gl_FragColor = color;\n"
  "}\n";

static const char shader_main[] =
  "\n"
  "void main (void)\n"
  "{\n"
  "  vec4 color = vec4 (0.0, 0.0, 0.0, 1.0);\n"
  "  mainImage (color, gl_FragCoord.xy);\n"
  "  color.w = 1.0;\n"
  "  gl_FragColor = color;\n"
  "}\n";


/* copy of src with comments blanked out, so they don't confuse the checks */
static char *
strip_comments (const char *src)
{
  char *out = strdup (src);
  char *c = out;

  while (*c)
    {
      if (c[0] == '/' && c[1] == '/')
        {
          while (*c && *c != '\n')
            *c++ = ' ';
        }
      else if (c[0] == '/' && c[1] == '*')
        {
          while (*c && !(c[0] == '*' && c[1] == '/'))
            {
              if (*c != '\n')
                *c = ' ';
              c++;
            }
          if (*c)
            {
              c[0] = c[1] = ' ';
              c += 2;
            }
        }
      else
        {
          c++;
        }
    }

  return out;
}


static int
is_ident_char (char c)
{
  return isalnum ((unsigned char) c) || c == '_';
}


/* next occurrence of word as a whole identifier in [src, end), or NULL */
static const char *
find_word (const char *src, const char *end, const char *word)
{
  size_t len = strlen (word);
  const char *pos = src;

  while ((pos = strstr (pos, word)) != NULL && pos < end)
    {
      if ((pos == src || !is_ident_char (pos[-1])) && !is_ident_char (pos[len]))
        return pos;
      pos += len;
    }

  return NULL;
}


static int
declares_uniform (const char *src, const char *name)
{
  const char *pos = src;
  const char *end = src + strlen (src);

  while ((pos = find_word (pos, end, "uniform")) != NULL)
    {
      const char *semi = strchr (pos, ';');

      if (!semi)
        semi = end;
      if (find_word (pos, semi, name))
        return 1;
      pos = semi;
    }

  return 0;
}


static int
has_main (const char *src)
{
  const char *pos = src;
  const char *end = src + strlen (src);

  while ((pos = find_word (pos, end, "main")) != NULL)
    {
      const char *c = pos + 4;

      while (isspace ((unsigned char) *c))
        c++;
      if (*c == '(')
        return 1;
      pos += 4;
    }

  return 0;
}


/* GLSL 1.30+ texture() mapped onto the GLSL 1.20 functions */
static const char shader_texture_helpers[] =
  "vec4 texture (sampler2D s, vec2 c) { return texture2D (s, c); }\n"
  "vec4 texture (sampler2D s, vec2 c, float b) { return texture2D (s, c, b); }\n"
  "vec4 texture (samplerCube s, vec3 c) { return textureCube (s, c); }\n"
  "vec4 texture (samplerCube s, vec3 c, float b) { return textureCube (s, c, b); }\n";


/* WebGL (GLSL ES) precision syntax, which desktop GLSL 1.20 rejects */
static const char *precision_words[] = { "highp", "mediump", "lowp" };


/* blank out "precision ... ;" statements in code (and its comment-free
 * copy plain, which has the same layout), keeping line numbers intact */
static void
remove_precision_statements (char *code, char *plain)
{
  const char *end = plain + strlen (plain);
  const char *pos = plain;

  while ((pos = find_word (pos, end, "precision")) != NULL)
    {
      size_t i, from = pos - plain;

      for (i = from; plain[i] && plain[i] != ';'; i++)
        if (plain[i] != '\n')
          code[i] = plain[i] = ' ';
      if (plain[i] == ';')
        code[i] = plain[i] = ' ';
      pos = plain + i;
    }
}


/* Turn code pasted from shadertoy.com into a complete fragment shader:
 * declare the inputs it uses but doesn't declare, and add a main() that
 * calls mainImage() if there is none.  iChannels in cube_mask are declared
 * as samplerCube; the channels that got declared are returned in
 * *added_channels.  Messages about the additions only when verbose. */
char *
wrap_shader (const char *src,
             int         cube_mask,
             int        *added_channels,
             int         verbose,
             int         cube_pass)
{
  char *code = strdup (src);
  char *plain = strip_comments (code);
  const char *body = code;
  const char *end = plain + strlen (plain);
  size_t size, i;
  int version_lines = 0;
  int need_main;
  char *out;

  if (added_channels)
    *added_channels = 0;

  remove_precision_statements (code, plain);

  /* a #version directive has to stay in front */
  if (strncmp (plain, "#version", 8) == 0)
    {
      const char *nl = strchr (code, '\n');

      body = nl ? nl + 1 : code + strlen (code);
      version_lines = 1;
    }

  need_main = !has_main (plain) && find_word (plain, end, "mainImage");

  size = strlen (code) + sizeof (shader_main) + sizeof (shader_cube_main) +
         sizeof (shader_texture_helpers) + 64;
  for (i = 0; i < sizeof (shader_inputs) / sizeof (shader_inputs[0]); i++)
    size += strlen (shader_inputs[i].decl) + 8;
  size += 3 * 32;

  out = calloc (size, 1);
  if (version_lines)
    strncat (out, code, body - code);
  else
    strcat (out, "#version 120\n");  /* default is 1.10, without array constructors */

  for (i = 0; i < sizeof (shader_inputs) / sizeof (shader_inputs[0]); i++)
    {
      const char *name = shader_inputs[i].name;
      const char *decl = shader_inputs[i].decl;
      char cube_decl[64];

      if (!find_word (plain, end, name) || declares_uniform (plain, name))
        continue;

      if (strncmp (name, "iChannel", 8) == 0 && name[9] == '\0')
        {
          int channel = name[8] - '0';

          if (added_channels)
            *added_channels |= 1 << channel;
          if (cube_mask & (1 << channel))
            {
              snprintf (cube_decl, sizeof (cube_decl),
                        "uniform samplerCube %s;", name);
              decl = cube_decl;
            }
        }

      strcat (out, decl);
      strcat (out, "\n");
      if (verbose)
        fprintf (stderr, "added: %s\n", decl);
      if (verbose && !shader_inputs[i].supported)
        fprintf (stderr, "warning: %s is not supported and stays 0\n", name);
    }

  for (i = 0; i < sizeof (precision_words) / sizeof (precision_words[0]); i++)
    {
      if (find_word (plain, end, precision_words[i]))
        snprintf (out + strlen (out), 32, "#define %s\n", precision_words[i]);
    }

  if (find_word (plain, end, "texture"))
    strcat (out, shader_texture_helpers);

  /* keep compiler error line numbers matching the file */
  snprintf (out + strlen (out), 64, "#line %d\n", version_lines);
  strcat (out, body);

  if (cube_pass)
    {
      strcat (out, shader_cube_main);
    }
  else if (need_main)
    {
      strcat (out, shader_main);
      if (verbose)
        fprintf (stderr, "added: main() calling mainImage()\n");
    }

  free (plain);
  free (code);

  return out;
}


/* built-in environment for cube map channels without a texture:
 * sky gradient with a sun above, a checkered ground plane below */
static void
sky_color (float x, float y, float z, uint8_t *rgb)
{
  const float sun[3] = { 0.52, 0.45, 0.72 };
  float len = sqrtf (x * x + y * y + z * z);
  float c[3], s, t;
  int i;

  x /= len; y /= len; z /= len;

  if (y > 0.0)
    {
      const float horizon[3] = { 0.85, 0.90, 0.95 };
      const float zenith[3]  = { 0.20, 0.45, 0.85 };
      float d = fmaxf (x * sun[0] + y * sun[1] + z * sun[2], 0.0);

      t = sqrtf (y);
      s = powf (d, 800.0) * 4.0;
      for (i = 0; i < 3; i++)
        c[i] = horizon[i] + (zenith[i] - horizon[i]) * t + s;
      c[0] += powf (d, 12.0) * 0.35;
      c[1] += powf (d, 12.0) * 0.25;
    }
  else
    {
      const float near[3] = { 0.45, 0.40, 0.35 };
      const float far[3]  = { 0.60, 0.62, 0.65 };
      float px = x / -y, pz = z / -y;
      float checker = ((int) floorf (px) + (int) floorf (pz)) & 1 ? 0.6 : 1.0;
      float fade = expf (-sqrtf (px * px + pz * pz) * 0.04);

      for (i = 0; i < 3; i++)
        c[i] = far[i] + (near[i] * checker - far[i]) * fade;
    }

  for (i = 0; i < 3; i++)
    rgb[i] = c[i] >= 1.0 ? 255 : (uint8_t) (c[i] * 255.0);
}


/* does the shader have a "Cube A" pass (a mainCubemap() function)? */
int
has_cubemap_pass (const char *src)
{
  char *plain = strip_comments (src);
  int found = find_word (plain, plain + strlen (plain), "mainCubemap") != NULL;

  free (plain);
  return found;
}


/* run the Cube A program once for each face into a new cube map */
GLuint
render_cubemap (GLint program, int size)
{
  /* forward, right, up for each face, in GL order +X -X +Y -Y +Z -Z */
  static const GLfloat basis[6][9] = {
    {  1,  0,  0,    0,  0, -1,    0, -1,  0 },
    { -1,  0,  0,    0,  0,  1,    0, -1,  0 },
    {  0,  1,  0,    1,  0,  0,    0,  0,  1 },
    {  0, -1,  0,    1,  0,  0,    0,  0, -1 },
    {  0,  0,  1,    1,  0,  0,    0, -1,  0 },
    {  0,  0, -1,   -1,  0,  0,    0, -1,  0 },
  };
  GLuint id, fbo;
  GLint uindex;
  int face;

  glGenTextures (1, &id);
  glBindTexture (GL_TEXTURE_CUBE_MAP, id);
  for (face = 0; face < 6; face++)
    glTexImage2D (GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, 0, GL_RGBA16F_ARB,
                  size, size, 0, GL_RGBA, GL_FLOAT, NULL);
  glTexParameteri (GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri (GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri (GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri (GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexParameteri (GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);

  glGenFramebuffers (1, &fbo);
  glBindFramebuffer (GL_FRAMEBUFFER, fbo);
  glUseProgram (program);
  glViewport (0, 0, size, size);

  uindex = glGetUniformLocation (program, "iResolution");
  if (uindex >= 0)
    glUniform3f (uindex, size, size, 1.0);

  for (face = 0; face < 6; face++)
    {
      glFramebufferTexture2D (GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                              GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, id, 0);
      if (glCheckFramebufferStatus (GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        {
          fprintf (stderr, "cube pass: can't render into cube face %d\n", face);
          break;
        }

      glUniform3fv (glGetUniformLocation (program, "cubeFaceForward"), 1, basis[face]);
      glUniform3fv (glGetUniformLocation (program, "cubeFaceRight"), 1, basis[face] + 3);
      glUniform3fv (glGetUniformLocation (program, "cubeFaceUp"), 1, basis[face] + 6);
      glRectf (-1.0, -1.0, 1.0, 1.0);
    }

  glFinish ();
  glBindFramebuffer (GL_FRAMEBUFFER, 0);
  glDeleteFramebuffers (1, &fbo);

  fprintf (stderr, "cube pass: rendered %dx%d cube map --> id %d\n", size, size, id);

  return id;
}


/* stand-in for shadertoy's noise textures on 2D channels without a texture */
GLuint
make_noise_texture (void)
{
  const int size = 256;
  uint8_t *data = malloc (size * size * 4);
  GLuint id;
  int i;

  srand (1);
  for (i = 0; i < size * size * 4; i++)
    data[i] = rand () & 0xff;

  glGenTextures (1, &id);
  glBindTexture (GL_TEXTURE_2D, id);
  glTexImage2D (GL_TEXTURE_2D, 0, GL_RGBA, size, size, 0,
                GL_RGBA, GL_UNSIGNED_BYTE, data);
  glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
  glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
  glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
  glGenerateMipmap (GL_TEXTURE_2D);

  free (data);

  return id;
}


GLuint
make_sky_cubemap (void)
{
  const int size = 256;
  uint8_t *data = malloc (size * size * 3);
  GLuint id;
  int face, x, y;

  glGenTextures (1, &id);
  glBindTexture (GL_TEXTURE_CUBE_MAP, id);

  for (face = 0; face < 6; face++)
    {
      for (y = 0; y < size; y++)
        for (x = 0; x < size; x++)
          {
            float s = 2.0 * (x + 0.5) / size - 1.0;
            float t = 2.0 * (y + 0.5) / size - 1.0;
            float d[6][3] = {
              {  1.0,  -t,  -s }, { -1.0,  -t,   s },
              {    s, 1.0,   t }, {    s, -1.0, -t },
              {    s,  -t, 1.0 }, {   -s,  -t, -1.0 },
            };

            sky_color (d[face][0], d[face][1], d[face][2],
                       data + (y * size + x) * 3);
          }

      glTexImage2D (GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, 0, GL_RGB,
                    size, size, 0, GL_RGB, GL_UNSIGNED_BYTE, data);
    }

  glTexParameteri (GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri (GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri (GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri (GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexParameteri (GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);

  free (data);

  return id;
}


void
ipc_socket_send_message (char *msg)
{
  struct sockaddr_in servaddr;

  bzero (&servaddr, sizeof (servaddr));
  servaddr.sin_family      = AF_INET;
  servaddr.sin_addr.s_addr = htonl (IPC_ADDR);
  servaddr.sin_port        = htons (IPC_PORT);

  sendto (sockfd, msg ,strlen (msg), 0,
          (struct sockaddr *) &servaddr, sizeof(servaddr));
}


void
ipc_socket_handle_message (void)
{
  char msg[1000];
  int len;
  char *pos, *token;

  len = recvfrom (sockfd, msg, sizeof (msg) - 1, 0, NULL, NULL);

  msg[len] = '\0';

  pos = msg;
  token = strsep (&pos, ":");

  if (strcmp (token, "iMouse") == 0)
    {
      int i = 0;

      for (i = 0; i < 4; i++)
        {
          token = strsep (&pos, ",");
          if (!token)
            break;
          mouse[i] = atof (token);
        }
    }
}


int
ipc_socket_open (int port)
{
  struct sockaddr_in servaddr;
  int ret;
  int flag = 1;

  sockfd = socket (AF_INET, SOCK_DGRAM, 0);
  if (sockfd < 0)
    perror ("socket");

  setsockopt (sockfd, SOL_SOCKET, SO_REUSEPORT, (char*) &flag, sizeof (flag));
  setsockopt (sockfd, SOL_SOCKET, SO_BROADCAST, (char*) &flag, sizeof (flag));

  bzero (&servaddr, sizeof (servaddr));
  servaddr.sin_family      = AF_INET;
  servaddr.sin_addr.s_addr = htonl (IPC_ADDR);
  servaddr.sin_port        = htons (port);

  ret = bind (sockfd, (struct sockaddr *) &servaddr, sizeof (servaddr));
  if (ret < 0)
    perror ("bind");

  return sockfd;
}


int
main (int   argc,
      char *argv[])
{
  char *frag_code = NULL;
  glutInit (&argc, argv);

  static struct option long_options[] = {
      { "texture",  required_argument, NULL,  't' },
      { "geometry", required_argument, NULL,  'g' },
      { "help",     no_argument, NULL,        'h' },
      { 0,          0,                 NULL,  0   }
  };

  glutInitWindowSize (800, 600);
  glutInitDisplayMode (GLUT_RGBA | GLUT_DOUBLE);
  glutCreateWindow ("Shadertoy");
#ifdef __APPLE__
  enable_hidpi ();
  {
    /* sync buffer swaps to the display refresh (60/120 Hz) */
    GLint one = 1;
    CGLSetParameter (CGLGetCurrentContext (), kCGLCPSwapInterval, &one);
  }
#endif

  init_glew ();

  /* option parsing */
  while (1)
    {
      int c, slot, i;
      char nearest, repeat;

      c = getopt_long (argc, argv, ":t:g:?", long_options, NULL);
      if (c == -1)
        break;

      switch (c)
        {
          case 'g':
            for (i = 0; i < 4; i++)
              {
                char *token = strsep (&optarg, "x+");
                if (!token)
                  break;
                geometry[i] = atof (token);
              }

            fprintf (stderr, "geometry: %.0fx%.0f+%.0f+%.0f\n",
                     geometry[0], geometry[1], geometry[2], geometry[3]);
            break;

          case 't':
            if (optarg[0] <  '0' || optarg[0] >  '3' || strchr (optarg, ':') == NULL)
              {
                fprintf (stderr, "Argument for texture file needs a slot from 0 to 3\n");
                exit (1);
              }

            slot = optarg[0] - '0';

            repeat = 1;
            nearest = 0;

            for (c = 1; optarg[c] != ':' && optarg[c] != '\0'; c++)
              {
                switch (optarg[c])
                  {
                    case 'r':
                      repeat = 1;
                      break;
                    case 'o':
                      repeat = 0;
                      break;
                    case 'i':
                      nearest = 0;
                      break;
                    case 'n':
                      nearest = 1;
                      break;
                    default:
                      break;
                  }
              }

            if (optarg[c] == ':' && is_cubemap_path (optarg + c + 1))
              {
                if (!load_cubemap (optarg + c + 1, &tex[slot]))
                  {
                    fprintf (stderr, "Failed to load cube map. Aborting.\n");
                    exit (1);
                  }
                cube_loaded |= 1 << slot;
              }
            else if (optarg[c] != ':' ||
                !load_texture (optarg + c + 1, GL_TEXTURE_2D, &tex[slot], nearest, repeat))
              {
                fprintf (stderr, "Failed to load texture. Aborting.\n");
                exit (1);
              }
            break;

          case 'h':
          case ':':
          default:
            fprintf (stderr, "Usage:\n  %s [options] <shaderfile>\n", argv[0]);
            fprintf (stderr, "Options:    --help\n");
            fprintf (stderr, "            --texture [0-3]:<textureimage>\n");
            exit (c == ':' ? 1 : 0);
            break;
        }
    }

  if (optind != argc - 1)
    {
      fprintf (stderr, "No shaderfile specified. Aborting.\n");
      exit (-1);
    }

  frag_code = load_file (argv[optind]);
  if (!frag_code)
    {
      fprintf (stderr, "Failed to load Shaderfile. Aborting.\n");
      exit (-1);
    }

  {
    char *wrapped;
    int channels, mask;

    wrapped = wrap_shader (frag_code, 0, &channels, 1, 0);
    quiet = 1;
    prog = link_program (wrapped);

    /* the code doesn't say which channels are cube maps, so if it doesn't
     * compile with 2D textures, try declaring some of them as cube maps */
    for (mask = 1; prog < 0 && mask < 16; mask++)
      {
        char *attempt;

        if ((mask & channels) != mask)
          continue;

        attempt = wrap_shader (frag_code, mask, NULL, 0, 0);
        prog = link_program (attempt);
        free (attempt);
        if (prog >= 0)
          cube_channels = mask;
      }
    quiet = 0;

    /* nothing worked: compile again to show the errors */
    if (prog < 0)
      prog = link_program (wrapped);

    free (wrapped);
  }

  if (prog < 0)
    {
      fprintf (stderr, "Failed to link shader program. Aborting\n");
      exit (-1);
    }

  {
    GLuint default_cube = 0;
    GLuint noise = 0;
    char default_path[4096];
    char *exe = strdup (argv[0]);
    int i;

    /* a "Cube A" pass in the shader provides the environment */
    if (cube_channels && has_cubemap_pass (frag_code))
      {
        char *cube_code = wrap_shader (frag_code, cube_channels, NULL, 0, 1);
        GLint cube_prog;

        fprintf (stderr, "cube pass: found mainCubemap()\n");
        cube_prog = link_program (cube_code);
        free (cube_code);
        if (cube_prog >= 0)
          default_cube = render_cubemap (cube_prog, 1024);
        else
          fprintf (stderr, "cube pass: failed to compile, using default\n");
      }

    /* default environment: cubemaps/park2 next to the executable */
    snprintf (default_path, sizeof (default_path), "%s/cubemaps/park2",
              dirname (exe));
    free (exe);

    for (i = 0; i < 4; i++)
      {
        if (!(cube_channels & (1 << i)))
          {
            char name[16];

            if (cube_loaded & (1 << i))
              fprintf (stderr, "warning: iChannel%d is a 2D texture in this "
                       "shader, but got a cube map\n", i);

            snprintf (name, sizeof (name), "iChannel%d", i);
            if (!tex[i] && glGetUniformLocation (prog, name) >= 0)
              {
                if (!noise)
                  noise = make_noise_texture ();
                fprintf (stderr, "iChannel%d: 2D, using built-in noise\n", i);
                tex[i] = noise;
              }
            continue;
          }

        if (cube_loaded & (1 << i))
          {
            fprintf (stderr, "iChannel%d: cube map\n", i);
            continue;
          }

        if (tex[i])
          fprintf (stderr, "warning: iChannel%d needs a cube map, "
                   "ignoring its 2D texture\n", i);

        if (!default_cube)
          {
            if (!is_cubemap_path (default_path) ||
                !load_cubemap (default_path, &default_cube))
              {
                fprintf (stderr, "%s not found, using built-in sky\n",
                         default_path);
                default_cube = make_sky_cubemap ();
              }
          }

        fprintf (stderr, "iChannel%d: cube map (default)\n", i);
        tex[i] = default_cube;
      }
  }

  ipc_socket_open (IPC_PORT);

  glutDisplayFunc  (display);
  glutMouseFunc    (mouse_press_handler);
  glutMotionFunc   (mouse_move_handler);
  glutKeyboardFunc (keyboard_handler);

  glutIdleFunc     (idle);

  glutMainLoop ();

  return 0;
}

