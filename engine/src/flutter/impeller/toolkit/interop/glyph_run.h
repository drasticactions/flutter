// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_IMPELLER_TOOLKIT_INTEROP_GLYPH_RUN_H_
#define FLUTTER_IMPELLER_TOOLKIT_INTEROP_GLYPH_RUN_H_

#include <memory>
#include <mutex>
#include <vector>

#include "flutter/display_list/dl_text.h"
#include "flutter/display_list/geometry/dl_path.h"
#include "impeller/toolkit/interop/font.h"
#include "impeller/toolkit/interop/impeller.h"
#include "impeller/toolkit/interop/object.h"

namespace impeller::interop {

/// Positioned glyphs of one font, prepared once for drawing many times.
class GlyphRun final
    : public Object<GlyphRun, IMPELLER_INTERNAL_HANDLE_NAME(ImpellerGlyphRun)> {
 public:
  GlyphRun(const Font& font,
           const uint16_t* glyphs,
           const ImpellerPoint* positions,
           uint32_t count);

  ~GlyphRun() override;

  GlyphRun(const GlyphRun&) = delete;

  GlyphRun& operator=(const GlyphRun&) = delete;

  bool IsValid() const;

  const std::shared_ptr<flutter::DlText>& GetText() const;

  /// The glyph outlines relative to the run origin, built on first use. Empty
  /// for color glyphs, which have no outlines.
  const flutter::DlPath& GetOutlines() const;

 private:
  SkFont font_;
  std::vector<uint16_t> glyphs_;
  std::vector<ImpellerPoint> positions_;
  std::shared_ptr<flutter::DlText> text_;
  mutable std::once_flag outlines_once_;
  mutable flutter::DlPath outlines_;
};

}  // namespace impeller::interop

#endif  // FLUTTER_IMPELLER_TOOLKIT_INTEROP_GLYPH_RUN_H_
