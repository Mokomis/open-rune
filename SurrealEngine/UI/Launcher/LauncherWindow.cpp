
#include "Precomp.h"
#include "LauncherWindow.h"
#include "RuneUpdatesPage.h"
#include "LauncherBanner.h"
#include "LauncherButtonbar.h"
#include "PlayGamePage.h"
#include "VideoSettingsPage.h"
// #include "AudioSettingsPage.h"
#include "GameFoldersPage.h"
#include "LauncherSettings.h"
#include <surrealwidgets/core/resourcedata.h>
#include <surrealwidgets/window/window.h>
#include <surrealwidgets/widgets/tabwidget/tabwidget.h>
#include <surrealwidgets/widgets/layout/vboxlayout.h>
#include <surrealwidgets/widgets/dialog/messagebox.h>

int LauncherWindow::ExecModal()
{
	Size screenSize = GetScreenSize();
	double windowWidth = 1024.0;
	double windowHeight = 675.0;

	auto launcher = std::make_unique<LauncherWindow>();
	launcher->SetFrameGeometry((screenSize.width - windowWidth) * 0.5, (screenSize.height - windowHeight) * 0.5, windowWidth, windowHeight);
	launcher->Show();

	DisplayWindow::RunLoop();

	return launcher->ExecResult;
}

LauncherWindow::LauncherWindow() : Widget(nullptr, WidgetType::Window)
{
	SetWindowTitle("Open Rune");
	SetWindowIcon({Image::LoadResource("open-rune-mark.png")});

	Banner = new LauncherBanner(this);
	Pages = new TabWidget(this);
	Buttonbar = new LauncherButtonbar(this);

	PlayGame = new PlayGamePage(this);
	GraphicsSettings = new VideoSettingsPage(this);
	// AudioSettings = new AudioSettingsPage(this);
	GameFolders = new GameFoldersPage(this);

	Pages->AddTab(PlayGame, "Games");
	Pages->AddTab(GameFolders, "Folders");
	Pages->AddTab(GraphicsSettings, "Video Settings");
	Pages->AddTab(new RuneUpdatesPage(this), "Updates");
	// Pages->AddTab(AudioSettings, "Audio Settings");

	Pages->SetCurrentWidget(PlayGame);
	PlayGame->SetFocus();

	auto mainLayout = new VBoxLayout();

	mainLayout->AddWidget(Banner);
	mainLayout->AddWidget(Pages);
	mainLayout->AddWidget(Buttonbar);

	mainLayout->SetGapHeight(0);

	SetLayout(mainLayout);
}

void LauncherWindow::Save()
{
	PlayGame->Save();
	GraphicsSettings->Save();
	// AudioSettings->Save();
	GameFolders->Save();
	LauncherSettings::Get().Save();
}

void LauncherWindow::GamesListChanged()
{
	PlayGame->UpdateList();
}

void LauncherWindow::Start()
{
	Save();
	ExecResult = PlayGame->GetSelectedGame();
	DisplayWindow::ExitLoop();
}

void LauncherWindow::Exit()
{
	// Hehe, this is too annoying, but it is an easy way to test the message box with this theme
	// if (MessageBox::Question(this, "Are you sure you want to exit?", "Exit Open Rune") == DialogButton::Yes)
	{
		Save();
		ExecResult = -1;
		DisplayWindow::ExitLoop();
	}
}

void LauncherWindow::OnClose()
{
	Exit();
}
