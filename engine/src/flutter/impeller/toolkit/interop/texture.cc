// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "impeller/toolkit/interop/texture.h"

#include "flutter/fml/synchronization/waitable_event.h"
#include "impeller/base/validation.h"
#include "impeller/renderer/command_buffer.h"
#include "impeller/renderer/command_queue.h"

#if IMPELLER_ENABLE_OPENGLES
#include "impeller/renderer/backend/gles/context_gles.h"
#endif  // IMPELLER_ENABLE_OPENGLES

namespace impeller::interop {

Texture::Texture(const Context& context, const TextureDescriptor& descriptor) {
  if (!context.IsValid()) {
    return;
  }
  auto texture =
      context.GetContext()->GetResourceAllocator()->CreateTexture(descriptor);
  if (!texture || !texture->IsValid()) {
    return;
  }
  texture->SetLabel("UserCreated");
  backend_ = context.GetContext()->GetBackendType();
  texture_ = std::move(texture);
}

Texture::Texture(impeller::Context::BackendType backend,
                 std::shared_ptr<impeller::Texture> texture)
    : backend_(backend), texture_(std::move(texture)) {}

Texture::~Texture() = default;

ScopedObject<Texture> Texture::CreateRenderTarget(const Context& context,
                                                  ISize size) {
  if (!context.IsValid() || size.IsEmpty()) {
    return nullptr;
  }
  // Use the format the context renders to so that the renderer can blit into
  // the texture. Readbacks convert to RGBA.
  auto format =
      context.GetContext()->GetCapabilities()->GetDefaultColorFormat();
  if (format != PixelFormat::kR8G8B8A8UNormInt &&
      format != PixelFormat::kB8G8R8A8UNormInt) {
    format = PixelFormat::kR8G8B8A8UNormInt;
  }
  TextureDescriptor desc;
  desc.storage_mode = StorageMode::kDevicePrivate;
  desc.type = TextureType::kTexture2D;
  desc.format = format;
  desc.size = size;
  desc.mip_count = 1u;
  desc.usage = TextureUsage::kRenderTarget | TextureUsage::kShaderRead;
  auto texture = Create<Texture>(context, desc);
  if (!texture->IsValid()) {
    return nullptr;
  }
  texture->GetTexture()->SetLabel("UserRenderTarget");
  return texture;
}

bool Texture::ReadPixels(const Context& context,
                         const IRect& region,
                         uint8_t* destination,
                         uint64_t destination_row_bytes) const {
  if (!IsValid() || !context.IsValid()) {
    return false;
  }
  const auto format = texture_->GetTextureDescriptor().format;
  if (format != PixelFormat::kR8G8B8A8UNormInt &&
      format != PixelFormat::kB8G8R8A8UNormInt) {
    VALIDATION_LOG << "Unsupported pixel format for readback.";
    return false;
  }
  if (region.IsEmpty() ||
      !IRect::MakeSize(texture_->GetSize()).Contains(region)) {
    VALIDATION_LOG << "Readback region is outside the texture.";
    return false;
  }
  const uint64_t row_bytes = region.GetWidth() * 4u;
  if (destination_row_bytes < row_bytes) {
    VALIDATION_LOG << "Destination rows are too small.";
    return false;
  }

  const auto& impeller_context = context.GetContext();
#if IMPELLER_ENABLE_OPENGLES
  // The readback completes when the reactor runs, which only happens on a
  // thread with the context current.
  if (impeller_context->GetBackendType() ==
          impeller::Context::BackendType::kOpenGLES &&
      !ContextGLES::Cast(*impeller_context)
           .GetReactor()
           ->CanReactOnCurrentThread()) {
    VALIDATION_LOG << "Readbacks of OpenGL textures must happen on a thread "
                      "where the context is current.";
    return false;
  }
#endif  // IMPELLER_ENABLE_OPENGLES
  DeviceBufferDescriptor buffer_desc;
  buffer_desc.storage_mode = StorageMode::kHostVisible;
  buffer_desc.readback = true;
  buffer_desc.size = row_bytes * region.GetHeight();
  auto buffer =
      impeller_context->GetResourceAllocator()->CreateBuffer(buffer_desc);
  if (!buffer) {
    VALIDATION_LOG << "Could not allocate the readback buffer.";
    return false;
  }

  auto command_buffer = impeller_context->CreateCommandBuffer();
  if (!command_buffer) {
    return false;
  }
  command_buffer->SetLabel("Texture Readback");
  auto blit_pass = command_buffer->CreateBlitPass();
  // The copy leaves Vulkan images in a transfer layout. Return the texture to
  // a sampleable layout so it can still be drawn.
  if (!blit_pass || !blit_pass->AddCopy(texture_, buffer, region) ||
      !blit_pass->ConvertTextureToShaderRead(texture_) ||
      !blit_pass->EncodeCommands()) {
    VALIDATION_LOG << "Could not encode the readback.";
    return false;
  }

  fml::AutoResetWaitableEvent latch;
  bool completed = false;
  if (!impeller_context->GetCommandQueue()
           ->Submit({std::move(command_buffer)},
                    [&latch, &completed](CommandBuffer::Status status) {
                      completed = status == CommandBuffer::Status::kCompleted;
                      latch.Signal();
                    })
           .ok()) {
    VALIDATION_LOG << "Could not submit the readback.";
    return false;
  }
  latch.Wait();
  impeller_context->DisposeThreadLocalCachedResources();
  if (!completed) {
    return false;
  }

  buffer->Invalidate();
  const uint8_t* source = buffer->OnGetContents();
  if (!source) {
    return false;
  }
  const bool swizzle = format == PixelFormat::kB8G8R8A8UNormInt;
  for (int64_t y = 0; y < region.GetHeight(); y++) {
    const uint8_t* source_row = source + y * row_bytes;
    uint8_t* destination_row = destination + y * destination_row_bytes;
    if (!swizzle) {
      ::memcpy(destination_row, source_row, row_bytes);
      continue;
    }
    for (int64_t x = 0; x < region.GetWidth(); x++) {
      destination_row[x * 4 + 0] = source_row[x * 4 + 2];
      destination_row[x * 4 + 1] = source_row[x * 4 + 1];
      destination_row[x * 4 + 2] = source_row[x * 4 + 0];
      destination_row[x * 4 + 3] = source_row[x * 4 + 3];
    }
  }
  return true;
}

bool Texture::IsValid() const {
  return !!texture_;
}

bool Texture::SetContents(const uint8_t* contents, uint64_t length) {
  if (!IsValid()) {
    return false;
  }
  return texture_->SetContents(contents, length);
}

bool Texture::SetContents(std::shared_ptr<const fml::Mapping> contents) {
  if (!IsValid()) {
    return false;
  }
  return texture_->SetContents(std::move(contents));
}

sk_sp<DlImageImpeller> Texture::MakeImage() const {
  return DlImageImpeller::Make(texture_);
}

impeller::Context::BackendType Texture::GetBackendType() const {
  return backend_;
}

const std::shared_ptr<impeller::Texture>& Texture::GetTexture() const {
  return texture_;
}

}  // namespace impeller::interop
