#include "RegionMath.h"

namespace rm {

FloatRect FitAspect(SizeF viewport, SizeF content)
{
    FloatRect out{};
    if (viewport.width <= 0.f || viewport.height <= 0.f || content.width <= 0.f || content.height <= 0.f) {
        return out;
    }

    const float viewportAspect = viewport.width / viewport.height;
    const float contentAspect = content.width / content.height;

    if (contentAspect > viewportAspect) {
        // Letterbox (bars top/bottom)
        out.left = 0.f;
        out.right = viewport.width;
        const float h = viewport.width / contentAspect;
        out.top = (viewport.height - h) * 0.5f;
        out.bottom = out.top + h;
    } else {
        // Pillarbox (bars left/right)
        out.top = 0.f;
        out.bottom = viewport.height;
        const float w = viewport.height * contentAspect;
        out.left = (viewport.width - w) * 0.5f;
        out.right = out.left + w;
    }
    return out;
}

bool ViewportToContent(PointF viewportPoint, FloatRect fitted, SizeI contentSize, PointF& outContent)
{
    if (fitted.empty() || contentSize.width <= 0 || contentSize.height <= 0) {
        return false;
    }
    if (viewportPoint.x < fitted.left || viewportPoint.x > fitted.right ||
        viewportPoint.y < fitted.top || viewportPoint.y > fitted.bottom) {
        return false;
    }

    const float u = (viewportPoint.x - fitted.left) / fitted.width();
    const float v = (viewportPoint.y - fitted.top) / fitted.height();
    outContent.x = u * static_cast<float>(contentSize.width);
    outContent.y = v * static_cast<float>(contentSize.height);
    return true;
}

PointF ContentToViewport(PointF contentPoint, FloatRect fitted, SizeI contentSize)
{
    PointF out{};
    if (fitted.empty() || contentSize.width <= 0 || contentSize.height <= 0) {
        return out;
    }
    const float u = contentPoint.x / static_cast<float>(contentSize.width);
    const float v = contentPoint.y / static_cast<float>(contentSize.height);
    out.x = fitted.left + u * fitted.width();
    out.y = fitted.top + v * fitted.height();
    return out;
}

FloatRect NormalizedToViewportRect(FloatRect normalized, FloatRect fitted, SizeI contentSize)
{
    FloatRect out{};
    if (fitted.empty() || contentSize.width <= 0 || contentSize.height <= 0 || normalized.empty()) {
        return out;
    }
    const PointF tl = ContentToViewport(
        { normalized.left * static_cast<float>(contentSize.width),
          normalized.top * static_cast<float>(contentSize.height) },
        fitted,
        contentSize);
    const PointF br = ContentToViewport(
        { normalized.right * static_cast<float>(contentSize.width),
          normalized.bottom * static_cast<float>(contentSize.height) },
        fitted,
        contentSize);
    out.left = tl.x;
    out.top = tl.y;
    out.right = br.x;
    out.bottom = br.y;
    return out;
}

SelectionHit HitTestSelection(
    PointF viewportPoint,
    FloatRect normalizedSelection,
    FloatRect fitted,
    SizeI contentSize,
    float handlePx)
{
    const FloatRect box = NormalizedToViewportRect(normalizedSelection, fitted, contentSize);
    if (box.empty()) {
        return SelectionHit::None;
    }

    const float hx = std::max(6.f, handlePx);
    const float left = box.left;
    const float right = box.right;
    const float top = box.top;
    const float bottom = box.bottom;

    const bool nearL = std::abs(viewportPoint.x - left) <= hx;
    const bool nearR = std::abs(viewportPoint.x - right) <= hx;
    const bool nearT = std::abs(viewportPoint.y - top) <= hx;
    const bool nearB = std::abs(viewportPoint.y - bottom) <= hx;
    const bool inX = viewportPoint.x >= left - hx && viewportPoint.x <= right + hx;
    const bool inY = viewportPoint.y >= top - hx && viewportPoint.y <= bottom + hx;

    if (nearT && nearL) {
        return SelectionHit::TopLeft;
    }
    if (nearT && nearR) {
        return SelectionHit::TopRight;
    }
    if (nearB && nearL) {
        return SelectionHit::BottomLeft;
    }
    if (nearB && nearR) {
        return SelectionHit::BottomRight;
    }
    if (nearT && inX) {
        return SelectionHit::Top;
    }
    if (nearB && inX) {
        return SelectionHit::Bottom;
    }
    if (nearL && inY) {
        return SelectionHit::Left;
    }
    if (nearR && inY) {
        return SelectionHit::Right;
    }
    if (viewportPoint.x >= left && viewportPoint.x <= right &&
        viewportPoint.y >= top && viewportPoint.y <= bottom) {
        return SelectionHit::Move;
    }
    return SelectionHit::None;
}

bool ApplySelectionEdit(
    SelectionHit hit,
    FloatRect startCrop,
    PointF startContent,
    PointF currentContent,
    SizeI contentSize,
    FloatRect& outNormalized)
{
    if (hit == SelectionHit::None || contentSize.width <= 0 || contentSize.height <= 0 || startCrop.empty()) {
        return false;
    }

    const float w = static_cast<float>(contentSize.width);
    const float h = static_cast<float>(contentSize.height);
    const float minSize = 1.f;

    float left = startCrop.left * w;
    float top = startCrop.top * h;
    float right = startCrop.right * w;
    float bottom = startCrop.bottom * h;

    currentContent = ClampToContent(currentContent, contentSize);
    startContent = ClampToContent(startContent, contentSize);
    const float dx = currentContent.x - startContent.x;
    const float dy = currentContent.y - startContent.y;

    switch (hit) {
    case SelectionHit::Move: {
        const float width = right - left;
        const float height = bottom - top;
        left = std::clamp(left + dx, 0.f, w - width);
        top = std::clamp(top + dy, 0.f, h - height);
        right = left + width;
        bottom = top + height;
        break;
    }
    case SelectionHit::Left:
        left = std::clamp(left + dx, 0.f, right - minSize);
        break;
    case SelectionHit::Right:
        right = std::clamp(right + dx, left + minSize, w);
        break;
    case SelectionHit::Top:
        top = std::clamp(top + dy, 0.f, bottom - minSize);
        break;
    case SelectionHit::Bottom:
        bottom = std::clamp(bottom + dy, top + minSize, h);
        break;
    case SelectionHit::TopLeft:
        left = std::clamp(left + dx, 0.f, right - minSize);
        top = std::clamp(top + dy, 0.f, bottom - minSize);
        break;
    case SelectionHit::TopRight:
        right = std::clamp(right + dx, left + minSize, w);
        top = std::clamp(top + dy, 0.f, bottom - minSize);
        break;
    case SelectionHit::BottomLeft:
        left = std::clamp(left + dx, 0.f, right - minSize);
        bottom = std::clamp(bottom + dy, top + minSize, h);
        break;
    case SelectionHit::BottomRight:
        right = std::clamp(right + dx, left + minSize, w);
        bottom = std::clamp(bottom + dy, top + minSize, h);
        break;
    default:
        return false;
    }

    if ((right - left) < minSize || (bottom - top) < minSize) {
        return false;
    }

    outNormalized.left = left / w;
    outNormalized.top = top / h;
    outNormalized.right = right / w;
    outNormalized.bottom = bottom / h;
    return !outNormalized.empty();
}

PointF ClampToContent(PointF p, SizeI contentSize)
{
    PointF out = p;
    out.x = std::clamp(out.x, 0.f, static_cast<float>(std::max(0, contentSize.width)));
    out.y = std::clamp(out.y, 0.f, static_cast<float>(std::max(0, contentSize.height)));
    return out;
}

bool NormalizeCropFromCorners(PointF a, PointF b, SizeI contentSize, FloatRect& outNormalized)
{
    if (contentSize.width <= 0 || contentSize.height <= 0) {
        return false;
    }

    a = ClampToContent(a, contentSize);
    b = ClampToContent(b, contentSize);

    const float left = std::min(a.x, b.x);
    const float right = std::max(a.x, b.x);
    const float top = std::min(a.y, b.y);
    const float bottom = std::max(a.y, b.y);

    // Reject empty or near-empty selections (< 1 source pixel on either axis).
    if ((right - left) < 1.f || (bottom - top) < 1.f) {
        return false;
    }

    const float w = static_cast<float>(contentSize.width);
    const float h = static_cast<float>(contentSize.height);
    outNormalized.left = left / w;
    outNormalized.top = top / h;
    outNormalized.right = right / w;
    outNormalized.bottom = bottom / h;
    return true;
}

bool NormalizedToPixelRect(FloatRect normalized, SizeI contentSize, IntRect& outPixels)
{
    if (contentSize.width <= 0 || contentSize.height <= 0) {
        return false;
    }
    if (normalized.empty()) {
        return false;
    }

    const float w = static_cast<float>(contentSize.width);
    const float h = static_cast<float>(contentSize.height);

    float l = std::clamp(normalized.left, 0.f, 1.f) * w;
    float t = std::clamp(normalized.top, 0.f, 1.f) * h;
    float r = std::clamp(normalized.right, 0.f, 1.f) * w;
    float b = std::clamp(normalized.bottom, 0.f, 1.f) * h;

    if (r < l) {
        std::swap(l, r);
    }
    if (b < t) {
        std::swap(t, b);
    }

    int left = static_cast<int>(std::floor(l));
    int top = static_cast<int>(std::floor(t));
    int right = static_cast<int>(std::ceil(r));
    int bottom = static_cast<int>(std::ceil(b));

    left = std::clamp(left, 0, contentSize.width);
    top = std::clamp(top, 0, contentSize.height);
    right = std::clamp(right, 0, contentSize.width);
    bottom = std::clamp(bottom, 0, contentSize.height);

    if (right <= left || bottom <= top) {
        // Expand a degenerate clamp to at least 1x1 when the normalized rect was nonempty.
        if (left >= contentSize.width) {
            left = contentSize.width - 1;
        }
        if (top >= contentSize.height) {
            top = contentSize.height - 1;
        }
        right = std::min(contentSize.width, left + 1);
        bottom = std::min(contentSize.height, top + 1);
    }

    if (right <= left || bottom <= top) {
        return false;
    }

    outPixels = { left, top, right, bottom };
    return true;
}

FloatRect PixelCropToTextureUV(IntRect cropPixels, SizeI contentSize, SizeI textureSize)
{
    FloatRect uv{};
    if (textureSize.width <= 0 || textureSize.height <= 0 || contentSize.width <= 0 || contentSize.height <= 0) {
        return uv;
    }

    const float sx = static_cast<float>(contentSize.width) / static_cast<float>(textureSize.width);
    const float sy = static_cast<float>(contentSize.height) / static_cast<float>(textureSize.height);

    uv.left = (static_cast<float>(cropPixels.left) / static_cast<float>(contentSize.width)) * sx;
    uv.top = (static_cast<float>(cropPixels.top) / static_cast<float>(contentSize.height)) * sy;
    uv.right = (static_cast<float>(cropPixels.right) / static_cast<float>(contentSize.width)) * sx;
    uv.bottom = (static_cast<float>(cropPixels.bottom) / static_cast<float>(contentSize.height)) * sy;
    return uv;
}

} // namespace rm
