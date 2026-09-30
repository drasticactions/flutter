// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "solid_color_contents.h"

#include "impeller/entity/contents/content_context.h"
#include "impeller/entity/draw_batch.h"
#include "impeller/entity/entity.h"
#include "impeller/entity/geometry/geometry.h"
#include "impeller/renderer/render_pass.h"

namespace impeller {

SolidColorContents::SolidColorContents(const Geometry* geometry)
    : geometry_(geometry) {}

SolidColorContents::~SolidColorContents() = default;

void SolidColorContents::SetColor(Color color) {
  color_ = color;
}

Color SolidColorContents::GetColor() const {
  return color_.WithAlpha(color_.alpha * GetOpacityFactor());
}

const Geometry* SolidColorContents::GetGeometry() const {
  return geometry_;
}

bool SolidColorContents::IsSolidColor() const {
  return true;
}

bool SolidColorContents::IsOpaque(const Matrix& transform) const {
  return GetColor().IsOpaque() && !AppliesAlphaForStrokeCoverage(transform);
}

std::optional<Rect> SolidColorContents::GetCoverage(
    const Entity& entity) const {
  if (GetColor().IsTransparent()) {
    return std::nullopt;
  }

  const Geometry* geometry = GetGeometry();
  if (geometry == nullptr) {
    return std::nullopt;
  }
  return geometry->GetCoverage(entity.GetTransform());
};

bool SolidColorContents::Render(const ContentContext& renderer,
                                const Entity& entity,
                                RenderPass& pass) const {
  return RenderGeometry(renderer, entity, pass, DefaultCreateGeometryCallback);
}

bool SolidColorContents::RenderGeometry(
    const ContentContext& renderer,
    const Entity& entity,
    RenderPass& pass,
    const CreateGeometryCallback& create_geom_callback) const {
  using VS = SolidFillPipeline::VertexShader;
  using FS = SolidFillPipeline::FragmentShader;
  auto& data_host_buffer = renderer.GetTransientsDataBuffer();

  VS::FrameInfo frame_info;
  FS::FragInfo frag_info;
  frag_info.color = GetColor().Premultiply() *
                    GetGeometry()->ComputeAlphaCoverage(entity.GetTransform());

  PipelineBuilderCallback pipeline_callback =
      [&renderer](ContentContextOptions options) {
        return renderer.GetSolidFillPipeline(options);
      };
  return ColorSourceContents::DrawGeometry<VS>(
      renderer, entity, pass, pipeline_callback, frame_info,
      [&frag_info, &data_host_buffer](RenderPass& pass) {
        FS::BindFragInfo(pass, data_host_buffer.EmplaceUniform(frag_info));
        pass.SetCommandLabel("Solid Fill");
        return true;
      },
      /*force_stencil=*/false, create_geom_callback);
}

// Same draw state and vertices as Render, for geometry without stencil.
bool SolidColorContents::AppendToBatch(DrawBatch& batch,
                                       const ContentContext& renderer,
                                       const Entity& entity,
                                       RenderPass& pass) const {
  const Geometry* geometry = GetGeometry();
  GeometryResult::Mode mode = geometry->GetResultMode();
  if (mode != GeometryResult::Mode::kNormal &&
      mode != GeometryResult::Mode::kPreventOverdraw) {
    return false;
  }

  GeometryResult result = geometry->GetPositionBuffer(renderer, entity, pass);
  if (result.vertex_buffer.vertex_count == 0u) {
    return true;
  }
  // Draws the geometry made above after the pending batch.
  auto draw_now = [&]() {
    batch.Flush(renderer, pass);
    RenderGeometry(renderer, entity, pass,
                   [&result](const ContentContext&, const Entity&, RenderPass&,
                             const Geometry*) { return result; });
    return true;
  };
  if (!DrawBatch::CanBatch(result)) {
    return draw_now();
  }

  auto options = OptionsFromPassAndEntity(pass, entity);
  options.primitive_type = PrimitiveType::kTriangle;
  options.depth_write_enabled = options.blend_mode == BlendMode::kSrc;
  if (result.mode == GeometryResult::Mode::kPreventOverdraw) {
    options.depth_write_enabled = true;
    options.depth_compare = CompareFunction::kGreater;
  }

  if (!batch.AppendFill(renderer, pass, options, result,
                        GetColor().Premultiply() *
                            geometry->ComputeAlphaCoverage(
                                entity.GetTransform()))) {
    return draw_now();
  }
  return true;
}

std::optional<Color> SolidColorContents::AsBackgroundColor(
    const Entity& entity,
    ISize target_size) const {
  const Geometry* geometry = GetGeometry();
  if (geometry == nullptr) {
    return std::nullopt;
  }
  IRect target_rect = IRect::MakeSize(target_size);
  return geometry->CoversArea(entity.GetTransform(), target_rect)
             ? GetColor()
             : std::optional<Color>();
}

bool SolidColorContents::ApplyColorFilter(
    const ColorFilterProc& color_filter_proc) {
  color_ = color_filter_proc(color_);
  return true;
}

}  // namespace impeller
