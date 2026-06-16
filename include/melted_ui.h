#ifndef MELTED_UI_H
#define MELTED_UI_H

#include "melted_glass.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  float r;
  float g;
  float b;
  float a;
} MeltedUIColor;

typedef struct {
  GLuint quadVao;
  GLuint roundedRectProgram;
} MeltedUIRenderer;

typedef struct {
  Rect rect;

  int isOn;
  int targetOn;
  int isPressed;
  int isPointerDown;
  int isDragging;
  int isAnimating;

  float value;
  float startValue;
  float targetValue;
  float animationTime;
  float animationDuration;
  float pressAmount;
  float pointerDownX;
  float pointerDownY;
  float dragOffsetX;

  float knobInsetPx;
  float knobWidthRatio;
  float pressGrowPx;
  float dragThresholdPx;
  float antiAliasPx;

  MeltedUIColor offTrackColor;
  MeltedUIColor onTrackColor;
  MeltedUIColor offTrackBorderColor;
  MeltedUIColor onTrackBorderColor;
  MeltedUIColor knobTintColor;
} MeltedUISwitch;

typedef struct {
  Rect rect;
  int shapeMode;

  int isPressed;
  int isPointerDown;

  float pressAmount;
  float pressGrowPx;
  float cornerRadiusPx;
  float antiAliasPx;
  float opacity;

  MeltedUIColor color;
  MeltedUIColor pressedColor;
} MeltedUIButton;

typedef enum {
  MELTED_UI_MENU_LIGHT = 0,
  MELTED_UI_MENU_DARK = 1,
} MeltedUIMenuTheme;

typedef enum {
  MELTED_UI_WIDGET_LIGHT = 0,
  MELTED_UI_WIDGET_DARK = 1,
} MeltedUIWidgetTheme;

typedef struct {
  Rect rect;
  int theme;
  int shapeMode;

  float cornerRadiusPx;
  float antiAliasPx;

  float lightOpacity;
  float darkOpacity;
  float lightTintStrength;
  float darkTintStrength;
  float lightWhiteLift;
  float darkWhiteLift;

  MeltedUIColor lightTint;
  MeltedUIColor darkTint;
} MeltedUIWidget;

#define MELTED_UI_MENU_MAX_ITEMS 8

typedef struct {
  Rect rect;

  int itemCount;
  int selectedIndex;
  int targetIndex;
  int theme;
  int isAnimating;
  int isPointerDown;
  int isDragging;

  float selectedPosition;
  float startPosition;
  float targetPosition;
  float animationTime;
  float animationDuration;
  float pressAmount;
  float pointerDownX;
  float pointerDownY;
  float dragOffsetX;
  float dragVelocityX;
  float dragThresholdPx;

  float cornerRadiusPx;
  float itemInsetPx;
  float selectorInsetPx;
  float selectorGrowPx;
  float antiAliasPx;
  float backgroundOpacity;
  float selectorOpacity;

  MeltedUIColor lightBackgroundTint;
  MeltedUIColor lightSelectorTint;
  MeltedUIColor darkBackgroundTint;
  MeltedUIColor darkSelectorTint;
} MeltedUIMenu;

int melted_ui_renderer_init(MeltedUIRenderer *renderer);
void melted_ui_renderer_destroy(MeltedUIRenderer *renderer);

void melted_ui_switch_defaults(MeltedUISwitch *control, float x, float y, float width, float height);
void melted_ui_switch_set_on(MeltedUISwitch *control, int isOn, int animated);
void melted_ui_switch_toggle(MeltedUISwitch *control, int animated);
int melted_ui_switch_hit_test(const MeltedUISwitch *control, float x, float y);
void melted_ui_switch_pointer_down(MeltedUISwitch *control);
void melted_ui_switch_pointer_up(MeltedUISwitch *control, int commitToggle);
void melted_ui_switch_pointer_down_at(MeltedUISwitch *control, float x, float y);
void melted_ui_switch_pointer_drag_to(MeltedUISwitch *control, float x, float y);
void melted_ui_switch_pointer_up_at(MeltedUISwitch *control, float x, float y, int commitToggle);
void melted_ui_switch_update(MeltedUISwitch *control, float dt);

Rect melted_ui_switch_knob_rect(const MeltedUISwitch *control);
MeltedGlassInteraction melted_ui_switch_interaction(const MeltedUISwitch *control);

void melted_ui_draw_switch_track(MeltedUIRenderer *ui,
                                 const MeltedUISwitch *control,
                                 int targetWidth,
                                 int targetHeight);
void melted_ui_draw_switch_knob(const MeltedUISwitch *control,
                                MeltedGlassRenderer *glass,
                                const MeltedGlassConfig *cfg,
                                GLuint sceneTexture,
                                GLuint blurredTexture,
                                int targetWidth,
                                int targetHeight);
void melted_ui_draw_switch(MeltedUIRenderer *ui,
                           const MeltedUISwitch *control,
                           MeltedGlassRenderer *glass,
                           const MeltedGlassConfig *cfg,
                           GLuint sceneTexture,
                           GLuint blurredTexture,
                           int targetWidth,
                           int targetHeight);

void melted_ui_button_defaults(MeltedUIButton *button, float x, float y, float width, float height);
int melted_ui_button_hit_test(const MeltedUIButton *button, float x, float y);
void melted_ui_button_pointer_down(MeltedUIButton *button);
int melted_ui_button_pointer_up(MeltedUIButton *button, int commitClick);
void melted_ui_button_update(MeltedUIButton *button, float dt);
Rect melted_ui_button_current_rect(const MeltedUIButton *button);
void melted_ui_draw_button(const MeltedUIButton *button,
                           MeltedGlassRenderer *glass,
                           const MeltedGlassConfig *cfg,
                           GLuint sceneTexture,
                           GLuint blurredTexture,
                           int targetWidth,
                           int targetHeight);

void melted_ui_widget_defaults(MeltedUIWidget *widget,
                               float x,
                               float y,
                               float width,
                               float height,
                               int theme);
void melted_ui_widget_set_theme(MeltedUIWidget *widget, int theme);
int melted_ui_widget_hit_test(const MeltedUIWidget *widget, float x, float y);
void melted_ui_draw_widget(const MeltedUIWidget *widget,
                           MeltedGlassRenderer *glass,
                           const MeltedGlassConfig *cfg,
                           GLuint sceneTexture,
                           GLuint blurredTexture,
                           int targetWidth,
                           int targetHeight);

void melted_ui_menu_defaults(MeltedUIMenu *menu,
                             float x,
                             float y,
                             float width,
                             float height,
                             int itemCount,
                             int theme);
void melted_ui_menu_set_theme(MeltedUIMenu *menu, int theme);
void melted_ui_menu_set_item_count(MeltedUIMenu *menu, int itemCount);
void melted_ui_menu_set_selected(MeltedUIMenu *menu, int selectedIndex, int animated);
int melted_ui_menu_hit_test(const MeltedUIMenu *menu, float x, float y);
int melted_ui_menu_item_at(const MeltedUIMenu *menu, float x, float y);
void melted_ui_menu_pointer_down_at(MeltedUIMenu *menu, float x, float y);
void melted_ui_menu_pointer_drag_to(MeltedUIMenu *menu, float x, float y);
int melted_ui_menu_pointer_up_at(MeltedUIMenu *menu, float x, float y);
void melted_ui_menu_update(MeltedUIMenu *menu, float dt);
Rect melted_ui_menu_item_rect(const MeltedUIMenu *menu, int itemIndex);
Rect melted_ui_menu_selection_rect(const MeltedUIMenu *menu);
void melted_ui_draw_menu_background(const MeltedUIMenu *menu,
                                    MeltedGlassRenderer *glass,
                                    const MeltedGlassConfig *cfg,
                                    GLuint sceneTexture,
                                    GLuint blurredTexture,
                                    int targetWidth,
                                    int targetHeight);
void melted_ui_draw_menu_selection(const MeltedUIMenu *menu,
                                   MeltedGlassRenderer *glass,
                                   const MeltedGlassConfig *cfg,
                                   GLuint sceneTexture,
                                   GLuint blurredTexture,
                                   int targetWidth,
                                   int targetHeight);
void melted_ui_draw_menu(const MeltedUIMenu *menu,
                         MeltedGlassRenderer *glass,
                         const MeltedGlassConfig *cfg,
                         GLuint sceneTexture,
                         GLuint blurredTexture,
                         int targetWidth,
                         int targetHeight);

#ifdef __cplusplus
}
#endif

#endif
