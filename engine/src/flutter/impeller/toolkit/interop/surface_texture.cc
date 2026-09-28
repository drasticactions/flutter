// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "impeller/toolkit/interop/surface_texture.h"

#include "impeller/renderer/render_target.h"

namespace impeller::interop {

static std::shared_ptr<impeller::Surface> CreateTextureSurface(
    const Context& context,
    const Texture& texture) {
  if (!context.IsValid() || !texture.IsValid() ||
      !(texture.GetTexture()->GetTextureDescriptor().usage &
        TextureUsage::kRenderTarget)) {
    return nullptr;
  }
  const auto& impeller_context = context.GetContext();
  RenderTargetAllocator allocator(impeller_context->GetResourceAllocator());
  auto render_target = allocator.CreateOffscreen(
      *impeller_context,                              //
      texture.GetTexture()->GetSize(),                //
      1,                                              //
      "Texture Surface",                              //
      RenderTarget::kDefaultColorAttachmentConfig,    //
      RenderTarget::kDefaultStencilAttachmentConfig,  //
      texture.GetTexture()                            //
  );
  if (!render_target.IsValid()) {
    return nullptr;
  }
  return std::make_shared<impeller::Surface>(render_target);
}

SurfaceTexture::SurfaceTexture(Context& context, const Texture& texture)
    : Surface(context,
              CreateTextureSurface(context, texture),
              /*is_onscreen=*/false) {}

SurfaceTexture::~SurfaceTexture() = default;

}  // namespace impeller::interop
