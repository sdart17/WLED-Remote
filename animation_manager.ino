// ─────────────────────────────────────────────────────────────
// Animation Manager - PHASE 4: Smooth UI Transitions and Professional Animations
// Provides fluid page transitions, smooth animations, and professional visual effects
// ─────────────────────────────────────────────────────────────

#if ENABLE_SMOOTH_ANIMATIONS

// ───── Animation Configuration ─────
#define MAX_ANIMATIONS 4                    // Maximum concurrent animations
#define ANIMATION_FRAMERATE 60              // Target 60 FPS for smooth animations
#define PAGE_TRANSITION_DURATION_MS 250     // Page transition duration
#define BOUNCE_ANIMATION_DURATION_MS 150    // Button press bounce duration
#define FADE_ANIMATION_DURATION_MS 300      // Fade in/out duration

// ───── Animation Types ─────
enum AnimationType {
  ANIM_PAGE_SLIDE = 1,
  ANIM_BUTTON_BOUNCE = 2,
  ANIM_FADE_IN = 3,
  ANIM_FADE_OUT = 4,
  ANIM_BRIGHTNESS_BAR = 5,
  ANIM_ICON_SPIN = 6
};

enum EasingFunction {
  EASE_LINEAR = 0,
  EASE_OUT_CUBIC = 1,
  EASE_OUT_BOUNCE = 2,
  EASE_IN_OUT_QUAD = 3,
  EASE_OUT_ELASTIC = 4
};

// ───── Animation Data Structure ─────
struct Animation {
  AnimationType type;
  EasingFunction easing;
  uint32_t startTime;
  uint32_t duration;
  int16_t startValue;
  int16_t endValue;
  int16_t currentValue;
  void* target;         // Target object (page, button, etc.)
  bool active;
  bool (*updateCallback)(struct Animation* anim);
  void (*completeCallback)(struct Animation* anim);
};

// ───── Page Transition State ─────
struct PageTransition {
  bool active;
  int sourcePage;
  int targetPage;
  int direction;        // -1 = left, +1 = right
  int16_t offset;
  uint16_t* sourceBuffer;
  uint16_t* targetBuffer;
};

// ───── Global Animation State ─────
static Animation AnimationManager_animations[MAX_ANIMATIONS];
static uint8_t AnimationManager_activeCount = 0;
static PageTransition AnimationManager_pageTransition = {0};
static bool AnimationManager_initialized = false;
static uint32_t AnimationManager_lastFrameTime = 0;

// Forward declarations
bool DisplayManager_isReady();
void DisplayManager_startWrite();
void DisplayManager_endWrite();
void DisplayManager_setWindow(int16_t x, int16_t y, int16_t w, int16_t h);
void DisplayManager_pushColors(uint16_t* colors, uint32_t len);
void DisplayManager_capturePageBuffer(int page, uint16_t* buffer);
void UIManager_renderPage(int page);
int UIManager_getCurrentPage();
void FreqManager_notifyDisplayActivity();

// ───── Easing Functions ─────
float AnimationManager_applyEasing(float progress, EasingFunction easing) {
  switch (easing) {
    case EASE_LINEAR:
      return progress;
      
    case EASE_OUT_CUBIC:
      return 1.0f - pow(1.0f - progress, 3.0f);
      
    case EASE_OUT_BOUNCE:
      if (progress < 1.0f / 2.75f) {
        return 7.5625f * progress * progress;
      } else if (progress < 2.0f / 2.75f) {
        progress -= 1.5f / 2.75f;
        return 7.5625f * progress * progress + 0.75f;
      } else if (progress < 2.5f / 2.75f) {
        progress -= 2.25f / 2.75f;
        return 7.5625f * progress * progress + 0.9375f;
      } else {
        progress -= 2.625f / 2.75f;
        return 7.5625f * progress * progress + 0.984375f;
      }
      
    case EASE_IN_OUT_QUAD:
      if (progress < 0.5f) {
        return 2.0f * progress * progress;
      } else {
        return -1.0f + (4.0f - 2.0f * progress) * progress;
      }
      
    case EASE_OUT_ELASTIC:
      if (progress == 0.0f) return 0.0f;
      if (progress == 1.0f) return 1.0f;
      return pow(2.0f, -10.0f * progress) * sin((progress * 10.0f - 0.75f) * (2.0f * PI / 3.0f)) + 1.0f;
      
    default:
      return progress;
  }
}

// ───── Animation Management ─────
uint8_t AnimationManager_createAnimation(AnimationType type, EasingFunction easing,
                                       uint32_t duration, int16_t startValue, int16_t endValue,
                                       void* target, bool (*updateCallback)(struct Animation*)) {
  if (!AnimationManager_initialized) {
    Serial.println("[ANIM] ERROR: Animation manager not initialized");
    return 255;
  }
  
  if (AnimationManager_activeCount >= MAX_ANIMATIONS) {
    Serial.println("[ANIM] Max animations reached");
    return 255; // Invalid index
  }
  
  if (duration == 0) {
    Serial.println("[ANIM] ERROR: Animation duration cannot be zero");
    return 255;
  }
  
  uint8_t index = 255;
  for (uint8_t i = 0; i < MAX_ANIMATIONS; i++) {
    if (!AnimationManager_animations[i].active) {
      index = i;
      break;
    }
  }
  
  if (index == 255) {
    Serial.println("[ANIM] No available animation slots");
    return 255;
  }
  
  Animation* anim = &AnimationManager_animations[index];
  if (!anim) {
    Serial.println("[ANIM] ERROR: Animation slot is null");
    return 255;
  }
  
  anim->type = type;
  anim->easing = easing;
  anim->startTime = millis();
  anim->duration = duration;
  anim->startValue = startValue;
  anim->endValue = endValue;
  anim->currentValue = startValue;
  anim->target = target;
  anim->active = true;
  anim->updateCallback = updateCallback;
  anim->completeCallback = nullptr;
  
  AnimationManager_activeCount++;
  
  Serial.printf("[ANIM] Created animation %d: type=%d, duration=%dms\n", index, type, duration);
  return index;
}

void AnimationManager_stopAnimation(uint8_t animIndex) {
  if (animIndex >= MAX_ANIMATIONS) return;
  
  Animation* anim = &AnimationManager_animations[animIndex];
  if (anim->active) {
    if (anim->completeCallback) {
      anim->completeCallback(anim);
    }
    anim->active = false;
    AnimationManager_activeCount--;
  }
}

// ───── Page Transition Implementation ─────
bool AnimationManager_pageSlideUpdate(struct Animation* anim) {
  if (!anim) {
    Serial.println("[ANIM] ERROR: pageSlideUpdate called with null animation");
    return false;
  }
  
  PageTransition* transition = &AnimationManager_pageTransition;
  
  if (!transition->active || !DisplayManager_isReady()) {
    return false; // Transition stopped or display busy
  }
  
  transition->offset = anim->currentValue;
  
  // Render both pages with offset
  DisplayManager_startWrite();
  
  // Render source page (sliding out)
  if (transition->sourceBuffer) {
    int16_t sourceX = transition->direction * anim->currentValue;
    if (sourceX > -SCREEN_WIDTH && sourceX < SCREEN_WIDTH) {
      DisplayManager_setWindow(max(0, sourceX), 0, 
                              min(SCREEN_WIDTH, SCREEN_WIDTH + sourceX), SCREEN_HEIGHT);
      
      uint16_t* bufferStart = transition->sourceBuffer;
      if (sourceX < 0) {
        int32_t offset = abs(sourceX);
        if (offset >= SCREEN_WIDTH * SCREEN_HEIGHT) {
          Serial.println("[ANIM] ERROR: Buffer offset out of bounds");
          DisplayManager_endWrite();
          return false;
        }
        bufferStart += offset;
      }
      
      uint32_t pixelCount = min(SCREEN_WIDTH, SCREEN_WIDTH - abs(sourceX)) * SCREEN_HEIGHT;
      if (pixelCount > 0 && bufferStart) {
        DisplayManager_pushColors(bufferStart, pixelCount);
      }
    }
  }
  
  // Render target page (sliding in)
  if (transition->targetBuffer) {
    int16_t targetX = transition->direction * (anim->currentValue - SCREEN_WIDTH);
    if (targetX > -SCREEN_WIDTH && targetX < SCREEN_WIDTH) {
      DisplayManager_setWindow(max(0, targetX), 0,
                              min(SCREEN_WIDTH, SCREEN_WIDTH + targetX), SCREEN_HEIGHT);
      
      uint16_t* bufferStart = transition->targetBuffer;
      if (targetX < 0) {
        int32_t offset = abs(targetX);
        if (offset >= SCREEN_WIDTH * SCREEN_HEIGHT) {
          Serial.println("[ANIM] ERROR: Buffer offset out of bounds");
          DisplayManager_endWrite();
          return false;
        }
        bufferStart += offset;
      }
      
      uint32_t pixelCount = min(SCREEN_WIDTH, SCREEN_WIDTH - abs(targetX)) * SCREEN_HEIGHT;
      if (pixelCount > 0 && bufferStart) {
        DisplayManager_pushColors(bufferStart, pixelCount);
      }
    }
  }
  
  DisplayManager_endWrite();
  FreqManager_notifyDisplayActivity();
  
  return true;
}

void AnimationManager_pageTransitionComplete(struct Animation* anim) {
  Serial.println("[ANIM] Page transition complete");
  
  PageTransition* transition = &AnimationManager_pageTransition;
  transition->active = false;
  
  // Free transition buffers
  if (transition->sourceBuffer) {
    free(transition->sourceBuffer);
    transition->sourceBuffer = nullptr;
  }
  if (transition->targetBuffer) {
    free(transition->targetBuffer);
    transition->targetBuffer = nullptr;
  }
  
  // Force full page redraw
  UIManager_renderPage(transition->targetPage);
}

bool AnimationManager_startPageTransition(int sourcePage, int targetPage, int direction) {
  if (AnimationManager_pageTransition.active) {
    Serial.println("[ANIM] Page transition already active");
    return false;
  }
  
  Serial.printf("[ANIM] Starting page transition: %d -> %d (dir=%d)\n", 
                sourcePage, targetPage, direction);
  
  // Allocate transition buffers
  size_t bufferSize = SCREEN_WIDTH * SCREEN_HEIGHT * 2; // 16-bit color
  uint16_t* sourceBuffer = (uint16_t*)ps_malloc(bufferSize);
  uint16_t* targetBuffer = (uint16_t*)ps_malloc(bufferSize);
  
  if (!sourceBuffer || !targetBuffer) {
    Serial.println("[ANIM] Failed to allocate transition buffers");
    if (sourceBuffer) free(sourceBuffer);
    if (targetBuffer) free(targetBuffer);
    return false;
  }
  
  // Capture current page to source buffer
  DisplayManager_capturePageBuffer(sourcePage, sourceBuffer);
  
  // Pre-render target page to buffer
  UIManager_renderPage(targetPage);
  DisplayManager_capturePageBuffer(targetPage, targetBuffer);
  
  // Setup transition state
  PageTransition* transition = &AnimationManager_pageTransition;
  transition->active = true;
  transition->sourcePage = sourcePage;
  transition->targetPage = targetPage;
  transition->direction = direction;
  transition->offset = 0;
  transition->sourceBuffer = sourceBuffer;
  transition->targetBuffer = targetBuffer;
  
  // Create slide animation
  AnimationManager_createAnimation(ANIM_PAGE_SLIDE, EASE_OUT_CUBIC,
                                  PAGE_TRANSITION_DURATION_MS, 0, SCREEN_WIDTH,
                                  transition, AnimationManager_pageSlideUpdate);
  
  return true;
}

// ───── Button Bounce Animation ─────
bool AnimationManager_buttonBounceUpdate(struct Animation* anim) {
  // Scale factor for button (1.0 = normal, 1.1 = 10% larger, 0.9 = 10% smaller)
  float scale = 1.0f + (anim->currentValue / 1000.0f);
  
  // Update button visual state (implementation depends on button system)
  // This would integrate with the touch/button system to apply scale
  
  return true;
}

uint8_t AnimationManager_startButtonBounce(void* button) {
  return AnimationManager_createAnimation(ANIM_BUTTON_BOUNCE, EASE_OUT_BOUNCE,
                                         BOUNCE_ANIMATION_DURATION_MS, 100, 0,
                                         button, AnimationManager_buttonBounceUpdate);
}

// ───── Brightness Bar Animation ─────
bool AnimationManager_brightnessBarUpdate(struct Animation* anim) {
  // Smooth brightness bar updates (implementation depends on UI system)
  int brightness = anim->currentValue;
  
  // This would update the brightness bar visual smoothly
  // Integration point with UIManager brightness display
  
  return true;
}

uint8_t AnimationManager_startBrightnessAnimation(int startBrightness, int endBrightness) {
  return AnimationManager_createAnimation(ANIM_BRIGHTNESS_BAR, EASE_OUT_QUAD,
                                         200, startBrightness, endBrightness,
                                         nullptr, AnimationManager_brightnessBarUpdate);
}

// ───── Core Animation System ─────
void AnimationManager_init() {
  if (AnimationManager_initialized) return;
  
  Serial.println("[ANIM] Initializing animation manager...");
  
  // Initialize animation slots
  for (uint8_t i = 0; i < MAX_ANIMATIONS; i++) {
    AnimationManager_animations[i].active = false;
  }
  
  AnimationManager_activeCount = 0;
  AnimationManager_lastFrameTime = millis();
  AnimationManager_initialized = true;
  
  Serial.printf("[ANIM] Initialized with %d animation slots\n", MAX_ANIMATIONS);
}

void AnimationManager_update() {
  if (!ENABLE_SMOOTH_ANIMATIONS || !AnimationManager_initialized) return;
  
  uint32_t now = millis();
  
  // Maintain consistent frame rate
  if (now - AnimationManager_lastFrameTime < (1000 / ANIMATION_FRAMERATE)) {
    return; // Too early for next frame
  }
  
  bool anyActive = false;
  
  for (uint8_t i = 0; i < MAX_ANIMATIONS; i++) {
    Animation* anim = &AnimationManager_animations[i];
    
    if (!anim->active) continue;
    
    anyActive = true;
    uint32_t elapsed = now - anim->startTime;
    
    if (elapsed >= anim->duration) {
      // Animation complete
      anim->currentValue = anim->endValue;
      
      if (anim->updateCallback) {
        if (!anim->updateCallback(anim)) {
          Serial.printf("[ANIM] Animation callback %d requested stop\n", i);
          AnimationManager_stopAnimation(i);
          continue;
        }
      }
      
      AnimationManager_stopAnimation(i);
    } else {
      // Update animation
      float progress = (float)elapsed / (float)anim->duration;
      float easedProgress = AnimationManager_applyEasing(progress, anim->easing);
      
      anim->currentValue = anim->startValue + 
                          (int16_t)((anim->endValue - anim->startValue) * easedProgress);
      
      if (anim->updateCallback) {
        if (!anim->updateCallback(anim)) {
          // Callback requested stop
          AnimationManager_stopAnimation(i);
        }
      }
    }
  }
  
  if (anyActive) {
    AnimationManager_lastFrameTime = now;
    FreqManager_notifyDisplayActivity(); // Keep high performance during animations
  }
}

// ───── Public API Functions ─────
bool AnimationManager_isTransitionActive() {
  return ENABLE_SMOOTH_ANIMATIONS && AnimationManager_pageTransition.active;
}

bool AnimationManager_isAnyAnimationActive() {
  return ENABLE_SMOOTH_ANIMATIONS && AnimationManager_activeCount > 0;
}

uint8_t AnimationManager_getActiveCount() {
  return AnimationManager_activeCount;
}

void AnimationManager_stopAllAnimations() {
  for (uint8_t i = 0; i < MAX_ANIMATIONS; i++) {
    if (AnimationManager_animations[i].active) {
      AnimationManager_stopAnimation(i);
    }
  }
}

// ───── Statistics and Debugging ─────
void AnimationManager_printStats() {
  Serial.printf("[ANIM] Stats: Active=%d/%d, PageTransition=%s\n",
                AnimationManager_activeCount, MAX_ANIMATIONS,
                AnimationManager_pageTransition.active ? "Yes" : "No");
  
  if (AnimationManager_pageTransition.active) {
    Serial.printf("[ANIM] Transition: %d->%d, offset=%d\n",
                  AnimationManager_pageTransition.sourcePage,
                  AnimationManager_pageTransition.targetPage,
                  AnimationManager_pageTransition.offset);
  }
}

#else // !ENABLE_SMOOTH_ANIMATIONS

// ───── Stub Functions When Animations are Disabled ─────
void AnimationManager_init() {}
void AnimationManager_update() {}
bool AnimationManager_startPageTransition(int sourcePage, int targetPage, int direction) { return false; }
uint8_t AnimationManager_startButtonBounce(void* button) { return 255; }
uint8_t AnimationManager_startBrightnessAnimation(int startBrightness, int endBrightness) { return 255; }
bool AnimationManager_isTransitionActive() { return false; }
bool AnimationManager_isAnyAnimationActive() { return false; }
uint8_t AnimationManager_getActiveCount() { return 0; }
void AnimationManager_stopAllAnimations() {}
void AnimationManager_printStats() { Serial.println("[ANIM] Animations disabled"); }

#endif // ENABLE_SMOOTH_ANIMATIONS