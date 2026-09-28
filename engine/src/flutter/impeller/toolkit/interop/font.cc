// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "impeller/toolkit/interop/font.h"

#include <vector>

#include "flutter/third_party/skia/include/core/SkPath.h"

namespace impeller::interop {

Font::Font(const Typeface& typeface, float size)
    : font_(typeface.GetTypeface(), size) {
  font_.setEdging(SkFont::Edging::kAntiAlias);
  font_.setHinting(SkFontHinting::kSlight);
  font_.setSubpixel(true);
}

Font::~Font() = default;

const SkFont& Font::GetFont() const {
  return font_;
}

void Font::SetSkewX(float skew) {
  font_.setSkewX(skew);
}

void Font::SetEmbolden(bool embolden) {
  font_.setEmbolden(embolden);
}

void Font::SetSubpixel(bool subpixel) {
  font_.setSubpixel(subpixel);
}

ScopedObject<Path> Font::CreateGlyphPath(uint16_t glyph) const {
  auto path = font_.getPath(glyph);
  if (!path.has_value()) {
    return nullptr;
  }
  return Create<Path>(path.value());
}

void Font::GetGlyphBounds(const uint16_t* glyphs,
                          uint32_t count,
                          ImpellerRect* out_bounds) const {
  // Bounds of the outlines, not of hinted glyph images rounded to pixels.
  SkFont font = font_;
  font.setHinting(SkFontHinting::kNone);
  font.setLinearMetrics(true);
  std::vector<SkRect> bounds(count);
  font.getBounds({glyphs, count}, bounds, nullptr);
  for (uint32_t i = 0; i < count; i++) {
    out_bounds[i] = ImpellerRect{bounds[i].x(), bounds[i].y(),
                                 bounds[i].width(), bounds[i].height()};
  }
}

void Font::GetGlyphAdvances(const uint16_t* glyphs,
                            uint32_t count,
                            float* out_advances) const {
  font_.getWidths({glyphs, count}, {out_advances, count});
}

sk_sp<SkTextBlob> Font::MakeTextBlob(const uint16_t* glyphs,
                                     const ImpellerPoint* positions,
                                     uint32_t count) const {
  if (count == 0u) {
    return nullptr;
  }
  SkTextBlobBuilder builder;
  const auto& run = builder.allocRunPos(font_, static_cast<int>(count));
  for (uint32_t i = 0; i < count; i++) {
    run.glyphs[i] = glyphs[i];
    run.points()[i] = SkPoint::Make(positions[i].x, positions[i].y);
  }
  return builder.make();
}

}  // namespace impeller::interop
