#include "Precomp.h"
#include "RuneNavigation.h"
#include "Engine.h"
#include "Package/PackageManager.h"
#include "Packages/Core/UClass.h"
#include "Packages/Engine/Actors/Pawn/UPawn.h"
#include "Packages/Engine/Actors/NavigationPoint/UNavigationPoint.h"
#include "Packages/Engine/Actors/Info/ULevelInfo.h"
#include "Packages/Engine/Resources/Level/ULevel.h"
#include "Utils/Logger.h"
#include <map>
#include <queue>
#include <set>
#include <chrono>
namespace RuneNavigation {
namespace {
struct Node {vec3 at;int x=0,y=0,parent=-1;float cost=0;};
struct Search {
 vec3 origin{0},goal{0};std::vector<Node> nodes;
 std::map<std::tuple<int,int,int>,int> found;
 using Entry=std::pair<float,int>;
 std::priority_queue<Entry,std::vector<Entry>,std::greater<Entry>> open;
 float cellSize=12;unsigned expanded=0;bool finished=false;int end=-1;
};
struct State {Search search;std::vector<vec3> route;UActor* marker=nullptr;double retryAt=0;};
std::map<UPawn*,State> states;ULevel* level=nullptr;
double budgetTime=-1,budgetMs=0;
std::map<UNavigationPoint*,std::vector<int>> incoming;
size_t authoredSpecCount=0;bool authoredReady=false;
void buildIncoming(ULevel* world) {
 incoming.clear();authoredSpecCount=world->ReachSpecs.size();authoredReady=true;
 std::set<UNavigationPoint*> nodes;
 for(auto* n=engine->LevelInfo->NavigationPointList();n && nodes.insert(n).second;n=n->nextNavigationPoint()){}
 auto add=[&](UNavigationPoint* from,UNavigationPoint* to,int index) {
  if(index<0 || size_t(index)>=world->ReachSpecs.size())return;
  const auto& edge=world->ReachSpecs[index];
  if(!edge.startActor || !edge.endActor || (from && (!nodes.contains(edge.startActor) || !nodes.contains(edge.endActor))) ||
     edge.startActor->bDeleteMe() || edge.endActor->bDeleteMe() || (from && edge.startActor!=from) || (to && edge.endActor!=to))return;
  auto& list=incoming[edge.endActor];
  if(std::find(list.begin(),list.end(),index)==list.end())list.push_back(index);
 };
 for(auto* node:nodes) {
  incoming.try_emplace(node);
  // Keep valid original incoming-only links and their order. A stale slot is
  // skipped; only the negative sentinel terminates the authored array.
  for(int index:node->upstreamPaths()){if(index<0)break;add(nullptr,node,index);}
 }
 for(auto* node:nodes)for(int index:node->Paths()) {
  if(index<0)break;
  // The same directed A->B edge belongs in B's incoming view. This does not
  // create the opposite B->A route or a geometric connection across a gap.
  add(node,nullptr,index);
 }
}
bool eligible(UPawn* p){return p && engine && engine->LaunchInfo.IsRune() && p->XLevel()==engine->Level && !p->NetworkReplica && p->Health()>0 && !p->bIsPlayer() && p->IsA("ScriptPawn") && !p->GetBool("bIsBoss") && p->Physics()==PHYS_Walking;}
float flat(vec3 d){d.z=0;return length(d);}
UActor* sweep(UPawn* p,vec3 from,vec3 to,float radius,float height,vec3& hit,vec3& normal){
 for(auto& h:p->XLevel()->Collision.Trace(from,to,height,radius,true,true,false)){
  auto* a=h.Actor;if(!a||a==p||a->IsA("Pawn")||a->IsOwnedBy(p))continue;
  if(a!=p->Level()&&!p->IsBlockedBy(a))continue;
  hit=from+(to-from)*h.Fraction;normal=h.Normal;return a;
 }
 hit=to;normal=vec3(0);return nullptr;
}
bool safeZone(UPawn* p,vec3 at){auto* z=p->Level()->GetLocZone(at,{}).Zone;return z&&!z->bPainZone()&&!z->bWaterZone();}
bool floorAt(UPawn* p,vec3 from,float down,vec3& standing){
 vec3 hit,n;if(!sweep(p,from,from-vec3(0,0,down),4,0,hit,n)||n.z<.7f)return false;
 standing=hit+vec3(0,0,p->CollisionHeight()+.5f);return safeZone(p,standing);
}
bool walk(UPawn* p,vec3 from,vec3 to){
 int count=std::max(1,int(std::ceil(flat(to-from)/24.f)));if(count>20)return false;
 auto at=from;float step=std::clamp(p->MaxStepHeight(),0.f,48.f);
 float height=std::max(1.f,p->CollisionHeight()-.5f);vec3 hit,n;
 for(int i=1;i<=count;++i){
  vec3 xy=from+(to-from)*(float(i)/count);xy.z=at.z;vec3 next;
  if(!floorAt(p,xy+vec3(0,0,step+2),p->CollisionHeight()+step*2+10,next) ||
     next.z-at.z>step+1 || at.z-next.z>step+4)return false;
  if(sweep(p,at,next,p->CollisionRadius(),height,hit,n)) {
   vec3 rise(0,0,step);
   if(sweep(p,at,at+rise,p->CollisionRadius(),height,hit,n)||
      sweep(p,at+rise,next+rise,p->CollisionRadius(),height,hit,n)||
      sweep(p,next+rise,next,p->CollisionRadius(),height,hit,n))return false;
  }
  at=next;
 }
 return std::abs(at.z-to.z)<8;
}
auto cell(vec3 origin,int x,int y,float z){return std::tuple{x,y,int(std::floor((z-origin.z)/24.f))};}
void start(UPawn* p,State& s,vec3 goal){
 s.route.clear();s.search={};auto& q=s.search;q.origin=p->Location();q.goal=goal;q.cellSize=std::clamp(p->CollisionRadius()*.5f,8.f,16.f);
 q.nodes.push_back({q.origin,0,0,-1,0});q.found[cell(q.origin,0,0,q.origin.z)]=0;q.open.push({flat(goal-q.origin)*1.25f,0});
}
void advance(UPawn* p,State& s){
 auto& q=s.search;const auto began=std::chrono::steady_clock::now();
 auto elapsed=[&](){return std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-began).count();};
 if(budgetTime!=engine->TotalTime){budgetTime=engine->TotalTime;budgetMs=0;}
 if(budgetMs>=2)return;
 unsigned steps=0;
 while(!q.open.empty() && q.expanded<4096 && q.nodes.size()<8192){
  auto [estimate,i]=q.open.top();q.open.pop();auto current=q.nodes[i];
  if(estimate>current.cost+flat(q.goal-current.at)*1.25f+.01f)continue;
  ++q.expanded;
  if(flat(q.goal-current.at)<72 && std::abs(q.goal.z-current.at.z)<p->MaxStepHeight()+4 && walk(p,current.at,q.goal)){q.end=i;q.finished=true;break;}
  for(auto [dx,dy]:{std::pair{1,0},std::pair{-1,0},std::pair{0,1},std::pair{0,-1},std::pair{1,1},std::pair{-1,1},std::pair{1,-1},std::pair{-1,-1}}){
   int x=current.x+dx,y=current.y+dy;if(std::abs(x)*q.cellSize>2304||std::abs(y)*q.cellSize>2304)continue;
   vec3 next;
   if(!floorAt(p,current.at+vec3(dx*q.cellSize,dy*q.cellSize,p->MaxStepHeight()+2),p->CollisionHeight()+p->MaxStepHeight()*2+10,next)||!walk(p,current.at,next))continue;
   float cost=current.cost+q.cellSize*std::sqrt(float(dx*dx+dy*dy))+std::abs(next.z-current.at.z)*.3f;auto key=cell(q.origin,x,y,next.z);int j;
   auto known=q.found.find(key);
   if(known!=q.found.end()){j=known->second;if(q.nodes[j].cost<=cost)continue;q.nodes[j]={next,x,y,i,cost};}
   else {j=int(q.nodes.size());q.found[key]=j;q.nodes.push_back({next,x,y,i,cost});}
   q.open.push({cost+flat(q.goal-next)*1.25f,j});
  }
  if(++steps>=48 || (steps%8==0 && budgetMs+elapsed()>=2))break;
 }
 if(q.open.empty() || q.expanded>=4096 || q.nodes.size()>=8192)q.finished=true;
 budgetMs+=elapsed();
 if(q.finished && q.end>=0){
  for(int i=q.end;i>=0;i=q.nodes[i].parent)s.route.push_back(q.nodes[i].at);
  std::reverse(s.route.begin(),s.route.end());s.route.push_back(q.goal);
  LogMessage("RUNE_NAV_LOCAL_ROUTE actor="+p->Name.ToString()+" points="+std::to_string(s.route.size())+" expanded="+std::to_string(q.expanded));
 }
 if(q.finished && q.end<0)s.retryAt=engine->TotalTime+2;
 if(q.finished){q.found.clear();q.open={};q.nodes.clear();q.nodes.push_back({q.origin,0,0,-1,0});}
}
UActor* marker(UPawn* p,State& s,vec3 at){
 if(!s.marker || s.marker->bDeleteMe()){
  // A private invisible Actor is a movement goal, not a new navigation graph
  // or game object. Level ownership preserves the existing save reference.
  for(auto* a:p->XLevel()->Actors)if(a && !a->bDeleteMe() && a->Owner()==p && a->Tag()=="OpenRuneLocalRoute"){s.marker=a;break;}
  if(!s.marker){
   s.marker=UObject::TryCast<UActor>(engine->LevelPackage->NewObject("OpenRuneRoute"+std::to_string(p->Index),engine->packages->FindClass("Engine.Actor"),ObjectFlags::Transient,true));
   if(!s.marker)return nullptr;
   auto* a=s.marker;a->Outer()=p->XLevel()->Outer();a->bTicked()=p->bTicked();a->XLevel()=p->XLevel();a->Level()=p->Level();a->Tag()="OpenRuneLocalRoute";a->bHidden()=true;a->bStatic()=false;a->bNoDelete()=false;a->bMovable()=true;
   a->bCollideActors()=a->bBlockActors()=a->bBlockPlayers()=a->bCollideWorld()=false;a->CollisionRadius()=a->CollisionHeight()=0;
   a->Owner()=p;a->Role()=ROLE_Authority;a->RemoteRole()=ROLE_None;a->Index=int(p->XLevel()->Actors.size());p->XLevel()->Actors.push_back(a);p->ChildActors.push_back(a);
  }
 }
 s.marker->Location()=s.marker->OldLocation()=at;s.marker->InitActorZone();return s.marker;
}
}
void Reset(){states.clear();incoming.clear();authoredReady=false;authoredSpecCount=0;level=nullptr;budgetTime=-1;budgetMs=0;}
std::span<const int> Incoming(ULevel* world,UNavigationPoint* node) {
 if(!engine || !engine->LaunchInfo.IsRune() || world!=engine->Level || !node)return {};
 if(level!=world){Reset();level=world;}
 if(!authoredReady || authoredSpecCount!=world->ReachSpecs.size())buildIncoming(world);
 auto found=incoming.find(node);
 if(found!=incoming.end())return std::span<const int>(found->second);
 // Explicit scripted destinations need not occur in NavigationPointList.
 // Preserve their original native array; the caller validates every slot.
 auto native=node->upstreamPaths();return {native.data(),native.size()};
}
void Tick(ULevel* world,float elapsed,bool paused){
 if(!engine || world!=engine->Level || elapsed<=0 || paused)return;
 // Work continues between original script queries. Every actor shares the
 // same two-millisecond frame budget; no renderer or worker thread touches it.
 for(auto& [p,s]:states)if(eligible(p) && !s.search.nodes.empty() && !s.search.finished)advance(p,s);
}
void OnDestroy(UActor* a){
 if(UObject::TryCast<UNavigationPoint>(a)){incoming.clear();authoredReady=false;}
 auto* p=UObject::TryCast<UPawn>(a);if(!p)return;
 auto it=states.find(p);UActor* mark=it==states.end()?nullptr:it->second.marker;
 if(it!=states.end())states.erase(it);
 // Loaded saves have no native search state, but can retain the movement goal.
 if(!mark && p->XLevel())for(auto* actor:p->XLevel()->Actors)
  if(actor && !actor->bDeleteMe() && actor->Owner()==p && actor->Tag()=="OpenRuneLocalRoute"){mark=actor;break;}
 if(mark && !mark->bDeleteMe())mark->Destroy();
}
UActor* Fallback(UPawn* p,vec3 goal){
 if(!eligible(p))return nullptr;
 if(p->Enemy() && length(goal-p->Enemy()->Location())<1 && !p->CanSee(p->Enemy()))
  goal=p->GetVector("LastSeenPos"); // Follow the original observation, not hidden live movement.
 if(level!=p->XLevel()){Reset();level=p->XLevel();}
 if(!states.contains(p) && states.size()>=64)return nullptr;
 auto& s=states[p];
 if(s.search.nodes.empty() || length(s.search.goal-goal)>72 || (s.search.finished && engine->TotalTime>=s.retryAt && s.route.empty()))start(p,s,goal);
 if(!s.search.finished)advance(p,s);
 if(s.route.empty())return nullptr;
 size_t nearest=0;float distance=1e30f;
 for(size_t i=0;i<s.route.size();++i)if(length(s.route[i]-p->Location())<distance){distance=length(s.route[i]-p->Location());nearest=i;}
 if(distance>144){start(p,s,goal);return nullptr;}
 size_t next=std::min(nearest+1,s.route.size()-1),last=next;
 for(size_t i=next;i<s.route.size() && i<next+32;++i){if(!walk(p,p->Location(),s.route[i]))break;last=i;}
 if(!walk(p,p->Location(),s.route[last])){start(p,s,goal);return nullptr;}
 return marker(p,s,s.route[last]);
}
}
