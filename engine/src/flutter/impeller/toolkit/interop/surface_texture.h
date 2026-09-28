// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_IMPELLER_TOOLKIT_INTEROP_SURFACE_TEXTURE_H_
#define FLUTTER_IMPELLER_TOOLKIT_INTEROP_SURFACE_TEXTURE_H_

#include "impeller/toolkit/interop/surface.h"
#include "impeller/toolkit/interop/texture.h"

namespace impeller::interop {

//------------------------------------------------------------------------------
/// A surface that renders into a texture created with
/// `Texture::CreateRenderTarget`.
///
class SurfaceTexture final : public Surface {
 public:
  SurfaceTexture(Context& context, const Texture& texture);

  // |Surface|
  ~SurfaceTexture() override;

  SurfaceTexture(const SurfaceTexture&) = delete;

  SurfaceTexture& operator=(const SurfaceTexture&) = delete;
};

}  // namespace impeller::interop

#endif  // FLUTTER_IMPELLER_TOOLKIT_INTEROP_SURFACE_TEXTURE_H_
