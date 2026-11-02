#pragma once

class VisibleFrame;
class BspNode;

class VisibleNode
{
public:
	void Draw(VisibleFrame* frame);
    bool WaterPlane(float& height) const;

	BspNode* Node = nullptr;
	bool Front = false;
	uint32_t PolyFlags = 0;
};
