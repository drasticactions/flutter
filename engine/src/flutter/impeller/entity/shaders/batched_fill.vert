// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <impeller/types.glsl>

// Positions are in clip space, with each entity's clip depth.
in vec3 position;
in mediump vec4 color;

out mediump vec4 v_color;

void main() {
  gl_Position = vec4(position, 1.0);
  v_color = color;
}
