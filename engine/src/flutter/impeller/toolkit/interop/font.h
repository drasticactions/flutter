// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_IMPELLER_TOOLKIT_INTEROP_FONT_H_
#define FLUTTER_IMPELLER_TOOLKIT_INTEROP_FONT_H_

#include "flutter/third_party/skia/include/core/SkFont.h"
#include "flutter/third_party/skia/include/core/SkTextBlob.h"
#include "impeller/geometry/point.h"
#include "impeller/toolkit/interop/impeller.h"
#include "impeller/toolkit/interop/object.h"
#include "impeller/toolkit/interop/path.h"
#include "impeller/toolkit/interop/typeface.h"

namespace impeller::interop {

class Font final
    : public Object<Font, IMPELLER_INTERNAL_HANDLE_NAME(ImpellerFont)> {
 public:
  Font(const Typeface& typeface, float size);

  ~Font() override;

  Font(const Font&) = delete;

  Font& operator=(const Font&) = delete;

  const SkFont& GetFont() const;

  void SetSkewX(float skew);

  void SetEmbolden(bool embolden);

  void SetSubpixel(bool subpixel);

  ScopedObject<Path> CreateGlyphPath(uint16_t glyph) const;

  void GetGlyphBounds(const uint16_t* glyphs,
                      uint32_t count,
                      ImpellerRect* out_bounds) const;

  sk_sp<SkTextBlob> MakeTextBlob(const uint16_t* glyphs,
                                 const ImpellerPoint* positions,
                                 uint32_t count) const;

 private:
  SkFont font_;
};

}  // namespace impeller::interop

#endif  // FLUTTER_IMPELLER_TOOLKIT_INTEROP_FONT_H_
