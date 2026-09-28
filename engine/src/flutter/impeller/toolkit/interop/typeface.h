// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_IMPELLER_TOOLKIT_INTEROP_TYPEFACE_H_
#define FLUTTER_IMPELLER_TOOLKIT_INTEROP_TYPEFACE_H_

#include <memory>
#include <string>

#include "flutter/fml/mapping.h"
#include "flutter/third_party/skia/include/core/SkTypeface.h"
#include "impeller/toolkit/interop/impeller.h"
#include "impeller/toolkit/interop/object.h"

namespace impeller::interop {

class Typeface final
    : public Object<Typeface, IMPELLER_INTERNAL_HANDLE_NAME(ImpellerTypeface)> {
 public:
  static ScopedObject<Typeface> Make(std::unique_ptr<fml::Mapping> data,
                                     uint32_t face_index);

  explicit Typeface(sk_sp<SkTypeface> typeface);

  ~Typeface() override;

  Typeface(const Typeface&) = delete;

  Typeface& operator=(const Typeface&) = delete;

  const sk_sp<SkTypeface>& GetTypeface() const;

  ScopedObject<Typeface> WithVariations(const ImpellerFontVariation* variations,
                                        uint32_t count) const;

  uint32_t GetUnitsPerEm() const;

  uint64_t CopyTableData(uint32_t tag, void* dst, uint64_t dst_size) const;

  uint64_t CopyData(void* dst,
                    uint64_t dst_size,
                    uint32_t* out_face_index) const;

  std::string GetFamilyName() const;

  SkFontStyle GetStyle() const;

 private:
  sk_sp<SkTypeface> typeface_;
};

}  // namespace impeller::interop

#endif  // FLUTTER_IMPELLER_TOOLKIT_INTEROP_TYPEFACE_H_
