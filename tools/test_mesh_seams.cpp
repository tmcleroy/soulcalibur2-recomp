// SPDX-License-Identifier: GPL-3.0-or-later
// Regression over private renderer captures; no guest execution is needed.
#include "d3d_visual_interpolation.h"
#include <array>
#include <fstream>
#include <map>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
struct Draw {
    RecompD3dPresenterDrawCommand d{};
    std::vector<unsigned char> vertices;
    std::vector<uint16_t> indices;
    void bind(){d.vertex_bytes=vertices.data();d.index_bytes=indices.data();}
};
std::map<unsigned,Draw> load(const char *path) {
    std::ifstream f(path,std::ios::binary);if(!f)throw std::runtime_error("capture unavailable");
    std::map<unsigned,Draw> out;
    while(f.peek()!=EOF) {
        uint32_t h[6];f.read(reinterpret_cast<char*>(h),sizeof h);
        if(!f || h[2]<12 || h[2]>256 || h[3]>1000000 || h[4]>3000000)throw std::runtime_error("bad capture");
        auto &r=out[h[0]];auto &d=r.d;
        d.fvf=h[1];d.vertex_stride=h[2];d.vertex_count=h[3];d.index_count=h[4];d.texture.data=h[5];d.has_transform=true;
        f.read(reinterpret_cast<char*>(d.transform),sizeof d.transform);
        r.vertices.resize(size_t(h[2])*h[3]);r.indices.resize(h[4]);
        f.read(reinterpret_cast<char*>(r.vertices.data()),r.vertices.size());
        f.read(reinterpret_cast<char*>(r.indices.data()),r.indices.size()*2);
        if(!f)throw std::runtime_error("truncated capture");r.bind();
    }
    return out;
}
std::array<float,3> position(const RecompD3dPresenterDrawCommand &d,unsigned i) {
    std::array<float,3> v;std::memcpy(v.data(),static_cast<const char*>(d.vertex_bytes)+size_t(i)*d.vertex_stride,12);return v;
}
std::array<double,2> project(const RecompD3dPresenterDrawCommand &d,unsigned i) {
    const auto p=position(d,i);double q[4]{};
    for(unsigned k=0;k<4;++k)q[k]=p[0]*double(d.transform[k])+p[1]*double(d.transform[4+k])+p[2]*double(d.transform[8+k])+d.transform[12+k];
    return {q[0]/q[3]*1280,q[1]/q[3]*960};
}
int main(int argc,char **argv) {
    if(argc!=3)return 2;
    try {
        auto old=load(argv[1]),now=load(argv[2]);
        auto &hp=old.at(141).d;auto &hc=now.at(141).d;
        hp.primitive_type=hc.primitive_type=5;
        if(!std::memcmp(hp.index_bytes,hc.index_bytes,hc.index_count*2) ||
            !sc2_visual::sameTopology(hp,hc) || sc2_visual::key(hp)!=sc2_visual::key(hc))return 1;
        RecompD3dPresenterDrawCommand hair{};std::vector<unsigned char> hairVertices;
        if(!sc2_visual::interpolate(hp,hc,.5f,hair,hairVertices) || hair.index_bytes!=hc.index_bytes)return 1;
        unsigned shared=0;double before=0,after=0;
        for(unsigned id=85;id<112;++id) {
            auto &bp=old.at(id).d;auto &bc=now.at(id).d;
            bp.primitive_type=bc.primitive_type=6;
            RecompD3dPresenterDrawCommand body{};std::vector<unsigned char> bodyVertices;
            if(!sc2_visual::interpolate(bp,bc,.5f,body,bodyVertices))return 1;
            std::map<std::array<float,3>,unsigned> lookup;
            for(auto i:now.at(id).indices)lookup[position(bc,i)]=i;
            std::vector<uint16_t> hairIndices=now.at(141).indices;
            std::sort(hairIndices.begin(),hairIndices.end());hairIndices.erase(std::unique(hairIndices.begin(),hairIndices.end()),hairIndices.end());
            for(auto i:hairIndices) {
                const auto found=lookup.find(position(hc,i));
                if(found==lookup.end() || position(hp,i)!=position(bp,found->second))continue;
                auto b=project(body,found->second),h=project(hair,i),unblended=project(hc,i);
                before=std::max(before,std::hypot(unblended[0]-b[0],unblended[1]-b[1]));
                after=std::max(after,std::hypot(h[0]-b[0],h[1]-b[1]));++shared;
            }
        }
        std::printf("Shared indexed seam vertices=%u; old gap=%.6f px; interpolated gap=%.9f px at 2560x1920\n",shared,before,after);
        return shared>=10 && before>1 && after<0.00001?0:1;
    } catch(const std::exception &e) {std::fprintf(stderr,"%s\n",e.what());return 1;}
}
