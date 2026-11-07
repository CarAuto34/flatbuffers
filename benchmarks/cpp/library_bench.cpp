// Benchmarks for the FlatBuffers runtime library: schema/JSON parsing, text
// generation, verification, reflection and FlexBuffers.

#include <benchmark/benchmark.h>

#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

#include "benchmarks/cpp/flatbuffers/bench_generated.h"
#include "flatbuffers/flatbuffers.h"
#include "flatbuffers/flexbuffers.h"
#include "flatbuffers/idl.h"
#include "flatbuffers/reflection.h"
#include "flatbuffers/util.h"

static const std::string kRootDir = FLATBUFFERS_BENCH_ROOT_DIR;
static const std::string kTestsDir = kRootDir + "/tests/";

static std::string LoadOrDie(const std::string& path) {
  std::string contents;
  if (!flatbuffers::LoadFile(path.c_str(), false, &contents)) {
    std::fprintf(stderr, "Failed to load %s\n", path.c_str());
    std::abort();
  }
  return contents;
}

// Shared test inputs: the monster schema and its JSON sample data.
struct MonsterInputs {
  MonsterInputs()
      : schema(LoadOrDie(kTestsDir + "monster_test.fbs")),
        json(LoadOrDie(kTestsDir + "monsterdata_test.json")) {
    include_dirs_storage.push_back(kTestsDir);
    include_dirs_storage.push_back(kTestsDir + "include_test/");
    for (const auto& dir : include_dirs_storage) {
      include_dirs.push_back(dir.c_str());
    }
    include_dirs.push_back(nullptr);
  }

  // Parses the schema into `parser`, aborting on failure.
  void ParseSchema(flatbuffers::Parser& parser) const {
    const std::string schema_path = kTestsDir + "monster_test.fbs";
    if (!parser.Parse(schema.c_str(), include_dirs.data(),
                      schema_path.c_str())) {
      std::fprintf(stderr, "Schema parse error: %s\n", parser.error_.c_str());
      std::abort();
    }
  }

  std::string schema;
  std::string json;
  std::vector<std::string> include_dirs_storage;
  mutable std::vector<const char*> include_dirs;
};

static const MonsterInputs& Inputs() {
  static const MonsterInputs inputs;
  return inputs;
}

// A parser with the monster schema loaded and the sample JSON parsed into
// its builder.
struct LoadedParser {
  LoadedParser() {
    Inputs().ParseSchema(parser);
    if (!parser.ParseJson(Inputs().json.c_str())) {
      std::fprintf(stderr, "JSON parse error: %s\n", parser.error_.c_str());
      std::abort();
    }
    binary.assign(parser.builder_.GetBufferPointer(),
                  parser.builder_.GetBufferPointer() +
                      parser.builder_.GetSize());

    // Binary schema (bfbs) used for reflection-based verification.
    flatbuffers::Parser schema_parser;
    Inputs().ParseSchema(schema_parser);
    schema_parser.Serialize();
    bfbs.assign(schema_parser.builder_.GetBufferPointer(),
                schema_parser.builder_.GetBufferPointer() +
                    schema_parser.builder_.GetSize());
  }

  flatbuffers::Parser parser;
  std::vector<uint8_t> binary;
  std::vector<uint8_t> bfbs;
};

static LoadedParser& Loaded() {
  static LoadedParser loaded;
  return loaded;
}

// Parse the monster_test.fbs schema (with its includes).
static void BM_Parser_ParseSchema(benchmark::State& state) {
  const MonsterInputs& inputs = Inputs();
  for (auto _ : state) {
    flatbuffers::Parser parser;
    inputs.ParseSchema(parser);
    benchmark::DoNotOptimize(parser.structs_.vec.size());
  }
}
BENCHMARK(BM_Parser_ParseSchema);

// Parse the monster sample JSON into a binary FlatBuffer.
static void BM_Parser_ParseJson(benchmark::State& state) {
  flatbuffers::Parser& parser = Loaded().parser;
  const std::string& json = Inputs().json;
  for (auto _ : state) {
    bool ok = parser.ParseJson(json.c_str());
    benchmark::DoNotOptimize(ok);
    benchmark::DoNotOptimize(parser.builder_.GetBufferPointer());
    benchmark::ClobberMemory();
  }
}
BENCHMARK(BM_Parser_ParseJson);

// Serialize a parsed schema to the binary schema format (bfbs).
static void BM_Parser_SerializeSchema(benchmark::State& state) {
  flatbuffers::Parser parser;
  Inputs().ParseSchema(parser);
  for (auto _ : state) {
    parser.Serialize();
    benchmark::DoNotOptimize(parser.builder_.GetBufferPointer());
    benchmark::ClobberMemory();
  }
}
BENCHMARK(BM_Parser_SerializeSchema);

// Convert a binary monster FlatBuffer back to JSON text.
static void BM_GenText_Monster(benchmark::State& state) {
  LoadedParser& loaded = Loaded();
  for (auto _ : state) {
    std::string text;
    const char* err = flatbuffers::GenText(loaded.parser, loaded.binary.data(),
                                           &text);
    benchmark::DoNotOptimize(err);
    benchmark::DoNotOptimize(text.data());
  }
}
BENCHMARK(BM_GenText_Monster);

// Verify a binary monster FlatBuffer through reflection.
static void BM_Reflection_VerifyMonster(benchmark::State& state) {
  LoadedParser& loaded = Loaded();
  const reflection::Schema* schema = reflection::GetSchema(loaded.bfbs.data());
  const reflection::Object* root = schema->root_table();
  if (!flatbuffers::Verify(*schema, *root, loaded.binary.data(),
                           loaded.binary.size())) {
    state.SkipWithError("monster buffer failed verification");
    return;
  }
  for (auto _ : state) {
    bool ok = flatbuffers::Verify(*schema, *root, loaded.binary.data(),
                                  loaded.binary.size());
    benchmark::DoNotOptimize(ok);
  }
}
BENCHMARK(BM_Reflection_VerifyMonster);

// Verify the benchmark FooBarContainer buffer with the generated verifier.
static void BM_Flatbuffers_Verify(benchmark::State& state) {
  using namespace benchmarks_flatbuffers;
  flatbuffers::FlatBufferBuilder fbb;
  std::vector<flatbuffers::Offset<FooBar>> list;
  for (int i = 0; i < 64; ++i) {
    Foo foo(0xABADCAFEABADCAFE + i, static_cast<int16_t>(10000 + i),
            static_cast<int8_t>('@' + i), static_cast<uint32_t>(1000000 + i));
    Bar bar(foo, 123456 + i, 3.14159f + i, static_cast<uint16_t>(10000 + i));
    auto name = fbb.CreateString("Hello, World!");
    list.push_back(CreateFooBar(fbb, &bar, name, 3.1415432432445543543 + i,
                                static_cast<uint8_t>('!' + i)));
  }
  auto location = fbb.CreateString("http://google.com/flatbuffers/");
  auto vec = fbb.CreateVector(list);
  fbb.Finish(CreateFooBarContainer(fbb, vec, true, Enum_Bananas, location));
  {
    flatbuffers::Verifier verifier(fbb.GetBufferPointer(), fbb.GetSize());
    if (!VerifyFooBarContainerBuffer(verifier)) {
      state.SkipWithError("FooBarContainer buffer failed verification");
      return;
    }
  }

  for (auto _ : state) {
    flatbuffers::Verifier verifier(fbb.GetBufferPointer(), fbb.GetSize());
    bool ok = VerifyFooBarContainerBuffer(verifier);
    benchmark::DoNotOptimize(ok);
  }
}
BENCHMARK(BM_Flatbuffers_Verify);

static void BuildFlexBuffer(flexbuffers::Builder& fbb, int count) {
  fbb.Map([&]() {
    fbb.Vector("vec", [&]() {
      for (int i = 0; i < count; ++i) {
        fbb.Map([&]() {
          fbb.Int("id", i);
          fbb.Double("rating", 3.14159 * i);
          fbb.String("name", "Hello, World!");
          fbb.Bool("flag", (i & 1) != 0);
        });
      }
    });
    fbb.String("location", "http://google.com/flatbuffers/");
    fbb.Int("fruit", 2);
  });
  fbb.Finish();
}

// Build a FlexBuffer map containing a vector of maps.
static void BM_FlexBuffers_Build(benchmark::State& state) {
  const int count = static_cast<int>(state.range(0));
  for (auto _ : state) {
    flexbuffers::Builder fbb;
    BuildFlexBuffer(fbb, count);
    benchmark::DoNotOptimize(fbb.GetBuffer().data());
  }
}
BENCHMARK(BM_FlexBuffers_Build)->Arg(16)->Arg(256);

// Read every field back from a FlexBuffer built by BuildFlexBuffer.
static void BM_FlexBuffers_Read(benchmark::State& state) {
  const int count = static_cast<int>(state.range(0));
  flexbuffers::Builder fbb;
  BuildFlexBuffer(fbb, count);
  const std::vector<uint8_t>& buf = fbb.GetBuffer();
  for (auto _ : state) {
    auto root = flexbuffers::GetRoot(buf).AsMap();
    auto vec = root["vec"].AsVector();
    int64_t sum = root["fruit"].AsInt64();
    sum += static_cast<int64_t>(root["location"].AsString().length());
    for (size_t i = 0; i < vec.size(); ++i) {
      auto m = vec[i].AsMap();
      sum += m["id"].AsInt64();
      sum += static_cast<int64_t>(m["rating"].AsDouble());
      sum += static_cast<int64_t>(m["name"].AsString().length());
      sum += m["flag"].AsBool() ? 1 : 0;
    }
    benchmark::DoNotOptimize(sum);
  }
}
BENCHMARK(BM_FlexBuffers_Read)->Arg(16)->Arg(256);
