// Windows presentation of the default RuneHudLayout and RuneHudView artwork.
// Draw into the game framebuffer so screenshots and the live view use the same HUD.
#pragma once
#include <objidl.h>
namespace Gdiplus { using std::min; using std::max; }
#include <gdiplus.h>
#pragma comment(lib, "gdiplus.lib")

namespace WindowsRuneHud {
namespace gd = Gdiplus;
struct Runtime {
    ULONG_PTR token = 0;
    Runtime() { gd::GdiplusStartupInput options; gd::GdiplusStartup(&token, &options, nullptr); }
    ~Runtime() { if (token) gd::GdiplusShutdown(token); }
};
struct Tile {
    UnrealMipmap mip;
    std::string caption;
    int level = -1, paintedHeight = 0;
    float rasterScale = 0;
};
constexpr unsigned Gold = 0xffc8a56b, Ivory = 0xffeee8db;
constexpr unsigned Colors[] = {0xffbd2429,0xffc29a57,0xff329ed6,0xffe88e25,0xff60c6c5};
constexpr int Width[] = {80,80,88,330,330,360,320,180};
constexpr int Height[] = {255,210,240,50,50,100,32,32};
constexpr float AnchorX[] = {.02f,.077f,.98f,.5f,.5f,.5f,.5f,.98f};
constexpr float AnchorY[] = {.07f,.07f,.07f,.95f,.88f,.065f,.015f,.015f};
constexpr const wchar_t* Names[] = {L"Health",L"Shield",L"Rune power",L"Bloodlust",L"Air"};

inline void frame(gd::Graphics& g, int w, int h) {
    gd::SolidBrush iron(gd::Color(0xe80d1112)), shine(gd::Color(0x332e3435));
    gd::Pen gold(gd::Color(Gold),1.2f);
    g.FillRectangle(&iron,0,0,w,h);
    g.FillRectangle(&shine,2,2,w-4,std::min(h/3,25));
    g.DrawRectangle(&gold,1,1,w-2,h-2);
    const gd::Point knot[]={{2,17},{17,2},{23,2},{5,20},{5,11},{13,11},{13,4}};
    for (int c=0;c<4;++c) {
        gd::Point corners[7];
        for (int j=0;j<7;++j) corners[j]={c%2?w-knot[j].X:knot[j].X,c/2?h-knot[j].Y:knot[j].Y};
        g.DrawLines(&gold,corners,7);
    }
}
inline void icon(gd::Graphics& g,int i,float x,float y) {
    auto saved=g.Save();
    g.TranslateTransform(x,y); g.ScaleTransform(.9f,.9f);
    gd::Pen border(gd::Color(Gold),1.3f);
    gd::Point diamond[]={{0,-21},{21,0},{0,21},{-21,0}};
    g.DrawPolygon(&border,diamond,4);
    gd::Color tint(i==2?0xff85d5f8:i==3?0xffffbe56:0xffdecb9e);
    gd::Pen ink(tint,2); gd::SolidBrush bone(tint), dark(gd::Color(0xff101314));
    if (i==0) {
        g.FillEllipse(&bone,-10,-12,20,20); g.FillRectangle(&bone,-6,4,12,9);
        g.FillEllipse(&dark,-7,-5,6,6); g.FillEllipse(&dark,1,-5,6,6);
        gd::Point nose[]={{0,2},{-2,6},{2,6}}; g.FillPolygon(&dark,nose,3);
        gd::Pen teeth(gd::Color(0xff34291e)); for(int t=-4;t<=4;t+=4)g.DrawLine(&teeth,t,8,t,13);
    } else if (i==1) {
        gd::Point shield[]={{-11,-10},{0,-14},{11,-10},{9,5},{0,14},{-9,5}};
        g.DrawPolygon(&ink,shield,6); g.DrawLine(&ink,0,-13,0,13);
    } else if (i==2) {
        gd::Point rune[]={{-4,15},{-4,-15},{8,-7},{-4,0},{8,6},{-4,14}}; g.DrawLines(&ink,rune,6);
    } else if (i==3) {
        gd::GraphicsPath flame; flame.AddBezier(0,15,-18,8,-6,-1,-7,-9); flame.AddLine(-7,-9,-1,-4);
        flame.AddLine(-1,-4,3,-16); flame.AddBezier(3,-16,4,-4,17,4,5,14); flame.CloseFigure();
        g.FillPath(&bone,&flame); gd::SolidBrush core(gd::Color(0xffffe1a4)); g.FillEllipse(&core,-3,2,7,11);
    } else {
        g.DrawEllipse(&ink,-8,-6,16,16); g.DrawEllipse(&ink,5,-13,6,6); g.DrawEllipse(&ink,-8,-15,4,4);
    }
    g.Restore(saved);
}
inline void rounded(gd::GraphicsPath& p,int x,int y,int w,int h,int d) {
    p.AddArc(x,y,d,d,180,90); p.AddArc(x+w-d,y,d,d,270,90);
    p.AddArc(x+w-d,y+h-d,d,d,0,90); p.AddArc(x,y+h-d,d,d,90,90); p.CloseFigure();
}
inline void meter(gd::Graphics& g,int i,int w,int h,int level) {
    bool vertical=i<3;
    int x=vertical?w/2-10:57, y=vertical?76:26, mw=vertical?20:w-145, mh=vertical?h-114:14;
    gd::GraphicsPath outline; rounded(outline,x,y,mw,mh,std::min(mw,mh));
    gd::SolidBrush well(gd::Color(0xff080c0d)); g.FillPath(&well,&outline);
    gd::Color color(Colors[i]);
    auto boost=[](int n){return std::min(255,n*135/100);};
    gd::Color stops[]={color,gd::Color(255,boost(color.GetR()),boost(color.GetG()),boost(color.GetB())),color};
    float locations[]={0,.5f,1};
    gd::LinearGradientBrush gradient(gd::Point(x,y),gd::Point(x+(vertical?mw:0),y+(vertical?0:mh)),color,color);
    gradient.SetInterpolationColors(stops,locations,3);
    auto saved=g.Save(); float ratio=level/1000.f;
    g.SetClip(gd::RectF(float(x+2),y+2+(vertical?(mh-4)*(1-ratio):0), (mw-4)*(vertical?1:ratio), (mh-4)*(vertical?ratio:1)));
    gd::GraphicsPath fill; rounded(fill,x+2,y+2,mw-4,mh-4,8); g.FillPath(&gradient,&fill); g.Restore(saved);
    gd::Pen rim(gd::Color(Gold),1);g.DrawPath(&rim,&outline);
}
inline std::wstring wide(const std::string& text) {
    int count=MultiByteToWideChar(CP_UTF8,0,text.data(),int(text.size()),nullptr,0);
    std::wstring result(count,L' ');
    if(count)MultiByteToWideChar(CP_UTF8,0,text.data(),int(text.size()),result.data(),count);
    return result;
}
inline void label(gd::Graphics& g,const std::wstring& text,float size,gd::RectF box,bool center=true) {
    gd::Font font(L"Segoe UI",size,gd::FontStyleRegular,gd::UnitPixel);
    gd::SolidBrush ivory{gd::Color(Ivory)};
    gd::StringFormat format;
    format.SetAlignment(center?gd::StringAlignmentCenter:gd::StringAlignmentNear);
    format.SetLineAlignment(gd::StringAlignmentCenter);
    g.DrawString(text.c_str(),int(text.size()),&font,box,&format,&ivory);
}
inline void paint(Tile& tile,int i,const std::string& caption,int level,float rasterScale) {
    int w=Width[i],h=Height[i];
    // Keep a stable GPU allocation when text wraps. Only draw its occupied rows.
    const int capacity=i==5?920:h, scale=i==5?1:2;
    tile.mip.Width=w*scale;tile.mip.Height=capacity*scale;
    tile.mip.Data.resize(tile.mip.Width*tile.mip.Height*4);
    gd::Bitmap image(tile.mip.Width,tile.mip.Height,tile.mip.Width*4,PixelFormat32bppPARGB,tile.mip.Data.data());
    gd::Graphics g(&image);
    g.Clear(gd::Color(0,0,0,0)); g.ScaleTransform(rasterScale,rasterScale);
    g.SetSmoothingMode(gd::SmoothingModeAntiAlias);g.SetTextRenderingHint(gd::TextRenderingHintAntiAliasGridFit);
    auto text=wide(caption);
    float size=i==5?20.f:17.f;
    if(i==5 || i==6 || i==7) {
        gd::RectF measured;
        for(;;) {
            gd::Font font(L"Segoe UI",size,gd::FontStyleRegular,gd::UnitPixel);
            g.MeasureString(text.c_str(),int(text.size()),&font,gd::RectF(0,0,float(w-20),10000),nullptr,&measured);
            if(i==5){h=std::clamp(int(std::ceil(measured.Height))+18,44,capacity);if(measured.Height<=capacity-18)break;}
            else if(measured.Height<=h-4)break;
            if(size<=10)break;
            size-=1;
        }
    }
    frame(g,w,h);
    if(i<5) {
        bool vertical=i<3;
        icon(g,i,vertical?w*.5f:26.f,vertical?29.f:25.f);
        if(vertical) {
            label(g,Names[i],i==2?13.f:17.f,{0,44,float(w),24});
            label(g,text,14.f,{0,float(h-32),float(w),24});
        } else {
            label(g,Names[i],18,{57,0,150,23},false);
            label(g,text,16,{float(w-85),12,82,32});
        }
        meter(g,i,w,h,level);
    } else label(g,text,size,{8,2,float(w-16),float(h-4)});
    tile.caption=caption;tile.level=level;tile.paintedHeight=h;tile.rasterScale=rasterScale;
}
inline void draw(RenderSubsystem& render,int width,int height,const std::array<float,12>& stats,
                 const std::string& message,const std::string& counter,const std::string& score) {
    static Runtime gdiplus;
    static std::array<Tile,8> tiles;
    if(!gdiplus.token || width<=0 || height<=0)return;
    float scale=std::min(width/1536.f,height/1024.f);
    for(int i=0;i<8;i++) {
        int level=0; std::string caption;
        if(i<5) {
            if(!(int(stats[0])&(1<<i)))continue;
            float value=std::max(0.f,stats[1+2*i]),maximum=std::max(1.f,stats[2+2*i]);
            level=int(std::lround(std::clamp(value/maximum,0.f,1.f)*1000));
            caption=i>=3?std::to_string(int(std::lround(value/maximum*100)))+"%":
                std::to_string(int(std::lround(value)))+" / "+std::to_string(int(std::lround(maximum)));
        } else {
            caption=i==5?message:i==6?counter:score;
            if(caption.empty())continue;
            if(i==7)caption="Score  "+caption;
        }
        Tile& tile=tiles[i];
        // Rasterize glyphs at their displayed pixel size to keep thin strokes readable.
        float rasterScale=std::min(scale,i==5?1.f:2.f);
        bool changed=tile.caption!=caption || tile.level!=level || tile.rasterScale!=rasterScale;
        if(changed)paint(tile,i,caption,level,rasterScale);
        TextureInfo tex;tex.CacheID=uint64_t(uintptr_t(&tile));tex.Format=TextureFormat::BGRA8;
        tex.Mips=&tile.mip;tex.NumMips=1;tex.USize=tile.mip.Width;tex.VSize=tile.mip.Height;tex.bRealtimeChanged=changed;
        float x=10*scale+AnchorX[i]*std::max(0.f,width-(20+Width[i])*scale);
        float y=10*scale+AnchorY[i]*std::max(0.f,height-(20+Height[i])*scale);
        render.DrawTile(tex,std::round(x),std::round(y),Width[i]*scale,tile.paintedHeight*scale,0,0,
                        Width[i]*rasterScale,tile.paintedHeight*rasterScale,1,vec4(1),vec4(0),PF_Highlighted);
    }
}
}
