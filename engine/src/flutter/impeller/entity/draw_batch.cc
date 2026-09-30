// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "impeller/entity/draw_batch.h"

#include <algorithm>

#include "impeller/core/formats.h"
#include "impeller/core/host_buffer.h"
#include "impeller/core/vertex_buffer.h"

namespace impeller {

namespace {

constexpr size_t kChunkVertices = 16384;
constexpr size_t kChunkIndices = kChunkVertices * 3;
constexpr size_t kMaxBatchedPoints = 256;

size_t PointCount(const VertexBuffer& vertex_buffer) {
  return vertex_buffer.index_type == IndexType::kNone
             ? vertex_buffer.vertex_count
             : vertex_buffer.vertex_buffer.GetRange().length / sizeof(Point);
}

template <typename T>
bool Reserve(HostBuffer& host_buffer,
             size_t needed,
             size_t chunk_size,
             BufferView& view,
             uint8_t*& data,
             size_t& capacity,
             size_t& count) {
  if (count + needed <= capacity) {
    return true;
  }
  capacity = std::max(needed, chunk_size);
  view = host_buffer.Emplace(capacity * sizeof(T), alignof(T),
                             [](uint8_t*) {});
  data = view.GetBuffer()->OnGetContents() + view.GetRange().offset;
  count = 0;
  return static_cast<bool>(view);
}

}  // namespace

bool DrawBatch::CanBatch(const GeometryResult& geometry) {
  return (geometry.type == PrimitiveType::kTriangle ||
          geometry.type == PrimitiveType::kTriangleStrip) &&
         !geometry.transform.HasPerspective() &&
         PointCount(geometry.vertex_buffer) <= kMaxBatchedPoints;
}

DrawBatch::DrawBatch() = default;

DrawBatch::~DrawBatch() = default;

bool DrawBatch::AppendFill(const ContentContext& renderer,
                           RenderPass& pass,
                           const ContentContextOptions& options,
                           const GeometryResult& geometry,
                           const Color& color) {
  const VertexBuffer& vertex_buffer = geometry.vertex_buffer;
  const BufferView& points_view = vertex_buffer.vertex_buffer;
  bool indexed = vertex_buffer.index_type != IndexType::kNone;
  size_t point_count = PointCount(vertex_buffer);
  size_t count = vertex_buffer.vertex_count;
  size_t index_count = geometry.type == PrimitiveType::kTriangle
                           ? count
                           : (count >= 3 ? (count - 2) * 3 : 0);

  // A new chunk can't continue the pending draw.
  if (pending_ && (options_.ToKey() != options.ToKey() ||
                   vertices_.count + point_count > vertices_.capacity ||
                   indices_.count + index_count > indices_.capacity)) {
    Flush(renderer, pass);
  }
  if (!Reserve<Vertex>(renderer.GetTransientsDataBuffer(), point_count,
                       kChunkVertices, vertices_.view, vertices_.data,
                       vertices_.capacity, vertices_.count) ||
      !Reserve<uint32_t>(renderer.GetTransientsIndexesBuffer(), index_count,
                         kChunkIndices, indices_.view, indices_.data,
                         indices_.capacity, indices_.count)) {
    return false;
  }
  pending_ = true;
  options_ = options;

  // The same terms as the vertex shader's mvp * vec4(position, 0, 1).
  const auto* points = reinterpret_cast<const Point*>(
      points_view.GetBuffer()->OnGetContents() + points_view.GetRange().offset);
  const Matrix& m = geometry.transform;
  Vector4 color_vector(color.red, color.green, color.blue, color.alpha);
  auto* out = reinterpret_cast<Vertex*>(vertices_.data) + vertices_.count;
  for (size_t i = 0; i < point_count; i++) {
    Point p = points[i];
    out[i].position = Vector3(m.m[0] * p.x + m.m[4] * p.y + m.m[12],
                              m.m[1] * p.x + m.m[5] * p.y + m.m[13],
                              m.m[2] * p.x + m.m[6] * p.y + m.m[14]);
    out[i].color = color_vector;
  }

  const uint8_t* index_data =
      indexed ? vertex_buffer.index_buffer.GetBuffer()->OnGetContents() +
                    vertex_buffer.index_buffer.GetRange().offset
              : nullptr;
  auto index_at = [&](size_t i) -> uint32_t {
    switch (vertex_buffer.index_type) {
      case IndexType::k16bit:
        return reinterpret_cast<const uint16_t*>(index_data)[i];
      case IndexType::k32bit:
        return reinterpret_cast<const uint32_t*>(index_data)[i];
      default:
        return i;
    }
  };

  uint32_t base = vertices_.count;
  auto* indices = reinterpret_cast<uint32_t*>(indices_.data) + indices_.count;
  if (geometry.type == PrimitiveType::kTriangle) {
    for (size_t i = 0; i < count; i++) {
      indices[i] = base + index_at(i);
    }
  } else {
    for (size_t i = 2; i < count; i++) {
      *indices++ = base + index_at(i - 2);
      *indices++ = base + index_at(i - 1);
      *indices++ = base + index_at(i);
    }
  }
  vertices_.count += point_count;
  indices_.count += index_count;
  return true;
}

bool DrawBatch::Flush(const ContentContext& renderer, RenderPass& pass) {
  if (!pending_) {
    return true;
  }
  pending_ = false;
  if (indices_.count == 0) {
    return true;
  }

  Range vertex_range{vertices_.view.GetRange().offset,
                     vertices_.count * sizeof(Vertex)};
  Range index_range{indices_.view.GetRange().offset,
                    indices_.count * sizeof(uint32_t)};
  vertices_.view.GetBuffer()->Flush(vertex_range);
  indices_.view.GetBuffer()->Flush(index_range);

  VertexBuffer vertex_buffer;
  vertex_buffer.vertex_buffer = vertices_.view.WithRange(vertex_range);
  vertex_buffer.index_buffer = indices_.view.WithRange(index_range);
  vertex_buffer.vertex_count = indices_.count;
  vertex_buffer.index_type = IndexType::k32bit;

  // The rest of the chunks stays available to the next batch.
  vertices_.view = vertices_.view.WithRange(
      {vertex_range.offset + vertex_range.length,
       vertices_.view.GetRange().length - vertex_range.length});
  vertices_.data += vertex_range.length;
  vertices_.capacity -= vertices_.count;
  vertices_.count = 0;
  indices_.view = indices_.view.WithRange(
      {index_range.offset + index_range.length,
       indices_.view.GetRange().length - index_range.length});
  indices_.data += index_range.length;
  indices_.capacity -= indices_.count;
  indices_.count = 0;

  pass.SetCommandLabel("Batched Fill");
  pass.SetStencilReference(0);
  pass.SetPipeline(renderer.GetBatchedFillPipeline(options_));
  pass.SetVertexBuffer(std::move(vertex_buffer));
  return pass.Draw().ok();
}

}  // namespace impeller
