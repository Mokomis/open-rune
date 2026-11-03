#pragma once

#include "Math/vec.h"
#include <algorithm>
#include <cfloat>
#include <numeric>
#include <vector>

// Fixed-world visibility only. Moving occluders continue through collision's
// live non-level query; no gameplay collision traversal is replaced.
class LightingVisibilityBvh
{
public:
    struct Path { dvec4 plane;int parent=-1;bool front=false; };
    struct Triangle { dvec3 a,b,c; dvec4 plane;int path=-1; };
    explicit LightingVisibilityBvh(std::vector<Triangle> geometry,std::vector<Path> ancestry={}) : triangles(std::move(geometry)),paths(std::move(ancestry))
    {
        order.resize(triangles.size());std::iota(order.begin(),order.end(),0);
        if(!order.empty())Build(0,int(order.size()));
    }
    bool Trace(const vec3& from,const vec3& to) const
    {
        if(from==to||nodes.empty())return false;
        dvec3 origin(from),direction=dvec3(to)-origin;
        double distance=length(direction);
        if(distance<0.01f)return false;
        // Match the collision implementation's reciprocal multiplication,
        // including its rounding at polygon vertices and the float start bias.
        direction*=1.0f/distance;
        return TraceNode(0,origin,direction,0.01f,distance+1.0);
    }
    std::size_t TriangleCount() const { return triangles.size(); }
    bool TraceLinear(const vec3& from,const vec3& to) const
    {
        dvec3 origin(from),direction=dvec3(to)-origin;double distance=length(direction);
        if(distance<.01f)return false;direction*=1.0f/distance;
        for(const auto& triangle:triangles)if(Hit(triangle,origin,direction,.01f,distance+1)&&PathAllows(triangle.path,origin,direction,distance+1))return true;
        return false;
    }

private:
    struct Bounds
    {
        dvec3 min{DBL_MAX},max{-DBL_MAX};
        void Add(const dvec3& p){for(int k=0;k<3;++k){min[k]=std::min(min[k],p[k]);max[k]=std::max(max[k],p[k]);}}
        bool Intersects(const dvec3& origin,const dvec3& direction,double lo,double hi) const
        {
            for(int k=0;k<3;++k){
                if(direction[k]==0){if(origin[k]<min[k]||origin[k]>max[k])return false;continue;}
                double a=(min[k]-origin[k])/direction[k],b=(max[k]-origin[k])/direction[k];
                if(a>b)std::swap(a,b);
                lo=std::max(lo,a);hi=std::min(hi,b);
                if(lo>hi)return false;
            }
            return true;
        }
    };
    struct Node { Bounds bounds;int begin=0,count=0,left=-1,right=-1; };
    std::vector<Triangle> triangles;
    std::vector<Path> paths;
    std::vector<int> order;
    std::vector<Node> nodes;

    int Build(int begin,int end)
    {
        int index=int(nodes.size());nodes.emplace_back();
        Bounds bounds,centers;
        for(int i=begin;i<end;++i){const auto& t=triangles[order[i]];bounds.Add(t.a);bounds.Add(t.b);bounds.Add(t.c);centers.Add((t.a+t.b+t.c)/3.0);}
        // Outward padding protects edge/parallel slab tests from rounding. It
        // only admits extra candidates; exact triangle tests decide visibility.
        for(int k=0;k<3;++k){bounds.min[k]-=1e-6;bounds.max[k]+=1e-6;}
        nodes[index].bounds=bounds;
        if(end-begin<=4){nodes[index].begin=begin;nodes[index].count=end-begin;return index;}
        dvec3 extent=centers.max-centers.min;int axis=extent.y>extent.x?1:0;if(extent.z>extent[axis])axis=2;
        int middle=(begin+end)/2;
        std::nth_element(order.begin()+begin,order.begin()+middle,order.begin()+end,[&](int a,int b){
            const auto& x=triangles[a];const auto& y=triangles[b];return x.a[axis]+x.b[axis]+x.c[axis]<y.a[axis]+y.b[axis]+y.c[axis];
        });
        int left=Build(begin,middle),right=Build(middle,end);
        nodes[index].left=left;nodes[index].right=right;return index;
    }
    static bool Hit(const Triangle& triangle,const dvec3& origin,const dvec3& direction,double lo,double hi)
    {
        // Same plane guard, determinant tolerance, barycentrics and strict end
        // boundary as TraceRayModel::NodeRayIntersect/TriangleRayIntersect.
        double from=dot(dvec4(origin,1.0),triangle.plane),to=dot(dvec4(origin+direction*hi,1.0),triangle.plane);
        if((from>0&&to>0)||(from<0&&to<0))return false;
        dvec3 e1=triangle.b-triangle.a,e2=triangle.c-triangle.a,P=cross(direction,e2);
        double det=dot(e1,P);if(det>-FLT_EPSILON&&det<FLT_EPSILON)return false;
        double inverse=1.0/det;dvec3 T=origin-triangle.a;
        double u=dot(T,P)*inverse;if(u<0||u>1)return false;
        dvec3 Q=cross(T,e1);double v=dot(direction,Q)*inverse;if(v<0||u+v>1)return false;
        double t=dot(e2,Q)*inverse;return t>FLT_EPSILON&&t>=lo&&t<hi;
    }
    bool PathAllows(int index,const dvec3& origin,const dvec3& direction,double hi) const
    {
        // Some original polygons extend outside their BSP ancestor halfspaces.
        // Preserve the original branch predicates rather than treating every
        // geometric triangle intersection as a visibility blocker.
        auto start=dvec4(origin,1.0),end=dvec4(origin+direction*hi,1.0);
        while(index>=0){const auto& path=paths[index];double from=dot(start,path.plane),to=dot(end,path.plane);
            if(path.front ? !(from>=0||to>=0) : !(from<=0||to<=0))return false;
            index=path.parent;
        }
        return true;
    }
    bool TraceNode(int index,const dvec3& origin,const dvec3& direction,double lo,double hi) const
    {
        const auto& node=nodes[index];if(!node.bounds.Intersects(origin,direction,lo,hi))return false;
        if(node.count){for(int i=0;i<node.count;++i){const auto& triangle=triangles[order[node.begin+i]];if(Hit(triangle,origin,direction,lo,hi)&&PathAllows(triangle.path,origin,direction,hi))return true;}return false;}
        return TraceNode(node.left,origin,direction,lo,hi)||TraceNode(node.right,origin,direction,lo,hi);
    }
};
