// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "impeller/toolkit/interop/typeface.h"

#include <algorithm>
#include <vector>

#include "flutter/third_party/skia/include/core/SkData.h"
#include "flutter/third_party/skia/include/core/SkFontArguments.h"
#include "flutter/third_party/skia/include/core/SkFontMgr.h"
#include "flutter/third_party/skia/include/core/SkStream.h"
#include "flutter/txt/src/txt/platform.h"
#include "impeller/base/validation.h"

namespace impeller::interop {

ScopedObject<Typeface> Typeface::Make(std::unique_ptr<fml::Mapping> data,
                                      uint32_t face_index) {
  if (!data) {
    return nullptr;
  }
  auto mapping = data.release();
  auto sk_data = SkData::MakeWithProc(
      mapping->GetMapping(),  //
      mapping->GetSize(),     //
      [](const void*, void* context) {
        delete reinterpret_cast<fml::Mapping*>(context);
      },
      mapping);
  auto typeface = txt::GetDefaultFontManager()->makeFromData(
      std::move(sk_data), static_cast<int>(face_index));
  if (!typeface) {
    VALIDATION_LOG << "Could not create typeface with data.";
    return nullptr;
  }
  return Create<Typeface>(std::move(typeface));
}

Typeface::Typeface(sk_sp<SkTypeface> typeface)
    : typeface_(std::move(typeface)) {}

Typeface::~Typeface() = default;

const sk_sp<SkTypeface>& Typeface::GetTypeface() const {
  return typeface_;
}

ScopedObject<Typeface> Typeface::WithVariations(
    const ImpellerFontVariation* variations,
    uint32_t count) const {
  std::vector<SkFontArguments::VariationPosition::Coordinate> coordinates;
  coordinates.reserve(count);
  for (uint32_t i = 0; i < count; i++) {
    coordinates.push_back({variations[i].axis_tag, variations[i].value});
  }
  SkFontArguments arguments;
  arguments.setVariationDesignPosition(
      {coordinates.data(), static_cast<int>(coordinates.size())});
  auto clone = typeface_->makeClone(arguments);
  if (!clone) {
    VALIDATION_LOG << "Could not apply font variations.";
    return nullptr;
  }
  return Create<Typeface>(std::move(clone));
}

uint32_t Typeface::GetUnitsPerEm() const {
  return static_cast<uint32_t>(std::max(typeface_->getUnitsPerEm(), 0));
}

uint64_t Typeface::CopyTableData(uint32_t tag,
                                 void* dst,
                                 uint64_t dst_size) const {
  const auto size = typeface_->getTableSize(tag);
  if (dst == nullptr || size == 0u) {
    return size;
  }
  return typeface_->getTableData(tag, 0u, std::min<uint64_t>(size, dst_size),
                                 dst);
}

uint64_t Typeface::CopyData(void* dst,
                            uint64_t dst_size,
                            uint32_t* out_face_index) const {
  int face_index = 0;
  auto stream = typeface_->openStream(&face_index);
  if (!stream || !stream->hasLength()) {
    return 0u;
  }
  if (out_face_index) {
    *out_face_index = static_cast<uint32_t>(std::max(face_index, 0));
  }
  const uint64_t length = stream->getLength();
  if (dst == nullptr) {
    return length;
  }
  return stream->read(dst, std::min(length, dst_size));
}

std::string Typeface::GetFamilyName() const {
  SkString name;
  typeface_->getFamilyName(&name);
  return std::string{name.c_str(), name.size()};
}

SkFontStyle Typeface::GetStyle() const {
  return typeface_->fontStyle();
}

}  // namespace impeller::interop
