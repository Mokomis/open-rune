#pragma once
#include <surrealwidgets/core/widget.h>
#include <surrealwidgets/core/timer.h>
class TextLabel;class PushButton;
class RuneUpdatesPage final:public Widget {
public:
 explicit RuneUpdatesPage(Widget* parent);
private:
 void Refresh();void OnGeometryChanged()override;
 Timer poll;TextLabel *identity,*status,*notes;PushButton *check,*automatic,*release;
};
// Standalone entry point for clients whose in-game menu is still script-owned.
int ShowRuneUpdatesWindow();
