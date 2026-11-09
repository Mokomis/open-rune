#pragma once
#include <array>
#include "RuneNavigation.h"
// Real-map authored-route contract checks. Does not rewrite map assets.
inline void RunRuneNavigationIntegrityProbe(Engine& game,UPawn* pawn) {
 auto check=[](bool value,const char* message){if(!value)throw std::runtime_error(std::string("navigation integrity: ")+message);};
 const auto& specs=game.Level->ReachSpecs;
 std::set<UNavigationPoint*> nodes;
 for(auto* n=game.LevelInfo->NavigationPointList();n && nodes.insert(n).second;n=n->nextNavigationPoint()){}
 std::map<UNavigationPoint*,std::vector<int>> outgoing;
 for(auto* n:nodes)for(int index:n->Paths()) {
  if(index<0)break;if(size_t(index)>=specs.size())continue;
  const auto& s=specs[index];
  if(s.startActor!=n || !nodes.contains(s.endActor) || s.bPruned || n->bPlayerOnly() || s.endActor->bPlayerOnly() || s.collisionRadius<pawn->CollisionRadius() || s.collisionHeight<pawn->CollisionHeight())continue;
  outgoing[n].push_back(index);
 }
 if(commandline->HasArg("","--navigation-safety")) {
  int index=-1;for(auto& [n,list]:outgoing)for(int i:list)if(specs[i].startActor!=specs[i].endActor){index=i;break;}
  check(index>=0 && nodes.size()>2,"safe graph fixture");auto edge=specs[index];auto* from=edge.startActor;auto* to=edge.endActor;
  struct Saved{UNavigationPoint* node;std::array<int,16> paths,upstream;bool end;};std::vector<Saved> saved;
  for(auto* n:nodes){Saved old{n,{},{},bool(n->bEndPoint())};for(int i=0;i<16;++i){old.paths[i]=n->Paths()[i];old.upstream[i]=n->upstreamPaths()[i];n->Paths()[i]=n->upstreamPaths()[i]=-1;}saved.push_back(old);}
  auto at=pawn->Location();pawn->Location()=vec3(100000,100000,100000);
  auto seed=[&](UNavigationPoint* node){for(auto* n:nodes)n->bEndPoint()=n==node;RuneNavigation::Reset();};
  // A bad positive slot must not hide the valid next slot.
  to->upstreamPaths()[0]=int(specs.size())+50;to->upstreamPaths()[1]=index;
  seed(from);auto valid=pawn->FindPathToEndPoint(to,1000);check(!valid.first.empty() && valid.second==edge.distance,"skip stale slot then use valid authored edge");
  // Nor may an array claim an edge belonging to a different destination.
  auto* wrong=*std::find_if(nodes.begin(),nodes.end(),[&](auto* n){return n!=to&&n!=from;});
  to->upstreamPaths()[0]=to->upstreamPaths()[1]=-1;wrong->upstreamPaths()[0]=index;
  seed(from);check(pawn->FindPathToEndPoint(wrong,1000).first.empty(),"reject mismatched endpoint");wrong->upstreamPaths()[0]=-1;
  to->upstreamPaths()[0]=index;game.Level->ReachSpecs[index].startActor=nullptr;
  seed(from);check(pawn->FindPathToEndPoint(to,1000).first.empty(),"reject missing endpoint without dereference");game.Level->ReachSpecs[index]=edge;
  to->upstreamPaths()[0]=-1;to->upstreamPaths()[1]=index;
  seed(from);check(pawn->FindPathToEndPoint(to,1000).first.empty(),"negative sentinel ends array");to->upstreamPaths()[1]=-1;
  from->Paths()[0]=index;seed(from);check(!pawn->FindPathToEndPoint(to,1000).first.empty(),"derive incoming view from valid outgoing edge");
  seed(to);check(pawn->FindPathToEndPoint(from,1000).first.empty(),"do not synthesize opposite directed edge");
  for(auto& old:saved){for(int i=0;i<16;++i){old.node->Paths()[i]=old.paths[i];old.node->upstreamPaths()[i]=old.upstream[i];}old.node->bEndPoint()=old.end;}
  pawn->Location()=at;RuneNavigation::Reset();
  for(auto& old:saved)for(int i=0;i<16;++i)check(old.node->Paths()[i]==old.paths[i]&&old.node->upstreamPaths()[i]==old.upstream[i],"restore native fixture arrays");
  std::cout<<"RUNE_NAV_SAFETY_OK cases=6 arrays_preserved=1\n";return;
 }
 unsigned cases=0,improved=0;
 auto savedLocation=pawn->Location();pawn->Location()=vec3(100000,100000,100000);
 for(auto& [from,indices]:outgoing)for(int index:indices) {
  const auto& edge=specs[index];bool listed=false;
  for(int incoming:edge.endActor->upstreamPaths()){if(incoming<0)break;if(incoming==index)listed=true;}
  if(listed)continue;
  for(auto* n:nodes)n->bEndPoint()=n==from;
  std::map<UNavigationPoint*,int> cost;cost[from]=0;
  std::set<std::pair<int,UNavigationPoint*>> queue;queue.insert({0,from});
  while(!queue.empty()) {
   auto [distance,at]=*queue.begin();queue.erase(queue.begin());if(cost[at]!=distance)continue;
   for(int i:outgoing[at]){const auto& s=specs[i];int next=distance+std::max(1,s.distance);auto prior=cost.find(s.endActor);
    if(prior==cost.end()||next<prior->second){cost[s.endActor]=next;queue.insert({next,s.endActor});}}
  }
  auto result=pawn->FindPathToEndPoint(edge.endActor,1000);
  int expected=cost[edge.endActor];bool correct=!result.first.empty()&&result.second==expected;
  std::cout<<"NAV_REVERSE_CASE index="<<index<<" from="<<from->Name.ToString()<<" to="<<edge.endActor->Name.ToString()<<" expected="<<expected<<" actual="<<result.second<<" points="<<result.first.size()<<" correct="<<correct<<"\n";
  ++cases;improved+=correct;
 }
 pawn->Location()=savedLocation;for(auto* n:nodes)n->bEndPoint()=false;
 std::cout<<"RUNE_NAV_INTEGRITY_RESULT passed="<<improved<<" cases="<<cases<<"\n";
 if(commandline->HasArg("","--navigation-integrity-strict"))check(cases>0&&cases==improved,"authored forward paths lost by incomplete reverse arrays");
}
