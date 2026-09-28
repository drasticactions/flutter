// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "impeller/toolkit/interop/image_decoder.h"

#include <algorithm>
#include <vector>

#include "flutter/third_party/skia/include/codec/SkBmpDecoder.h"
#include "flutter/third_party/skia/include/codec/SkWbmpDecoder.h"
#include "flutter/third_party/skia/include/core/SkData.h"
#include "flutter/third_party/skia/include/core/SkPixmap.h"
#include "flutter/third_party/skia/include/core/SkSamplingOptions.h"
#include "flutter/third_party/skia/include/core/SkStream.h"
#include "impeller/base/validation.h"

#if defined(SK_CODEC_DECODES_GIF)
#include "flutter/third_party/skia/include/codec/SkGifDecoder.h"
#endif
#if defined(SK_CODEC_DECODES_ICO)
#include "flutter/third_party/skia/include/codec/SkIcoDecoder.h"
#endif
#if defined(SK_CODEC_DECODES_JPEG)
#include "flutter/third_party/skia/include/codec/SkJpegDecoder.h"
#endif
#if defined(SK_CODEC_DECODES_PNG)
#include "flutter/third_party/skia/include/codec/SkPngDecoder.h"
#endif
#if defined(SK_CODEC_DECODES_WEBP)
#include "flutter/third_party/skia/include/codec/SkWebpDecoder.h"
#endif
#if defined(SK_CODEC_ENCODES_JPEG)
#include "flutter/third_party/skia/include/encode/SkJpegEncoder.h"
#endif
#if defined(SK_CODEC_ENCODES_PNG)
#include "flutter/third_party/skia/include/encode/SkPngEncoder.h"
#endif
#if defined(SK_CODEC_ENCODES_WEBP)
#include "flutter/third_party/skia/include/encode/SkWebpEncoder.h"
#endif

namespace impeller::interop {

static std::vector<SkCodecs::Decoder> GetDecoders() {
  std::vector<SkCodecs::Decoder> decoders;
#if defined(SK_CODEC_DECODES_PNG)
  decoders.push_back(SkPngDecoder::Decoder());
#endif
#if defined(SK_CODEC_DECODES_JPEG)
  decoders.push_back(SkJpegDecoder::Decoder());
#endif
#if defined(SK_CODEC_DECODES_WEBP)
  decoders.push_back(SkWebpDecoder::Decoder());
#endif
#if defined(SK_CODEC_DECODES_GIF)
  decoders.push_back(SkGifDecoder::Decoder());
#endif
#if defined(SK_CODEC_DECODES_ICO)
  decoders.push_back(SkIcoDecoder::Decoder());
#endif
  decoders.push_back(SkBmpDecoder::Decoder());
  decoders.push_back(SkWbmpDecoder::Decoder());
  return decoders;
}

ScopedObject<ImageDecoder> ImageDecoder::Make(
    std::unique_ptr<fml::Mapping> data) {
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
  static const auto kDecoders = GetDecoders();
  auto codec = SkCodec::MakeFromData(std::move(sk_data), kDecoders);
  if (!codec) {
    VALIDATION_LOG << "Could not recognize the image data.";
    return nullptr;
  }
  return Create<ImageDecoder>(std::move(codec));
}

ImageDecoder::ImageDecoder(std::unique_ptr<SkCodec> codec)
    : codec_(std::move(codec)) {}

ImageDecoder::~ImageDecoder() = default;

ISize ImageDecoder::GetSize() const {
  const auto size = codec_->dimensions();
  return ISize::MakeWH(size.width(), size.height());
}

static SkImageInfo MakeInfo(int width, int height) {
  return SkImageInfo::Make(width, height, kRGBA_8888_SkColorType,
                           kPremul_SkAlphaType, SkColorSpace::MakeSRGB());
}

bool ImageDecoder::Decode(ISize size,
                          uint8_t* destination,
                          uint64_t row_bytes) {
  if (size.IsEmpty() || row_bytes < static_cast<uint64_t>(size.width) * 4u) {
    VALIDATION_LOG << "Invalid decode size or row bytes.";
    return false;
  }
  const auto info = MakeInfo(size.width, size.height);
  const auto native = codec_->dimensions();
  if (native.width() == size.width && native.height() == size.height) {
    const auto result = codec_->getPixels(info, destination, row_bytes);
    return result == SkCodec::kSuccess || result == SkCodec::kIncompleteInput;
  }

  // Let the codec downscale as far as it can without going below the target,
  // then resample to the exact size.
  const float scale = std::max(
      static_cast<float>(size.width) / static_cast<float>(native.width()),
      static_cast<float>(size.height) / static_cast<float>(native.height()));
  auto decoded = codec_->getScaledDimensions(std::min(scale, 1.0f));
  if (decoded.width() < size.width || decoded.height() < size.height) {
    decoded = native;
  }
  const auto decoded_info = MakeInfo(decoded.width(), decoded.height());
  std::vector<uint8_t> pixels(decoded_info.computeMinByteSize());
  const auto result = codec_->getPixels(decoded_info, pixels.data(),
                                        decoded_info.minRowBytes());
  if (result != SkCodec::kSuccess && result != SkCodec::kIncompleteInput) {
    return false;
  }
  const SkPixmap source(decoded_info, pixels.data(),
                        decoded_info.minRowBytes());
  const SkPixmap target(info, destination, row_bytes);
  return source.scalePixels(target,
                            SkSamplingOptions(SkCubicResampler::Mitchell()));
}

namespace {

class CallbackStream final : public SkWStream {
 public:
  CallbackStream(ImpellerWriteCallback write, void* user_data)
      : write_(write), user_data_(user_data) {}

  bool write(const void* buffer, size_t size) override {
    write_(buffer, size, user_data_);
    written_ += size;
    return true;
  }

  size_t bytesWritten() const override { return written_; }

 private:
  ImpellerWriteCallback write_;
  void* user_data_;
  size_t written_ = 0u;
};

}  // namespace

bool EncodeImage(const uint8_t* pixels,
                 ISize size,
                 uint64_t row_bytes,
                 ImpellerImageFormat format,
                 uint32_t quality,
                 ImpellerWriteCallback write,
                 void* user_data) {
  if (size.IsEmpty() || row_bytes < static_cast<uint64_t>(size.width) * 4u) {
    VALIDATION_LOG << "Invalid encode size or row bytes.";
    return false;
  }
  const SkPixmap pixmap(MakeInfo(size.width, size.height), pixels, row_bytes);
  CallbackStream stream(write, user_data);
  quality = std::min(quality, 100u);
  switch (format) {
    case kImpellerImageFormatPNG: {
#if defined(SK_CODEC_ENCODES_PNG)
      SkPngEncoder::Options options;
      options.fZLibLevel = static_cast<int>(std::min(quality, 9u));
      return SkPngEncoder::Encode(&stream, pixmap, options);
#else
      return false;
#endif
    }
    case kImpellerImageFormatJPEG: {
#if defined(SK_CODEC_ENCODES_JPEG)
      SkJpegEncoder::Options options;
      options.fQuality = static_cast<int>(quality);
      return SkJpegEncoder::Encode(&stream, pixmap, options);
#else
      return false;
#endif
    }
    case kImpellerImageFormatWebP: {
#if defined(SK_CODEC_ENCODES_WEBP)
      SkWebpEncoder::Options options;
      options.fQuality = static_cast<float>(quality);
      options.fCompression = quality == 100u
                                 ? SkWebpEncoder::Compression::kLossless
                                 : SkWebpEncoder::Compression::kLossy;
      return SkWebpEncoder::Encode(&stream, pixmap, options);
#else
      return false;
#endif
    }
  }
  return false;
}

}  // namespace impeller::interop
