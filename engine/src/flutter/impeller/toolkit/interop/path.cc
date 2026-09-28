// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "impeller/toolkit/interop/path.h"

#include "flutter/third_party/skia/include/core/SkPaint.h"
#include "flutter/third_party/skia/include/core/SkPathEffect.h"
#include "flutter/third_party/skia/include/core/SkStrokeRec.h"
#include "flutter/third_party/skia/include/effects/SkDashPathEffect.h"
#include "flutter/third_party/skia/include/pathops/SkPathOps.h"
#include "impeller/base/validation.h"
#include "impeller/toolkit/interop/formats.h"
#include "third_party/skia/include/core/SkRect.h"

namespace impeller::interop {

namespace {

ImpellerRect ToImpellerRect(const SkRect& bounds) {
  return ImpellerRect{
      .x = bounds.x(),
      .y = bounds.y(),
      .width = bounds.width(),
      .height = bounds.height(),
  };
}

SkPathOp ToSkiaType(ImpellerPathOp op) {
  switch (op) {
    case kImpellerPathOpDifference:
      return SkPathOp::kDifference_SkPathOp;
    case kImpellerPathOpIntersect:
      return SkPathOp::kIntersect_SkPathOp;
    case kImpellerPathOpUnion:
      return SkPathOp::kUnion_SkPathOp;
    case kImpellerPathOpXor:
      return SkPathOp::kXOR_SkPathOp;
    case kImpellerPathOpReverseDifference:
      return SkPathOp::kReverseDifference_SkPathOp;
  }
  return SkPathOp::kUnion_SkPathOp;
}

SkPaint::Cap ToSkiaType(ImpellerStrokeCap cap) {
  switch (cap) {
    case kImpellerStrokeCapButt:
      return SkPaint::kButt_Cap;
    case kImpellerStrokeCapRound:
      return SkPaint::kRound_Cap;
    case kImpellerStrokeCapSquare:
      return SkPaint::kSquare_Cap;
  }
  return SkPaint::kButt_Cap;
}

SkPaint::Join ToSkiaType(ImpellerStrokeJoin join) {
  switch (join) {
    case kImpellerStrokeJoinMiter:
      return SkPaint::kMiter_Join;
    case kImpellerStrokeJoinRound:
      return SkPaint::kRound_Join;
    case kImpellerStrokeJoinBevel:
      return SkPaint::kBevel_Join;
  }
  return SkPaint::kMiter_Join;
}

// Copies the path with every contour closed.
SkPath CloseContours(const SkPath& path) {
  SkPathBuilder builder(path.getFillType());
  SkPath::Iter iter(path, true);
  while (auto rec = iter.next()) {
    const auto pts = rec->fPoints;
    switch (rec->fVerb) {
      case SkPathVerb::kMove:
        builder.moveTo(pts[0]);
        break;
      case SkPathVerb::kLine:
        builder.lineTo(pts[1]);
        break;
      case SkPathVerb::kQuad:
        builder.quadTo(pts[1], pts[2]);
        break;
      case SkPathVerb::kConic:
        builder.conicTo(pts[1], pts[2], rec->conicWeight());
        break;
      case SkPathVerb::kCubic:
        builder.cubicTo(pts[1], pts[2], pts[3]);
        break;
      case SkPathVerb::kClose:
        builder.close();
        break;
    }
  }
  return builder.detach();
}

}  // namespace

Path::Path(const SkPath& path) : path_(SkPathBuilder(path)) {}

Path::~Path() = default;

SkPath Path::GetPath() const {
  return path_.snapshot();
}

ImpellerRect Path::GetBounds() const {
  return ToImpellerRect(path_.computeFiniteBounds().value_or(SkRect()));
}

ImpellerRect Path::GetTightBounds() const {
  return ToImpellerRect(GetPath().computeTightBounds());
}

bool Path::Contains(const Point& point) const {
  return GetPath().contains(ToSkiaType(point));
}

bool Path::IsEmpty() const {
  return GetPath().isEmpty();
}

ImpellerFillType Path::GetFillType() const {
  switch (path_.fillType()) {
    case SkPathFillType::kEvenOdd:
    case SkPathFillType::kInverseEvenOdd:
      return kImpellerFillTypeOdd;
    case SkPathFillType::kWinding:
    case SkPathFillType::kInverseWinding:
      return kImpellerFillTypeNonZero;
  }
  return kImpellerFillTypeNonZero;
}

ScopedObject<Path> Path::WithFillType(ImpellerFillType fill) const {
  return Create<Path>(
      GetPath().makeFillType(ToSkiaType(ToImpellerType(fill))));
}

ScopedObject<Path> Path::Transformed(const Matrix& transform) const {
  return Create<Path>(GetPath().makeTransform(ToSkMatrix(transform)));
}

ScopedObject<Path> Path::Op(const Path& other, ImpellerPathOp op) const {
  auto result = ::Op(GetPath(), other.GetPath(), ToSkiaType(op));
  if (!result.has_value()) {
    VALIDATION_LOG << "Path operation failed.";
    return nullptr;
  }
  return Create<Path>(result.value());
}

ScopedObject<Path> Path::Stroked(const ImpellerStrokeParameters& stroke,
                                 float resolution_scale) const {
  if (!(stroke.width > 0.0f) || !std::isfinite(stroke.width)) {
    // Hairlines have no fillable outline.
    return nullptr;
  }
  if (!(resolution_scale > 0.0f) || !std::isfinite(resolution_scale)) {
    resolution_scale = 1.0f;
  }
  SkStrokeRec rec(SkStrokeRec::kFill_InitStyle);
  rec.setStrokeStyle(stroke.width);
  rec.setStrokeParams(ToSkiaType(stroke.cap), ToSkiaType(stroke.join),
                      stroke.miter_limit);
  rec.setResScale(resolution_scale);
  SkPathBuilder builder;
  if (!rec.applyToPath(&builder, GetPath())) {
    return nullptr;
  }
  auto outline = builder.detach();
  outline.setFillType(SkPathFillType::kWinding);
  return Create<Path>(CloseContours(outline));
}

ScopedObject<Path> Path::Dashed(const float* intervals,
                                uint32_t interval_count,
                                float phase) const {
  auto effect = SkDashPathEffect::Make({intervals, interval_count}, phase);
  if (!effect) {
    VALIDATION_LOG << "Invalid dash intervals.";
    return nullptr;
  }
  SkStrokeRec rec(SkStrokeRec::kHairline_InitStyle);
  SkPathBuilder builder;
  if (!effect->filterPath(&builder, GetPath(), &rec)) {
    return nullptr;
  }
  return Create<Path>(builder.detach());
}

}  // namespace impeller::interop
