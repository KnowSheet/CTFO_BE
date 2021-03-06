/*******************************************************************************
The MIT License (MIT)

Copyright (c) 2015 Maxim Zhurovich <zhurovich@gmail.com>
          (c) 2015 Dmitry "Dima" Korolev <dmitry.korolev@gmail.com>

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*******************************************************************************/

// Enable "$REST/data/user?export" unconditionally.
#define CURRENT_ALLOW_STORAGE_EXPORT_FROM_MASTER

#include "ctfo_server.h"

#include "../Current/Bricks/dflags/dflags.h"

using namespace CTFO;

DEFINE_int32(rand_seed, 42, "The answer to the question of life, universe and everything.");
DEFINE_string(config_file, "ctfo_config.json", "The file to read CTFOServerParams in JSON format from.");
DEFINE_bool(helpconfig, false, "Display the config format information.");

int main(int argc, char **argv) {
  ParseDFlags(&argc, &argv);
  if (FLAGS_helpconfig) {
    current::reflection::StructSchema schema;
    schema.AddType<CTFOServerParams>();
    std::cout << schema.GetSchemaInfo().Describe<current::reflection::Language::FSharp>(false);
  } else {
    current::random::SetRandomSeed(FLAGS_rand_seed);
    CTFOServer(FLAGS_config_file).Join();
  }
  return 0;
}
