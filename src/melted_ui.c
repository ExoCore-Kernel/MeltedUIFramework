#define GL_SILENCE_DEPRECATION

#include "melted_ui.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static const char *melted_ui_fullscreen_vs =
    "#version 330 core\n"
    "const vec2 verts[6] = vec2[6](\n"
    "  vec2(-1.0, -1.0), vec2(1.0, -1.0), vec2(-1.0, 1.0),\n"
    "  vec2(-1.0, 1.0), vec2(1.0, -1.0), vec2(1.0, 1.0)\n"
    ");\n"
    "void main() {\n"
    "  gl_Position = vec4(verts[gl_VertexID], 0.0, 1.0);\n"
    "}\n";

static const char *melted_ui_rounded_rect_fs =
    "#version 330 core\n"
    "out vec4 outColor;\n"
    "uniform vec2 uResolution;\n"
    "uniform vec4 uRect;\n"
    "uniform float uRadiusPx;\n"
    "uniform float uAAWidthPx;\n"
    "uniform float uBorderWidthPx;\n"
    "uniform vec4 uColor;\n"
    "uniform vec4 uBorderColor;\n"
    "uniform float uTopLight;\n"
    "float roundedRectSDF(vec2 p, vec2 halfSize, float radius) {\n"
    "  vec2 q = abs(p) - halfSize + radius;\n"
    "  return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - radius;\n"
    "}\n"
    "void main() {\n"
    "  vec2 halfSize = uRect.zw * 0.5;\n"
    "  vec2 center = vec2(uRect.x + halfSize.x, uResolution.y - uRect.y - halfSize.y);\n"
    "  vec2 p = gl_FragCoord.xy - center;\n"
    "  float radius = min(uRadiusPx, max(min(halfSize.x, halfSize.y) - 0.5, 0.0));\n"
    "  float d = roundedRectSDF(p, halfSize, radius);\n"
    "  float aa = max(uAAWidthPx, 0.35);\n"
    "  float mask = 1.0 - smoothstep(0.0, aa, d);\n"
    "  if (mask <= 0.001) discard;\n"
    "  float inside = max(-d, 0.0);\n"
    "  float border = 1.0 - smoothstep(max(uBorderWidthPx, 0.0), max(uBorderWidthPx, 0.0) + aa, inside);\n"
    "  border *= step(0.001, uBorderWidthPx);\n"
    "  float top = clamp((p.y / max(halfSize.y, 1.0)) * 0.5 + 0.5, 0.0, 1.0);\n"
    "  vec3 color = uColor.rgb;\n"
    "  color += vec3(1.0) * top * uTopLight;\n"
    "  color = mix(color, uBorderColor.rgb, clamp(border * uBorderColor.a, 0.0, 1.0));\n"
    "  float alpha = uColor.a * mask;\n"
    "  outColor = vec4(color * alpha, alpha);\n"
    "}\n";

static float clampf(float v, float lo, float hi) {
  if (v < lo) return lo;
  if (v > hi) return hi;
  return v;
}

static float smoother01(float x) {
  x = clampf(x, 0.0f, 1.0f);
  return x * x * x * (x * (x * 6.0f - 15.0f) + 10.0f);
}

static float mixf(float a, float b, float t) {
  return a + (b - a) * t;
}

static int clampi(int v, int lo, int hi) {
  if (v < lo) return lo;
  if (v > hi) return hi;
  return v;
}

static MeltedUIColor color_mix(MeltedUIColor a, MeltedUIColor b, float t) {
  MeltedUIColor out = {
      mixf(a.r, b.r, t),
      mixf(a.g, b.g, t),
      mixf(a.b, b.b, t),
      mixf(a.a, b.a, t),
  };
  return out;
}

static Rect rect_make(float x, float y, float width, float height) {
  Rect rect = {x, y, width, height};
  return rect;
}

static Rect rect_grow(Rect rect, float grow) {
  rect.x -= grow * 0.5f;
  rect.y -= grow * 0.5f;
  rect.width += grow;
  rect.height += grow;
  return rect;
}

static void switch_metrics(const MeltedUISwitch *control,
                           float *inset,
                           float *knobWidth,
                           float *knobHeight,
                           float *travel) {
  float localInset = clampf(control->knobInsetPx, 0.0f, fmaxf(control->rect.height * 0.45f, 0.0f));
  float localKnobHeight = fmaxf(control->rect.height - localInset * 2.0f, 1.0f);
  float localKnobWidth = fminf(localKnobHeight * fmaxf(control->knobWidthRatio, 1.0f),
                               fmaxf(control->rect.width - localInset * 2.0f, 1.0f));
  float localTravel = fmaxf(control->rect.width - localInset * 2.0f - localKnobWidth, 0.0f);

  if (inset) *inset = localInset;
  if (knobWidth) *knobWidth = localKnobWidth;
  if (knobHeight) *knobHeight = localKnobHeight;
  if (travel) *travel = localTravel;
}

static void switch_set_value_from_knob_x(MeltedUISwitch *control, float knobX) {
  float inset = 0.0f;
  float travel = 0.0f;
  switch_metrics(control, &inset, NULL, NULL, &travel);
  if (travel <= 0.001f) {
    control->value = 0.0f;
    return;
  }

  control->value = clampf((knobX - control->rect.x - inset) / travel, 0.0f, 1.0f);
  control->targetValue = control->value;
  control->targetOn = control->value >= 0.5f ? 1 : 0;
}

static void menu_selection_metrics(const MeltedUIMenu *menu,
                                   float *innerX,
                                   float *itemWidth,
                                   float *maxPosition) {
  float inset = menu ? fmaxf(menu->selectorInsetPx, 0.0f) : 0.0f;
  float count = menu && menu->itemCount > 0 ? (float)menu->itemCount : 1.0f;
  float innerW = menu ? fmaxf(menu->rect.width - inset * 2.0f, 1.0f) : 1.0f;

  if (innerX) *innerX = menu ? menu->rect.x + inset : 0.0f;
  if (itemWidth) *itemWidth = innerW / count;
  if (maxPosition) *maxPosition = count - 1.0f;
}

static float menu_drag_position_at(const MeltedUIMenu *menu, float x) {
  if (!menu || menu->itemCount <= 0) {
    return 0.0f;
  }

  float innerX = 0.0f;
  float itemW = 1.0f;
  float maxPosition = 0.0f;
  menu_selection_metrics(menu, &innerX, &itemW, &maxPosition);
  float selectorX = x - menu->dragOffsetX;
  return clampf((selectorX - innerX) / fmaxf(itemW, 1.0f), 0.0f, maxPosition);
}

static void menu_set_drag_position(MeltedUIMenu *menu, float x) {
  if (!menu || menu->itemCount <= 0) {
    return;
  }

  float itemW = 1.0f;
  menu_selection_metrics(menu, NULL, &itemW, NULL);
  float oldPosition = menu->selectedPosition;
  menu->selectedPosition = menu_drag_position_at(menu, x);
  menu->targetPosition = menu->selectedPosition;
  menu->startPosition = menu->selectedPosition;
  menu->targetIndex = clampi((int)floorf(menu->selectedPosition + 0.5f), 0, menu->itemCount - 1);
  menu->selectedIndex = menu->targetIndex;
  menu->isAnimating = 0;
  menu->animationTime = 0.0f;
  menu->dragVelocityX = (menu->selectedPosition - oldPosition) * fmaxf(itemW, 1.0f);
  menu->pressAmount = fmaxf(menu->pressAmount, 0.92f);
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
    fprintf(stderr, "MeltedUI: failed to compile %s: %.*s\n", label, (int)length, log);
    glDeleteShader(shader);
    return 0;
  }

  return shader;
}

static GLuint link_program(const char *fragmentSource, const char *label) {
  GLuint vs = compile_shader(GL_VERTEX_SHADER, melted_ui_fullscreen_vs, "fullscreen vertex shader");
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
    fprintf(stderr, "MeltedUI: failed to link %s: %.*s\n", label, (int)length, log);
    glDeleteProgram(program);
    return 0;
  }

  return program;
}

static GLint uni(GLuint program, const char *name) {
  return glGetUniformLocation(program, name);
}

static void draw_fullscreen(GLuint vao) {
  glBindVertexArray(vao);
  glDrawArrays(GL_TRIANGLES, 0, 6);
}

static void set_color(GLuint program, const char *name, MeltedUIColor color) {
  glUniform4f(uni(program, name), color.r, color.g, color.b, color.a);
}

static void draw_rounded_rect(MeltedUIRenderer *ui,
                              Rect rect,
                              float radius,
                              MeltedUIColor color,
                              MeltedUIColor borderColor,
                              float borderWidth,
                              float aa,
                              float topLight,
                              int targetWidth,
                              int targetHeight) {
  if (!ui || !ui->roundedRectProgram || rect.width <= 0.0f || rect.height <= 0.0f) {
    return;
  }

  GLuint program = ui->roundedRectProgram;
  glUseProgram(program);
  glUniform2f(uni(program, "uResolution"), (float)targetWidth, (float)targetHeight);
  glUniform4f(uni(program, "uRect"), rect.x, rect.y, rect.width, rect.height);
  glUniform1f(uni(program, "uRadiusPx"), radius);
  glUniform1f(uni(program, "uAAWidthPx"), aa);
  glUniform1f(uni(program, "uBorderWidthPx"), borderWidth);
  glUniform1f(uni(program, "uTopLight"), topLight);
  set_color(program, "uColor", color);
  set_color(program, "uBorderColor", borderColor);
  draw_fullscreen(ui->quadVao);
}

int melted_ui_renderer_init(MeltedUIRenderer *renderer) {
  if (!renderer) {
    return 0;
  }

  memset(renderer, 0, sizeof(*renderer));
  glGenVertexArrays(1, &renderer->quadVao);
  renderer->roundedRectProgram = link_program(melted_ui_rounded_rect_fs, "rounded rect fragment shader");
  return renderer->quadVao != 0 && renderer->roundedRectProgram != 0;
}

void melted_ui_renderer_destroy(MeltedUIRenderer *renderer) {
  if (!renderer) {
    return;
  }

  if (renderer->roundedRectProgram) glDeleteProgram(renderer->roundedRectProgram);
  if (renderer->quadVao) glDeleteVertexArrays(1, &renderer->quadVao);
  memset(renderer, 0, sizeof(*renderer));
}

void melted_ui_switch_defaults(MeltedUISwitch *control, float x, float y, float width, float height) {
  if (!control) {
    return;
  }

  memset(control, 0, sizeof(*control));
  control->rect = rect_make(x, y, width, height);
  control->animationDuration = 0.46f;
  control->knobInsetPx = fmaxf(4.0f, height * 0.085f);
  control->knobWidthRatio = 1.81f;
  control->pressGrowPx = fmaxf(24.0f, height * 0.52f);
  control->dragThresholdPx = fmaxf(4.0f, height * 0.07f);
  control->antiAliasPx = 1.35f;
  control->offTrackColor = (MeltedUIColor){0.84f, 0.85f, 0.86f, 1.0f};
  control->onTrackColor = (MeltedUIColor){0.00f, 0.46f, 1.00f, 1.0f};
  control->offTrackBorderColor = (MeltedUIColor){0.62f, 0.64f, 0.66f, 0.16f};
  control->onTrackBorderColor = (MeltedUIColor){0.00f, 0.24f, 0.72f, 0.24f};
  control->knobTintColor = (MeltedUIColor){1.0f, 1.0f, 1.0f, 1.0f};
}

void melted_ui_switch_set_on(MeltedUISwitch *control, int isOn, int animated) {
  if (!control) {
    return;
  }

  int next = isOn ? 1 : 0;
  if (!animated) {
    control->isOn = next;
    control->targetOn = next;
    control->targetValue = next ? 1.0f : 0.0f;
    control->startValue = control->targetValue;
    control->value = control->targetValue;
    control->pressAmount = 0.0f;
    control->animationTime = 0.0f;
    control->isAnimating = 0;
    control->isPressed = 0;
    control->isPointerDown = 0;
    control->isDragging = 0;
    return;
  }

  control->targetOn = next;
  control->startValue = clampf(control->value, 0.0f, 1.0f);
  control->targetValue = next ? 1.0f : 0.0f;
  control->animationTime = 0.0f;
  control->isAnimating = fabsf(control->targetValue - control->startValue) > 0.001f ? 1 : 0;
  if (!control->isAnimating) {
    control->isOn = next;
  }
}

void melted_ui_switch_toggle(MeltedUISwitch *control, int animated) {
  if (!control) {
    return;
  }
  int current = control->isAnimating ? control->targetOn : control->isOn;
  melted_ui_switch_set_on(control, !current, animated);
}

int melted_ui_switch_hit_test(const MeltedUISwitch *control, float x, float y) {
  if (!control) {
    return 0;
  }

  return x >= control->rect.x &&
         y >= control->rect.y &&
         x <= control->rect.x + control->rect.width &&
         y <= control->rect.y + control->rect.height;
}

void melted_ui_switch_pointer_down(MeltedUISwitch *control) {
  if (!control) {
    return;
  }
  control->isPressed = 1;
  control->isPointerDown = 1;
  control->isDragging = 0;
}

void melted_ui_switch_pointer_up(MeltedUISwitch *control, int commitToggle) {
  if (!control) {
    return;
  }
  int wasDragging = control->isDragging;
  control->isPressed = 0;
  control->isPointerDown = 0;
  control->isDragging = 0;
  if (wasDragging) {
    melted_ui_switch_set_on(control, control->value >= 0.5f, 1);
  } else if (commitToggle) {
    melted_ui_switch_toggle(control, 1);
  }
}

void melted_ui_switch_pointer_down_at(MeltedUISwitch *control, float x, float y) {
  if (!control) {
    return;
  }

  Rect knob = melted_ui_switch_knob_rect(control);
  int hitKnob = x >= knob.x &&
                y >= knob.y &&
                x <= knob.x + knob.width &&
                y <= knob.y + knob.height;
  control->isPressed = 1;
  control->isPointerDown = 1;
  control->isDragging = 0;
  control->isAnimating = 0;
  control->pointerDownX = x;
  control->pointerDownY = y;
  control->dragOffsetX = hitKnob ? x - knob.x : knob.width * 0.5f;
}

void melted_ui_switch_pointer_drag_to(MeltedUISwitch *control, float x, float y) {
  if (!control || !control->isPointerDown) {
    return;
  }

  float dx = x - control->pointerDownX;
  float dy = y - control->pointerDownY;
  float threshold = fmaxf(control->dragThresholdPx, 1.0f);
  if (!control->isDragging && sqrtf(dx * dx + dy * dy) >= threshold) {
    control->isDragging = 1;
    control->isAnimating = 0;
  }

  if (control->isDragging) {
    switch_set_value_from_knob_x(control, x - control->dragOffsetX);
    control->isOn = control->value >= 0.5f ? 1 : 0;
  }
}

void melted_ui_switch_pointer_up_at(MeltedUISwitch *control, float x, float y, int commitToggle) {
  if (!control) {
    return;
  }

  int wasDragging = control->isDragging;
  control->isPressed = 0;
  control->isPointerDown = 0;
  control->isDragging = 0;

  if (wasDragging) {
    switch_set_value_from_knob_x(control, x - control->dragOffsetX);
    melted_ui_switch_set_on(control, control->value >= 0.5f, 1);
  } else if (commitToggle) {
    melted_ui_switch_toggle(control, 1);
  }

  (void)y;
}

void melted_ui_switch_update(MeltedUISwitch *control, float dt) {
  if (!control) {
    return;
  }

  dt = clampf(dt, 0.0f, 0.08f);
  if (control->animationDuration <= 0.001f) {
    control->animationDuration = 0.46f;
  }

  if (control->isAnimating) {
    control->animationTime += dt;
    float t = clampf(control->animationTime / control->animationDuration, 0.0f, 1.0f);
    float moveT = smoother01((t - 0.18f) / 0.64f);
    control->value = mixf(control->startValue, control->targetValue, moveT);

    float pressStep = 1.0f - expf(-dt * 18.0f);
    control->pressAmount += (0.0f - control->pressAmount) * pressStep;

    if (t >= 1.0f) {
      control->isAnimating = 0;
      control->isOn = control->targetOn;
      control->value = control->targetValue;
      control->pressAmount = 0.0f;
      control->animationTime = 0.0f;
    }
    return;
  }

  if (control->isDragging) {
    float targetPress = 1.0f;
    float pressStep = 1.0f - expf(-dt * 18.0f);
    control->pressAmount += (targetPress - control->pressAmount) * pressStep;
    control->targetValue = control->value;
    control->targetOn = control->value >= 0.5f ? 1 : 0;
    return;
  }

  float targetValue = control->isOn ? 1.0f : 0.0f;
  float valueStep = 1.0f - expf(-dt * 16.0f);
  control->value += (targetValue - control->value) * valueStep;

  float targetPress = 0.0f;
  float pressStep = 1.0f - expf(-dt * 18.0f);
  control->pressAmount += (targetPress - control->pressAmount) * pressStep;
  if (control->pressAmount < 0.001f) {
    control->pressAmount = 0.0f;
  }
}

Rect melted_ui_switch_knob_rect(const MeltedUISwitch *control) {
  if (!control) {
    return rect_make(0.0f, 0.0f, 0.0f, 0.0f);
  }

  float inset = 0.0f;
  float knobW = 0.0f;
  float knobH = 0.0f;
  float travel = 0.0f;
  switch_metrics(control, &inset, &knobW, &knobH, &travel);
  float x = control->rect.x + inset + travel * clampf(control->value, 0.0f, 1.0f);
  float y = control->rect.y + inset;
  return rect_make(x, y, knobW, knobH);
}

MeltedGlassInteraction melted_ui_switch_interaction(const MeltedUISwitch *control) {
  MeltedGlassInteraction interaction;
  memset(&interaction, 0, sizeof(interaction));
  if (control) {
    interaction.pressAmount = clampf(control->pressAmount, 0.0f, 1.0f);
  }
  return interaction;
}

void melted_ui_draw_switch_track(MeltedUIRenderer *ui,
                                 const MeltedUISwitch *control,
                                 int targetWidth,
                                 int targetHeight) {
  if (!ui || !control || targetWidth <= 0 || targetHeight <= 0) {
    return;
  }

  glEnable(GL_BLEND);
  glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

  float value = smoother01(clampf(control->value, 0.0f, 1.0f));
  MeltedUIColor trackColor = color_mix(control->offTrackColor, control->onTrackColor, value);
  MeltedUIColor borderColor = color_mix(control->offTrackBorderColor, control->onTrackBorderColor, value);
  float radius = control->rect.height * 0.5f;
  float aa = fmaxf(control->antiAliasPx, 0.5f);

  Rect trackShadow = control->rect;
  trackShadow.y += fmaxf(1.0f, control->rect.height * 0.035f);
  MeltedUIColor trackShadowColor = {0.0f, 0.0f, 0.0f, value * 0.035f};
  draw_rounded_rect(ui, trackShadow, radius, trackShadowColor,
                    (MeltedUIColor){0.0f, 0.0f, 0.0f, 0.0f},
                    0.0f, aa, 0.0f, targetWidth, targetHeight);

  draw_rounded_rect(ui, control->rect, radius, trackColor, borderColor,
                    fmaxf(1.0f, control->rect.height * 0.025f),
                    aa, mixf(0.05f, 0.015f, value), targetWidth, targetHeight);

  Rect knobShadow = melted_ui_switch_knob_rect(control);
  knobShadow = rect_grow(knobShadow, control->pressAmount * control->pressGrowPx * 0.55f);
  knobShadow.y += fmaxf(1.5f, control->rect.height * 0.045f);
  MeltedUIColor knobShadowColor = {0.0f, 0.0f, 0.0f, value * 0.06f * (1.0f - control->pressAmount * 0.24f)};
  draw_rounded_rect(ui, knobShadow, knobShadow.height * 0.5f, knobShadowColor,
                    (MeltedUIColor){0.0f, 0.0f, 0.0f, 0.0f},
                    0.0f, aa, 0.0f, targetWidth, targetHeight);
}

void melted_ui_draw_switch_knob(const MeltedUISwitch *control,
                                MeltedGlassRenderer *glass,
                                const MeltedGlassConfig *cfg,
                                GLuint sceneTexture,
                                GLuint blurredTexture,
                                int targetWidth,
                                int targetHeight) {
  if (!control || !glass || !cfg || targetWidth <= 0 || targetHeight <= 0) {
    return;
  }

  Rect knob = melted_ui_switch_knob_rect(control);
  MeltedGlassConfig buttonCfg = *cfg;
  buttonCfg.drawAsRoundedRect = 1;
  buttonCfg.clipWithSDF = 1;
  buttonCfg.cornerRadiusPx = knob.height * 0.5f;
  buttonCfg.antiAliasPx = fmaxf(buttonCfg.antiAliasPx, control->antiAliasPx);
  buttonCfg.edgeWidthPx = fmaxf(knob.height * 0.42f, 1.0f);
  buttonCfg.refractionTransitionWidthPx = fmaxf(knob.height * 0.70f, 1.0f);
  buttonCfg.refractionTransitionSoftnessPx = fmaxf(knob.height * 0.22f, 1.0f);
  buttonCfg.refractionStrength = fmaxf(buttonCfg.refractionStrength, 3.0f);
  buttonCfg.opticalThicknessPx = fmaxf(buttonCfg.opticalThicknessPx, knob.height * 1.7f);
  buttonCfg.lensCurvature = fmaxf(buttonCfg.lensCurvature, 0.42f);
  buttonCfg.magnification = fmaxf(buttonCfg.magnification, 1.08f);
  buttonCfg.buttonIdleOpacity = 0.97f;
  buttonCfg.buttonPressedOpacity = 0.99f;
  buttonCfg.buttonPressedSharpness = fmaxf(buttonCfg.buttonPressedSharpness, 0.24f);
  buttonCfg.buttonDiffusionRadiusPx = fmaxf(buttonCfg.buttonDiffusionRadiusPx, knob.height * 0.22f);
  buttonCfg.prismaticPressGrowPx = control->pressGrowPx;
  buttonCfg.whiteLift = fmaxf(buttonCfg.whiteLift, 0.08f);
  buttonCfg.tintStrength = 0.0f;

  MeltedGlassPanel panel;
  panel.rect = knob;
  panel.shapeMode = MELTED_GLASS_SHAPE_ROUNDED_RECT;
  panel.tintColor[0] = control->knobTintColor.r;
  panel.tintColor[1] = control->knobTintColor.g;
  panel.tintColor[2] = control->knobTintColor.b;
  panel.tintStrength = 0.0f;

  MeltedGlassInteraction interaction = melted_ui_switch_interaction(control);
  melted_glass_draw_button_panel(glass, &buttonCfg, &panel, &interaction,
                                 sceneTexture, blurredTexture, targetWidth, targetHeight);
}

void melted_ui_draw_switch(MeltedUIRenderer *ui,
                           const MeltedUISwitch *control,
                           MeltedGlassRenderer *glass,
                           const MeltedGlassConfig *cfg,
                           GLuint sceneTexture,
                           GLuint blurredTexture,
                           int targetWidth,
                           int targetHeight) {
  melted_ui_draw_switch_track(ui, control, targetWidth, targetHeight);
  melted_ui_draw_switch_knob(control, glass, cfg, sceneTexture, blurredTexture, targetWidth, targetHeight);
}

void melted_ui_button_defaults(MeltedUIButton *button, float x, float y, float width, float height) {
  if (!button) {
    return;
  }

  memset(button, 0, sizeof(*button));
  button->rect = rect_make(x, y, width, height);
  button->shapeMode = MELTED_GLASS_SHAPE_CIRCLE;
  button->pressGrowPx = fmaxf(12.0f, height * 0.22f);
  button->cornerRadiusPx = fmaxf(12.0f, height * 0.26f);
  button->antiAliasPx = 1.35f;
  button->opacity = 0.95f;
  button->color = (MeltedUIColor){0.0f, 0.46f, 1.0f, 1.0f};
  button->pressedColor = (MeltedUIColor){0.12f, 0.56f, 1.0f, 1.0f};
}

int melted_ui_button_hit_test(const MeltedUIButton *button, float x, float y) {
  if (!button) {
    return 0;
  }

  if (button->shapeMode == MELTED_GLASS_SHAPE_CIRCLE) {
    float radius = fminf(button->rect.width, button->rect.height) * 0.5f;
    float cx = button->rect.x + button->rect.width * 0.5f;
    float cy = button->rect.y + button->rect.height * 0.5f;
    float dx = x - cx;
    float dy = y - cy;
    return dx * dx + dy * dy <= radius * radius;
  }

  if (button->shapeMode == MELTED_GLASS_SHAPE_ELLIPSE) {
    float rx = fmaxf(button->rect.width * 0.5f, 1.0f);
    float ry = fmaxf(button->rect.height * 0.5f, 1.0f);
    float cx = button->rect.x + button->rect.width * 0.5f;
    float cy = button->rect.y + button->rect.height * 0.5f;
    float dx = (x - cx) / rx;
    float dy = (y - cy) / ry;
    return dx * dx + dy * dy <= 1.0f;
  }

  return x >= button->rect.x &&
         y >= button->rect.y &&
         x <= button->rect.x + button->rect.width &&
         y <= button->rect.y + button->rect.height;
}

void melted_ui_button_pointer_down(MeltedUIButton *button) {
  if (!button) {
    return;
  }

  button->isPressed = 1;
  button->isPointerDown = 1;
}

int melted_ui_button_pointer_up(MeltedUIButton *button, int commitClick) {
  if (!button) {
    return 0;
  }

  int clicked = button->isPointerDown && commitClick;
  button->isPressed = 0;
  button->isPointerDown = 0;
  return clicked;
}

void melted_ui_button_update(MeltedUIButton *button, float dt) {
  if (!button) {
    return;
  }

  dt = clampf(dt, 0.0f, 0.08f);
  float targetPress = button->isPressed ? 1.0f : 0.0f;
  float pressStep = 1.0f - expf(-dt * 18.0f);
  button->pressAmount += (targetPress - button->pressAmount) * pressStep;
  if (!button->isPressed && button->pressAmount < 0.001f) {
    button->pressAmount = 0.0f;
  }
}

Rect melted_ui_button_current_rect(const MeltedUIButton *button) {
  if (!button) {
    return rect_make(0.0f, 0.0f, 0.0f, 0.0f);
  }

  return rect_grow(button->rect, clampf(button->pressAmount, 0.0f, 1.0f) * button->pressGrowPx);
}

void melted_ui_draw_button(const MeltedUIButton *button,
                           MeltedGlassRenderer *glass,
                           const MeltedGlassConfig *cfg,
                           GLuint sceneTexture,
                           GLuint blurredTexture,
                           int targetWidth,
                           int targetHeight) {
  if (!button || !glass || !cfg || targetWidth <= 0 || targetHeight <= 0) {
    return;
  }

  Rect rect = melted_ui_button_current_rect(button);
  if (rect.width <= 0.0f || rect.height <= 0.0f) {
    return;
  }

  float pressT = smoother01(clampf(button->pressAmount, 0.0f, 1.0f));
  MeltedUIColor tint = color_mix(button->color, button->pressedColor, pressT);
  float opacity = clampf(button->opacity * clampf(tint.a, 0.0f, 1.0f), 0.0f, 1.0f);
  MeltedGlassConfig buttonCfg = *cfg;
  buttonCfg.drawAsRoundedRect = 1;
  buttonCfg.clipWithSDF = 1;
  buttonCfg.cornerRadiusPx = fminf(button->cornerRadiusPx, fmaxf(fminf(rect.width, rect.height) * 0.5f - 0.5f, 0.0f));
  buttonCfg.antiAliasPx = fmaxf(buttonCfg.antiAliasPx, button->antiAliasPx);
  buttonCfg.edgeWidthPx = fmaxf(rect.height * 0.40f, 1.0f);
  buttonCfg.refractionTransitionWidthPx = fmaxf(rect.height * 0.70f, 1.0f);
  buttonCfg.refractionTransitionSoftnessPx = fmaxf(rect.height * 0.22f, 1.0f);
  buttonCfg.refractionStrength = fmaxf(buttonCfg.refractionStrength, 2.5f);
  buttonCfg.opticalThicknessPx = fmaxf(buttonCfg.opticalThicknessPx, rect.height * 1.6f);
  buttonCfg.lensCurvature = fmaxf(buttonCfg.lensCurvature, 0.38f);
  buttonCfg.magnification = fmaxf(buttonCfg.magnification, 1.06f);
  buttonCfg.opacity = opacity;
  buttonCfg.prismaticIdleOpacity = opacity;
  buttonCfg.prismaticClearOpacity = opacity;
  buttonCfg.buttonPressedSharpness = 0.0f;
  buttonCfg.fresnelEnabled = 1;
  buttonCfg.fresnelStrength = fmaxf(buttonCfg.fresnelStrength, 0.14f);
  buttonCfg.specularEnabled = 1;
  buttonCfg.specularOverallStrength = fmaxf(buttonCfg.specularOverallStrength, 0.56f);
  buttonCfg.borderEnabled = 1;
  buttonCfg.bevelEnabled = 1;
  buttonCfg.rimHighlightWidthPx = fmaxf(buttonCfg.rimHighlightWidthPx, 1.35f);
  buttonCfg.rimHighlightSmoothPx = fmaxf(buttonCfg.rimHighlightSmoothPx, rect.height * 0.18f);
  buttonCfg.innerHighlightAlpha = fmaxf(buttonCfg.innerHighlightAlpha, 0.28f);
  buttonCfg.outerHighlightAlpha = fmaxf(buttonCfg.outerHighlightAlpha, 0.12f);
  buttonCfg.innerShadowAlpha = fmaxf(buttonCfg.innerShadowAlpha, 0.055f);
  buttonCfg.topHighlightEnabled = 1;
  buttonCfg.topHighlightStrength = fmaxf(buttonCfg.topHighlightStrength, 0.16f);
  buttonCfg.cornerGlintsEnabled = 1;
  buttonCfg.cornerGlintStrength = fmaxf(buttonCfg.cornerGlintStrength, 0.12f);
  buttonCfg.shadowEnabled = 0;
  buttonCfg.shadowAlpha = 0.0f;

  MeltedGlassPanel panel;
  panel.rect = rect;
  panel.shapeMode = button->shapeMode;
  panel.tintColor[0] = tint.r;
  panel.tintColor[1] = tint.g;
  panel.tintColor[2] = tint.b;
  panel.tintStrength = 1.0f;

  melted_glass_draw_panel(glass, &buttonCfg, &panel, sceneTexture, blurredTexture, targetWidth, targetHeight);
}

void melted_ui_widget_defaults(MeltedUIWidget *widget,
                               float x,
                               float y,
                               float width,
                               float height,
                               int theme) {
  if (!widget) {
    return;
  }

  memset(widget, 0, sizeof(*widget));
  widget->rect = rect_make(x, y, width, height);
  widget->theme = theme == MELTED_UI_WIDGET_DARK ? MELTED_UI_WIDGET_DARK : MELTED_UI_WIDGET_LIGHT;
  widget->shapeMode = MELTED_GLASS_SHAPE_ROUNDED_RECT;
  widget->cornerRadiusPx = fmaxf(28.0f, fminf(width, height) * 0.155f);
  widget->antiAliasPx = 1.55f;
  widget->lightOpacity = 0.76f;
  widget->darkOpacity = 0.84f;
  widget->lightTintStrength = 0.56f;
  widget->darkTintStrength = 0.84f;
  widget->lightWhiteLift = 0.085f;
  widget->darkWhiteLift = 0.018f;
  widget->lightTint = (MeltedUIColor){0.80f, 0.76f, 0.65f, 1.0f};
  widget->darkTint = (MeltedUIColor){0.052f, 0.072f, 0.052f, 1.0f};
}

void melted_ui_widget_set_theme(MeltedUIWidget *widget, int theme) {
  if (!widget) {
    return;
  }

  widget->theme = theme == MELTED_UI_WIDGET_DARK ? MELTED_UI_WIDGET_DARK : MELTED_UI_WIDGET_LIGHT;
}

int melted_ui_widget_hit_test(const MeltedUIWidget *widget, float x, float y) {
  if (!widget) {
    return 0;
  }

  return x >= widget->rect.x &&
         y >= widget->rect.y &&
         x <= widget->rect.x + widget->rect.width &&
         y <= widget->rect.y + widget->rect.height;
}

void melted_ui_draw_widget(const MeltedUIWidget *widget,
                           MeltedGlassRenderer *glass,
                           const MeltedGlassConfig *cfg,
                           GLuint sceneTexture,
                           GLuint blurredTexture,
                           int targetWidth,
                           int targetHeight) {
  if (!widget || !glass || !cfg || targetWidth <= 0 || targetHeight <= 0) {
    return;
  }

  Rect rect = widget->rect;
  if (rect.width <= 0.0f || rect.height <= 0.0f) {
    return;
  }

  int isDark = widget->theme == MELTED_UI_WIDGET_DARK;
  MeltedUIColor tint = isDark ? widget->darkTint : widget->lightTint;
  float opacity = clampf((isDark ? widget->darkOpacity : widget->lightOpacity) * tint.a, 0.0f, 1.0f);
  float tintStrength = clampf(isDark ? widget->darkTintStrength : widget->lightTintStrength, 0.0f, 1.0f);
  float minEdge = fminf(rect.width, rect.height);

  MeltedGlassConfig widgetCfg = *cfg;
  widgetCfg.drawAsRoundedRect = 1;
  widgetCfg.clipWithSDF = 1;
  widgetCfg.shapeMode = widget->shapeMode;
  widgetCfg.cornerRadiusPx = widget->cornerRadiusPx > 0.0f ? widget->cornerRadiusPx : fmaxf(28.0f, minEdge * 0.155f);
  widgetCfg.cornerRadiusPx = fminf(widgetCfg.cornerRadiusPx, fmaxf(minEdge * 0.5f - 0.5f, 0.0f));
  widgetCfg.antiAliasPx = fmaxf(widgetCfg.antiAliasPx, widget->antiAliasPx);
  widgetCfg.opacity = opacity;
  widgetCfg.whiteLift = isDark ? widget->darkWhiteLift : widget->lightWhiteLift;
  widgetCfg.tintStrength = tintStrength;
  widgetCfg.edgeWidthPx = fmaxf(widgetCfg.edgeWidthPx, minEdge * 0.16f);
  widgetCfg.refractionTransitionWidthPx = fmaxf(widgetCfg.refractionTransitionWidthPx, minEdge * 0.25f);
  widgetCfg.refractionTransitionSoftnessPx = fmaxf(widgetCfg.refractionTransitionSoftnessPx, minEdge * 0.065f);
  widgetCfg.refractionStrength = fmaxf(widgetCfg.refractionStrength, 2.6f);
  widgetCfg.opticalThicknessPx = fmaxf(widgetCfg.opticalThicknessPx, minEdge * 0.58f);
  widgetCfg.lensCurvature = fmaxf(widgetCfg.lensCurvature, 0.38f);
  widgetCfg.magnification = fmaxf(widgetCfg.magnification, 1.06f);
  widgetCfg.diffusionRadiusPx = fmaxf(widgetCfg.diffusionRadiusPx, minEdge * 0.11f);
  widgetCfg.buttonDiffusionRadiusPx = fmaxf(widgetCfg.buttonDiffusionRadiusPx, minEdge * 0.07f);
  widgetCfg.fresnelEnabled = 1;
  widgetCfg.fresnelStrength = fmaxf(widgetCfg.fresnelStrength, isDark ? 0.10f : 0.085f);
  widgetCfg.specularEnabled = 1;
  widgetCfg.specularOverallStrength = fmaxf(widgetCfg.specularOverallStrength, isDark ? 0.50f : 0.42f);
  widgetCfg.borderEnabled = 1;
  widgetCfg.outerBorderAlpha = fmaxf(widgetCfg.outerBorderAlpha, isDark ? 0.14f : 0.10f);
  widgetCfg.innerBorderAlpha = fmaxf(widgetCfg.innerBorderAlpha, isDark ? 0.13f : 0.12f);
  widgetCfg.darkEdgeAlpha = fmaxf(widgetCfg.darkEdgeAlpha, isDark ? 0.055f : 0.030f);
  widgetCfg.bevelEnabled = 1;
  widgetCfg.rimHighlightWidthPx = fmaxf(widgetCfg.rimHighlightWidthPx, 0.85f);
  widgetCfg.rimHighlightSmoothPx = fmaxf(widgetCfg.rimHighlightSmoothPx, minEdge * 0.055f);
  widgetCfg.innerHighlightAlpha = fmaxf(widgetCfg.innerHighlightAlpha, isDark ? 0.18f : 0.16f);
  widgetCfg.outerHighlightAlpha = fmaxf(widgetCfg.outerHighlightAlpha, isDark ? 0.09f : 0.08f);
  widgetCfg.innerShadowAlpha = fmaxf(widgetCfg.innerShadowAlpha, isDark ? 0.055f : 0.040f);
  widgetCfg.topHighlightEnabled = 1;
  widgetCfg.topHighlightStrength = fmaxf(widgetCfg.topHighlightStrength, isDark ? 0.075f : 0.095f);
  widgetCfg.cornerGlintsEnabled = 1;
  widgetCfg.cornerGlintStrength = fmaxf(widgetCfg.cornerGlintStrength, 0.10f);
  widgetCfg.shadowEnabled = 0;
  widgetCfg.shadowAlpha = 0.0f;

  MeltedGlassPanel panel;
  panel.rect = rect;
  panel.shapeMode = widget->shapeMode;
  panel.tintColor[0] = tint.r;
  panel.tintColor[1] = tint.g;
  panel.tintColor[2] = tint.b;
  panel.tintStrength = tintStrength;

  melted_glass_draw_panel(glass, &widgetCfg, &panel, sceneTexture, blurredTexture, targetWidth, targetHeight);
}

void melted_ui_menu_defaults(MeltedUIMenu *menu,
                             float x,
                             float y,
                             float width,
                             float height,
                             int itemCount,
                             int theme) {
  if (!menu) {
    return;
  }

  memset(menu, 0, sizeof(*menu));
  menu->rect = rect_make(x, y, width, height);
  menu->itemCount = clampi(itemCount, 1, MELTED_UI_MENU_MAX_ITEMS);
  menu->selectedIndex = 0;
  menu->targetIndex = 0;
  menu->theme = theme == MELTED_UI_MENU_DARK ? MELTED_UI_MENU_DARK : MELTED_UI_MENU_LIGHT;
  menu->selectedPosition = 0.0f;
  menu->startPosition = 0.0f;
  menu->targetPosition = 0.0f;
  menu->animationDuration = 0.34f;
  menu->dragThresholdPx = fmaxf(3.0f, height * 0.045f);
  menu->cornerRadiusPx = height * 0.5f;
  menu->itemInsetPx = fmaxf(6.0f, height * 0.085f);
  menu->selectorInsetPx = fmaxf(7.0f, height * 0.10f);
  menu->selectorGrowPx = fmaxf(11.0f, height * 0.16f);
  menu->antiAliasPx = 1.45f;
  menu->backgroundOpacity = 0.86f;
  menu->selectorOpacity = 0.93f;
  menu->lightBackgroundTint = (MeltedUIColor){0.96f, 0.97f, 0.98f, 1.0f};
  menu->lightSelectorTint = (MeltedUIColor){0.90f, 0.91f, 0.92f, 1.0f};
  menu->darkBackgroundTint = (MeltedUIColor){0.055f, 0.065f, 0.070f, 1.0f};
  menu->darkSelectorTint = (MeltedUIColor){0.22f, 0.25f, 0.26f, 1.0f};
}

void melted_ui_menu_set_theme(MeltedUIMenu *menu, int theme) {
  if (!menu) {
    return;
  }

  menu->theme = theme == MELTED_UI_MENU_DARK ? MELTED_UI_MENU_DARK : MELTED_UI_MENU_LIGHT;
}

void melted_ui_menu_set_item_count(MeltedUIMenu *menu, int itemCount) {
  if (!menu) {
    return;
  }

  menu->itemCount = clampi(itemCount, 1, MELTED_UI_MENU_MAX_ITEMS);
  menu->selectedIndex = clampi(menu->selectedIndex, 0, menu->itemCount - 1);
  menu->targetIndex = clampi(menu->targetIndex, 0, menu->itemCount - 1);
  menu->selectedPosition = clampf(menu->selectedPosition, 0.0f, (float)(menu->itemCount - 1));
  menu->startPosition = clampf(menu->startPosition, 0.0f, (float)(menu->itemCount - 1));
  menu->targetPosition = (float)menu->targetIndex;
  menu->isDragging = 0;
  menu->isPointerDown = 0;
}

void melted_ui_menu_set_selected(MeltedUIMenu *menu, int selectedIndex, int animated) {
  if (!menu || menu->itemCount <= 0) {
    return;
  }

  int next = clampi(selectedIndex, 0, menu->itemCount - 1);
  menu->targetIndex = next;
  menu->targetPosition = (float)next;

  if (!animated) {
    menu->selectedIndex = next;
    menu->selectedPosition = menu->targetPosition;
    menu->startPosition = menu->selectedPosition;
    menu->animationTime = 0.0f;
    menu->isAnimating = 0;
    menu->pressAmount = 0.0f;
    return;
  }

  menu->startPosition = clampf(menu->selectedPosition, 0.0f, (float)(menu->itemCount - 1));
  menu->animationTime = 0.0f;
  menu->isAnimating = fabsf(menu->targetPosition - menu->startPosition) > 0.001f ? 1 : 0;
  menu->pressAmount = fmaxf(menu->pressAmount, 0.7f);
  if (!menu->isAnimating) {
    menu->selectedIndex = next;
  }
}

int melted_ui_menu_hit_test(const MeltedUIMenu *menu, float x, float y) {
  if (!menu) {
    return 0;
  }

  return x >= menu->rect.x &&
         y >= menu->rect.y &&
         x <= menu->rect.x + menu->rect.width &&
         y <= menu->rect.y + menu->rect.height;
}

int melted_ui_menu_item_at(const MeltedUIMenu *menu, float x, float y) {
  if (!melted_ui_menu_hit_test(menu, x, y) || menu->itemCount <= 0) {
    return -1;
  }

  float localX = clampf(x - menu->rect.x, 0.0f, fmaxf(menu->rect.width - 0.001f, 0.0f));
  int index = (int)floorf(localX / fmaxf(menu->rect.width, 1.0f) * (float)menu->itemCount);
  return clampi(index, 0, menu->itemCount - 1);
}

void melted_ui_menu_pointer_down_at(MeltedUIMenu *menu, float x, float y) {
  if (!menu || !melted_ui_menu_hit_test(menu, x, y)) {
    return;
  }

  Rect selector = melted_ui_menu_selection_rect(menu);
  int hitSelector = x >= selector.x &&
                    y >= selector.y &&
                    x <= selector.x + selector.width &&
                    y <= selector.y + selector.height;

  menu->isPointerDown = 1;
  menu->isDragging = 0;
  menu->isAnimating = 0;
  menu->pointerDownX = x;
  menu->pointerDownY = y;
  menu->dragOffsetX = hitSelector ? x - selector.x : selector.width * 0.5f;
  menu->dragVelocityX = 0.0f;
}

void melted_ui_menu_pointer_drag_to(MeltedUIMenu *menu, float x, float y) {
  if (!menu || !menu->isPointerDown) {
    return;
  }

  float dx = x - menu->pointerDownX;
  float dy = y - menu->pointerDownY;
  float threshold = fmaxf(menu->dragThresholdPx, 1.0f);
  if (!menu->isDragging && sqrtf(dx * dx + dy * dy) >= threshold) {
    menu->isDragging = 1;
    menu->isAnimating = 0;
  }

  if (menu->isDragging) {
    menu_set_drag_position(menu, x);
  }
}

int melted_ui_menu_pointer_up_at(MeltedUIMenu *menu, float x, float y) {
  if (!menu) {
    return 0;
  }

  int wasDragging = menu->isDragging;
  if (wasDragging) {
    menu_set_drag_position(menu, x);
  }

  menu->isPointerDown = 0;
  menu->isDragging = 0;

  if (wasDragging) {
    int index = clampi((int)floorf(menu->selectedPosition + 0.5f), 0, menu->itemCount - 1);
    melted_ui_menu_set_selected(menu, index, 1);
    (void)y;
    return 1;
  }

  int index = melted_ui_menu_item_at(menu, x, y);
  if (index < 0) {
    return 0;
  }

  melted_ui_menu_set_selected(menu, index, 1);
  return 1;
}

void melted_ui_menu_update(MeltedUIMenu *menu, float dt) {
  if (!menu) {
    return;
  }

  dt = clampf(dt, 0.0f, 0.08f);
  if (menu->animationDuration <= 0.001f) {
    menu->animationDuration = 0.34f;
  }

  if (menu->isAnimating) {
    menu->animationTime += dt;
    float t = clampf(menu->animationTime / menu->animationDuration, 0.0f, 1.0f);
    menu->selectedPosition = mixf(menu->startPosition, menu->targetPosition, smoother01(t));
    if (t >= 1.0f) {
      menu->selectedIndex = menu->targetIndex;
      menu->selectedPosition = menu->targetPosition;
      menu->animationTime = 0.0f;
      menu->isAnimating = 0;
    }
  }

  if (!menu->isDragging && fabsf(menu->dragVelocityX) > 0.001f) {
    float velocityDecay = expf(-dt * 10.0f);
    menu->dragVelocityX *= velocityDecay;
    if (fabsf(menu->dragVelocityX) < 0.01f) {
      menu->dragVelocityX = 0.0f;
    }
  }

  float targetPress = (menu->isAnimating || menu->isDragging || fabsf(menu->dragVelocityX) > 0.001f) ? 1.0f : 0.0f;
  float pressStep = 1.0f - expf(-dt * 12.0f);
  menu->pressAmount += (targetPress - menu->pressAmount) * pressStep;
  if (!menu->isAnimating && menu->pressAmount < 0.001f) {
    menu->pressAmount = 0.0f;
  }
}

Rect melted_ui_menu_item_rect(const MeltedUIMenu *menu, int itemIndex) {
  if (!menu || menu->itemCount <= 0) {
    return rect_make(0.0f, 0.0f, 0.0f, 0.0f);
  }

  int index = clampi(itemIndex, 0, menu->itemCount - 1);
  float inset = fmaxf(menu->itemInsetPx, 0.0f);
  float innerW = fmaxf(menu->rect.width - inset * 2.0f, 1.0f);
  float itemW = innerW / (float)menu->itemCount;
  return rect_make(menu->rect.x + inset + itemW * (float)index,
                   menu->rect.y + inset,
                   itemW,
                   fmaxf(menu->rect.height - inset * 2.0f, 1.0f));
}

Rect melted_ui_menu_selection_rect(const MeltedUIMenu *menu) {
  if (!menu || menu->itemCount <= 0) {
    return rect_make(0.0f, 0.0f, 0.0f, 0.0f);
  }

  float inset = fmaxf(menu->selectorInsetPx, 0.0f);
  float innerW = fmaxf(menu->rect.width - inset * 2.0f, 1.0f);
  float itemW = innerW / (float)menu->itemCount;
  float pos = clampf(menu->selectedPosition, 0.0f, (float)(menu->itemCount - 1));
  return rect_make(menu->rect.x + inset + itemW * pos,
                   menu->rect.y + inset,
                   itemW,
                   fmaxf(menu->rect.height - inset * 2.0f, 1.0f));
}

void melted_ui_draw_menu_background(const MeltedUIMenu *menu,
                                    MeltedGlassRenderer *glass,
                                    const MeltedGlassConfig *cfg,
                                    GLuint sceneTexture,
                                    GLuint blurredTexture,
                                    int targetWidth,
                                    int targetHeight) {
  if (!menu || !glass || !cfg || targetWidth <= 0 || targetHeight <= 0) {
    return;
  }

  MeltedUIColor tint = menu->theme == MELTED_UI_MENU_DARK ? menu->darkBackgroundTint : menu->lightBackgroundTint;
  MeltedGlassConfig menuCfg = *cfg;
  menuCfg.drawAsRoundedRect = 1;
  menuCfg.clipWithSDF = 1;
  menuCfg.cornerRadiusPx = menu->cornerRadiusPx > 0.0f ? menu->cornerRadiusPx : menu->rect.height * 0.5f;
  menuCfg.antiAliasPx = fmaxf(menuCfg.antiAliasPx, menu->antiAliasPx);
  menuCfg.opacity = clampf(menu->backgroundOpacity * tint.a, 0.0f, 1.0f);
  menuCfg.whiteLift = menu->theme == MELTED_UI_MENU_DARK ? 0.02f : fmaxf(menuCfg.whiteLift, 0.10f);
  menuCfg.tintStrength = menu->theme == MELTED_UI_MENU_DARK ? 0.82f : 0.58f;
  menuCfg.shadowEnabled = 0;
  menuCfg.shadowAlpha = 0.0f;
  menuCfg.rimHighlightWidthPx = fmaxf(menuCfg.rimHighlightWidthPx, 0.85f);
  menuCfg.innerHighlightAlpha = fmaxf(menuCfg.innerHighlightAlpha, 0.16f);
  menuCfg.outerHighlightAlpha = fmaxf(menuCfg.outerHighlightAlpha, 0.08f);

  MeltedGlassPanel panel;
  panel.rect = menu->rect;
  panel.shapeMode = MELTED_GLASS_SHAPE_ROUNDED_RECT;
  panel.tintColor[0] = tint.r;
  panel.tintColor[1] = tint.g;
  panel.tintColor[2] = tint.b;
  panel.tintStrength = menuCfg.tintStrength;

  melted_glass_draw_panel(glass, &menuCfg, &panel, sceneTexture, blurredTexture, targetWidth, targetHeight);
}

void melted_ui_draw_menu_selection(const MeltedUIMenu *menu,
                                   MeltedGlassRenderer *glass,
                                   const MeltedGlassConfig *cfg,
                                   GLuint sceneTexture,
                                   GLuint blurredTexture,
                                   int targetWidth,
                                   int targetHeight) {
  if (!menu || !glass || !cfg || targetWidth <= 0 || targetHeight <= 0) {
    return;
  }

  Rect selector = melted_ui_menu_selection_rect(menu);
  if (selector.width <= 0.0f || selector.height <= 0.0f) {
    return;
  }

  MeltedUIColor tint = menu->theme == MELTED_UI_MENU_DARK ? menu->darkSelectorTint : menu->lightSelectorTint;
  MeltedGlassConfig selectorCfg = *cfg;
  selectorCfg.drawAsRoundedRect = 1;
  selectorCfg.clipWithSDF = 1;
  selectorCfg.cornerRadiusPx = selector.height * 0.5f;
  selectorCfg.antiAliasPx = fmaxf(selectorCfg.antiAliasPx, menu->antiAliasPx);
  selectorCfg.prismaticIdleOpacity = clampf(menu->selectorOpacity * tint.a, 0.0f, 1.0f);
  selectorCfg.prismaticClearOpacity = clampf(fmaxf(menu->selectorOpacity, 0.96f) * tint.a, 0.0f, 1.0f);
  selectorCfg.prismaticPressGrowPx = menu->selectorGrowPx;
  selectorCfg.prismaticPrismStrengthUV = fmaxf(selectorCfg.prismaticPrismStrengthUV, 0.014f);
  selectorCfg.refractionStrength = fmaxf(selectorCfg.refractionStrength, 3.0f);
  selectorCfg.refractionTransitionWidthPx = fmaxf(selector.height * 0.78f, 1.0f);
  selectorCfg.refractionTransitionSoftnessPx = fmaxf(selector.height * 0.22f, 1.0f);
  selectorCfg.opticalThicknessPx = fmaxf(selectorCfg.opticalThicknessPx, selector.height * 1.7f);
  selectorCfg.lensCurvature = fmaxf(selectorCfg.lensCurvature, 0.42f);
  selectorCfg.magnification = fmaxf(selectorCfg.magnification, 1.05f);
  selectorCfg.shadowEnabled = 0;
  selectorCfg.shadowAlpha = 0.0f;

  MeltedGlassPanel panel;
  panel.rect = selector;
  panel.shapeMode = MELTED_GLASS_SHAPE_ROUNDED_RECT;
  panel.tintColor[0] = tint.r;
  panel.tintColor[1] = tint.g;
  panel.tintColor[2] = tint.b;
  panel.tintStrength = 0.92f;

  MeltedGlassInteraction interaction;
  memset(&interaction, 0, sizeof(interaction));
  interaction.pressAmount = clampf(menu->pressAmount, 0.0f, 1.0f);
  interaction.velocityX = (menu->targetPosition - menu->selectedPosition) * fmaxf(selector.width, 1.0f) +
                          menu->dragVelocityX;

  melted_glass_draw_prismatic_panel(glass, &selectorCfg, &panel, &interaction,
                                    sceneTexture, blurredTexture, targetWidth, targetHeight);
}

void melted_ui_draw_menu(const MeltedUIMenu *menu,
                         MeltedGlassRenderer *glass,
                         const MeltedGlassConfig *cfg,
                         GLuint sceneTexture,
                         GLuint blurredTexture,
                         int targetWidth,
                         int targetHeight) {
  melted_ui_draw_menu_background(menu, glass, cfg, sceneTexture, blurredTexture, targetWidth, targetHeight);
  melted_ui_draw_menu_selection(menu, glass, cfg, sceneTexture, blurredTexture, targetWidth, targetHeight);
}
