#pragma once
#include "config/definitions.h"

// Short production isolates queue/egress tests from cost-dependent rule timing.
// calibration_tests.cpp and the graphical simulation smoke test use real defaults.
inline ra2::DefinitionRegistry fastProductionFixture(const std::filesystem::path& settings) {
  const auto defs = ra2::DefinitionRegistry::load(settings);
  auto profile = defs.replica();
  profile.overlay(ra2::IniDocument::parse("[Simulation]\nProductionFramesOverride=120", "fast-production-fixture.ini"));
  return ra2::DefinitionRegistry::compile(defs.rules(),defs.art(),profile);
}
