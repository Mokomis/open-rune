
#include "Precomp.h"
#include "Packages/Engine/Resources/Level/UModel.h"
#include "OverlapTest.h"
#include "Collision/BottomLevel/OverlapAABBModel.h"
#include "Packages/Engine/Actors/UActor.h"
#include "Packages/Engine/Actors/Info/ULevelInfo.h"
#include "Packages/Engine/Actors/Brush/MovingBrush.h"

CollisionHitList OverlapTester::TestOverlap(const vec3& location, float height, float radius, bool testActors, bool testWorld, bool visibilityOnly)
{
	if (!testActors && !testWorld)
		return {};

	vec3 extents = { radius, radius, height };

	CollisionHitList hits;

	if ((testActors && !visibilityOnly) || (testWorld && engine->LaunchInfo.IsRune()))
	{
		double dradius = radius;
		double dheight = height;
		dvec3 dlocation = to_dvec3(location);

		int checkCounter = NextCheckCounter();
		ivec3 start = GetStartExtents(location, extents);
		ivec3 end = GetEndExtents(location, extents);
		if (end.x - start.x < 100 && end.y - start.y < 100 && end.z - start.z < 100)
		{
			for (int z = start.z; z < end.z; z++)
			{
				for (int y = start.y; y < end.y; y++)
				{
					for (int x = start.x; x < end.x; x++)
					{
						for (UActor* actor : GetActors(x, y, z))
						{
							if (actor->Collision.CheckCounter != checkCounter)
							{
								actor->Collision.CheckCounter = checkCounter;
								if (IsRunePolyobj(actor) ? ((testWorld || (testActors && !visibilityOnly)) && CylinderActorOverlap(dlocation, dheight, dradius, actor))
								: (testActors && !visibilityOnly && CylinderActorOverlap(dlocation, dheight, dradius, actor)))
								{
									vec3 normal(0.0f); // To do: do we need the normal for contact tests?
									hits.push_back({ 0.0f, normal, actor, nullptr, nullptr });
								}
							}
						}
					}
				}
			}
		}
	}

	if (testWorld)
	{
		if (extents != vec3(0.0f))
		{
			OverlapAABBModel overlapmodel;
			CollisionHitList worldHits = overlapmodel.TestOverlap(GetLevelInfo(), GetLevel()->Model, location, extents, visibilityOnly);
			hits.push_back(worldHits);
		}
	}

	// Only include each actor once

	std::stable_sort(hits.begin(), hits.end(), [](const auto& a, const auto& b) { return a.Actor < b.Actor; });

	UActor* prevActor = nullptr;
	CollisionHitList uniqueHits;
	for (auto& hit : hits)
	{
		if (hit.Actor)
		{
			if (hit.Actor != prevActor)
			{
				prevActor = hit.Actor;
				uniqueHits.push_back(hit);
			}
		}
		else
		{
			uniqueHits.push_back(hit);
		}
	}

	return uniqueHits;
}

Array<UActor*> OverlapTester::CollidingActors(const vec3& origin, float radius)
{
	dvec3 dorigin = to_dvec3(origin);
	double dradius = radius;
	vec3 extents = { radius, radius, radius };

	Array<UActor*> hits;

	int checkCounter = NextCheckCounter();
	ivec3 start = GetStartExtents(origin, extents);
	ivec3 end = GetEndExtents(origin, extents);
	if (end.x - start.x < 100 && end.y - start.y < 100 && end.z - start.z < 100)
	{
		for (int z = start.z; z < end.z; z++)
		{
			for (int y = start.y; y < end.y; y++)
			{
				for (int x = start.x; x < end.x; x++)
				{
					for (UActor* actor : GetActors(x, y, z))
					{
						if (actor->Collision.CheckCounter != checkCounter)
						{
							actor->Collision.CheckCounter = checkCounter;
							if (SphereActorOverlap(dorigin, dradius, actor))
								hits.push_back(actor);
						}
					}
				}
			}
		}
	}

	std::set<UActor*> seenActors;
	Array<UActor*> uniqueHits;
	uniqueHits.reserve(hits.size());
	for (auto& hit : hits)
	{
		if (hit)
		{
			if (seenActors.find(hit) == seenActors.end())
			{
				seenActors.insert(hit);
				uniqueHits.push_back(hit);
			}
		}
		else
		{
			uniqueHits.push_back(hit);
		}
	}

	return uniqueHits;
}

Array<UActor*> OverlapTester::CollidingActors(const vec3& origin, float height, float radius)
{
	dvec3 dorigin = to_dvec3(origin);
	double dheight = height;
	double dradius = radius;
	vec3 extents = { radius, radius, height };

	Array<UActor*> hits;

	int checkCounter = NextCheckCounter();
	ivec3 start = GetStartExtents(origin, extents);
	ivec3 end = GetEndExtents(origin, extents);
	if (end.x - start.x < 100 && end.y - start.y < 100 && end.z - start.z < 100)
	{
		for (int z = start.z; z < end.z; z++)
		{
			for (int y = start.y; y < end.y; y++)
			{
				for (int x = start.x; x < end.x; x++)
				{
					for (UActor* actor : GetActors(x, y, z))
					{
						if (actor->Collision.CheckCounter != checkCounter)
						{
							actor->Collision.CheckCounter = checkCounter;
							if (CylinderActorOverlap(dorigin, dheight, dradius, actor))
								hits.push_back(actor);
						}
					}
				}
			}
		}
	}

	std::set<UActor*> seenActors;
	Array<UActor*> uniqueHits;
	uniqueHits.reserve(hits.size());
	for (auto& hit : hits)
	{
		if (hit)
		{
			if (seenActors.find(hit) == seenActors.end())
			{
				seenActors.insert(hit);
				uniqueHits.push_back(hit);
			}
		}
		else
		{
			uniqueHits.push_back(hit);
		}
	}

	return uniqueHits;
}

Array<UActor*> OverlapTester::EncroachingActors(UActor* actor)
{
	UModel* brush = actor->Brush();
	if (!brush)
		return CollidingActors(actor->Location(), actor->CollisionHeight(), actor->CollisionRadius());

	if (brush->Nodes.empty()) return {};

	// Rune Polyobj origins and cylinder bounds need not lie inside their geometry.

	vec3 origin = actor->Location();
	float height = actor->CollisionHeight();
	float radius = actor->CollisionRadius();
	vec3 extents = { radius, radius, height };

	mat4 rotateObjToWorld = Coords::Rotation(actor->Rotation()).ToMatrix();
	mat4 rotateWorldToObj = mat4::transpose(rotateObjToWorld);
	vec3 prePivot = actor->PrePivot();

	vec3 mainScale(1.0f);
	if (UBrush* mover = GetMovingBrush(actor))
	{
		mainScale = mover->MainScale().Scale;
	}

	Array<UActor*> hits;

	int checkCounter = NextCheckCounter();
	vec3 broadOrigin = origin;
	if (IsRunePolyobj(actor))
	{
		BBox bounds = brush->BoundingBox.transform(GetMovingBrushTransform(GetMovingBrush(actor)));
		broadOrigin = bounds.center();
		extents = bounds.extents() + 0.1f;
	}
	ivec3 start = GetStartExtents(broadOrigin, extents);
	ivec3 end = GetEndExtents(broadOrigin, extents);
	if (end.x - start.x < 100 && end.y - start.y < 100 && end.z - start.z < 100)
	{
		for (int z = start.z; z < end.z; z++)
		{
			for (int y = start.y; y < end.y; y++)
			{
				for (int x = start.x; x < end.x; x++)
				{
					for (UActor* testActor : GetActors(x, y, z))
					{
						if (testActor->Collision.CheckCounter != checkCounter)
						{
							testActor->Collision.CheckCounter = checkCounter;
							if (testActor == actor || testActor->Brush())
								continue;

							vec3 localOrigin = (rotateWorldToObj * vec4(testActor->Location() - origin, 1.0f)).xyz() / mainScale + prePivot;

							float testHeight = testActor->CollisionHeight();
							float testRadius = testActor->CollisionRadius();
							vec3 testExtents = { testRadius, testRadius, testHeight };
							if (IsRunePolyobj(actor))
								testExtents = MovingBrushLocalExtents(GetMovingBrush(actor), testExtents);
							else
								testExtents /= mainScale;

							OverlapAABBModel test;
							auto modelHits = test.TestOverlap(actor, brush, localOrigin, testExtents, false);
							if (!modelHits.empty())
								hits.push_back(testActor);
						}
					}
				}
			}
		}
	}

	std::set<UActor*> seenActors;
	Array<UActor*> uniqueHits;
	uniqueHits.reserve(hits.size());
	for (auto& hit : hits)
	{
		if (hit)
		{
			if (seenActors.find(hit) == seenActors.end())
			{
				seenActors.insert(hit);
				uniqueHits.push_back(hit);
			}
		}
		else
		{
			uniqueHits.push_back(hit);
		}
	}

	return uniqueHits;
}

bool OverlapTester::IsOverlapping(UActor* actor1, UActor* actor2)
{
	if (IsRunePolyobj(actor1) && !actor2->Brush())
		return CylinderActorOverlap(to_dvec3(actor2->Location()), actor2->CollisionHeight(), actor2->CollisionRadius(), actor1);
	return CylinderActorOverlap(to_dvec3(actor1->Location()), actor1->CollisionHeight(), actor1->CollisionRadius(), actor2);
}

bool OverlapTester::CylinderCylinderOverlap(const dvec3& cylinderCenterA, double cylinderHeightA, double cylinderRadiusA, const dvec3& cylinderCenterB, double cylinderHeightB, double cylinderRadiusB)
{
	dvec3 dist = cylinderCenterA - cylinderCenterB;
	double h = cylinderHeightA + cylinderHeightB;
	double r = cylinderRadiusA + cylinderRadiusB;
	return std::abs(dist.z) < h && dot(dist.xy(), dist.xy()) < r * r;
}

// Returns the squared distance between point c and segment ab
static double SqDistPointSegment(const dvec3& a, const dvec3& b, const dvec3& c)
{
	dvec3 ab = b - a;
	dvec3 ac = c - a;
	double e = dot(ac, ab);

	// Handle cases where c projects outside ab
	if (e <= 0.0)
	{
		return dot(ac, ac);
	}
	double f = dot(ab, ab);
	if (e >= f)
	{
		dvec3 bc = c - b;
		return dot(bc, bc);
	}

	// Handle cases where c projects onto ab
	return dot(ac, ac) - e * e / f;
}

bool OverlapTester::SphereCapsuleOverlap(const dvec3& sphereCenter, double sphereRadius, const dvec3& capsuleCenter, double capsuleHeight, double capsuleRadius)
{
	dvec3 base = capsuleCenter - capsuleHeight;
	dvec3 tip = capsuleCenter + capsuleHeight;

	// Compute (squared) distance between sphere center and capsule line segment
	double dist2 = SqDistPointSegment(base, tip, sphereCenter);

	// If (squared) distance smaller than (squared) sum of radii, they collide
	double totalradius = sphereRadius + capsuleRadius;
	return dist2 <= totalradius * totalradius;
}

bool OverlapTester::SphereActorOverlap(const dvec3& sphereCenter, double sphereRadius, UActor* actor)
{
	if (actor->Brush()) // Ignore brushes for now
		return false;

	dvec3 center = to_dvec3(actor->Location());
	double height = actor->CollisionHeight();
	double radius = actor->CollisionRadius();

	// Note: this isn't entirely accurate - what does UE1 do?
	return SphereCapsuleOverlap(sphereCenter, sphereRadius, center, std::max(height - radius, 0.0), radius);
}

bool OverlapTester::CylinderActorOverlap(const dvec3& origin, double cylinderHeight, double cylinderRadius, UActor* actor)
{
	if (IsRunePolyobj(actor))
	{
		auto* brush = GetMovingBrush(actor);
		if (!brush || !brush->Brush() || brush->Brush()->Nodes.empty()) return false;
		mat4 inverseRotation = mat4::transpose(Coords::Rotation(actor->Rotation()).ToMatrix());
		vec3 localOrigin = (inverseRotation * vec4(to_vec3(origin) - actor->Location(), 0)).xyz()
			/ brush->MainScale().Scale + actor->PrePivot();
		vec3 extents = MovingBrushLocalExtents(brush, vec3((float)cylinderRadius, (float)cylinderRadius, (float)cylinderHeight));
		OverlapAABBModel overlap;
		return !overlap.TestOverlap(actor, brush->Brush(), localOrigin, extents, false).empty();
	}
	if (actor->Brush()) // Other brush overlap behavior is unchanged.
		return false;

	dvec3 center = to_dvec3(actor->Location());
	double height = actor->CollisionHeight();
	double radius = actor->CollisionRadius();
	return CylinderCylinderOverlap(origin, cylinderHeight, cylinderRadius, center, height, radius);
}
