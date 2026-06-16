#define GL_SILENCE_DEPRECATION
#define SDL_MAIN_HANDLED

#include "melted_ui.h"

#include <SDL.h>
#include <math.h>
#include <png.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef MELTED_GLASS_CONFIG_PATH
#define MELTED_GLASS_CONFIG_PATH "external/MeltedGlassOpenGL/config/melted_glass_config.json"
#endif

static const char *fullscreen_vs =
    "#version 330 core\n"
    "out vec2 vUv;\n"
    "const vec2 verts[6] = vec2[6](\n"
    "  vec2(-1.0, -1.0), vec2(1.0, -1.0), vec2(-1.0, 1.0),\n"
    "  vec2(-1.0, 1.0), vec2(1.0, -1.0), vec2(1.0, 1.0)\n"
    ");\n"
    "void main() {\n"
    "  vec2 p = verts[gl_VertexID];\n"
    "  vUv = p * 0.5 + 0.5;\n"
    "  gl_Position = vec4(p, 0.0, 1.0);\n"
    "}\n";

static const char *background_fs =
    "#version 330 core\n"
    "in vec2 vUv;\n"
    "out vec4 outColor;\n"
    "uniform float uTime;\n"
    "float rectMask(vec2 p, vec2 pos, vec2 size) {\n"
    "  vec2 a = step(pos, p) * step(p, pos + size);\n"
    "  return a.x * a.y;\n"
    "}\n"
    "float sharpLine(float v, float width) {\n"
    "  float f = fract(v);\n"
    "  return 1.0 - step(width, min(f, 1.0 - f));\n"
    "}\n"
    "vec3 bandColor(float i) {\n"
    "  if (i < 1.0) return vec3(0.02, 0.20, 1.00);\n"
    "  if (i < 2.0) return vec3(0.00, 0.86, 0.95);\n"
    "  if (i < 3.0) return vec3(0.00, 0.95, 0.36);\n"
    "  if (i < 4.0) return vec3(1.00, 0.92, 0.04);\n"
    "  if (i < 5.0) return vec3(1.00, 0.28, 0.06);\n"
    "  if (i < 6.0) return vec3(1.00, 0.02, 0.50);\n"
    "  return vec3(0.58, 0.08, 1.00);\n"
    "}\n"
    "void main() {\n"
    "  vec2 p = vUv;\n"
    "  float shift = uTime * 0.025;\n"
    "  float band = floor(fract(p.x * 1.65 + p.y * 0.48 + shift) * 7.0);\n"
    "  vec3 color = bandColor(band);\n"
    "  float checker = mod(floor(p.x * 16.0) + floor(p.y * 10.0), 2.0);\n"
    "  color = mix(color, mix(vec3(0.015), vec3(0.98), checker), rectMask(p, vec2(0.08, 0.16), vec2(0.84, 0.68)) * 0.18);\n"
    "  float grid = max(sharpLine(p.x * 18.0, 0.018), sharpLine(p.y * 12.0, 0.018));\n"
    "  color = mix(color, vec3(1.0), grid * 0.30);\n"
    "  float diagonal = sharpLine((p.x + p.y * 0.35 + shift) * 25.0, 0.014);\n"
    "  color = mix(color, vec3(0.0), diagonal * 0.18);\n"
    "  color = mix(color, vec3(1.0, 0.03, 0.08), rectMask(p, vec2(0.07, 0.10), vec2(0.19, 0.16)));\n"
    "  color = mix(color, vec3(0.00, 0.05, 0.12), rectMask(p, vec2(0.74, 0.68), vec2(0.19, 0.18)));\n"
    "  color = mix(color, vec3(0.02, 0.95, 0.45), rectMask(p, vec2(0.64, 0.13), vec2(0.23, 0.075)));\n"
    "  color = mix(color, vec3(1.0, 0.92, 0.02), rectMask(p, vec2(0.13, 0.73), vec2(0.28, 0.07)));\n"
    "  float ringA = step(0.095, length(p - vec2(0.30, 0.40))) * (1.0 - step(0.125, length(p - vec2(0.30, 0.40))));\n"
    "  float ringB = step(0.070, length(p - vec2(0.70, 0.46))) * (1.0 - step(0.100, length(p - vec2(0.70, 0.46))));\n"
    "  color = mix(color, vec3(1.0), ringA);\n"
    "  color = mix(color, vec3(0.0), ringB);\n"
    "  outColor = vec4(clamp(color, 0.0, 1.0), 1.0);\n"
    "}\n";

static const char *copy_fs =
    "#version 330 core\n"
    "in vec2 vUv;\n"
    "out vec4 outColor;\n"
    "uniform sampler2D uTexture;\n"
    "void main() {\n"
    "  outColor = texture(uTexture, vUv);\n"
    "}\n";

static float clampf(float v, float lo, float hi) {
  if (v < lo) return lo;
  if (v > hi) return hi;
  return v;
}

static GLuint compile_shader(GLenum type, const char *source, const char *label) {
  GLuint shader = glCreateShader(type);
  glShaderSource(shader, 1, &source, NULL);
  glCompileShader(shader);

  GLint ok = GL_FALSE;
  glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
  if (!ok) {
    char log[2048];
    GLsizei length = 0;
    glGetShaderInfoLog(shader, sizeof(log), &length, log);
    fprintf(stderr, "Failed to compile %s: %.*s\n", label, (int)length, log);
    glDeleteShader(shader);
    return 0;
  }

  return shader;
}

static GLuint link_program(const char *fragmentSource, const char *label) {
  GLuint vs = compile_shader(GL_VERTEX_SHADER, fullscreen_vs, "fullscreen vertex shader");
  GLuint fs = compile_shader(GL_FRAGMENT_SHADER, fragmentSource, label);
  if (!vs || !fs) {
    if (vs) glDeleteShader(vs);
    if (fs) glDeleteShader(fs);
    return 0;
  }

  GLuint program = glCreateProgram();
  glAttachShader(program, vs);
  glAttachShader(program, fs);
  glLinkProgram(program);
  glDeleteShader(vs);
  glDeleteShader(fs);

  GLint ok = GL_FALSE;
  glGetProgramiv(program, GL_LINK_STATUS, &ok);
  if (!ok) {
    char log[2048];
    GLsizei length = 0;
    glGetProgramInfoLog(program, sizeof(log), &length, log);
    fprintf(stderr, "Failed to link %s: %.*s\n", label, (int)length, log);
    glDeleteProgram(program);
    return 0;
  }

  return program;
}

static GLint uni(GLuint program, const char *name) {
  return glGetUniformLocation(program, name);
}

static void destroy_target(RenderTarget *target) {
  if (!target) return;
  if (target->texture) glDeleteTextures(1, &target->texture);
  if (target->fbo) glDeleteFramebuffers(1, &target->fbo);
  memset(target, 0, sizeof(*target));
}

static int reset_target(RenderTarget *target, int width, int height) {
  if (!target || width <= 0 || height <= 0) {
    return 0;
  }
  if (target->texture && target->width == width && target->height == height) {
    return 1;
  }

  destroy_target(target);
  target->width = width;
  target->height = height;
  glGenFramebuffers(1, &target->fbo);
  glGenTextures(1, &target->texture);
  glBindTexture(GL_TEXTURE_2D, target->texture);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
  glBindFramebuffer(GL_FRAMEBUFFER, target->fbo);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, target->texture, 0);
  GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  return status == GL_FRAMEBUFFER_COMPLETE;
}

static void draw_fullscreen(GLuint vao) {
  glBindVertexArray(vao);
  glDrawArrays(GL_TRIANGLES, 0, 6);
}

static int write_png(const char *path, const unsigned char *rgba, int width, int height) {
  FILE *file = fopen(path, "wb");
  if (!file) {
    fprintf(stderr, "Failed to open %s for writing\n", path);
    return 0;
  }

  png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
  if (!png) {
    fclose(file);
    return 0;
  }

  png_infop info = png_create_info_struct(png);
  if (!info) {
    png_destroy_write_struct(&png, NULL);
    fclose(file);
    return 0;
  }

  if (setjmp(png_jmpbuf(png))) {
    png_destroy_write_struct(&png, &info);
    fclose(file);
    return 0;
  }

  png_init_io(png, file);
  png_set_IHDR(png,
               info,
               (png_uint_32)width,
               (png_uint_32)height,
               8,
               PNG_COLOR_TYPE_RGBA,
               PNG_INTERLACE_NONE,
               PNG_COMPRESSION_TYPE_DEFAULT,
               PNG_FILTER_TYPE_DEFAULT);
  png_write_info(png, info);

  png_bytep *rows = (png_bytep *)calloc((size_t)height, sizeof(png_bytep));
  if (!rows) {
    png_destroy_write_struct(&png, &info);
    fclose(file);
    return 0;
  }

  for (int y = 0; y < height; ++y) {
    rows[y] = (png_bytep)(rgba + (size_t)(height - 1 - y) * (size_t)width * 4u);
  }

  png_write_image(png, rows);
  png_write_end(png, NULL);
  free(rows);
  png_destroy_write_struct(&png, &info);
  fclose(file);
  return 1;
}

static int write_current_frame_png(const char *path, int width, int height) {
  if (!path || width <= 0 || height <= 0) {
    return 0;
  }

  size_t byteCount = (size_t)width * (size_t)height * 4u;
  unsigned char *pixels = (unsigned char *)malloc(byteCount);
  if (!pixels) {
    fprintf(stderr, "Failed to allocate screenshot pixels\n");
    return 0;
  }

  glReadBuffer(GL_BACK);
  glPixelStorei(GL_PACK_ALIGNMENT, 1);
  glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
  GLenum err = glGetError();
  if (err != GL_NO_ERROR) {
    fprintf(stderr, "glReadPixels failed: 0x%x\n", err);
    free(pixels);
    return 0;
  }

  int ok = write_png(path, pixels, width, height);
  free(pixels);
  if (!ok) {
    fprintf(stderr, "Failed to write %s\n", path);
    return 0;
  }
  fprintf(stderr, "Wrote %s\n", path);
  return 1;
}

static void render_background(GLuint program, GLuint vao, RenderTarget *target, float timeSeconds) {
  glBindFramebuffer(GL_FRAMEBUFFER, target->fbo);
  glDrawBuffer(GL_COLOR_ATTACHMENT0);
  glViewport(0, 0, target->width, target->height);
  glDisable(GL_BLEND);
  glUseProgram(program);
  glUniform1f(uni(program, "uTime"), timeSeconds);
  draw_fullscreen(vao);
}

static void copy_texture_to_default(GLuint program, GLuint vao, GLuint texture, int width, int height) {
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glDrawBuffer(GL_BACK);
  glViewport(0, 0, width, height);
  glDisable(GL_BLEND);
  glUseProgram(program);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, texture);
  glUniform1i(uni(program, "uTexture"), 0);
  draw_fullscreen(vao);
}

static void copy_texture_to_target(GLuint program, GLuint vao, GLuint texture, RenderTarget *target) {
  glBindFramebuffer(GL_FRAMEBUFFER, target->fbo);
  glDrawBuffer(GL_COLOR_ATTACHMENT0);
  glViewport(0, 0, target->width, target->height);
  glDisable(GL_BLEND);
  glUseProgram(program);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, texture);
  glUniform1i(uni(program, "uTexture"), 0);
  draw_fullscreen(vao);
}

static void layout_controls(MeltedUIWidget *lightWidget,
                            MeltedUIWidget *darkWidget,
                            MeltedUIMenu *menu,
                            MeltedUISwitch *control,
                            MeltedUIButton *button,
                            int width,
                            int height) {
  float menuW = clampf((float)width * 0.58f, 320.0f, 540.0f);
  float menuH = clampf(menuW * 0.18f, 66.0f, 88.0f);
  float switchW = clampf((float)width * 0.24f, 116.0f, 174.0f);
  float switchH = switchW * 0.41f;
  if (switchH > (float)height * 0.32f) {
    switchH = fmaxf(48.0f, (float)height * 0.32f);
    switchW = switchH / 0.41f;
  }

  float buttonDiameter = clampf((float)width * 0.12f, 72.0f, 108.0f);
  float widgetSize = clampf((float)width * 0.18f, 122.0f, 176.0f);
  widgetSize = fminf(widgetSize, fmaxf(112.0f, (float)height * 0.28f));
  float widgetGap = clampf((float)width * 0.045f, 24.0f, 46.0f);
  float gap = fmaxf(26.0f, (float)height * 0.055f);
  float totalH = widgetSize + gap + menuH + gap + switchH + gap + buttonDiameter;
  float startY = ((float)height - totalH) * 0.5f;
  if (startY < 18.0f) {
    startY = 18.0f;
  }

  float widgetsW = widgetSize * 2.0f + widgetGap;
  lightWidget->rect.x = ((float)width - widgetsW) * 0.5f;
  lightWidget->rect.y = startY;
  lightWidget->rect.width = widgetSize;
  lightWidget->rect.height = widgetSize;
  lightWidget->cornerRadiusPx = fmaxf(28.0f, widgetSize * 0.22f);

  darkWidget->rect.x = lightWidget->rect.x + widgetSize + widgetGap;
  darkWidget->rect.y = startY;
  darkWidget->rect.width = widgetSize;
  darkWidget->rect.height = widgetSize;
  darkWidget->cornerRadiusPx = lightWidget->cornerRadiusPx;

  menu->rect.x = ((float)width - menuW) * 0.5f;
  menu->rect.y = startY + widgetSize + gap;
  menu->rect.width = menuW;
  menu->rect.height = menuH;
  menu->cornerRadiusPx = menuH * 0.5f;
  menu->itemInsetPx = fmaxf(6.0f, menuH * 0.085f);
  menu->selectorInsetPx = fmaxf(7.0f, menuH * 0.10f);
  menu->selectorGrowPx = fmaxf(11.0f, menuH * 0.16f);

  control->rect.x = ((float)width - switchW) * 0.5f;
  control->rect.y = menu->rect.y + menuH + gap;
  control->rect.width = switchW;
  control->rect.height = switchH;
  control->knobInsetPx = fmaxf(4.0f, switchH * 0.085f);
  control->pressGrowPx = fmaxf(24.0f, switchH * 0.52f);
  control->dragThresholdPx = fmaxf(4.0f, switchH * 0.07f);

  button->rect.x = ((float)width - buttonDiameter) * 0.5f;
  button->rect.y = control->rect.y + switchH + gap;
  button->rect.width = buttonDiameter;
  button->rect.height = buttonDiameter;
  button->shapeMode = MELTED_GLASS_SHAPE_CIRCLE;
  button->cornerRadiusPx = buttonDiameter * 0.5f;
  button->pressGrowPx = fmaxf(14.0f, buttonDiameter * 0.20f);
}

int main(int argc, char **argv) {
  int maxFrames = 0;
  int hidden = 0;
  const char *screenshotPath = NULL;
  for (int i = 1; i < argc; ++i) {
    if (strcmp(argv[i], "--frames") == 0 && i + 1 < argc) {
      maxFrames = atoi(argv[++i]);
    } else if (strcmp(argv[i], "--hidden") == 0) {
      hidden = 1;
    } else if (strcmp(argv[i], "--screenshot") == 0 && i + 1 < argc) {
      screenshotPath = argv[++i];
    }
  }
  if (screenshotPath) {
    hidden = 1;
    if (maxFrames <= 0) {
      maxFrames = 4;
    }
  }

  if (SDL_Init(SDL_INIT_VIDEO) != 0) {
    fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
    return 1;
  }

  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
  SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
  SDL_GL_SetAttribute(SDL_GL_ACCELERATED_VISUAL, 1);

  Uint32 windowFlags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI;
  windowFlags |= hidden ? SDL_WINDOW_HIDDEN : SDL_WINDOW_SHOWN;
  SDL_Window *window = SDL_CreateWindow("MeltedUI controls demo",
                                        SDL_WINDOWPOS_CENTERED,
                                        SDL_WINDOWPOS_CENTERED,
                                        820,
                                        520,
                                        windowFlags);
  if (!window) {
    fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
    SDL_Quit();
    return 1;
  }

  SDL_GLContext context = SDL_GL_CreateContext(window);
  if (!context) {
    fprintf(stderr, "SDL_GL_CreateContext failed: %s\n", SDL_GetError());
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 1;
  }
  SDL_GL_SetSwapInterval(hidden ? 0 : 1);

  GLuint demoVao = 0;
  glGenVertexArrays(1, &demoVao);
  GLuint backgroundProgram = link_program(background_fs, "background fragment shader");
  GLuint copyProgram = link_program(copy_fs, "copy fragment shader");
  if (!demoVao || !backgroundProgram || !copyProgram) {
    SDL_GL_DeleteContext(context);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 1;
  }

  int width = 0;
  int height = 0;
  SDL_GL_GetDrawableSize(window, &width, &height);

  MeltedGlassConfig glassCfg;
  melted_glass_config_defaults(&glassCfg);
  melted_glass_load_config_file(MELTED_GLASS_CONFIG_PATH, &glassCfg);
  melted_glass_apply_quality_profile(&glassCfg, QUALITY_HIGH);
  glassCfg.downsampleScale = fminf(glassCfg.downsampleScale, 3.0f);
  glassCfg.blurIterations = fmaxf((float)glassCfg.blurIterations, 2.0f);

  MeltedGlassRenderer glass;
  if (!melted_glass_renderer_init(&glass, &glassCfg, width, height)) {
    fprintf(stderr, "Failed to initialize MeltedGlassOpenGL renderer\n");
    SDL_GL_DeleteContext(context);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 1;
  }

  MeltedUIRenderer ui;
  if (!melted_ui_renderer_init(&ui)) {
    fprintf(stderr, "Failed to initialize MeltedUI renderer\n");
    melted_glass_renderer_destroy(&glass);
    SDL_GL_DeleteContext(context);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 1;
  }

  RenderTarget scene = {0};
  RenderTarget menuScene = {0};
  MeltedUIWidget lightWidget;
  melted_ui_widget_defaults(&lightWidget, 0.0f, 0.0f, 160.0f, 160.0f, MELTED_UI_WIDGET_LIGHT);
  MeltedUIWidget darkWidget;
  melted_ui_widget_defaults(&darkWidget, 0.0f, 0.0f, 160.0f, 160.0f, MELTED_UI_WIDGET_DARK);
  MeltedUIMenu menu;
  melted_ui_menu_defaults(&menu, 0.0f, 0.0f, 420.0f, 78.0f, 3, MELTED_UI_MENU_DARK);
  MeltedUISwitch control;
  melted_ui_switch_defaults(&control, 0.0f, 0.0f, 150.0f, 64.0f);
  MeltedUIButton button;
  melted_ui_button_defaults(&button, 0.0f, 0.0f, 88.0f, 88.0f);
  layout_controls(&lightWidget, &darkWidget, &menu, &control, &button, width, height);

  if (!hidden) {
    fprintf(stderr, "MeltedUI controls: light/dark widgets are substrate-only, click or drag the menu slider, T toggles light/dark, [/] changes item count, drag the switch knob, click the blue button, Space toggles switch, R resets, Esc quits.\n");
  }

  int trackingMenu = 0;
  int trackingSwitch = 0;
  int trackingButton = 0;
  int running = 1;
  int frame = 0;
  int screenshotWritten = 0;
  int exitCode = 0;
  Uint64 lastCounter = SDL_GetPerformanceCounter();
  double perfFrequency = (double)SDL_GetPerformanceFrequency();

  while (running) {
    Uint64 nowCounter = SDL_GetPerformanceCounter();
    float dt = (float)((double)(nowCounter - lastCounter) / perfFrequency);
    lastCounter = nowCounter;
    dt = clampf(dt, 0.0f, 0.05f);

    int windowW = 0;
    int windowH = 0;
    SDL_GetWindowSize(window, &windowW, &windowH);
    SDL_GL_GetDrawableSize(window, &width, &height);
    float sx = windowW > 0 ? (float)width / (float)windowW : 1.0f;
    float sy = windowH > 0 ? (float)height / (float)windowH : 1.0f;

    SDL_Event event;
    while (SDL_PollEvent(&event)) {
      if (event.type == SDL_QUIT) {
        running = 0;
      } else if (event.type == SDL_KEYDOWN) {
        SDL_Keycode key = event.key.keysym.sym;
        if (key == SDLK_ESCAPE || key == SDLK_q) {
          running = 0;
        } else if (key == SDLK_SPACE) {
          melted_ui_switch_toggle(&control, 1);
        } else if (key == SDLK_r) {
          melted_ui_menu_set_selected(&menu, 0, 1);
          melted_ui_switch_set_on(&control, 0, 1);
          button.isPressed = 0;
          button.isPointerDown = 0;
          button.pressAmount = 0.0f;
        } else if (key == SDLK_t) {
          melted_ui_menu_set_theme(&menu, menu.theme == MELTED_UI_MENU_DARK ? MELTED_UI_MENU_LIGHT : MELTED_UI_MENU_DARK);
        } else if (key == SDLK_LEFTBRACKET || key == SDLK_MINUS) {
          melted_ui_menu_set_item_count(&menu, menu.itemCount - 1);
        } else if (key == SDLK_RIGHTBRACKET || key == SDLK_EQUALS) {
          melted_ui_menu_set_item_count(&menu, menu.itemCount + 1);
        }
      } else if (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_RESIZED) {
        SDL_GL_GetDrawableSize(window, &width, &height);
        layout_controls(&lightWidget, &darkWidget, &menu, &control, &button, width, height);
      } else if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_LEFT) {
        float mx = (float)event.button.x * sx;
        float my = (float)event.button.y * sy;
        if (melted_ui_menu_hit_test(&menu, mx, my)) {
          trackingMenu = 1;
          melted_ui_menu_pointer_down_at(&menu, mx, my);
        } else if (melted_ui_button_hit_test(&button, mx, my)) {
          trackingButton = 1;
          melted_ui_button_pointer_down(&button);
        } else if (melted_ui_switch_hit_test(&control, mx, my)) {
          trackingSwitch = 1;
          melted_ui_switch_pointer_down_at(&control, mx, my);
        }
      } else if (event.type == SDL_MOUSEBUTTONUP && event.button.button == SDL_BUTTON_LEFT) {
        float mx = (float)event.button.x * sx;
        float my = (float)event.button.y * sy;
        if (trackingMenu) {
          (void)melted_ui_menu_pointer_up_at(&menu, mx, my);
          trackingMenu = 0;
        }
        if (trackingButton) {
          int commit = melted_ui_button_hit_test(&button, mx, my);
          (void)melted_ui_button_pointer_up(&button, commit);
          trackingButton = 0;
        }
        if (trackingSwitch) {
          int commit = melted_ui_switch_hit_test(&control, mx, my);
          melted_ui_switch_pointer_up_at(&control, mx, my, commit);
          trackingSwitch = 0;
        }
      } else if (event.type == SDL_MOUSEMOTION && trackingMenu) {
        float mx = (float)event.motion.x * sx;
        float my = (float)event.motion.y * sy;
        melted_ui_menu_pointer_drag_to(&menu, mx, my);
      } else if (event.type == SDL_MOUSEMOTION && trackingSwitch) {
        float mx = (float)event.motion.x * sx;
        float my = (float)event.motion.y * sy;
        melted_ui_switch_pointer_drag_to(&control, mx, my);
      }
    }

    melted_ui_menu_update(&menu, dt);
    melted_ui_switch_update(&control, dt);
    melted_ui_button_update(&button, dt);
    layout_controls(&lightWidget, &darkWidget, &menu, &control, &button, width, height);
    if (!reset_target(&scene, width, height) || !reset_target(&menuScene, width, height)) {
      fprintf(stderr, "Failed to create scene framebuffer\n");
      break;
    }
    melted_glass_renderer_resize(&glass, &glassCfg, width, height);

    float timeSeconds = (float)SDL_GetTicks() / 1000.0f;
    render_background(backgroundProgram, demoVao, &scene, timeSeconds);

    glBindFramebuffer(GL_FRAMEBUFFER, scene.fbo);
    glDrawBuffer(GL_COLOR_ATTACHMENT0);
    glViewport(0, 0, width, height);
    melted_ui_draw_switch_track(&ui, &control, width, height);

    GLuint sceneBlurredTexture = melted_glass_blur_background(&glass, &glassCfg, scene.texture);

    copy_texture_to_target(copyProgram, demoVao, scene.texture, &menuScene);
    glBindFramebuffer(GL_FRAMEBUFFER, menuScene.fbo);
    glDrawBuffer(GL_COLOR_ATTACHMENT0);
    glViewport(0, 0, width, height);
    melted_glass_set_premultiplied_blend();
    melted_ui_draw_widget(&lightWidget, &glass, &glassCfg, scene.texture, sceneBlurredTexture, width, height);
    melted_ui_draw_widget(&darkWidget, &glass, &glassCfg, scene.texture, sceneBlurredTexture, width, height);
    melted_ui_draw_menu_background(&menu, &glass, &glassCfg, scene.texture, sceneBlurredTexture, width, height);

    GLuint menuBlurredTexture = melted_glass_blur_background(&glass, &glassCfg, menuScene.texture);
    copy_texture_to_default(copyProgram, demoVao, menuScene.texture, width, height);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDrawBuffer(GL_BACK);
    glViewport(0, 0, width, height);
    melted_glass_set_premultiplied_blend();
    melted_ui_draw_menu_selection(&menu, &glass, &glassCfg, menuScene.texture, menuBlurredTexture, width, height);
    melted_ui_draw_switch_knob(&control, &glass, &glassCfg, menuScene.texture, menuBlurredTexture, width, height);
    melted_ui_draw_button(&button, &glass, &glassCfg, menuScene.texture, menuBlurredTexture, width, height);

    glDisable(GL_BLEND);
    if (hidden) {
      glFinish();
    } else {
      SDL_GL_SwapWindow(window);
    }

    if (screenshotPath && !screenshotWritten && frame >= maxFrames - 1) {
      if (!write_current_frame_png(screenshotPath, width, height)) {
        exitCode = 1;
      }
      screenshotWritten = 1;
    }

    ++frame;
    if (maxFrames > 0 && frame >= maxFrames) {
      running = 0;
    }
  }

  destroy_target(&scene);
  destroy_target(&menuScene);
  melted_ui_renderer_destroy(&ui);
  melted_glass_renderer_destroy(&glass);
  if (backgroundProgram) glDeleteProgram(backgroundProgram);
  if (copyProgram) glDeleteProgram(copyProgram);
  if (demoVao) glDeleteVertexArrays(1, &demoVao);
  SDL_GL_DeleteContext(context);
  SDL_DestroyWindow(window);
  SDL_Quit();
  return exitCode;
}
