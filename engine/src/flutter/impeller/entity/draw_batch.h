// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_IMPELLER_ENTITY_DRAW_BATCH_H_
#define FLUTTER_IMPELLER_ENTITY_DRAW_BATCH_H_

#include "impeller/entity/contents/content_context.h"
#include "impeller/entity/contents/pipelines.h"
#include "impeller/entity/geometry/geometry.h"
#include "impeller/geometry/color.h"
#include "impeller/renderer/render_pass.h"

namespace impeller {

/// Draws consecutive solid fills with the same draw state at once.
class DrawBatch {
 public:
  using Vertex = BatchedFillPipeline::VertexShader::PerVertexData;

  DrawBatch();

  ~DrawBatch();

  /// Whether tessellated geometry can be batched. Large draws aren't: their
  /// per-draw cost is small next to copying their vertices.
  static bool CanBatch(const GeometryResult& geometry);

  /// Adds geometry that CanBatch accepts, drawing a pending batch with other
  /// options first. Returns false if the geometry couldn't be added.
  bool AppendFill(const ContentContext& renderer,
                  RenderPass& pass,
                  const ContentContextOptions& options,
                  const GeometryResult& geometry,
                  const Color& color);

  /// Draws the pending batch, if any.
  bool Flush(const ContentContext& renderer, RenderPass& pass);

 private:
  // Vertices and indices are written straight into chunks of the transient
  // buffers and drawn from there.
  struct Chunk {
    BufferView view;
    uint8_t* data = nullptr;
    size_t capacity = 0;
    size_t count = 0;
  };

  bool pending_ = false;
  ContentContextOptions options_;
  Chunk vertices_;
  Chunk indices_;

  DrawBatch(const DrawBatch&) = delete;

  DrawBatch& operator=(const DrawBatch&) = delete;
};

}  // namespace impeller

#endif  // FLUTTER_IMPELLER_ENTITY_DRAW_BATCH_H_
