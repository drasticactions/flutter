// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_IMPELLER_TOOLKIT_INTEROP_PATH_MEASURE_H_
#define FLUTTER_IMPELLER_TOOLKIT_INTEROP_PATH_MEASURE_H_

#include "flutter/third_party/skia/include/core/SkContourMeasure.h"
#include "impeller/geometry/point.h"
#include "impeller/toolkit/interop/impeller.h"
#include "impeller/toolkit/interop/object.h"
#include "impeller/toolkit/interop/path.h"

namespace impeller::interop {

class PathMeasure final
    : public Object<PathMeasure,
                    IMPELLER_INTERNAL_HANDLE_NAME(ImpellerPathMeasure)> {
 public:
  PathMeasure(const Path& path, bool force_closed);

  ~PathMeasure();

  PathMeasure(const PathMeasure&) = delete;

  PathMeasure& operator=(const PathMeasure&) = delete;

  float GetLength() const;

  bool GetPositionAndTangent(float distance,
                             Point& out_position,
                             Point& out_tangent) const;

  ScopedObject<Path> CreateSegment(float start,
                                   float stop,
                                   bool start_with_move_to) const;

  bool NextContour();

 private:
  SkContourMeasureIter iter_;
  sk_sp<SkContourMeasure> contour_;
};

}  // namespace impeller::interop

#endif  // FLUTTER_IMPELLER_TOOLKIT_INTEROP_PATH_MEASURE_H_
