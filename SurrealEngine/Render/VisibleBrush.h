#pragma once

class VisibleFrame;
class UActor;
class UModel;
class UBrush;
struct Poly;

class VisibleBrush
{
public:
	bool Draw(VisibleFrame* frame, UActor* actor, bool translucentPass);

private:
	bool DrawBrushPoly(VisibleFrame* frame, UModel* model, const Poly& poly, int pass, UBrush* mover, bool translucentPass);
};
