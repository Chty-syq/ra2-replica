#include "simulation/world.h"
#include "shp_ts.h"
#include "test_support.h"
#include <algorithm>
#include <iostream>
using namespace ra2;
namespace {
std::uint64_t seq=0;
std::optional<EntitySnapshot> get(const World& w,EntityId id){for(const auto& e:w.snapshot())if(e.id==id)return e;return {};}
void run(World& w,int frames){while(frames-->0)w.step();}
template<class P>void until(World& w,P p,const char* message,int limit=3000){for(int i=0;i<limit && !p();++i)w.step();if(!p()){std::cerr<<"Failure tick "<<w.tick()<<"\n";for(const auto& e:w.snapshot()){std::cerr<<e.id<<" "<<e.type<<" pos "<<e.position.x<<","<<e.position.y<<" z "<<e.heightLeptons<<" hp "<<e.hp<<"\n";for(const auto& slot:e.spawns)std::cerr<<" slot "<<slot.unit<<" phase "<<int(slot.phase)<<" ready "<<slot.ready<<"\n";}}require(p(),message);}
void order(World& w,CommandKind kind,EntityId actor,EntityId target=0,TileCoord cell={},std::uint32_t player=0,std::string type={}){require(w.enqueue({w.tick()+1,player,++seq,kind,actor,target,cell,type}),"Command rejected");}
void water(World& w){TerrainSurface s;s.land="WATER";for(int x=-4;x<=40;++x)for(int y=-4;y<=20;++y)w.setTerrain({x,y},s);}
DefinitionRegistry fixture(const DefinitionRegistry& base,std::string extra={}){auto r=base.rules(),p=base.replica();for(const auto& [id,t]:base.units()){(void)t;r.overlay(IniDocument::parse("["+id+"]\nCanPassiveAquire=no\n"));}r.overlay(IniDocument::parse(extra));p.overlay(IniDocument::parse("[Simulation]\nProductionFramesOverride=4\n"));return DefinitionRegistry::compile(r,base.art(),p);}
void definitions(const DefinitionRegistry& d,const std::filesystem::path& root){
 for(const auto id:{"DEST","CARRIER","DRED","DLPH","SQD","HORNET","ASW","DMISL"}){const auto& t=d.unit(id);for(const auto& reason:t.unsupported)std::cerr<<id<<": "<<reason<<'\n';require(t.support()!=SupportStatus::Unsupported,"Special navy/child unsupported");}
 require(d.unit("DEST").sensorsRange==8000 && d.unit("DEST").navalSpawn->type=="ASW","Destroyer lost native sonar/ASW aircraft");
 require(d.unit("CARRIER").navalSpawn->count==3 && d.unit("CARRIER").navalSpawn->reloadFrames==150 && d.unit("CARRIER").navalSpawn->regenerateFrames==600,"Carrier native spawn count/timers lost");
 require(d.unit("DRED").navalSpawn->count==2 && d.missileFor("DMISL").damage==300 && d.missileFor("DMISL").pauseFrames==20 && d.missileFor("DMISL").bodyLength==128,"Dreadnought reused V3 constants");
 require(d.unit("DLPH").spriteVehicle->bodyFrames==116 && d.unit("SQD").spriteVehicle->bodyFrames==296 && d.unit("SQD").leap->naval,"Native sea creature body/NoShadow layout lost");
 require(d.weapon("SONICZAP").sonic && d.weapon("SONICZAP").ambientDamage==10 && d.warhead("PARASITEPLUS").culling,"Sonic/parasite abilities discarded");
 for(const auto icon:{"desticon","carricon","dredicon","dlphicon","sqdicon"}){const auto frame=ShpTsFile::load(root/"icons/cameo/vehicles"/(std::string(icon)+".shp")).decodeFrame(0);require(frame.width==60 && frame.height==48,"Native special navy cameo missing");}
}
void carrier(const DefinitionRegistry& base){const auto d=fixture(base,"[HYD]\nStrength=10000\n");World w(d);water(w);const auto parent=w.spawn("CARRIER",{1,2}),target=w.spawn("HYD",{12,2},1);require(parent && target,"Carrier fixture failed");order(w,CommandKind::AttackEntity,parent,target);until(w,[&]{int n=0;for(auto& e:w.snapshot())n+=e.type=="HORNET";return n==3;},"Carrier did not launch three Hornets");require(get(w,target)->hp==10000,"Virtual launcher damaged target before aircraft arrived");
 until(w,[&]{return get(w,target)->hp<10000;},"Carrier aircraft did not deliver bomb");bool reloading=false;until(w,[&]{const auto dock=get(w,parent);for(const auto& s:dock->spawns)reloading|=s.phase==NavalSpawnPhase::Reloading;return reloading;},"Carrier aircraft failed to return/reload");order(w,CommandKind::Stop,parent);run(w,400);for(const auto& e:w.snapshot())require(e.type!="HORNET","Stop left aircraft attacking or orbiting forever");
 const auto hp=get(w,target)->hp;run(w,200);require(get(w,target)->hp==hp,"Stopped carrier relaunched aircraft");
}
void aircraftFlightLevel(const DefinitionRegistry& base) {
 for(const int expected:{1500,960}) {
  const auto d=fixture(base,"[HORNET]\nFlightLevel="+std::to_string(expected)+"\n");World w(d);water(w);
  const auto parent=w.spawn("CARRIER",{1,2}),target=w.spawn("HYD",{20,2},1);order(w,CommandKind::AttackEntity,parent,target);
  int maximum=0;for(int i=0;i<120;++i){w.step();for(const auto& e:w.snapshot())if(e.type=="HORNET")maximum=std::max(maximum,e.heightLeptons);}
  require(maximum==expected,"Carrier aircraft used JumpjetControls.CruiseHeight instead of Aircraft FlightLevel");
 }
}
void destroyer(const DefinitionRegistry& base){const auto d=fixture(base,"[SUB]\nStrength=10000\n");World w(d);water(w);const auto parent=w.spawn("DEST",{1,2}),target=w.spawn("SUB",{7,2},1);run(w,30);order(w,CommandKind::AttackEntity,parent,target);until(w,[&]{for(const auto& e:w.snapshot())if(e.type=="ASW")return true;return false;},"Destroyer did not use ASW against submerged target");until(w,[&]{return get(w,target)->hp<10000;},"ASW depth charge failed to damage submarine");
 World gun(d);water(gun);const auto ship=gun.spawn("DEST",{1,2}),victim=gun.spawn("HYD",{6,2},1);order(gun,CommandKind::AttackEntity,ship,victim);until(gun,[&]{return get(gun,victim)->hp<400;},"Destroyer 155mm cannon failed");for(const auto& e:gun.snapshot())require(e.type!="ASW","Destroyer used ASW against ordinary surface ship");
}
void dread(const DefinitionRegistry& base){const auto d=fixture(base,"[HYD]\nStrength=10000\n");World w(d);water(w);const auto ship=w.spawn("DRED",{15,2}),target=w.spawn("HYD",{1,2},1);order(w,CommandKind::AttackEntity,ship,target);until(w,[&]{int n=0;for(const auto& e:w.snapshot())n+=e.type=="DMISL";return n==2;},"Dreadnought did not prepare two missiles");require(get(w,target)->hp==10000,"Virtual DredLauncher damaged target");until(w,[&]{for(const auto& e:w.snapshot())if(e.type=="DMISL" && !e.takingOff)return true;return false;},"Dread missiles never launched");bool turned=false;for(const auto& e:w.snapshot())if(e.type=="DMISL" && !e.takingOff)turned|=e.heading!=0;require(turned && get(w,ship)->heading==0,"Omni-fired Dread missile flew backward without turning toward target");order(w,CommandKind::Stop,ship);until(w,[&]{return get(w,target)->hp<10000;},"Launched missiles disappeared when parent stopped");
 World minimum(d);water(minimum);const auto p=minimum.spawn("DRED",{1,2}),t=minimum.spawn("HYD",{4,2},1);order(minimum,CommandKind::AttackEntity,p,t);run(minimum,100);for(const auto& e:minimum.snapshot())require(e.type!="DMISL","Dreadnought ignored eight-cell minimum range");
}
void groundBombardment(const DefinitionRegistry& base){const auto d=fixture(base,"[HYD]\nStrength=10000\n");for(const auto type:{"CARRIER","DRED"}){World w(d);water(w);const auto parent=w.spawn(type,{1,2}),target=w.spawn("HYD",{15,2},1);order(w,CommandKind::AttackGround,parent,0,{15,2});until(w,[&]{return get(w,target)->hp<10000;},"Naval spawner ignored force-fire ground command");order(w,CommandKind::Stop,parent);}}
void creatures(const DefinitionRegistry& base){const auto d=fixture(base);World w(d);water(w);const auto squid=w.spawn("SQD",{3,3}),victim=w.spawn("HYD",{4,3},1),rescue=w.spawn("DLPH",{1,3},1);run(w,35);order(w,CommandKind::AttackEntity,squid,victim);until(w,[&]{return get(w,victim)->grappled;},"Squid did not grapple a ship");const auto initial=get(w,victim)->hp;const auto point=get(w,victim)->position;order(w,CommandKind::Move,victim,0,{10,3},1);run(w,20);require(get(w,victim)->position==point && get(w,victim)->hp<initial,"Grappled ship moved or took no continuous damage");order(w,CommandKind::AttackEntity,rescue,victim,{},1);until(w,[&]{return !get(w,victim)->grappled;},"Dolphin sonic did not free allied ship");require(get(w,squid) && !get(w,squid)->parasiteHost,"Released squid disappeared or retained host link");
 World cull(d);water(cull);const auto a=cull.spawn("SQD",{3,3}),b=cull.spawn("HYD",{4,3},1);order(cull,CommandKind::AttackEntity,a,b);until(cull,[&]{return !get(cull,b);},"Squid did not sink/cull captive");require(get(cull,a) && !get(cull,a)->parasiteHost,"Squid did not exit sunk ship");
 World organic(d);water(organic);const auto s=organic.spawn("SQD",{3,3}),animal=organic.spawn("DLPH",{4,3},1);order(organic,CommandKind::AttackEntity,s,animal);until(organic,[&]{return get(organic,animal)->hp<200;},"Squid did not punch organic target");require(!get(organic,animal)->grappled && !get(organic,s)->parasiteHost,"Squid grappled an organic target");
}
void rescueEscape(const DefinitionRegistry& base){const auto d=fixture(base,"[SQD]\nCanPassiveAquire=yes\n");World w(d);water(w);w.spawn("SQD",{3,3});const auto ship=w.spawn("HYD",{4,3},1),dolphin=w.spawn("DLPH",{1,3},1);until(w,[&]{return get(w,ship)->grappled;},"Native squid guard did not acquire nearby ship");order(w,CommandKind::AttackEntity,dolphin,ship,{},1);until(w,[&]{return !get(w,ship)->grappled;},"Escape setup was not rescued");const auto point=get(w,ship)->position;order(w,CommandKind::Move,ship,0,{12,3},1);run(w,20);require(!get(w,ship)->grappled && get(w,ship)->position!=point,"Rescued ship was immediately re-grappled before it could escape");}
void lossesAndInterception(const DefinitionRegistry& base) {
 const auto d=fixture(base,"[AEGIS]\nPrimary=TestKillAA\n[TestKillAA]\nDamage=10000\nROF=1000\nRange=20\nProjectile=MedusaProjectile\nSpeed=1000\nWarhead=Super\nOmniFire=yes\n[HYD]\nStrength=10000\nPrimary=TestKill\n[TestKill]\nDamage=10000\nROF=1000\nRange=30\nProjectile=InvisibleLow\nSpeed=100\nWarhead=Super\nOmniFire=yes\n");
 World w(d);water(w);const auto carrier=w.spawn("CARRIER",{1,2}),target=w.spawn("HYD",{15,2},1),aa=w.spawn("AEGIS",{4,4},1);order(w,CommandKind::AttackEntity,carrier,target);
 EntityId child=0;until(w,[&]{for(const auto& e:w.snapshot())if(e.type=="HORNET"){child=e.id;return true;}return false;},"Loss fixture failed");
 order(w,CommandKind::AttackEntity,aa,child,{},1);until(w,[&]{return !get(w,child);},"Spawned aircraft cannot be targeted by AA");const auto deadAt=w.tick();
 const auto c=get(w,carrier);require(c->spawns[0].phase==NavalSpawnPhase::Regenerating && c->spawns[0].ready==deadAt+600,"Aircraft loss did not use native regeneration timer");
 run(w,599);const auto before=get(w,carrier);require(!before->spawns[0].unit,"Dead aircraft regenerated early");w.step();require(get(w,carrier)->spawns[0].phase!=NavalSpawnPhase::Regenerating,"Aircraft did not regenerate after native timer");
 until(w,[&]{return get(w,carrier)->spawns[0].unit!=0;},"Regenerated aircraft failed to leave the next launch slot",12);
 order(w,CommandKind::AttackEntity,target,carrier,{},1);until(w,[&]{return !get(w,carrier);},"Parent kill fixture failed");run(w,2);for(const auto& e:w.snapshot())require(e.type!="HORNET","Carrier death left orphan aircraft");
 World missile(d);water(missile);const auto dread=missile.spawn("DRED",{1,2}),victim=missile.spawn("HYD",{15,2},1),interceptor=missile.spawn("AEGIS",{3,5},1);order(missile,CommandKind::AttackEntity,dread,victim);
 EntityId rocket=0;until(missile,[&]{for(const auto& e:missile.snapshot())if(e.type=="DMISL" && !e.takingOff){rocket=e.id;return true;}return false;},"Interception fixture failed");
 order(missile,CommandKind::Stop,dread);order(missile,CommandKind::AttackEntity,interceptor,rocket,{},1);until(missile,[&]{return !get(missile,rocket);},"Launched Dread missile could not be intercepted");
 run(missile,500);require(get(missile,victim)->hp==10000,"Intercepted missile still delivered its warhead");
}
void productionAndSonar(const DefinitionRegistry& base) {
 const auto d=fixture(base);for(const auto id:{"DEST","CARRIER","DLPH","DRED","SQD"}) {
  World w(d);water(w);w.spawn("GAPOWR",{42,1});const auto yard=w.spawn(id==std::string_view("DRED") || id==std::string_view("SQD")?"NAYARD":"GAYARD",{3,3});order(w,CommandKind::Produce,yard,0,{},0,id);
  until(w,[&]{for(const auto& e:w.snapshot())if(e.type==id)return true;return false;},"Special navy could not leave original shipyard");
 }
 World cloak(d);water(cloak);const auto dolphin=cloak.spawn("DLPH",{1,1}),squid=cloak.spawn("SQD",{12,1},1);run(cloak,30);
 require(get(cloak,dolphin)->submerged && !get(cloak,squid)->submerged,"CloakingSpeed should be frames per stage (low is faster)");run(cloak,40);require(get(cloak,squid)->submerged && !cloak.recognizes(squid,0),"Squid did not fully submerge/hide");
 order(cloak,CommandKind::Move,dolphin,squid,{9,1});until(cloak,[&]{return cloak.recognizes(squid,0);},"Dolphin sonar did not detect nearby squid");
 const auto notSandbox=[&]{auto p=base.replica();p.overlay(IniDocument::parse("[Simulation]\nSandbox=no\n"));return DefinitionRegistry::compile(base.rules(),base.art(),p);}();
 World tech(notSandbox);water(tech);tech.spawn("GAPOWR",{42,1});const auto dock=tech.spawn("GAYARD",{3,3});const auto credits=tech.credits();order(tech,CommandKind::Produce,dock,0,{},0,"CARRIER");tech.step();require(!get(tech,dock)->queueSize && tech.credits()==credits,"Carrier ignored technology prerequisites or charged rejected order");
 tech.spawn("GATECH",{42,5});order(tech,CommandKind::Produce,dock,0,{},0,"CARRIER");tech.step();require(get(tech,dock)->queueSize==1 && tech.credits()==credits-2000,"Carrier native technology/cost gate incorrect");
}

void replay(const DefinitionRegistry& base){const auto d=fixture(base,"[HYD]\nStrength=10000\n");World a(d),b(d);for(auto* w:{&a,&b}){water(*w);const auto ship=w->spawn("CARRIER",{1,2}),target=w->spawn("HYD",{14,2},1);w->enqueue({1,0,1,CommandKind::AttackEntity,ship,target});w->spawn("DRED",{2,6});w->enqueue({2,0,2,CommandKind::AttackEntity,3,target});}for(int i=0;i<500;++i){a.step();b.step();require(a.stateHash()==b.stateHash(),"Special navy replay diverged");}}
}
int main(int argc,char** argv){try{require(argc==2,"settings path required");const auto d=DefinitionRegistry::load(argv[1]);definitions(d,std::filesystem::path(argv[1]).parent_path());carrier(d);aircraftFlightLevel(d);destroyer(d);dread(d);groundBombardment(d);creatures(d);rescueEscape(d);lossesAndInterception(d);productionAndSonar(d);replay(d);std::cout<<"Special navy regressions passed\n";}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
