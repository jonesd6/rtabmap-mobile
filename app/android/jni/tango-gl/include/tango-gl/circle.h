/*
 * Copyright 2014 Google Inc. All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef TANGO_GL_CIRCLE_H_
#define TANGO_GL_CIRCLE_H_

#include "tango-gl/mesh.h"

namespace tango_gl {
class Circle : public Mesh {
 public:
  Circle(float radius = 0.3f, int resolution = 10);
};
}  // namespace tango_gl
#endif  // TANGO_GL_CIRCLE_H_
