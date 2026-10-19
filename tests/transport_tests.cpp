#include "simulation/world.h"
#include "test_support.h"
#include <algorithm>
#include <iostream>
#include <set>
using namespace ra2;
namespace {
std::optional<EntitySnapshot> find(const World& w,EntityId id) {for(const auto& e:w.snapshot())if(e.id==id)return e;return {};}
void run(World& w,int n){while(n-->0)w.step();}
template<class Predicate> void until(World& w,Predicate predicate,const char* message,int limit=1200) {
  for(int i=0;i<limit && !predicate();++i)w.step();require(predicate(),message);
}
DefinitionRegistry fixture(const DefinitionRegistry& base,std::string extra={}) {
  auto rules=base.rules();
  rules.overlay(IniDocument::parse("[E1]\nCanPassiveAquire=no\n[E2]\nCanPassiveAquire=no\n[FV]\nCanPassiveAquire=no\n[HTK]\nCanPassiveAquire=no\n[SNIPE]\nCanPassiveAquire=no\n[HTNK]\nCanPassiveAquire=no\n[GAREFN]\nFreeUnit=none\n"+extra));
  return DefinitionRegistry::compile(rules,base.art(),base.replica());
}
void capacityAndTravel(const DefinitionRegistry& base) {
  const auto d=fixture(base);World w(d);const auto host=w.spawn("HTK",{5,0});std::vector<EntityId> infantry;
  for(int i=0;i<6;++i){const auto id=w.spawn(i&1?"E1":"E2",{0,i/3});infantry.push_back(id);w.enqueue({1,0,static_cast<unsigned>(i+1),CommandKind::EnterTransport,id,host});}
  run(w,400);require(find(w,host)->cargo->used==5,"Transport capacity was ignored");
  int loaded=0;for(auto id:infantry)if(find(w,id)->containedBy) {
    ++loaded;require(find(w,id)->hp==125 && find(w,id)->containedBy==host,"Boarding lost identity or health");
    for(int x=-1;x<8;++x)for(int y=-1;y<4;++y) {const auto occupants=w.occupants({x,y});require(std::find(occupants.begin(),occupants.end(),id)==occupants.end(),"Contained passenger retained ground occupancy");}
  }
  require(loaded==5,"Too many infantry boarded one carrier");
  w.enqueue({w.tick()+1,0,7,CommandKind::Move,host,0,{10,3}});run(w,350);
  require(find(w,host)->cell==TileCoord{10,3},"Loaded carrier could not move");
  for(auto id:find(w,host)->cargo->passengers)require(find(w,id)->position==find(w,host)->position,"Passenger did not follow its moving host");
  w.enqueue({w.tick()+1,0,8,CommandKind::Unload,host});run(w,150);
  require(find(w,host)->cargo->passengers.empty(),"Unload lost or stranded a passenger");
  std::map<std::pair<int,int>,int> slots;
  for(auto id:infantry) {const auto e=find(w,id);require(e && !e->containedBy && e->hp==125,"Unload duplicated, killed or left hidden infantry");require(++slots[{e->cell.x,e->cell.y}]<=3,"Unload exceeded subcell capacity");}
  require(w.snapshot().size()==7,"Board/unload changed the entity count");
}
void blockedUnloadAndCancellation(const DefinitionRegistry& base) {
  const auto d=fixture(base);World w(d);const auto host=w.spawn("HTK",{4,0}),actor=w.spawn("ENGINEER",{});
  w.enqueue({1,0,1,CommandKind::EnterTransport,actor,host});until(w,[&]{return find(w,actor)->containedBy==host;},"Engineer could not board");
  for(auto cell:{TileCoord{3,0},{4,-1},{5,0},{4,1}})w.setBuildable(cell,false);
  w.enqueue({w.tick()+1,0,2,CommandKind::Deploy,host});run(w,40);
  require(find(w,host)->cargo->unloading && find(w,actor)->containedBy==host,"Blocked unload deleted its passenger");
  w.setBuildable({3,0},true);run(w,20);require(!find(w,actor)->containedBy && !find(w,host)->cargo->unloading,"Blocked unload did not retry");
  const auto next=w.spawn("ENGINEER",{});w.enqueue({w.tick()+1,0,3,CommandKind::EnterTransport,next,host});
  until(w,[&]{const auto e=find(w,next);return e->buildingInteraction && e->buildingInteraction->entering;},"Boarding never reached entry");
  w.enqueue({w.tick()+1,0,4,CommandKind::Stop,next});run(w,40);
  require(!find(w,next)->containedBy && !find(w,next)->buildingInteraction,"Cancelled boarding persisted");
  w.enqueue({w.tick()+1,0,5,CommandKind::Move,next,0,{0,4}});run(w,220);require(find(w,next)->cell==TileCoord{0,4},"Cancelled passenger was stranded");
  World moving(d);const auto carrier=moving.spawn("HTK",{4,0}),follower=moving.spawn("ENGINEER",{});
  moving.enqueue({1,0,1,CommandKind::EnterTransport,follower,carrier});run(moving,12);
  moving.enqueue({moving.tick()+1,0,2,CommandKind::Move,carrier,0,{8,0}});run(moving,650);
  require(find(moving,carrier)->cell==TileCoord{8,0} && find(moving,follower)->containedBy==carrier,"Boarding did not follow a moving carrier's new entrance");
}
void sizeOwnershipAndDeath(const DefinitionRegistry& base) {
  const auto d=fixture(base,"[E1]\nSize=2\n[E2]\nSize=3\n");World w(d);const auto host=w.spawn("HTK",{5,0});
  const auto a=w.spawn("E1",{}),b=w.spawn("E1",{}),c=w.spawn("E1",{}),large=w.spawn("E2",{0,1}),enemy=w.spawn("ENGINEER",{0,2},1);
  int sequence=0;for(auto id:{a,b,c,large})w.enqueue({1,0,static_cast<unsigned>(++sequence),CommandKind::EnterTransport,id,host});
  w.enqueue({1,1,1,CommandKind::EnterTransport,enemy,host});run(w,400);
  require(find(w,host)->cargo->used==4 && !find(w,large)->containedBy && !find(w,enemy)->containedBy,"Capacity, SizeLimit or ownership was ignored");
  const auto lethal=fixture(base,"[M60]\nDamage=100000\n");World destroyed(lethal);
  const auto passenger=destroyed.spawn("ENGINEER",{}),carrier=destroyed.spawn("HTK",{4,0}),gun=destroyed.spawn("E1",{4,-3},1);
  destroyed.enqueue({1,0,1,CommandKind::EnterTransport,passenger,carrier});until(destroyed,[&]{return find(destroyed,passenger)->containedBy==carrier;},"Death fixture did not board");
  destroyed.enqueue({destroyed.tick()+1,1,1,CommandKind::AttackEntity,gun,carrier});run(destroyed,150);
  require(!find(destroyed,carrier) && !find(destroyed,passenger) && destroyed.infantryCorpses().empty(),"Destroyed carrier left a hidden passenger or ground corpse");
}
void garrison(const DefinitionRegistry& base) {
  const auto d=fixture(base,"[E2]\nStrength=100000\nFearless=yes\n");World w(d);const auto house=w.spawn("CAHSE01",{4,0},2),a=w.spawn("E1",{}),b=w.spawn("E1",{});
  run(w,10);require(w.shots().empty() && find(w,house)->activeWeapon.empty(),"Empty neutral house fired a weapon");
  w.enqueue({w.tick()+1,0,1,CommandKind::EnterBuilding,a,house});w.enqueue({w.tick()+1,0,2,CommandKind::EnterBuilding,b,house});run(w,250);
  require(find(w,house)->owner==0 && find(w,house)->cargo->used==2 && find(w,a)->containedBy==house && find(w,b)->containedBy==house,"Garrison lost multiple simultaneous occupants");
  const auto enemy=w.spawn("E2",{8,0},1);int shots=0;std::set<EntityId> sources;
  for(int i=0;i<100;++i){w.step();for(const auto& shot:w.shots())if(shot.started==w.tick() && shot.sourceType=="CAHSE01"){++shots;require(shot.weaponId=="ALLIEDOCCUPYW","Garrison used the infantry's outside weapon");}}
  require(shots>=4 && find(w,enemy)->hp<100000,"Garrison occupants did not provide combined fire");
  w.enqueue({w.tick()+1,1,1,CommandKind::EnterBuilding,enemy,house});run(w,5);require(!find(w,enemy)->buildingInteraction,"Enemy infantry entered an occupied house");
  w.enqueue({w.tick()+1,0,3,CommandKind::Unload,house});run(w,100);
  require(find(w,house)->owner==2 && !find(w,a)->containedBy && !find(w,b)->containedBy && find(w,house)->activeWeapon.empty(),"Evacuated building retained owner or free firepower");
  const auto repair=w.spawn("ENGINEER",{});w.enqueue({w.tick()+1,0,4,CommandKind::EnterBuilding,repair,house});run(w,50);
  require(!find(w,repair)->containedBy,"Engineer was treated as a garrison occupant");
  World soviet(d);const auto building=soviet.spawn("CAHSE01",{3,0},2),conscript=soviet.spawn("E2",{},1);
  soviet.enqueue({1,1,1,CommandKind::EnterBuilding,conscript,building});run(soviet,250);
  require(find(soviet,building)->owner==1 && find(soviet,building)->activeWeapon=="SOVIETOCCUPYW","Soviet garrison selected Allied firepower");
  const auto passive=fixture(base,"[E1]\nCanPassiveAquire=yes\n");World neutral(passive);
  const auto houseId=neutral.spawn("CAHSE01",{2,0},2);neutral.spawn("E1",{});run(neutral,100);
  require(find(neutral,houseId)->hp==600,"Idle infantry automatically attacked a neutral empty house");
}
void forcedEvacuation(const DefinitionRegistry& base) {
  for(const bool lethal:{false,true}) {
    const auto d=fixture(base,std::string("[M60]\nDamage=")+(lethal?"100000":"475")+"\n[SA]\nVerses=100%,100%,100%,100%,100%,100%,100%,100%,100%,100%,100%\n");
    World w(d);const auto house=w.spawn("CAHSE01",{4,0},2),actor=w.spawn("E1",{}),gun=w.spawn("E1",{4,-2});
    w.enqueue({1,0,1,CommandKind::EnterBuilding,actor,house});until(w,[&]{return find(w,actor)->containedBy==house;},"Evacuation fixture failed");
    w.enqueue({w.tick()+1,0,2,CommandKind::AttackGround,gun,0,{4,0}});
    until(w,[&]{return !find(w,house) || find(w,house)->hp<=150;},"Demolition fixture did not hit house");
    w.enqueue({w.tick()+1,0,3,CommandKind::Stop,gun});run(w,80);
    require(find(w,actor) && !find(w,actor)->containedBy,"Damaged/destroyed garrison lost its evacuating occupant");
    if(!lethal)require(find(w,house)->cargo->passengers.empty() && find(w,house)->owner==2,"Red-health house retained garrison ownership");
  }
}
void ifvModes(const DefinitionRegistry& base) {
  const auto d=fixture(base);for(const auto passenger:{"E1","E2","SNIPE","ENGINEER","SENGINEER"}) {
    World w(d);const auto host=w.spawn("FV",{4,0});require(host,"IFV was not enabled: "+w.lastMessage());const auto actor=w.spawn(passenger,{});require(actor,"IFV passenger was not enabled");
    w.enqueue({1,0,1,CommandKind::EnterTransport,actor,host});run(w,250);const auto view=find(w,host);
    const auto mode=d.unit(passenger).ifvMode;require(find(w,actor)->containedBy==host && view->cargo->mode==mode && view->activeWeapon==d.unit("FV").weaponSlots[mode],"IFV did not select its passenger weapon");
    require(view->cargo->turret==(mode==1?2:1),"IFV selected the wrong original turret");
    w.enqueue({w.tick()+1,0,2,CommandKind::Unload,host});run(w,30);
    require(find(w,host)->cargo->mode==0 && find(w,host)->cargo->turret==0 && find(w,host)->activeWeapon=="HOVERMISSILE","IFV did not restore its empty form");
  }
  World rockets(d);const auto host=rockets.spawn("FV",{}),tank=rockets.spawn("HTNK",{4,0},1);
  rockets.enqueue({1,0,1,CommandKind::AttackEntity,host,tank});std::vector<Tick> times;
  for(int i=0;i<110;++i){rockets.step();for(const auto& shot:rockets.shots())if(shot.started==rockets.tick() && shot.sourceType=="FV")times.push_back(shot.started);}
  std::string timing;for(auto tick:times)timing+=" "+std::to_string(tick);
  require(times.size()>=4 && times[1]-times[0]==5 && times[2]-times[0]==50 && find(rockets,tank)->hp<400,"IFV missile burst, reload or impact failed: ticks"+timing+" hp="+std::to_string(find(rockets,tank)->hp));
}
void ifvRepair(const DefinitionRegistry& base) {
  for(const bool automatic:{false,true}) {
  const auto d=fixture(base,automatic?"[FV]\nCanPassiveAquire=yes\n":"");World w(d);const int x=automatic?1:3;
  const auto host=w.spawn("FV",{}),engineer=w.spawn("ENGINEER",{0,2}),tank=w.spawn("HTNK",{x,0}),gun=w.spawn("E1",{x,2});
  w.enqueue({1,0,1,CommandKind::AttackGround,gun,0,{x,0}});until(w,[&]{return find(w,tank)->hp<400;},"Repair target was never damaged");
  w.enqueue({w.tick()+1,0,2,CommandKind::Stop,gun});w.enqueue({w.tick()+1,0,3,CommandKind::EnterTransport,engineer,host});
  until(w,[&]{return find(w,engineer)->containedBy==host;},"Repair IFV did not load engineer");
  if(!automatic)w.enqueue({w.tick()+1,0,4,CommandKind::AttackEntity,host,tank});
  until(w,[&]{return find(w,tank)->hp==400;},automatic?"Engineer IFV lost automatic friendly repair":"Engineer IFV could not repair friendly vehicle");
  require(find(w,engineer) && find(w,engineer)->containedBy==host,"Repair IFV consumed its engineer");
  }
}
void replay(const DefinitionRegistry& base) {
  const auto d=fixture(base);std::uint64_t expected=0;
  for(const int hz:{30,60,144}) {
    World w(d);w.setGameSpeed({GameSpeedMode::Multiplayer,6});const auto host=w.spawn("HTK",{4,0}),actor=w.spawn("E1",{}),house=w.spawn("CAHSE01",{10,0},2);
    w.enqueue({1,0,1,CommandKind::EnterTransport,actor,host});w.enqueue({180,0,2,CommandKind::Move,host,0,{8,0}});w.enqueue({300,0,3,CommandKind::Unload,host});w.enqueue({360,0,4,CommandKind::EnterBuilding,actor,house});
    for(int frame=0;w.tick()<600;++frame){const auto a=static_cast<std::uint64_t>(frame)*1000000/hz,b=static_cast<std::uint64_t>(frame+1)*1000000/hz;w.advance(b-a,static_cast<std::uint32_t>(600-w.tick()));}
    if(expected)require(w.stateHash()==expected,"Transport/garrison replay depends on rendering cadence");else expected=w.stateHash();
  }
}
}
int main(int argc,char** argv) {
  try {require(argc==2,"Expected settings directory");const auto d=DefinitionRegistry::load(argv[1]);
    capacityAndTravel(d);blockedUnloadAndCancellation(d);sizeOwnershipAndDeath(d);garrison(d);forcedEvacuation(d);ifvModes(d);ifvRepair(d);replay(d);
    std::cout<<"Transport/garrison regressions passed\n";
  }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
