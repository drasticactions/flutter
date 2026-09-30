// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "impeller/toolkit/interop/glyph_run.h"

#include "flutter/third_party/skia/include/core/SkPathBuilder.h"
#include "impeller/display_list/dl_text_impeller.h"

namespace impeller::interop {

GlyphRun::GlyphRun(const Font& font,
                   const uint16_t* glyphs,
                   const ImpellerPoint* positions,
                   uint32_t count)
    : font_(font.GetFont()),
      glyphs_(glyphs, glyphs + count),
      positions_(positions, positions + count) {
  if (auto blob = font.MakeTextBlob(glyphs, positions, count)) {
    text_ = flutter::DlTextImpeller::MakeFromBlob(blob);
  }
}

GlyphRun::~GlyphRun() = default;

bool GlyphRun::IsValid() const {
  return text_ != nullptr;
}

const std::shared_ptr<flutter::DlText>& GlyphRun::GetText() const {
  return text_;
}

const flutter::DlPath& GlyphRun::GetOutlines() const {
  std::call_once(outlines_once_, [this] {
    SkPathBuilder outlines;
    for (size_t i = 0; i < glyphs_.size(); i++) {
      if (auto glyph_path = font_.getPath(glyphs_[i])) {
        outlines.addPath(glyph_path.value(), positions_[i].x, positions_[i].y);
      }
    }
    outlines_ = flutter::DlPath(outlines.detach());
  });
  return outlines_;
}

}  // namespace impeller::interop
