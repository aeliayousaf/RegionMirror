#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace rm {

struct FloatRect {
    float left = 0.f;
    float top = 0.f;
    float right = 0.f;
    float bottom = 0.f;

    float width() const { return right - left; }
    float height() const { return bottom - top; }
    bool empty() const { return width() <= 0.f || height() <= 0.f; }
};

struct IntRect {
    int left = 0;
    int top = 0;
    int right = 0;
    int bottom = 0;

    int width() const { return right - left; }
    int height() const { return bottom - top; }
    bool empty() const { return width() <= 0 || height() <= 0; }
};

struct SizeF {
    float width = 0.f;
    float height = 0.f;
};

struct SizeI {
    int width = 0;
    int height = 0;
};

struct PointF {
    float x = 0.f;
    float y = 0.f;
};

// Axis-aligned rectangle that fits content into a viewport while preserving aspect ratio.
FloatRect FitAspect(SizeF viewport, SizeF content);

// Map a point in viewport client space into content pixel space using the fitted video rect.
// Returns false if the point is outside the fitted video rectangle (letterbox/pillarbox).
bool ViewportToContent(PointF viewportPoint, FloatRect fitted, SizeI contentSize, PointF& outContent);

// Map content pixel space into viewport client space using the fitted video rect.
PointF ContentToViewport(PointF contentPoint, FloatRect fitted, SizeI contentSize);

// Map a normalized content crop into a viewport rectangle.
FloatRect NormalizedToViewportRect(FloatRect normalized, FloatRect fitted, SizeI contentSize);

// Clamp a content-space point into [0, contentSize].
PointF ClampToContent(PointF p, SizeI contentSize);

enum class SelectionHit {
    None = 0,
    Move,
    Left,
    Right,
    Top,
    Bottom,
    TopLeft,
    TopRight,
    BottomLeft,
    BottomRight,
};

// Hit-test a selection rectangle in viewport space. handlePx is the grab thickness.
SelectionHit HitTestSelection(
    PointF viewportPoint,
    FloatRect normalizedSelection,
    FloatRect fitted,
    SizeI contentSize,
    float handlePx);

// Apply a move/resize edit. startCrop/startContent are from mouse-down; currentContent is now.
// Returns false if the result would be empty.
bool ApplySelectionEdit(
    SelectionHit hit,
    FloatRect startCrop,
    PointF startContent,
    PointF currentContent,
    SizeI contentSize,
    FloatRect& outNormalized);

// Build a normalized crop (0..1 relative to content) from two content-space corners.
// Handles reverse-direction drags. Returns false if the resulting rect is empty after clamping.
bool NormalizeCropFromCorners(PointF a, PointF b, SizeI contentSize, FloatRect& outNormalized);

// Convert a normalized crop into a clamped, nonempty pixel rectangle for a given content size.
// Returns false if content size is invalid or the result would be empty.
bool NormalizedToPixelRect(FloatRect normalized, SizeI contentSize, IntRect& outPixels);

// Convert pixel crop to UVs relative to the full texture (content may be a sub-rectangle of the texture).
FloatRect PixelCropToTextureUV(IntRect cropPixels, SizeI contentSize, SizeI textureSize);

} // namespace rm
