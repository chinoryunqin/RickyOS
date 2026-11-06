#pragma once
#include <ArduinoJson.h>
#include "ReaderTapZones.h"

namespace readerTapZones {
inline void save(JsonDocument& doc, const Config& input) {
  Config config = input;
  normalize(config);
  JsonObject object = doc["readerTapZones"].to<JsonObject>();
  object["version"] = 1;
  object["enabled"] = config.enabled;
  constexpr const char* keys[] = {"portrait", "landscape"};
  for (int b = 0; b < 2; ++b) {
    auto values = object[keys[b]].to<JsonArray>();
    for (auto a : config.layouts[b]) values.add(a);
  }
}
inline Config load(JsonVariantConst doc) {
  Config config;
  JsonObjectConst object = doc["readerTapZones"];
  if (object.isNull() || !object["version"].is<int>() || object["version"].as<int>() != 1) return config;
  constexpr const char* keys[] = {"portrait", "landscape"};
  for (int b = 0; b < 2; ++b) {
    JsonArrayConst values = object[keys[b]];
    if (values.isNull() || values.size() != 9) return Config{};
    for (int i = 0; i < 9; ++i) {
      if (!values[i].is<int>()) return Config{};
      const int v = values[i].as<int>();
      if (v < 0 || v >= value(Action::Count)) return Config{};
      config.layouts[b][i] = static_cast<uint8_t>(v);
    }
  }
  config.enabled = object["enabled"].is<bool>() && object["enabled"].as<bool>();
  normalize(config);
  return config;
}
}  // namespace readerTapZones
