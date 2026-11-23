#include "../src/util/RegionMath.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

using namespace rm;

static int g_failed = 0;

static void Expect(bool cond, const char* name)
{
    if (!cond) {
        std::printf("FAIL: %s\n", name);
        ++g_failed;
    } else {
        std::printf("PASS: %s\n", name);
    }
}

static bool Near(float a, float b, float eps = 0.01f)
{
    return std::fabs(a - b) <= eps;
}

int main()
{
    // Letterbox: wide content in tall viewport.
    {
        const FloatRect fitted = FitAspect({ 200.f, 200.f }, { 200.f, 100.f });
        Expect(Near(fitted.left, 0.f) && Near(fitted.right, 200.f), "letterbox horizontal fill");
        Expect(Near(fitted.top, 50.f) && Near(fitted.bottom, 150.f), "letterbox vertical bars");

        PointF content{};
        Expect(!ViewportToContent({ 10.f, 10.f }, fitted, { 200, 100 }, content), "ignore letterbox click");
        Expect(ViewportToContent({ 100.f, 100.f }, fitted, { 200, 100 }, content), "map center of video");
        Expect(Near(content.x, 100.f) && Near(content.y, 50.f), "center maps to content center");
    }

    // Pillarbox: tall content in wide viewport.
    {
        const FloatRect fitted = FitAspect({ 200.f, 100.f }, { 50.f, 100.f });
        Expect(Near(fitted.top, 0.f) && Near(fitted.bottom, 100.f), "pillarbox vertical fill");
        Expect(fitted.left > 0.f && fitted.right < 200.f, "pillarbox side bars");
    }

    // Reverse-direction drag.
    {
        FloatRect normalized{};
        Expect(NormalizeCropFromCorners({ 150.f, 120.f }, { 50.f, 40.f }, { 200, 200 }, normalized),
               "reverse drag accepted");
        Expect(Near(normalized.left, 0.25f) && Near(normalized.top, 0.2f) &&
                   Near(normalized.right, 0.75f) && Near(normalized.bottom, 0.6f),
               "reverse drag normalized");
    }

    // Empty selection rejected.
    {
        FloatRect normalized{};
        Expect(!NormalizeCropFromCorners({ 10.f, 10.f }, { 10.2f, 10.2f }, { 200, 200 }, normalized),
               "empty selection rejected");
    }

    // Clamping when corners leave content.
    {
        FloatRect normalized{};
        Expect(NormalizeCropFromCorners({ -20.f, -20.f }, { 500.f, 500.f }, { 100, 100 }, normalized),
               "out of bounds corners clamped");
        Expect(Near(normalized.left, 0.f) && Near(normalized.top, 0.f) &&
                   Near(normalized.right, 1.f) && Near(normalized.bottom, 1.f),
               "clamped to full content");
    }

    // Capture size change preserves normalized crop mapping.
    {
        const FloatRect normalized{ 0.25f, 0.25f, 0.75f, 0.75f };
        IntRect a{};
        IntRect b{};
        Expect(NormalizedToPixelRect(normalized, { 200, 100 }, a), "pixel rect @200x100");
        Expect(NormalizedToPixelRect(normalized, { 400, 200 }, b), "pixel rect @400x200");
        Expect(a.width() == 100 && a.height() == 50, "crop size @200x100");
        Expect(b.width() == 200 && b.height() == 100, "crop size scales with capture");
        Expect(a.left == 50 && b.left == 100, "left edge scales with capture");
    }

    // Clamp pointer to fitted video rectangle conceptually via ClampToContent.
    {
        const PointF clamped = ClampToContent({ -5.f, 999.f }, { 100, 50 });
        Expect(Near(clamped.x, 0.f) && Near(clamped.y, 50.f), "clamp to content");
    }

    if (g_failed != 0) {
        std::printf("%d test(s) failed\n", g_failed);
        return 1;
    }
    std::printf("All RegionMath tests passed\n");
    return 0;
}
