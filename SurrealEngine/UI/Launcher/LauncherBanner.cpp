
#include "Precomp.h"
#include "LauncherBanner.h"
#include <surrealwidgets/widgets/imagebox/imagebox.h>
#include <surrealwidgets/widgets/textlabel/textlabel.h>
#include <surrealwidgets/core/image.h>

LauncherBanner::LauncherBanner(Widget* parent) : Widget(parent)
{
	SetStyleClass("launcher-banner");

	Logo = new ImageBox(this);
	VersionLabel = new TextLabel(this);
	VersionLabel->SetTextAlignment(TextLabelAlignment::Left);
	VersionLabel->SetText("OPEN RUNE");

	Logo->SetImage(Image::LoadResource("open-rune-mark.png"));
	Logo->SetImageMode(ImageBoxMode::Contain);
}

double LauncherBanner::GetPreferredHeight()
{
	return 100.0;
}

double LauncherBanner::GetPreferredWidth()
{
	return 360.0;
}


void LauncherBanner::OnGeometryChanged()
{
	Logo->SetFrameGeometry(12.0, 2.0, 96.0, 96.0);
	VersionLabel->SetFrameGeometry(120.0, (GetHeight()-VersionLabel->GetPreferredHeight())*0.5, GetWidth()-140.0, VersionLabel->GetPreferredHeight());
}
