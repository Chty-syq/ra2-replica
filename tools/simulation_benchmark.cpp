#include "simulation/world.h"
#include <algorithm>
#include <chrono>
#include <iostream>
#include <vector>

int main(int argc, char** argv) {
  try {
    if (argc != 2) throw std::runtime_error("Usage: ra2_simulation_benchmark <settings-directory>");
    const auto definitions = ra2::DefinitionRegistry::load(argv[1]);
    using Clock = std::chrono::steady_clock;
    std::cout << "entities,scenario,ticks,mean_ms,p95_ms,settled,state_hash\n";
    for (const int count : {100,1000,5000}) for (const bool moving : {false,true}) {
      ra2::World world(definitions);
      for (int i=0; i<count; ++i) {
        const TileCoord cell{(i % 100) * 5, (i / 100) * 5};
        const auto id = world.spawn("HTNK", cell);
        if (!id) throw std::runtime_error("Benchmark spawn failed");
        if (moving) world.enqueue({1,0,static_cast<std::uint64_t>(i+1),ra2::CommandKind::Move,id,0,{cell.x+3,cell.y+2}});
      }
      std::vector<double> samples;
      double total = 0;
      for (int frame=0; frame<300; ++frame) {
        const auto start = Clock::now(); world.step();
        const double ms = std::chrono::duration<double,std::milli>(Clock::now()-start).count();
        samples.push_back(ms); total += ms;
      }
      std::sort(samples.begin(),samples.end());
      const auto views = world.snapshot();
      const auto settled = std::count_if(views.begin(),views.end(),[](const auto& view) { return view.activity == ra2::Activity::Idle; });
      std::cout << count << ',' << (moving ? "move_commands" : "idle") << ",300," << total/300 << ',' << samples[285] << ',' << settled << ',' << world.stateHash() << '\n';
    }
    return 0;
  } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
