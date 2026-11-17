#include "RuneBattleLaws.h"
#include "Precomp.h"
#include "RuneUpdates.h"
#include "RunePhysics.h"
#include "Render/ShadowSettings.h"
#include "RuneDesktopMenu.h"
#include "RuneDesktopSession.h"
#include "RuneMenuSections.h"
#include "Engine.h"
#include "GameWindow.h"
#include "RuneMenu.h"
#include "RuneCombat.h"
#include "RuneMultiplayer.h"
#include "RuneCharacterPreview.h"
#include "RuneCharacters.h"
#include "RuneView.h"
#include "RuneKick.h"
#include "RuneBash.h"
#include "RuneBloodlust.h"
#include "RuneDualWield.h"
#include "RuneTactics.h"
#include "RuneSnow.h"
#include "RuneVisualEffects.h"
#include "RuneGore.h"
#include "RenderDevice/RenderDevice.h"
#include "RenderDevice/VisualComparison.h"
#include "Packages/Engine/UPlayer.h"
#include "Packages/Engine/UViewport.h"
#include <shellapi.h>
#include <Xinput.h>
#include "RuneController.h"
#include "GameFeedback.h"
#include "Package/PackageManager.h"
#include "Utils/File.h"
#include "Utils/CommandLine.h"
#include "Utils/Logger.h"
#include <surrealwidgets/core/font.h>
#include <surrealwidgets/core/colorf.h>
#include <fstream>
#include <functional>
#include <sstream>
#include <cmath>

namespace RuneDesktopMenu {
namespace {
namespace fs=std::filesystem;
const Colorf gold(0.91f,0.70f,0.38f), ivory(0.94f,0.91f,0.85f), muted(0.66f,0.67f,0.65f);
struct Setting {std::string key,name,hint;std::vector<std::string> choices;int value,initial,minimum,maximum;};
struct Hit {Rect box;std::string id;std::function<void()> action;};
class MenuCanvas:public RenderDeviceCanvas {
public:
 explicit MenuCanvas(RenderDevice* d):RenderDeviceCanvas(d){attach(nullptr);}
 std::unique_ptr<CanvasTexture> createTexture(int w,int h,const void* pixels,ImageFormat format) override {
  // Whitespace glyphs have no bitmap; Vulkan forbids zero-sized images.
  if(w==0||h==0){uint32_t clear=0;return RenderDeviceCanvas::createTexture(1,1,&clear,format);}
  return RenderDeviceCanvas::createTexture(w,h,pixels,format);
 }
 void configure(int w,int h,double scale,Point offset){width=w;height=h;uiscale=scale;setOrigin(offset);}
 void preview(int id,int w,int h,const std::vector<uint32_t>& pixels,Rect box){
  auto& texture=previews[id];
  if(!texture)texture=createTexture(w,h,pixels.data(),ImageFormat::B8G8R8A8);
  else if(!pixels.empty()){auto t=static_cast<RenderDeviceTexture*>(texture.get());std::memcpy(t->Mip.Data.data(),pixels.data(),pixels.size()*4);t->Info.bRealtimeChanged=true;}
  if(texture){auto o=getOrigin();drawTile(texture.get(),gridFit(box.x+o.x),gridFit(box.y+o.y),gridFit(box.width),gridFit(box.height),0,0,(float)w,(float)h,Colorf(1,1,1));}
 }
 bool clipped(float& x,float& y,float& w,float& h,float& u,float& v,float& uw,float& vh){
  if(w<=0||h<=0)return false;float x0=std::max(x,float(getClipMinX())),y0=std::max(y,float(getClipMinY())),x1=std::min(x+w,float(getClipMaxX())),y1=std::min(y+h,float(getClipMaxY()));if(x1<=x0||y1<=y0)return false;u+=(x0-x)/w*uw;v+=(y0-y)/h*vh;uw*=(x1-x0)/w;vh*=(y1-y0)/h;x=x0;y=y0;w=x1-x0;h=y1-y0;return true;
 }
 void drawTile(CanvasTexture* t,float x,float y,float w,float h,float u,float v,float uw,float vh,Colorf color)override{if(clipped(x,y,w,h,u,v,uw,vh))RenderDeviceCanvas::drawTile(t,x,y,w,h,u,v,uw,vh,color);}
 void drawGlyph(CanvasTexture* t,float x,float y,float w,float h,float u,float v,float uw,float vh,Colorf color)override{if(clipped(x,y,w,h,u,v,uw,vh))RenderDeviceCanvas::drawGlyph(t,x,y,w,h,u,v,uw,vh,color);}
 void fillTile(float x,float y,float w,float h,Colorf color) override {
  int a=std::clamp((int)std::round(color.a*255),0,255);auto& t=alphaTiles[a];
  if(!t){uint32_t pixel=(uint32_t)a*0x01010101u;t=createTexture(1,1,&pixel,ImageFormat::B8G8R8A8);}
  drawTile(t.get(),x,y,w,h,0,0,1,1,Colorf(color.r,color.g,color.b));
 }
 std::map<int,std::unique_ptr<CanvasTexture>> previews,alphaTiles;
};
struct State {
 Engine* game=nullptr;std::unique_ptr<MenuCanvas> canvas;RuneDesktopSession session;RuneDesktopHost host;
 std::shared_ptr<Image> home,settings,mark;std::map<int,std::shared_ptr<Font>> fonts;
 std::vector<Setting> options;std::vector<Hit> hits;std::string page="home",tab="Video",section,helpKey,message,focus,pressed,edit,editTitle;
 std::function<void(std::string)> acceptText;std::string modal;std::function<void()> confirmed;
 uint32_t campaignLaws=RuneBattleLaws::DefaultCampaign;
 int scroll=0,characterStart=0,character=-1,skin=0;bool campaign=false,title=true,wasActive=false,previousDead=false,pending=false,fullscreen=false;
 struct Navigation {std::string page,tab,section,focus;int scroll;bool campaign;};
 std::vector<Navigation> history;bool comparing=false;
 double scale=1;Point offset,mouse;float yaw=0,spin=0;bool rotating=false;std::array<bool,11> saveExists{};int latestSave=-1;uint64_t saveScan=0;std::chrono::steady_clock::time_point last=std::chrono::steady_clock::now();
 fs::path config,probe;int probeId=0;std::string lastInvitation;
 HMODULE xinput=nullptr;DWORD (WINAPI *getPad)(DWORD,XINPUT_STATE*)=nullptr;DWORD (WINAPI *setPad)(DWORD,XINPUT_VIBRATION*)=nullptr;int pad=-1;WORD buttons=0;bool leftTrigger=false,rightTrigger=false;int padDirection=0;uint64_t padRepeat=0,rumbleEnd=0;int rumblePriority=-1;std::string dragging;
 ~State(){if(setPad&&pad>=0){XINPUT_VIBRATION zero{};setPad(pad,&zero);}if(xinput)FreeLibrary(xinput);GameFeedback::SetEnabled(false);RuneController::Set({});}
 bool laws()const{return page.ends_with("laws");}
 static constexpr int LawHeight=178,LawViewport=674,LawTop=196,LawMax=int(RuneBattleLaws::Entries.size())*LawHeight-LawViewport;
 bool lawLocked(int i)const{auto bit=RuneBattleLaws::Entries[i].bit;bool multi=page=="host-laws"||page=="match-laws";uint32_t mask=page=="match-laws"?RuneBattleLaws::Multiplayer.load():multi?host.laws:campaignLaws;return page=="match-laws"||(!multi&&bit==RuneBattleLaws::Leap)||(multi&&bit==RuneBattleLaws::Tactics)||(bit==RuneBattleLaws::Cross&&!(mask&RuneBattleLaws::Dual));}
 void revealLaw(int i){int top=i*LawHeight;if(top<scroll)scroll=top;else if(top+166>scroll+LawViewport)scroll=top+166-LawViewport;scroll=std::clamp(scroll,0,LawMax);}
 // Reuse the silhouettes and 64-unit coordinates of Android's touch-control icons.
 void controlIcon(int id,double x,double y,double scale=1,double rotation=0,Colorf color=ivory,bool mirror=false){
  auto point=[&](double a,double b){if(mirror)a=-a;double r=rotation*3.141592653589793/180;return Point{x+scale*(a*std::cos(r)-b*std::sin(r)),y+scale*(a*std::sin(r)+b*std::cos(r))};};
  auto stroke=[&](std::initializer_list<double> coords){auto it=coords.begin();double a=*it++,b=*it++;auto last=point(a,b);while(it!=coords.end()){a=*it++;b=*it++;auto next=point(a,b);for(double d:{-1.0,0.0,1.0})canvas->line({last.x+d,last.y},{next.x+d,next.y},color);last=next;}};
  auto circle=[&](double cx,double cy,double radius){for(int i=0;i<24;++i){double a=i*6.283185307/24,b=(i+1)*6.283185307/24;canvas->line(point(cx+radius*std::cos(a),cy+radius*std::sin(a)),point(cx+radius*std::cos(b),cy+radius*std::sin(b)),color);}};
  switch(id){
  case 101:circle(0,3,5);circle(-24,-20,5);stroke({0,13,0,23,-11,32});stroke({0,23,11,32});stroke({-3,14,-14,3,-12,-12});stroke({3,14,14,3,12,-13});stroke({-15,-18,7,-17});stroke({7,-17,19,-24,29,-20});stroke({7,-17,19,-7,30,-9});stroke({-11,-18,-16,-5,-8,-2});stroke({-9,-18,-5,-28,3,-29});break;
  case 100:stroke({-4,26,-4,-5,4,-5,4,26,-4,26});stroke({-25,-25,25,-25,25,-5,-25,-5,-25,-25});stroke({-16,-25,-16,-5});stroke({16,-25,16,-5});break;
  case 4:stroke({-17,20,19,-22,24,-25,23,-15,-13,23});stroke({-19,10,-4,24});stroke({-20,25,-12,16});break;
  case 3:stroke({0,-26,23,-17,20,6,12,20,0,28,-12,20,-20,6,-23,-17,0,-26});stroke({0,-20,0,22});break;
  case 5:circle(-14,-23,5.5);stroke({-14,-12,-8,2,-20,24});stroke({-24,26,-12,26});stroke({-8,2,7,-6,23,0});stroke({-12,-9,-27,-2});stroke({-11,-9,2,-15});stroke({19,-4,28,-3,32,5,20,5,19,-4});break;
  case 6:stroke({-26,18,26,18});stroke({-17,0,17,0});stroke({-8,-18,8,-18});break;
  case 7:stroke({-4,26,8,-23});stroke({7,-25,28,-27,21,-16,29,-5,5,-12,7,-25});stroke({-25,26,-15,12,-15,6,-10,1,1,3,6,9,3,17,-6,20,-13,29,-25,26});stroke({-29,-5,-25,-24,-10,-29,-11,-22});break;
  case 8:circle(14,0,6);stroke({-9,-7,4,6});stroke({-12,-8,-30,-29});stroke({-6,-11,-22,-32});stroke({0,4,15,16,29,28});stroke({5,7,20,15,32,24});break;
  case 2:stroke({-17,8,-22,-4,-18,-9,-10,0,-10,-23,-5,-26,-3,-24,-3,-5,-3,-30,3,-31,5,-28,5,-5,5,-25,11,-25,13,-21,13,-4,13,-17,19,-16,20,-10,20,13,13,25,-3,25,-17,8});break;
  default:stroke({0,-30,23,-9,16,19,0,29,-16,19,-23,-9,0,-30});stroke({-10,-8,0,-18,10,-8,0,11,-10,-8});stroke({0,11,0,22});break;
  }
 }
 void lawIcon(int i,double x,double y,bool locked){auto col=locked?muted:ivory;canvas->fillRect({x-40,y-40,80,80},Colorf(.18f,.15f,.11f));auto g=[&](int id,double dx=0,double dy=0,double scale=.88,double rotation=0){controlIcon(id,x+dx,y+dy,scale,rotation,col);};
  switch(i){case 0:g(5);break;case 1:g(3);break;case 2:g(4,-14,0,.70,-45);g(4,14,0,.70,-45);break;case 3:g(4);controlIcon(4,x,y,.88,0,col,true);break;case 4:g(6);break;case 5:g(3,-8,0,.74);g(6,19,0,.45,90);break;case 6:g(100,0,-5,.85);g(6,0,26,.34,180);break;case 7:g(101);break;case 8:g(2);break;case 9:g(8);break;case 10:g(4);break;case 12:g(2,-8,-8,.65);g(6,18,15,.45);break;default:g(11);break;}
 }
 void controller();
 void scanSaves(){auto now=GetTickCount64();if(now<saveScan+1000)return;saveScan=now;latestSave=-1;fs::file_time_type newest=fs::file_time_type::min();for(int i=0;i<11;++i){auto file=game->packages->GetSaveFolderPath()/("Save"+std::to_string(i)+"."+game->packages->GetSaveExtension());std::error_code ec;saveExists[i]=fs::is_regular_file(file,ec)&&(!commandline->HasArg("","--save-directory")||fs::is_regular_file(game->packages->GetSaveFolderPath()/("Save"+std::to_string(i)+".mapname"),ec));if(saveExists[i]){auto stamp=fs::last_write_time(file,ec);if(!ec&&stamp>newest){newest=stamp;latestSave=i;}}}}
 std::vector<std::string> category(const std::string&){for(const auto& p:RuneMenuSections())if(p.id==section)return p.keys;return {};}
 bool hostControlled(const std::string& key){return RuneMultiplayer::Active&&(key=="difficulty"||key=="tactics"||key=="kick"||key=="bash"||key=="dualWield"||key=="bloodlustMoves"||key=="normalBloodlust"||key=="physics");}
 std::string dependency(const std::string& key){if(key=="soundRemaster"){std::error_code error;auto root=fs::path(commandline->GetArg("","--sound-pack",(fs::path(game->LaunchInfo.gameRootFolder)/"SoundPacks/OpenRune-Layered-v1").string()));if(!fs::is_regular_file(root/"runtime.json",error))return "Optional sound pack is not installed. Original sounds will be used.";}if((key=="materialMaps"||key=="shadowLights"||key=="materialLighting"||key=="environmentProbes")&&v("pixelLighting")!=2)return "Requires Remastered lighting. Your choice is kept.";if(key=="scalingMethod"&&v("resolution")==0)return "Native resolution bypasses upscaling.";return {};}
 Setting& get(const std::string& key){auto i=std::find_if(options.begin(),options.end(),[&](auto& x){return x.key==key;});if(i==options.end())throw std::runtime_error("Unknown setting: "+key);return *i;}
 int v(const std::string& k){return get(k).value;}
 std::shared_ptr<Font> font(int h){auto& f=fonts[h];if(!f)f=Font::Create("Noto Sans",h);return f;}
 void text(double x,double y,const std::string& str,int size=24,Colorf color=ivory,double max=1300){canvas->drawTextEllipsis(font(size),{x,y},Rect(x,y-size,max,size*1.5),str,color);}
 void wrap(double x,double y,const std::string& str,double width,int size=20,int maxLines=4,Colorf color=muted){std::istringstream in(str);std::string word,line;int n=0;while(in>>word){auto next=line.empty()?word:line+" "+word;if(canvas->measureText(font(size),next).width>width&&!line.empty()){text(x,y,line,size,color,width);y+=size*1.35;line=word;if(++n>=maxLines)return;}else line=next;}if(!line.empty())text(x,y,line,size,color,width);}
 void panel(Rect r,float alpha=.90f){canvas->fillRect(r,Colorf(.045f,.057f,.061f,alpha));canvas->line(r.topLeft(),r.topRight(),Colorf(.42f,.33f,.20f));canvas->line(r.bottomLeft(),r.bottomRight(),Colorf(.25f,.22f,.17f));}
 void button(std::string id,Rect r,const std::string& name,std::function<void()> action,bool selected=false,bool enabled=true){
  bool hot=focus==id||r.contains(mouse);panel(r,hot?.97f:.83f);if(hot||selected){canvas->fillRect({r.x,r.y,4,r.height},gold);canvas->line(r.topLeft(),r.topRight(),gold);}text(r.x+22,r.y+r.height*.5+9,name,24,enabled?(hot||selected?gold:ivory):muted,r.width-42);if(enabled)hits.push_back({r,std::move(id),std::move(action)});
 }
 Navigation location()const{return {page,tab,section,focus,scroll,campaign};}
 void go(const std::string& p){if(p=="home")history.clear();else if(p!=page)history.push_back(location());page=p;scroll=0;focus.clear();pressed.clear();message.clear();LogMessage("DESKTOP_MENU page="+p);}
 void openSection(const std::string& id){if(id=="combat"){go(RuneMultiplayer::Active?"match-laws":"campaign-laws");return;}history.push_back(location());section=id;scroll=0;focus.clear();helpKey.clear();}
 void chooseCategory(const std::string& id){while(!history.empty()&&history.back().page=="settings")history.pop_back();tab=id;section.clear();scroll=0;focus.clear();helpKey.clear();}

 void save(){auto j=JsonValue::object();j["playerName"]=JsonValue::string(RuneMultiplayer::GetPlayerName());j["normalMovesDefaultV2"]=JsonValue::boolean(true);j["campaignLaws"]=JsonValue::number(campaignLaws);j["hostLaws"]=JsonValue::number(host.laws);j["hostPhysics"]=JsonValue::number(host.physics);for(auto& s:options)j[s.key=="snow"?"snowOverride":s.key]=JsonValue::number(s.value);j["character"]=JsonValue::number(RuneCharacters::Multiplayer);j["campaignCharacter"]=JsonValue::number(RuneCharacters::Campaign);j["lastKey"]=JsonValue::string(session.LastKey);j["hostName"]=JsonValue::string(host.name);auto temp=config;temp+=".tmp";std::ofstream f(temp,std::ios::binary);f<<j.to_json();f.close();if(!f||!MoveFileExW(temp.c_str(),config.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))message="Settings could not be saved.";}
 void apply(){
  RuneBattleLaws::Campaign=campaignLaws & ~RuneBattleLaws::Leap;
  RuneShadows::RequestedBudget=commandline->GetArgInt("","--shadow-lights",RuneShadows::ChoiceBudget(v("shadowLights")));
  RuneMultiplayer::ShowPlayerNames=v("playerNames");RuneMultiplayer::Diagnostics=v("networkDiagnostics");RuneMultiplayer::AdaptiveSmoothing=v("networkSmoothing");VisualComparison::RequestedUpscale=v("resolution");VisualComparison::RequestedMode=v("bloom");VisualComparison::RequestedClarity=v("sharp")|(v("filter")<<2)|(v("shadow")<<3);
  int lighting=v("pixelLighting");VisualComparison::RequestedWorldFeatures=v("environment")|(v("hrtf")<<3)|(v("detail")?2:0)|(v("depth")?4:0)|(v("detail")<<4)|(v("depth")<<6)|(lighting==2?768:lighting<<8)|(v("materialMaps")<<10)|(v("water")?2048:0)|(v("water")==2?8192:0)|((v("shadowLights")>0?1:0)<<12)|(v("ambientOcclusion")<<14)|(v("materialLighting")<<16)|(v("environmentProbes")<<17);
  VisualComparison::RequestedTexturePack=v("texturePack");RuneMenu::Antialias=v("aa");RuneMenu::TargetFps=v("fps");RuneMenu::Pacing=v("pacing");RuneMenu::Master=v("master");RuneMenu::Music=v("music");RuneMenu::Effects=v("effects");RuneMenu::Difficulty=v("difficulty");
  RuneView::FieldOfView=v("fov");RuneView::AutomaticAspect=v("wideView");RunePhysics::RequestedMode=commandline->GetArgInt("","--rune-physics",v("physics"));RuneKick::Enabled=true;RuneBash::Enabled=true;RuneBloodlust::Enabled=true;RuneBloodlust::AllowNormal=true;RuneDualWield::Enabled=true;RuneTactics::Enabled=true;RuneVisualEffects::Enabled=v("fireEffects");RuneGore::Enabled=v("gore");RuneSnow::Mode=v("snow")-1;GameFeedback::SetEnabled(v("rumble")!=0);
  if(comparing){VisualComparison::RequestedMode=0;VisualComparison::RequestedClarity=0;VisualComparison::RequestedWorldFeatures=v("environment")|(v("hrtf")<<3);VisualComparison::RequestedTexturePack=0;RuneVisualEffects::Enabled=false;RuneGore::Enabled=false;RuneSnow::Mode=RuneSnow::Original;}
  if(fullscreen!=(v("fullscreen")!=0)){game->window->ToggleWindowFullscreen(fullscreen?Size(1280,800):Size(game->window->GetNativePixelWidth(),game->window->GetNativePixelHeight()));fullscreen=v("fullscreen")!=0;}
 }
 void change(const std::string& key,int value){if(hostControlled(key))return;comparing=false;auto& s=get(key);s.value=std::clamp(value,s.minimum,s.maximum);if(key=="soundRemaster")message="Restart Open Rune to apply the sound effects setting.";apply();save();LogMessage("DESKTOP_SETTING "+key+"="+std::to_string(s.value));}
 void open(){RuneMenu::Open=true;RuneMenu::Covered=true;if(game->NetworkFocusLost)game->NetworkFocusLost();go("home");}
 void close(){if(title&&!RuneMultiplayer::Active)return;RuneMenu::Open=false;RuneMenu::Covered=false;game->viewport->bShowWindowsMouse()=false;game->window->LockCursor();}
 void ask(const std::string& str,std::function<void()> action){modal=str;confirmed=action;}
 void request(int action){if((action==7||action==4)&&commandline->HasArg("","--save-directory")){int slot=action==4?10:RuneMenu::Slot.load();if(!fs::is_regular_file(game->packages->GetSaveFolderPath()/("Save"+std::to_string(slot)+".mapname"))){message="This test save is missing its map metadata.";return;}}if(RuneCombat::Request(action)){pending=true;message="Please wait…";}else message="The game is not ready yet.";}
 void chooseName(std::function<void()> next={}){input("Player name · 1–24 characters",RuneMultiplayer::GetPlayerName(),[this,next](std::string name){if(!RuneMultiplayer::SetPlayerName(name))throw std::runtime_error("Use 1–24 letters/numbers, spaces, apostrophe, dot, hyphen or underscore.");save();if(next)next();});}
 void identified(std::function<void()> next){if(RuneMultiplayer::GetPlayerName().empty())chooseName(std::move(next));else next();}
 void confirmInput(){auto fn=acceptText;auto value=edit;acceptText={};try{fn(value);}catch(...){acceptText=fn;edit=value;throw;}}
 void input(const std::string& titleText,const std::string& initial,std::function<void(std::string)> fn){message.clear();editTitle=titleText;edit=initial;acceptText=fn;focus.clear();}
 void back(){if(!modal.empty()){modal.clear();return;}if(acceptText){acceptText={};return;}if(character>=0){character=-1;return;}if(!history.empty()){auto old=history.back();history.pop_back();page=old.page;tab=old.tab;section=old.section;focus=old.focus;scroll=old.scroll;campaign=old.campaign;message.clear();return;}if(page=="home"){close();return;}go("home");}
 void rowSetting(const std::string& key,double y){
  auto& s=get(key);Rect r(382,y,710,82);panel(r,.88f);text(400,y+32,s.name+(key=="ambientOcclusion"?" · Experimental":""),22,ivory,390);
  std::string hint=hostControlled(key)?"Host-controlled":dependency(key);
  auto label=hint.empty()?(s.choices.empty()?std::to_string(s.value):s.choices.at(s.value)):hostControlled(key)?"Host rules":"Unavailable";
  button("setting:"+key,{800,y+10,274,62},label,[this,key]{helpKey=key;if(hostControlled(key)||!dependency(key).empty())return;auto& option=get(key);if(!option.choices.empty())change(key,option.value==option.maximum?option.minimum:option.value+1);else input(option.name,std::to_string(option.value),[this,key](std::string value){size_t used=0;int n=std::stoi(value,&used);if(used!=value.size())throw std::runtime_error("Enter a whole number.");change(key,n);});});
  if(focus=="setting:"+key||r.contains(mouse))helpKey=key;
 }
 void drawCharacter(int c,int s,Rect box,bool expanded){std::vector<uint32_t> pixels;int result=RuneCharacterPreview::Poll(c,s,pixels,expanded,expanded?yaw:std::numeric_limits<float>::quiet_NaN());int id=c*4+s+(expanded?200:0);if(result>=0&&(result==1||canvas->previews.contains(id)))canvas->preview(id,expanded?384:192,expanded?448:224,pixels,box);else if(result<0)text(box.x+15,box.y+80,"Preview unavailable",20,muted,box.width-30);}
 void draw();
};
std::unique_ptr<State> state;
void State::controller(){
 if(!getPad)return;DWORD foregroundPid=0;GetWindowThreadProcessId(GetForegroundWindow(),&foregroundPid);
 XINPUT_STATE current{};int found=-1;if(foregroundPid==GetCurrentProcessId())for(int i=0;i<4;++i)if(getPad(i,&current)==ERROR_SUCCESS){found=i;break;}
 if(found!=pad){if(setPad&&pad>=0){XINPUT_VIBRATION zero{};setPad(pad,&zero);}pad=found;rumbleEnd=0;}
 auto input=[&](int key,bool before,bool after){if(before!=after){if(after)game->OnWindowKeyDown((EInputKey)key);else game->OnWindowKeyUp((EInputKey)key);}};
 WORD next=found>=0?current.Gamepad.wButtons:0;auto mapped=[&](WORD bit,int key){input(key,(buttons&bit)!=0,(next&bit)!=0);};
 mapped(XINPUT_GAMEPAD_START,IK_Escape);
 bool menu=RuneMenu::Open;bool lt=found>=0&&current.Gamepad.bLeftTrigger>80,rt=found>=0&&current.Gamepad.bRightTrigger>80;
 if(menu){mapped(XINPUT_GAMEPAD_A,IK_Enter);mapped(XINPUT_GAMEPAD_B,IK_Escape);RuneController::Set({});
  int direction=(next&XINPUT_GAMEPAD_DPAD_UP)||current.Gamepad.sThumbLY>20000?IK_Up:(next&XINPUT_GAMEPAD_DPAD_DOWN)||current.Gamepad.sThumbLY<-20000?IK_Down:(next&XINPUT_GAMEPAD_DPAD_LEFT)||current.Gamepad.sThumbLX<-20000?IK_Left:(next&XINPUT_GAMEPAD_DPAD_RIGHT)||current.Gamepad.sThumbLX>20000?IK_Right:0;
  auto now=GetTickCount64();if(direction&&(direction!=padDirection||now>=padRepeat)){game->OnWindowKeyDown((EInputKey)direction);game->OnWindowKeyUp((EInputKey)direction);padRepeat=now+(direction==padDirection?160:350);}padDirection=direction;
 }else{mapped(XINPUT_GAMEPAD_A,IK_Space);mapped(XINPUT_GAMEPAD_B,IK_Ctrl);mapped(XINPUT_GAMEPAD_Y,IK_Q);mapped(XINPUT_GAMEPAD_LEFT_SHOULDER,IK_E);mapped(XINPUT_GAMEPAD_LEFT_THUMB,IK_Ctrl);mapped(XINPUT_GAMEPAD_RIGHT_THUMB,IK_F);
  input(IK_LeftMouse,rightTrigger||(buttons&XINPUT_GAMEPAD_X),rt||(next&XINPUT_GAMEPAD_X));input(IK_RightMouse,leftTrigger,lt);
  auto axis=[](SHORT raw){float a=raw/32767.0f;return std::abs(a)<.22f?0.0f:std::copysign(std::min(1.0f,(std::abs(a)-.22f)/.78f),a);};
  RuneController::Set({axis(current.Gamepad.sThumbLX),axis(current.Gamepad.sThumbLY),axis(current.Gamepad.sThumbRX)*LookScale(),axis(current.Gamepad.sThumbRY)*LookScale()*(InvertLook()?-1:1)});
 }
 buttons=next;leftTrigger=lt;rightTrigger=rt;
 auto pulse=GameFeedback::Poll();auto now=GetTickCount64();if(setPad&&pad>=0){if(pulse.ms&&v("rumble")&&(now>=rumbleEnd||pulse.priority>=rumblePriority)){float strength=v("rumbleStrength")/100.0f;XINPUT_VIBRATION motors{(WORD)(65535*pulse.low*strength),(WORD)(65535*pulse.high*strength)};setPad(pad,&motors);rumbleEnd=now+pulse.ms;rumblePriority=pulse.priority;}else if(rumbleEnd&&(now>=rumbleEnd||!v("rumble")||(menu&&rumblePriority!=100))){XINPUT_VIBRATION zero{};setPad(pad,&zero);rumbleEnd=0;}}
}
void State::draw(){
 hits.clear();auto& c=*canvas;const bool landing=page=="home";
 c.drawImage(landing?home:settings,{0,0,1536,1024});c.fillRect({0,0,1536,1024},Colorf(0,0,0,.20f));
 if(landing){c.drawImage(mark,{84,45,128,128});text(235,125,"OPEN RUNE",64,gold);if(!title||RuneMenu::PlayerDead)text(88,238,RuneMenu::PlayerDead?"FALLEN IN BATTLE":"PAUSED",24,muted);
  double y=title&&!RuneMenu::PlayerDead?234:286;auto item=[&](std::string id,std::string label,std::function<void()> fn){button(id,{84,y,510,75},label,fn);y+=88;};
  if(RuneMultiplayer::Active){item("resume","Return to match",[this]{close();});if(RuneMenu::PlayerDead)item("respawn","Respawn",[this]{RuneMultiplayer::Respawn=true;close();});item("multiplayer","Match & hosting",[this]{go("multiplayer");});}
  else {if(title&&latestSave>=0)item("continue","Continue",[this]{RuneMenu::Slot=latestSave;request(7);});if(!title&&!RuneMenu::PlayerDead)item("resume","Continue",[this]{close();});if(RuneMenu::PlayerDead)item("retry","Retry checkpoint",[this]{request(4);});item("new","New campaign",[this]{go("campaign-new-laws");});item("load","Load game",[this]{go("load");});if(!title&&!RuneMenu::PlayerDead)item("save","Save game",[this]{go("save");});item("multiplayer","Multiplayer",[this]{go("multiplayer");});}
  item("settings","Settings",[this]{go("settings");});item("quit","Quit Open Rune",[this]{ask("Quit Open Rune? Unsaved progress will be lost.",[this]{game->quit=true;});});
  if(RuneUpdates::Current().available)button("update-available",{950,810,480,68},"Update available",[this]{go("settings");tab="Support";section="updates";});
  button("about",{1130,908,300,60},"About & support",[this]{go("settings");tab="Support";section.clear();});
 } else {
  text(88,105,(page=="campaign-laws"||page=="campaign-new-laws"||page=="host-laws"||page=="match-laws")?"LAWS OF BATTLE":page=="settings"?"SETTINGS":page=="multiplayer"?"MULTIPLAYER":page=="host"?"HOST A MATCH":page=="host-rules"?"MATCH RULES":page=="advanced-join"?"ADVANCED JOINING":page=="find"?"FIND GAMES":page=="characters"?"CHOOSE YOUR WARRIOR":page=="maps"?"MAP ROTATION":page=="about"?"ABOUT OPEN RUNE":page=="save"?"SAVE GAME":"LOAD GAME",46,gold);
  button("back",{86,912,230,64},"Back",[this]{back();});
  if(page=="campaign-laws"||page=="campaign-new-laws"||page=="host-laws"||page=="match-laws"){
   bool multiplayer=page=="host-laws"||page=="match-laws",readonly=page=="match-laws";
   uint32_t mask=readonly?RuneBattleLaws::Multiplayer.load():multiplayer?host.laws:campaignLaws;
   text(90,163,multiplayer?(readonly?"MULTIPLAYER / Current host rules":"MULTIPLAYER / New hosted match"):"SINGLE PLAYER / Players and eligible enemies",24,gold,1320);
   scroll=std::clamp(scroll,0,LawMax);c.pushClip({84+c.getOrigin().x,LawTop+c.getOrigin().y,1350,LawViewport});
   for(int i=0;i<(int)RuneBattleLaws::Entries.size();++i){auto law=RuneBattleLaws::Entries[i];double y=LawTop+i*LawHeight-scroll;if(y+166<=LawTop||y>=LawTop+LawViewport)continue;
    bool locked=lawLocked(i),checked=(RuneBattleLaws::Effective(mask)&law.bit)!=0;std::string id="law:"+std::to_string(i);bool hot=focus==id||(Rect(84,y,1320,166).contains(mouse)&&mouse.y>=LawTop&&mouse.y<LawTop+LawViewport);
    panel({84,y,1320,166},.96f);if(hot){c.line({84,y},{1404,y},gold);c.fillRect({84,y,4,166},gold);}
    lawIcon(i,140,y+79,locked);text(202,y+43,law.name,28,locked?muted:ivory,1050);
    std::string constraint=law.bit==RuneBattleLaws::Cross&&!(mask&RuneBattleLaws::Dual)?"Requires dual wield":multiplayer&&law.bit==RuneBattleLaws::Tactics?"Campaign only":!multiplayer&&law.bit==RuneBattleLaws::Leap?"Disabled in campaign":readonly?"Host rule":"";
    wrap(202,y+82,(constraint.empty()?"":constraint+" · ")+law.description,1050,23,3,muted);
    c.fillRect({1280,y+56,74,34},checked?Colorf(.38f,.26f,.14f):Colorf(.15f,.17f,.18f));c.fillRect({checked?1320.0:1286.0,y+60,26,26},locked?muted:checked?gold:ivory);
    if(!locked)hits.push_back({{84,std::max(y,double(LawTop)),1320,std::min(y+166,double(LawTop+LawViewport))-std::max(y,double(LawTop))},id,[this,multiplayer,law]{if(multiplayer){host.laws^=law.bit;host.normalBloodlust=(host.laws&RuneBattleLaws::Normal)!=0;}else{campaignLaws^=law.bit;campaignLaws&=~RuneBattleLaws::Leap;apply();}save();}});
   }
   c.popClip();double thumb=double(LawViewport)*LawViewport/(int(RuneBattleLaws::Entries.size())*LawHeight),thumbY=LawTop+(LawViewport-thumb)*scroll/LawMax;c.fillRect({1420,LawTop,8,LawViewport},Colorf(.14f,.16f,.16f));c.fillRect({1420,thumbY,8,thumb},gold);
   text(350,950,readonly?"Rules are set by the host":"Applies to players and eligible enemies",22,muted,580);
   if(page=="campaign-new-laws")button("begin-campaign",{1030,912,400,64},"Begin campaign",[this]{ask("Start a new campaign with these Laws of Battle? Unsaved progress will be lost.",[this]{apply();save();request(1);});});
   else if(!readonly)button("laws-physics",{960,912,470,64},"Physics: "+std::string(RunePhysics::ModeLabel(multiplayer?host.physics:v("physics"))),[this,multiplayer]{if(multiplayer){host.physics=(host.physics+1)%3;save();}else change("physics",(v("physics")+1)%3);});
  }else if(page=="settings"){
   int i=0;for(auto name:{"Video","Audio","Controls","Interface","Gameplay"}){std::string t=name;button("tab:"+t,{84,184.0+i++*78,260,62},t,[this,t]{chooseCategory(t);},tab==t);}
   button("tab:Support",{84,650,260,62},"About & support",[this]{chooseCategory("Support");},tab=="Support");
   const MenuSection* activeSection=nullptr;for(const auto& p:RuneMenuSections())if(p.id==section)activeSection=&p;
   if(!activeSection){int n=0;for(const auto& p:RuneMenuSections())if(p.category==tab){std::string id=p.id;button("section:"+id,{382,190.0+n++*95,1054,76},p.title,[this,id]{openSection(id);});}}
   else {
    text(390,168,tab+" / "+activeSection->title,25,gold,1040);
    auto keys=activeSection->keys;for(int n=0;n<(int)keys.size();++n)rowSetting(keys[n],198+n*82);
    panel({1110,198,326,625});text(1132,240,"DETAILS",22,gold,280);
    std::string explanation=activeSection->help;if(!helpKey.empty()){auto& option=get(helpKey);explanation=option.name+". "+option.hint;auto requirement=dependency(helpKey);if(!requirement.empty())explanation+=" "+requirement;if(hostControlled(helpKey))explanation+=" This is controlled by the host.";}
    wrap(1132,290,explanation,280,21,18);
    if(section=="effects")button("compare",{382,770,710,64},comparing?"Restore my visuals":"Compare original visuals",[this]{comparing=!comparing;apply();});
    if(section=="physics"&&!RuneMultiplayer::Active)button("physics-playground",{382,410,710,72},"Enter physics playground",[this]{ask("Replace the live scene with an isolated physics playground? Campaign saves stay separate.",[this]{if(!v("physics"))change("physics",RunePhysics::DefaultMode);request(9);close();});});
    if(section=="campaign")button("campaign-character",{382,410,710,72},"Campaign character & skin",[this]{campaign=true;go("characters");});
    if(section=="gamepad")wrap(402,250,pad>=0?"Controller connected. Left stick moves; right stick looks. A jumps, B crouches, X / RT attacks, LT blocks. Left-stick click uses; Y throws; LB activates Rune Power; right-stick click kicks. Double-tap Block to bash. Start pauses.":"No active controller detected. Connect a controller to use the gamepad bindings. Vibration settings are under Controls → Vibration.",650,25,12);
    if(section=="input")wrap(402,445,"WASD move · mouse look · Space jump · left mouse attack · right mouse block · Ctrl use · Q throw · E Rune Power · F kick · Escape menu",650,23,6);
    if(section=="vibration")button("test-rumble",{382,470,710,72},"Test vibration",[this]{if(setPad&&pad>=0){XINPUT_VIBRATION motors{(WORD)(v("rumbleStrength")*655), (WORD)(v("rumbleStrength")*655)};setPad(pad,&motors);rumbleEnd=GetTickCount64()+400;rumblePriority=100;}});
    if(section=="updates") {auto r=RuneUpdates::Current();text(402,238,RuneUpdates::Identity(),20,gold,670);wrap(402,290,r.message,650,24,3);button("check-updates",{382,380,340,68},r.busy?"Checking…":"Check for updates",[]{RuneUpdates::Check(true);},false,!r.busy);button("auto-updates",{740,380,352,68},RuneUpdates::Automatic()?"Startup checks: On":"Startup checks: Off",[]{RuneUpdates::SetAutomatic(!RuneUpdates::Automatic());});button("release-page",{382,470,710,68},r.available?"View update & downloads":"Open releases page",[this,r]{if(!RuneUpdates::OpenPage(r.url.empty()?"https://github.com/Mokomis/open-rune/releases":r.url))message="Could not open your browser.";});wrap(402,590,r.notes.empty()?"Only published stable releases are offered. Network protocol and gameplay rules must match the host. Saves, settings and game files are kept separate from updates.":r.notes,650,22,8);}
    if(section=="support"){button("notices",{382,220,710,76},"Open licenses & notices",[this]{auto p=fs::path(OS::executable_path())/"licenses";ShellExecuteW(nullptr,L"open",p.c_str(),nullptr,nullptr,SW_SHOWNORMAL);});wrap(402,410,"Open Rune · SurrealEngine fork. Requires your original Rune game files. Original assets remain the property of their owners.",650,26,6);}
    if(!keys.empty())button("restore",{382,912,300,64},"Reset this section",[this]{ask("Reset only the controls in this section? Other settings and layouts are kept.",[this]{for(auto& k:category(tab))if(!hostControlled(k)){auto& option=get(k);option.value=option.initial;}comparing=false;apply();save();});});
   }
   const float fractions[]={1,.75f,.5f,.25f};float fraction=fractions[v("resolution")];panel({708,916,728,58});text(720,950,std::to_string((int)std::round(game->window->GetNativePixelWidth()*fraction))+" × "+std::to_string((int)std::round(game->window->GetNativePixelHeight()*fraction))+" scene  /  "+std::to_string(game->window->GetNativePixelWidth())+" × "+std::to_string(game->window->GetNativePixelHeight())+" display",18,muted,720);
  }else if(page=="multiplayer"){
   panel({84,160,1350,704});wrap(112,204,RuneMultiplayer::Active?RuneMultiplayer::GetStatus():session.Message,1280,22,3);
   if(RuneMultiplayer::Active){
    button("resume",{112,300,610,76},RuneMenu::PlayerDead?"Respawn":"Resume match",[this]{if(RuneMenu::PlayerDead)RuneMultiplayer::Respawn=true;close();});
    button("settings",{748,300,650,76},"Settings",[this]{go("settings");});
    button("leave",{112,410,610,76},session.Hosting()?"Stop hosted match":"Leave match",[this]{ask(session.Hosting()?"Stop hosting? Everyone will disconnect.":"Leave this match?",[this]{RuneMultiplayer::Leave=true;session.StopHost();session.Leave();title=true;go("home");});});
    if(session.Hosting()){button("add-bot",{112,520,610,64},"Add bot ("+std::to_string(session.BotCount())+")",[this]{session.AddBot();message=session.Message;});button("remove-bot",{748,520,650,64},"Remove bot",[this]{session.RemoveBot();message=session.Message;},false,session.BotCount()>0);button("invite",{112,640,610,64},"Invite players",[this]{game->window->SetClipboardText(session.HostKey);message="Host identity copied. Friends can use Find games or Advanced joining.";});}
   }else{
    button("find",{112,300,610,76},"Find games",[this]{go("find");session.Browse();});
    button("host",{748,300,650,76},"Host game",[this]{host.normalBloodlust=(host.laws&RuneBattleLaws::Normal)!=0;host.Normalize(*game);go("host");},false,!session.Hosting());
    button("characters",{112,410,610,76},"Character & skin",[this]{campaign=false;go("characters");});
    if(!session.LastKey.empty())button("rejoin",{748,410,650,76},"Rejoin last match",[this]{identified([this]{session.Join(session.LastKey);go("find");});});
    button("advanced-join",{112,520,610,76},"Advanced joining",[this]{go("advanced-join");});
    if(session.Hosting())button("stop-host",{748,520,650,76},"Stop hosting",[this]{ask("Stop hosting?",[this]{session.StopHost();});});
   }
   button("player-name",{112,740,610,64},"Player name: "+(RuneMultiplayer::GetPlayerName().empty()?"Choose name":RuneMultiplayer::GetPlayerName()),[this]{chooseName();});
   button("player-names",{748,640,650,64},v("playerNames")?"Player names: On":"Player names: Off · team markers stay",[this]{change("playerNames",1-v("playerNames"));});
   button("connection",{748,740,650,64},"Connection settings",[this]{go("settings");tab="Connection";section="connection";});
  }else if(page=="advanced-join"){
   panel({84,160,1350,704});wrap(112,215,"Most players can use Find games. Advanced joining accepts a shared host identity or a private invitation file. Keep invitation files private.",1250,26,4);
   button("join-key",{112,400,1250,76},"Join with host identity",[this]{input("Host identity (64 characters)",session.LastKey,[this](std::string key){identified([this,key]{session.Join(key);go("find");});});});
   button("join-file",{112,510,1250,76},"Open invitation file",[this]{input("Full path to .rune-invite file","",[this](std::string p){if(!fs::is_regular_file(p)||fs::path(p).extension()!=".rune-invite")throw std::runtime_error("Choose an existing .rune-invite file.");lastInvitation=p;identified([this,p]{if(RuneMultiplayer::Join(p)){title=false;close();}});});});
  }else if(page=="host"){
   auto row=[&](int n,std::string id,std::string label,std::function<void()> f){button(id,{84+(n%2)*690.0,190+(n/2)*118.0,658,88},label,f);};
   row(0,"host-name","Name: "+host.name,[this]{input("Server name",host.name,[this](std::string n){if(n.empty()||n.size()>48)throw std::runtime_error("Use 1–48 characters.");host.name=n;save();});});
   row(1,"host-public",host.publicGame?"Visibility: Public · Find games":"Visibility: Private invitation",[this]{host.publicGame=!host.publicGame;});
   row(2,"host-mode","Mode: "+std::string(host.mode=="dm"?"Deathmatch":host.mode=="tdm"?"Team Deathmatch":host.mode=="arena"?"Arena":"HeadBall"),[this]{std::vector<std::string> m={"dm","tdm","arena","headball"};auto i=std::find(m.begin(),m.end(),host.mode);host.mode=m[(i-m.begin()+1)%m.size()];host.Normalize(*game);});
   row(3,"host-players","Player capacity: "+std::to_string(host.players),[this]{host.players=host.players==16?2:host.players+1;host.Normalize(*game);});
   row(4,"host-maps","Map rotation: "+std::to_string(host.maps.size())+" map(s)",[this]{go("maps");});
   row(5,"host-bots","Starting bots: "+std::to_string(host.bots),[this]{host.bots=(host.bots+1)%host.players;});
   row(6,"host-rules","Match rules",[this]{go("host-rules");});row(7,"host-laws","Laws of Battle",[this]{go("host-laws");});
   wrap(100,735,"Player capacity includes you, other players and bots. The host must stay open while others play. Your selected rules apply to this new match.",1310,25,4);
   button("start-host",{1030,912,400,64},"Start & join",[this]{ask("Start this match? Unsaved campaign progress will be replaced; saved games are kept.",[this]{identified([this]{session.Host(*game,host);});});});
  }else if(page=="host-rules"){
   auto row=[&](int n,std::string id,std::string label,std::function<void()> f){button(id,{84+(n%2)*690.0,190+(n/2)*118.0,658,88},label,f);};
   if(host.mode!="dm")row(0,"host-teams",host.mode=="arena"?"Arena team size: "+std::to_string(host.arenaSize):"Teams: "+std::to_string(host.teams),[this]{if(host.mode=="arena")host.arenaSize=host.arenaSize%4+1;else host.teams=host.teams==4?2:host.teams+1;host.Normalize(*game);});
   row(1,"host-score","Score / round limit: "+std::to_string(host.score),[this]{input("Score limit (0 disables)",std::to_string(host.score),[this](std::string v){host.score=std::clamp(std::stoi(v),0,10000);});});
   row(2,"host-time","Time limit: "+std::to_string(host.minutes)+" minutes",[this]{input("Minutes (0 disables)",std::to_string(host.minutes),[this](std::string v){host.minutes=std::clamp(std::stoi(v),0,1440);});});
   row(3,"host-laws","Laws of Battle",[this]{go("host-laws");});
   row(6,"host-physics","Physics: "+std::string(RunePhysics::ModeLabel(host.physics)),[this]{host.physics=(host.physics+1)%3;save();});
   if(!host.publicGame)row(4,"host-address","LAN address: "+host.address,[this]{input("Reachable LAN IPv4 address",host.address,[this](std::string v){if(v.size()>64)throw std::runtime_error("Address is too long.");host.address=v;});});
   if(host.mode=="headball")row(5,"host-limbs",host.limbs?"Limbs score: On":"Limbs score: Off",[this]{host.limbs=!host.limbs;});
   wrap(100,690,"These rules apply to the new hosted match. Campaign preferences are unchanged. At least one score or time limit must be enabled.",1300,25,4);
  }else if(page=="find"){
   panel({84,160,1350,700});wrap(108,203,session.Message,1290,22,2);auto& rows=session.Rows.items();scroll=std::clamp(scroll,0,std::max(0,(int)rows.size()-6));
   for(int n=0;n<6&&n+scroll<(int)rows.size();++n){auto row=rows[n+scroll];std::string name=row["name"].to_string(),key=row["key"].to_string();if(name.empty())name="Open Rune match";auto label=name+"   "+row["map"].to_string()+"   "+row["mode"].to_string()+"   "+std::to_string(row["players"].to_int())+"/"+std::to_string(row["capacity"].to_int())+" players";button("game:"+key,{110,256.0+n*91,1298,76},label,[this,key]{identified([this,key]{session.Join(key);});},false,!session.Busy()&&row["available"].to_int()>0);}
   if(rows.empty())wrap(112,302,"No verified games listed yet. Refresh to search again, or join using the host identity shared by your friend.",1200,24,3);
   button("refresh",{1030,912,190,64},"Refresh",[this]{session.Browse();},false,!session.Busy()&&!session.Message.starts_with("Searching"));button("cancel",{1240,912,190,64},"Cancel",[this]{session.Cancel();});
  }else if(page=="maps"){
   auto maps=host.Available(*game);scroll=std::clamp(scroll,0,std::max(0,(int)std::max(maps.size(),host.maps.size())-7));
   text(84,165,"SELECTED ORDER",24,gold,620);text(780,165,"AVAILABLE MAPS",24,gold,640);
   for(int n=0;n<7;++n){int index=n+scroll;double y=195+n*90.0;
    if(index<(int)host.maps.size()){auto m=host.maps[index];text(84,y+42,std::to_string(index+1)+". "+m,22,ivory,355);
     button("earlier:"+m,{450,y,65,65},"↑",[this,index]{std::swap(host.maps[index],host.maps[index-1]);},false,index>0);
     button("later:"+m,{525,y,65,65},"↓",[this,index]{std::swap(host.maps[index],host.maps[index+1]);},false,index+1<(int)host.maps.size());
     button("remove:"+m,{600,y,130,65},"Remove",[this,index]{if(host.maps.size()>1)host.maps.erase(host.maps.begin()+index);else message="Keep at least one map.";});}
    if(index<(int)maps.size()){auto m=maps[index];bool chosen=std::find(host.maps.begin(),host.maps.end(),m)!=host.maps.end();button("map:"+m,{780,y,650,72},(chosen?"✓  ":"+  ")+m,[this,m]{if(host.maps.size()<32)host.maps.push_back(m);},chosen,!chosen&&host.maps.size()<32);}
   }
   button("up",{1030,912,190,64},"Up",[this]{scroll-=6;});button("down",{1240,912,190,64},"Down",[this]{scroll+=6;});
  }else if(page=="characters"){
   button("scope",{84,150,700,58},campaign?"Selecting for: single-player campaign":"Selecting for: multiplayer",[this]{campaign=!campaign;});
   if(campaign)button("story",{810,150,620,58},"Use original story character",[this]{RuneCharacters::Campaign=-1;save();message="Original campaign character selected.";});
   characterStart=std::clamp(characterStart,0,(int)openrune::Characters.size()-4);
   for(int n=0;n<4;++n){int id=characterStart+n;auto& model=openrune::Characters[id];double x=84+n*342;Rect card(x,244,318,560);panel(card);drawCharacter(id,0,{x+10,268,298,348},false);text(x+18,655,model.name,26,ivory,284);if(model.skins>1)text(x+18,694,std::to_string(model.skins)+" skins",20,gold,284);hits.push_back({{x,244,318,470},"preview:"+std::to_string(id),[this,id]{character=id;skin=0;yaw=0;spin=0;}});button("select:"+std::to_string(id),{x+16,726,286,60},"Select",[this,id]{if(campaign)RuneCharacters::Campaign=id;else RuneCharacters::Multiplayer=id;save();message=std::string(openrune::Characters[id].name)+" selected.";});}
   button("previous-character",{1030,912,190,64},"Previous",[this]{--characterStart;});button("next-character",{1240,912,190,64},"Next",[this]{++characterStart;});
  }else if(page=="save"||page=="load"){
   bool saving=page=="save";for(int n=0;n<(saving?9:11);++n){int slot=n;std::string label=n==9?"Quick save":n==10?"Checkpoint":"Save slot "+std::to_string(n+1);bool exists=saveExists[n];if(!exists)label+=" (empty)";button("slot:"+std::to_string(n),{84+(n%2)*690.0,170+(n/2)*110.0,658,90},label,[this,slot,saving]{ask(saving?"Overwrite this save slot?":"Load this save? Unsaved progress will be lost.",[this,slot,saving]{RuneMenu::Slot=slot;request(saving?6:7);});});}
  }else if(page=="about"){
   panel({84,160,1350,700});text(112,220,"Open Rune",40,gold);wrap(112,278,"An independent recreation of Rune using Surreal Engine, with modern rendering, controls and multiplayer. Requires your own original Rune game data. Not affiliated with the original game's owners.",1260,25,4);
   wrap(112,440,"Credits, licenses, third-party notices and applicable source offers are included beside the executable. Original game artwork and data remain subject to their original terms.",1260,24,4);
   button("notices",{112,620,650,76},"Open licenses & notices folder",[this]{auto p=fs::path(OS::executable_path())/"licenses";if(!fs::is_directory(p))throw std::runtime_error("The licenses folder is missing from this build.");ShellExecuteW(nullptr,L"open",p.c_str(),nullptr,nullptr,SW_SHOWNORMAL);});
  }
 }
 if(comparing)button("restore-preview",{1030,94,400,60},"Restore my visuals",[this]{comparing=false;apply();});
 if(!message.empty()) {panel({84,850,946,46});text(100,881,message,20,gold,910);}
 if(session.Busy())text(650,115,session.Message,22,gold,760);
 if(character>=0){hits.clear();c.fillRect({0,0,1536,1024},Colorf(0,0,0,.86f));panel({330,40,876,940});auto& m=openrune::Characters[character];text(390,110,m.name,40,gold,750);drawCharacter(character,skin,{512,139,512,597},true);text(390,779,"Drag to rotate  ·  "+std::string(openrune::skinName({(uint8_t)character,(uint8_t)skin})),23,muted,740);if(m.skins>1)button("skin",{390,810,350,64},"Next skin",[this]{skin=(skin+1)%openrune::Characters[character].skins;});button("choose-skin",{760,810,380,64},"Select warrior",[this]{int packed=character|(skin<<8);if(campaign)RuneCharacters::Campaign=packed;else RuneCharacters::Multiplayer=packed;save();message="Character and skin selected.";character=-1;});button("close-preview",{390,900,350,54},"Back",[this]{character=-1;});}
 if(acceptText){hits.clear();c.fillRect({0,0,1536,1024},Colorf(0,0,0,.88f));panel({220,280,1096,440});text(260,350,editTitle,30,gold,1010);panel({260,390,1016,100});text(280,453,edit+"|",26,ivory,976);text(260,535,"Type to edit · Ctrl+A clears · Ctrl+V pastes · Enter confirms",20,muted,1010);if(!message.empty())text(260,575,message,20,gold,1010);button("input-ok",{950,600,326,68},"Confirm",[this]{confirmInput();});button("input-cancel",{260,600,300,68},"Cancel",[this]{acceptText={};});}
 if(!modal.empty()){hits.clear();c.fillRect({0,0,1536,1024},Colorf(0,0,0,.88f));panel({270,300,996,370});wrap(310,380,modal,910,29,4);button("confirm",{890,560,330,68},"Confirm",[this]{auto fn=confirmed;modal.clear();confirmed={};fn();});button("dismiss",{310,560,330,68},"Cancel",[this]{modal.clear();});}
}
}

bool Enabled(const Engine& game){return state&&state->game==&game;}
void Start(Engine& game,bool title){if(!game.LaunchInfo.IsRune())return;RuneUpdates::Check();state=std::make_unique<State>();auto& s=*state;s.game=&game;s.title=title;
 auto add=[&](std::string key,std::string name,std::string hint,std::vector<std::string> choices,int value,int minimum,int maximum){s.options.push_back({key,name,hint,choices,value,value,minimum,maximum});};
 #include "RuneDesktopSettings.inc"
 s.config=Directory::localAppData()/"SurrealEngine/DesktopMenu.json";fs::create_directories(s.config.parent_path());
 try{if(fs::is_regular_file(s.config)){auto j=JsonValue::parse(File::read_all_text(s.config.string()));RuneMultiplayer::SetPlayerName(j["playerName"].to_string());for(auto& x:s.options)if(j[x.key=="snow"?"snowOverride":x.key].type()==JsonType::number)x.value=std::clamp(j[x.key=="snow"?"snowOverride":x.key].to_int(),x.minimum,x.maximum);if(!j["normalMovesDefaultV2"].to_boolean())s.get("normalBloodlust").value=1;uint32_t migrated=RuneBattleLaws::DefaultCampaign;
 for(auto pair:{std::pair{"kick",RuneBattleLaws::Kick},{"bash",RuneBattleLaws::Bash},{"dualWield",RuneBattleLaws::Dual},{"tactics",RuneBattleLaws::Tactics},{"normalBloodlust",RuneBattleLaws::Normal}})if(!s.v(pair.first))migrated&=~pair.second;
 if(!s.v("bloodlustMoves"))migrated&=~RuneBattleLaws::Rush;
 s.campaignLaws=j["campaignLaws"].type()==JsonType::number?uint32_t(j["campaignLaws"].to_int())&RuneBattleLaws::All&~RuneBattleLaws::Leap:migrated;
 if(j["hostLaws"].type()==JsonType::number)s.host.laws=uint32_t(j["hostLaws"].to_int())&RuneBattleLaws::All;
 if(j["hostPhysics"].type()==JsonType::number)s.host.physics=std::clamp(j["hostPhysics"].to_int(),0,2);
 s.host.normalBloodlust=(s.host.laws&RuneBattleLaws::Normal)!=0;
 int multiplayer=j["character"].to_int();if(openrune::validCharacter({(uint8_t)(multiplayer&255),(uint8_t)((multiplayer>>8)&255)}))RuneCharacters::Multiplayer=multiplayer;if(j["campaignCharacter"].type()==JsonType::number){int campaign=j["campaignCharacter"].to_int();if(campaign==-1||openrune::validCharacter({(uint8_t)(campaign&255),(uint8_t)((campaign>>8)&255)}))RuneCharacters::Campaign=campaign;}s.session.LastKey=j["lastKey"].to_string();auto name=j["hostName"].to_string();if(!name.empty())s.host.name=name;}}
 catch(const std::exception& e){s.message="Could not read saved menu settings; using defaults.";LogMessage(e.what());}
 s.xinput=LoadLibraryExW(L"xinput1_4.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
 if(s.xinput){s.getPad=(decltype(s.getPad))GetProcAddress(s.xinput,"XInputGetState");s.setPad=(decltype(s.setPad))GetProcAddress(s.xinput,"XInputSetState");}
 s.canvas=std::make_unique<MenuCanvas>(game.window->GetRenderDevice());s.home=Image::LoadResource("menu-background.png");s.settings=Image::LoadResource("settings-background.png");s.mark=Image::LoadResource("open-rune-mark.png");s.fullscreen=game.window->IsFullscreen();
 if(commandline->HasArg("","--desktop-menu-probe")){
  if(!commandline->HasArg("","--save-directory"))throw std::runtime_error("Menu probe requires isolated saves.");
  s.probe=commandline->GetArg("","--desktop-menu-probe","");fs::create_directories(s.probe);
 }
 s.apply();if(title)s.open();LogMessage("DESKTOP_MENU ready");
}
void Stop(Engine& game){if(Enabled(game))state.reset();}
float LookScale(){if(!state)return 1;const float values[]={.4f,.7f,1,1.4f,2};return values[state->v("sensitivity")];}
bool InvertLook(){return state&&state->v("invert");}
std::string StatusText(const Engine& game){
 if(!Enabled(game)||!state->v("counter")||RuneMenu::Open)return {};
 auto d=game.window->GetRenderDevice();return "FPS "+std::to_string((int)std::round(VisualComparison::EngineFps.load()))+"  |  "+(d->UpscaleSGSR?"Software ":"Native ")+std::to_string((int)std::round(d->ResolutionScale*100))+"%";
}
void Tick(Engine& game){if(!Enabled(game))return;auto& s=*state;try{
 if(!s.probe.empty()){
  auto command=s.probe/"command.json";
  if(fs::is_regular_file(command)&&fs::file_size(command)<8192){auto j=JsonValue::parse(File::read_all_text(command.string()));int id=j["id"].to_int();if(id>s.probeId){s.probeId=id;auto action=j["action"].to_string();LogMessage("DESKTOP_PROBE id="+std::to_string(id)+" action="+action);
   if(action=="click"){auto name=j["target"].to_string();auto it=std::find_if(s.hits.begin(),s.hits.end(),[&](auto& h){return h.id==name;});if(it==s.hits.end())throw std::runtime_error("Probe target not visible: "+name);auto box=it->box;Point p{(box.x+box.width*.5+s.offset.x)*s.scale/game.window->GetDpiScale(),(box.y+box.height*.5+s.offset.y)*s.scale/game.window->GetDpiScale()};game.OnWindowMouseMove(p);game.OnWindowMouseDown(p,IK_LeftMouse);game.OnWindowMouseUp(p,IK_LeftMouse);}
   else if(action=="key"){game.OnWindowKeyDown((EInputKey)j["key"].to_int());game.OnWindowKeyUp((EInputKey)j["key"].to_int());}
   else if(action=="text"){s.edit.clear();game.OnWindowKeyChar(j["text"].to_string());}
   else if(action=="quit")game.quit=true;
  }}
  auto j=JsonValue::object();j["playerName"]=JsonValue::string(RuneMultiplayer::GetPlayerName());j["playerNamesVisible"]=JsonValue::boolean(RuneMultiplayer::ShowPlayerNames);j["inputActive"]=JsonValue::boolean(bool(s.acceptText));j["inputTitle"]=JsonValue::string(s.editTitle);j["inputValue"]=JsonValue::string(s.edit);j["id"]=JsonValue::number(s.probeId);j["page"]=JsonValue::string(s.page);j["section"]=JsonValue::string(s.section);j["tab"]=JsonValue::string(s.tab);j["focus"]=JsonValue::string(s.focus);j["comparing"]=JsonValue::boolean(s.comparing);j["campaignLaws"]=JsonValue::number(s.campaignLaws);j["hostLaws"]=JsonValue::number(s.host.laws);j["effectiveLaws"]=JsonValue::number(RuneBattleLaws::Mask());j["lawsScroll"]=JsonValue::number(s.scroll);j["hostNormalMoves"]=JsonValue::boolean(s.host.normalBloodlust);j["hostMaps"]=JsonValue::array();for(const auto& m:s.host.maps)j["hostMaps"].items().push_back(JsonValue::string(m));j["message"]=JsonValue::string(s.message);j["discovery"]=JsonValue::string(s.session.Message);j["hosting"]=JsonValue::boolean(s.session.Hosting());j["bots"]=JsonValue::number(s.session.BotCount());j["active"]=JsonValue::boolean(RuneMultiplayer::Active);j["status"]=JsonValue::string(RuneMultiplayer::GetStatus());j["hostKey"]=JsonValue::string(s.session.HostKey);j["rows"]=s.session.Rows;j["hits"]=JsonValue::array();for(auto& h:s.hits)j["hits"].items().push_back(JsonValue::string(h.id));j["open"]=JsonValue::boolean(RuneMenu::Open);j["aa"]=JsonValue::number(game.window->GetRenderDevice()->AntialiasMode);j["scale"]=JsonValue::number(game.window->GetRenderDevice()->ResolutionScale);j["shadowBudget"]=JsonValue::number(RuneShadows::Budget(game.window->GetRenderDevice()->WorldFeatures));j["worldFeatures"]=JsonValue::number(game.window->GetRenderDevice()->WorldFeatures);j["character"]=JsonValue::number(RuneCharacters::Multiplayer);j["campaignCharacter"]=JsonValue::number(RuneCharacters::Campaign);j["settings"]=JsonValue::object();for(auto& option:s.options)j["settings"][option.key]=JsonValue::number(option.value);
  std::ofstream f(s.probe/"state.tmp");f<<j.to_json();f.close();MoveFileExW((s.probe/"state.tmp").c_str(),(s.probe/"state.json").c_str(),MOVEFILE_REPLACE_EXISTING);
 }
 s.scanSaves();s.controller();s.session.Tick();auto invite=s.session.TakeInvite();if(!invite.empty())s.identified([&s,invite]{if(RuneMultiplayer::Join(invite)){s.title=false;s.lastInvitation=invite;s.save();s.close();}else s.message="Already connected to a match.";});
 bool active=RuneMultiplayer::Active;if(active&&!s.wasActive)s.apply();if(s.wasActive&&!active){s.apply();s.title=true;s.open();s.go("multiplayer");s.message=RuneMultiplayer::GetStatus();}s.wasActive=active;
 if(s.pending){int result=RuneCombat::SessionResult.exchange(0);if(result){s.pending=false;if(result==2||result==5){s.title=false;s.close();}else s.message=result==1?"Saved.":result==3?"That save slot is empty.":"Cannot save during a cinematic, after death or in multiplayer.";}}
 bool dead=RuneMenu::PlayerDead;if(dead&&!s.previousDead&&!RuneMenu::Open)s.open();s.previousDead=dead;if(RuneMenu::Open){RuneMenu::Covered=true;game.viewport->bShowWindowsMouse()=true;game.viewport->bWindowsMouseAvailable()=true;game.window->UnlockCursor();}
 auto now=std::chrono::steady_clock::now();float dt=std::min(.05f,std::chrono::duration<float>(now-s.last).count());s.last=now;if(!s.rotating&&s.character>=0){s.yaw+=s.spin*dt;s.spin*=std::exp(-3.0f*dt);}
 }catch(const std::exception& e){s.message=e.what();LogMessage("DESKTOP_MENU_ERROR "+s.message);}}
bool Draw(Engine& game){if(!Enabled(game)||!RuneMenu::Open)return false;auto& s=*state;auto d=game.window->GetRenderDevice();int w=game.window->GetNativePixelWidth(),h=game.window->GetNativePixelHeight();if(w<=0||h<=0)return true;s.scale=std::min(w/1536.0,h/1024.0);s.offset={(w/s.scale-1536)*.5,(h/s.scale-1024)*.5};s.canvas->configure(w,h,s.scale,s.offset);struct Restore {
 RenderDevice* d;float resolution,sharp,lift;bool gsr,enhanced,bloom;int world;
 ~Restore(){d->ResolutionScale=resolution;d->AdaptiveSharpness=sharp;d->ShadowLift=lift;d->UpscaleSGSR=gsr;d->VisualEnhanced=enhanced;d->Bloom=bloom;d->WorldFeatures=world;}
 } restore{d,d->ResolutionScale,d->AdaptiveSharpness,d->ShadowLift,d->UpscaleSGSR,d->VisualEnhanced,d->Bloom,d->WorldFeatures};
 d->ResolutionScale=1;d->AdaptiveSharpness=0;d->ShadowLift=0;d->UpscaleSGSR=false;d->VisualEnhanced=false;d->Bloom=false;d->WorldFeatures=0;s.canvas->begin(Colorf(0,0,0));try{s.draw();}catch(...){s.canvas->end();throw;}s.canvas->end();
 if(!s.probe.empty()){auto trigger=s.probe/"capture";if(fs::exists(trigger)){fs::remove(trigger);std::vector<TextureColor> pixels((size_t)w*h);d->ReadPixels(pixels.data());std::ofstream f(s.probe/"frame.ppm",std::ios::binary);f<<"P6\n"<<w<<' '<<h<<"\n255\n";std::vector<char> rgb(pixels.size()*3);size_t i=0;for(auto p:pixels){rgb[i++]=p.R;rgb[i++]=p.G;rgb[i++]=p.B;}f.write(rgb.data(),rgb.size());}}
 return true;}
bool Mouse(Engine& game,const Point& pos,int action,int key){if(!Enabled(game)||!RuneMenu::Open)return false;auto& s=*state;Point old=s.mouse;s.mouse={pos.x*game.window->GetDpiScale()/s.scale-s.offset.x,pos.y*game.window->GetDpiScale()/s.scale-s.offset.y};
 if(action==0&&s.rotating){float delta=(float)(s.mouse.x-old.x)*.018f;s.yaw+=delta;s.spin=std::clamp(delta*90.0f,-15.0f,15.0f);return true;}
 if(s.laws()&&s.dragging=="laws-scroll"&&(action==0||action==2)){double thumb=double(s.LawViewport)*s.LawViewport/(int(RuneBattleLaws::Entries.size())*s.LawHeight);s.scroll=std::clamp(int((s.mouse.y-s.LawTop-thumb*.5)/(s.LawViewport-thumb)*s.LawMax),0,s.LawMax);if(action==2)s.dragging.clear();return true;}
 if(s.laws()&&action==1&&key==IK_LeftMouse&&Rect(1412,s.LawTop,24,s.LawViewport).contains(s.mouse)){s.dragging="laws-scroll";double thumb=double(s.LawViewport)*s.LawViewport/(int(RuneBattleLaws::Entries.size())*s.LawHeight);s.scroll=std::clamp(int((s.mouse.y-s.LawTop-thumb*.5)/(s.LawViewport-thumb)*s.LawMax),0,s.LawMax);return true;}
 if(action==3){s.scroll=std::clamp(s.scroll+(s.laws()?96:1)*(key==IK_MouseWheelDown?1:-1),0,s.laws()?s.LawMax:100000);return true;}
 if(action==1&&key==IK_LeftMouse){if(s.character>=0&&Rect(460,130,620,620).contains(s.mouse)){s.rotating=true;s.spin=0;return true;}s.pressed.clear();for(auto& h:s.hits)if(h.box.contains(s.mouse)){s.pressed=h.id;s.focus=h.id;break;}}
 if(action==2&&key==IK_LeftMouse){if(s.rotating){s.rotating=false;return true;}for(auto& h:s.hits)if(h.id==s.pressed&&h.box.contains(s.mouse)){auto fn=h.action;s.pressed.clear();try{fn();}catch(const std::exception& e){s.message=e.what();}break;}s.pressed.clear();}return true;}
bool Key(Engine& game,int key,bool down){if(!Enabled(game))return false;auto& s=*state;if(!RuneMenu::Open){if(key==IK_Escape&&down){s.open();return true;}return false;}if(!down)return true;
 try{if(key==IK_Escape){s.back();return true;}if(s.acceptText){if(key==IK_Backspace&&!s.edit.empty()){unsigned char c;do{c=(unsigned char)s.edit.back();s.edit.pop_back();}while((c&192)==128&&!s.edit.empty());}else if(key==IK_Enter){s.confirmInput();}else if(key==IK_A&&(GetKeyState(VK_CONTROL)&0x8000))s.edit.clear();else if(key==IK_V&&(GetKeyState(VK_CONTROL)&0x8000)){auto text=game.window->GetClipboardText();if(s.edit.size()+text.size()<=4096)s.edit+=text;}return true;}
 if(key==IK_PageDown||key==IK_PageUp){s.scroll=std::max(0,s.scroll+(key==IK_PageDown?1:-1)*(s.laws()?500:5));return true;}
 if(s.laws()&&s.focus.starts_with("law:")&&(key==IK_Down||key==IK_Up)){
  int step=key==IK_Down?1:-1,i=std::stoi(s.focus.substr(4))+step;while(i>=0&&i<int(RuneBattleLaws::Entries.size())&&s.lawLocked(i))i+=step;if(i>=0&&i<int(RuneBattleLaws::Entries.size())){s.focus="law:"+std::to_string(i);s.revealLaw(i);s.mouse={-100,-100};return true;}
 }
 if(key==IK_Tab||key==IK_Down||key==IK_Up||key==IK_Left||key==IK_Right){
  if(s.hits.empty())return true;auto current=std::find_if(s.hits.begin(),s.hits.end(),[&](const auto& h){return h.id==s.focus;});
  if(key==IK_Tab){size_t index=current==s.hits.end()?0:(size_t(current-s.hits.begin())+1)%s.hits.size();s.focus=s.hits[index].id;}
  else if(current==s.hits.end()){auto first=std::min_element(s.hits.begin(),s.hits.end(),[](const auto& a,const auto& b){return a.box.y==b.box.y?a.box.x<b.box.x:a.box.y<b.box.y;});s.focus=first->id;}
  else {auto center=[](const Rect& r){return Point{r.x+r.width*.5,r.y+r.height*.5};};auto origin=center(current->box);double best=1e30;std::string next=s.focus;
   for(const auto& h:s.hits){if(h.id==s.focus)continue;auto point=center(h.box);double dx=point.x-origin.x,dy=point.y-origin.y;double primary=key==IK_Left?-dx:key==IK_Right?dx:key==IK_Up?-dy:dy;double cross=(key==IK_Left||key==IK_Right)?std::abs(dy):std::abs(dx);if(primary<=1)continue;double cost=primary+3*cross+cross*cross/(primary+1);if(cost<best){best=cost;next=h.id;}}s.focus=next;
  }s.mouse={-100,-100};
 }
 if(key==IK_Enter||key==IK_Space)for(auto& h:s.hits)if(h.id==s.focus){auto fn=h.action;fn();break;}
 }catch(const std::exception& e){s.message=e.what();}return true;}
bool Text(Engine& game,const std::string& chars){if(!Enabled(game)||!RuneMenu::Open)return false;auto& s=*state;if(s.acceptText&&!(GetKeyState(VK_CONTROL)&0x8000))for(unsigned char c:chars)if(c>=32&&c!=127&&s.edit.size()<4096)s.edit+=(char)c;return true;}
}
