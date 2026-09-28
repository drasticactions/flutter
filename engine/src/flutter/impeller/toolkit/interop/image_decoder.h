// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_IMPELLER_TOOLKIT_INTEROP_IMAGE_DECODER_H_
#define FLUTTER_IMPELLER_TOOLKIT_INTEROP_IMAGE_DECODER_H_

#include <memory>

#include "flutter/fml/mapping.h"
#include "flutter/third_party/skia/include/codec/SkCodec.h"
#include "impeller/geometry/size.h"
#include "impeller/toolkit/interop/impeller.h"
#include "impeller/toolkit/interop/object.h"

namespace impeller::interop {

class ImageDecoder final
    : public Object<ImageDecoder,
                    IMPELLER_INTERNAL_HANDLE_NAME(ImpellerImageDecoder)> {
 public:
  static ScopedObject<ImageDecoder> Make(std::unique_ptr<fml::Mapping> data);

  explicit ImageDecoder(std::unique_ptr<SkCodec> codec);

  ~ImageDecoder() override;

  ImageDecoder(const ImageDecoder&) = delete;

  ImageDecoder& operator=(const ImageDecoder&) = delete;

  ISize GetSize() const;

  bool Decode(ISize size, uint8_t* destination, uint64_t row_bytes);

 private:
  std::unique_ptr<SkCodec> codec_;
};

bool EncodeImage(const uint8_t* pixels,
                 ISize size,
                 uint64_t row_bytes,
                 ImpellerImageFormat format,
                 uint32_t quality,
                 ImpellerWriteCallback write,
                 void* user_data);

}  // namespace impeller::interop

#endif  // FLUTTER_IMPELLER_TOOLKIT_INTEROP_IMAGE_DECODER_H_
