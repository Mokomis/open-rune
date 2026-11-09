#include "Precomp.h"
#include "RuneUpdatesPage.h"
#include "RuneUpdates.h"
#include <surrealwidgets/window/window.h>
#include <surrealwidgets/widgets/textlabel/textlabel.h>
#include <surrealwidgets/widgets/pushbutton/pushbutton.h>
RuneUpdatesPage::RuneUpdatesPage(Widget* parent):Widget(parent),poll(this){
 identity=new TextLabel(this);status=new TextLabel(this);notes=new TextLabel(this);
 notes->SetTextVerticalAlignment(TextLabelVerticalAlignment::Top);
 check=new PushButton(this);automatic=new PushButton(this);release=new PushButton(this);
 check->SetText("Check for updates");check->OnClick=[this]{RuneUpdates::Check(true);Refresh();};
 automatic->OnClick=[this]{RuneUpdates::SetAutomatic(!RuneUpdates::Automatic());Refresh();};
 release->OnClick=[this]{auto r=RuneUpdates::Current();if(!RuneUpdates::OpenPage(r.url.empty()?"https://github.com/Mokomis/open-rune/releases":r.url))status->SetText("Could not open your browser.");};
 poll.FuncExpired=[this]{Refresh();};poll.Start(500);RuneUpdates::Check();Refresh();
}
void RuneUpdatesPage::Refresh(){
 auto r=RuneUpdates::Current();identity->SetText(RuneUpdates::Identity());status->SetText(r.message);
 check->SetEnabled(!r.busy);automatic->SetText(RuneUpdates::Automatic()?"Startup checks: On":"Startup checks: Off");
 release->SetText(r.available?"View update & downloads":"Open releases page");
 // Complete release notes remain available on the release page.
 auto preview=r.notes.substr(0,500);if(r.notes.size()>preview.size())preview+="… (full notes on release page)";
 notes->SetText(preview.empty()?"Only published stable releases are offered.\nNetwork protocol and gameplay rules must match the host.\nUpdates do not replace your game files, saves or texture packs.":preview);
}
void RuneUpdatesPage::OnGeometryChanged(){
 auto w=std::max(100.0,GetWidth()-40);identity->SetFrameGeometry(20,15,w,45);status->SetFrameGeometry(20,65,w,65);
 check->SetFrameGeometry(20,145,220,48);automatic->SetFrameGeometry(260,145,220,48);release->SetFrameGeometry(500,145,std::max(170.0,w-480),48);
 notes->SetFrameGeometry(20,215,w,std::max(80.0,GetHeight()-230));
}
namespace {
class UpdatesWindow final:public Widget {
public:
 UpdatesWindow():Widget(nullptr,WidgetType::Window){
  SetWindowTitle("Open Rune · Updates");page=new RuneUpdatesPage(this);close=new PushButton(this);close->SetText("Close");close->OnClick=[this]{OnClose();};
 }
private:
 void OnClose()override{DisplayWindow::ExitLoop();}
 void OnGeometryChanged()override{page->SetFrameGeometry(0,0,GetWidth(),GetHeight()-70);close->SetFrameGeometry(GetWidth()-150,GetHeight()-60,130,40);}
 RuneUpdatesPage* page;PushButton* close;
};
}
int ShowRuneUpdatesWindow(){auto size=Widget::GetScreenSize();auto window=std::make_unique<UpdatesWindow>();window->SetFrameGeometry((size.width-960)/2,(size.height-650)/2,960,650);window->Show();DisplayWindow::RunLoop();return 0;}
