#pragma once
#include "LightingVisibilityBvh.h"
#include "Packages/Engine/Resources/Level/UModel.h"
#include <memory>
#include <stdexcept>

inline std::unique_ptr<LightingVisibilityBvh> BuildLightingVisibilityBvh(UModel* model)
{
    std::vector<LightingVisibilityBvh::Triangle> triangles;
    std::vector<LightingVisibilityBvh::Path> paths;
    struct Branch {int node,path,depth;};std::vector<Branch> pending;
    if(!model->Nodes.empty())pending.push_back({0,-1,0});
    while(!pending.empty()) {
        auto branch=pending.back();pending.pop_back();int root=branch.node;
        if(root<0)continue;
        if(std::size_t(root)>=model->Nodes.size())throw std::runtime_error("Invalid visibility BSP root");
        if(std::size_t(branch.depth)>model->Nodes.size())throw std::runtime_error("Cyclic visibility BSP tree");
        // The original traversal expands children of the root, and only the
        // polygons of its coplanar chain. Do not invent children for that chain.
        const auto& head=model->Nodes[root];dvec4 ancestor(head.PlaneX,head.PlaneY,head.PlaneZ,-head.PlaneW);
        for(bool front:{false,true}){int child=front?head.Front:head.Back;if(child>=0){int path=int(paths.size());paths.push_back({ancestor,branch.path,front});pending.push_back({child,path,branch.depth+1});}}
        int planeCount=0;
        for(int i=root;i>=0;) {
            if(std::size_t(i)>=model->Nodes.size())throw std::runtime_error("Invalid visibility BSP plane chain");
            if(std::size_t(++planeCount)>model->Nodes.size())throw std::runtime_error("Cyclic visibility plane chain");
            const auto& node=model->Nodes[i];i=node.Plane;
            uint32_t flags=node.Surf>=0&&std::size_t(node.Surf)<model->Surfaces.size()?model->Surfaces[node.Surf].PolyFlags:0;
            if(node.NumVertices<3||(flags&PF_NotSolid)||(node.NodeFlags&NF_NotVisBlocking))continue;
            auto point=[&](int vertex){
                int v=node.VertPool+vertex;
                if(v<0||std::size_t(v)>=model->Vertices.size())throw std::runtime_error("Invalid visibility BSP vertex");
                int p=model->Vertices[v].Vertex;
                if(p<0||std::size_t(p)>=model->Points.size())throw std::runtime_error("Invalid visibility BSP point");
                return dvec3(model->Points[p]);
            };
            auto a=point(0),b=point(1);dvec4 plane(node.PlaneX,node.PlaneY,node.PlaneZ,-node.PlaneW);
            for(int vertex=2;vertex<node.NumVertices;++vertex){auto c=point(vertex);triangles.push_back({a,b,c,plane,branch.path});b=c;}
        }
    }
    return std::make_unique<LightingVisibilityBvh>(std::move(triangles),std::move(paths));
}
