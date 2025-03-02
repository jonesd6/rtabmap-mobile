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

#ifndef TANGO_GL_GRID_H_
#define TANGO_GL_GRID_H_

#include "tango-gl/line.h"

namespace tango_gl {
class Grid : public Line {
 public:
  Grid(float density = 0.01f, int qx = 50, int qy = 70);
};
}  // namespace tango_gl
#endif  // TANGO_GL_GRID_H_
