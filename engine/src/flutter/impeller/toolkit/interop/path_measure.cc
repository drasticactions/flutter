// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "impeller/toolkit/interop/path_measure.h"

#include "flutter/third_party/skia/include/core/SkPathBuilder.h"

namespace impeller::interop {

PathMeasure::PathMeasure(const Path& path, bool force_closed)
    : iter_(path.GetPath(), force_closed), contour_(iter_.next()) {}

PathMeasure::~PathMeasure() = default;

float PathMeasure::GetLength() const {
  return contour_ ? contour_->length() : 0.0f;
}

bool PathMeasure::GetPositionAndTangent(float distance,
                                        Point& out_position,
                                        Point& out_tangent) const {
  if (!contour_) {
    return false;
  }
  SkPoint position;
  SkVector tangent;
  if (!contour_->getPosTan(distance, &position, &tangent)) {
    return false;
  }
  out_position = Point{position.fX, position.fY};
  out_tangent = Point{tangent.fX, tangent.fY};
  return true;
}

ScopedObject<Path> PathMeasure::CreateSegment(float start,
                                              float stop,
                                              bool start_with_move_to) const {
  if (!contour_) {
    return nullptr;
  }
  SkPathBuilder builder;
  if (!contour_->getSegment(start, stop, &builder, start_with_move_to)) {
    return nullptr;
  }
  return Create<Path>(builder.detach());
}

bool PathMeasure::NextContour() {
  contour_ = iter_.next();
  return contour_ != nullptr;
}

}  // namespace impeller::interop
