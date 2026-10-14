#include "color_picker.h"
#include "i18n.h"
#include <array>

namespace {
D2D1_COLOR_F Hsb(double h,double s,double v) {
  h=std::fmod(h+360,360)/60;double c=v*s,x=c*(1-std::abs(std::fmod(h,2)-1)),m=v-c;
  std::array<double,3> a=h<1?std::array{c,x,0.}:h<2?std::array{x,c,0.}:h<3?std::array{0.,c,x}:h<4?std::array{0.,x,c}:h<5?std::array{x,0.,c}:std::array{c,0.,x};
  return Rgba(float(a[0]+m),float(a[1]+m),float(a[2]+m),1);
}
std::string HexColor(D2D1_COLOR_F c) {char b[16];snprintf(b,sizeof b,"#%02x%02x%02x",int(c.r*255+.5f),int(c.g*255+.5f),int(c.b*255+.5f));return b;}
class Picker : public Popover {
 public:
  ColorSpectrum* spectrum;
  TextField* hex;
  Toggle* web;
  Label* warning;
  TextButton* gamut;
  D2D1_COLOR_F old,current;
  Json converted;
  std::function<void(D2D1_COLOR_F)> done;
  std::vector<NumberField*> numbers;
  Picker(D2D1_COLOR_F c,std::function<void(D2D1_COLOR_F)> callback):old(c),current(c),done(std::move(callback)) {
    modal=true;
    Add<Label>(L"Color Picker",F_Title);
    spectrum=Add<ColorSpectrum>();spectrum->Set(c);
    spectrum->change=[this](D2D1_COLOR_F c,bool){Set("rgb",{c.r*255,c.g*255,c.b*255});};
    const wchar_t* labels[]={L"H°",L"S%",L"B%",L"R",L"G",L"B",L"L",L"a",L"b",L"C",L"M",L"Y",L"K"};
    for(int i=0;i<13;i++) {
      double lo=i==7||i==8?-128:0,hi=i==0?360:i>=3&&i<6?255:i==7||i==8?127:100;
      auto* n=Add<NumberField>(labels[i],0,lo,hi,1);numbers.push_back(n);
      n->onCommit=[this,i](double value) {
        numbers[i]->Set(value);int start=i<3?0:i<6?3:i<9?6:9,count=i<9?3:4;
        std::vector<double> v;for(int j=0;j<count;j++)v.push_back(numbers[start+j]->value);
        Set(start==0?"hsb":start==3?"rgb":start==6?"lab":"cmyk",v);
      };
    }
    hex=Add<TextField>(L"HEX",W(HexColor(c)));
    hex->onChange=[this](const std::wstring& text) {
      std::string s=Narrow(text);if(s.size()==6)s="#"+s;
      if(s.size()!=7 || s[0]!='#' || s.find_first_not_of("0123456789abcdefABCDEF",1)!=std::string::npos) {ui::Toast(L"Enter six hexadecimal digits",true);return;}
      unsigned r=0,g=0,b=0;sscanf_s(s.c_str()+1,"%2x%2x%2x",&r,&g,&b);Set("rgb",{double(r),double(g),double(b)});
    };
    web=Add<Toggle>(L"Only Web Colors",false);web->onChange=[this](bool on){web->Set(on);Set("rgb",{current.r*255,current.g*255,current.b*255});};
    Add<TextButton>(L"Add to Swatches",TextButton::Secondary,[this]{Exec("swatches.add",JsonObject().Str("color",HexColor(current)).Str("name",Narrow(i18n::Tr(L"Color Swatch"))).Text());});
    warning=Add<Label>(L"",F_Small);
    gamut=Add<TextButton>(L"Use printable color",TextButton::Quiet,[this] {hex->onChange(W(converted["printable"].Str()));});
    Add<TextButton>(L"Cancel",TextButton::Secondary,[this]{Close();});
    Add<TextButton>(L"OK",TextButton::Primary,[this]{Apply();});
    Add<TextButton>(L"×",TextButton::Quiet,[this]{Close();});
    Set("rgb",{c.r*255,c.g*255,c.b*255});
  }
  void Set(const char* model,const std::vector<double>& values) {
    std::string a="[";for(auto v:values)a+=(a.size()>1?",":"")+JsonNum(v);a+="]";
    auto p=JsonObject().Str("model",model).Add("values",a).Bool("webOnly",web->on).Text();
    auto result=Json::Parse(TakeString(pc_color_convert(p.c_str())));if(result.IsNull())return;
    converted=result;auto c=result["rgba"];current=Rgba(float(c[0].Num()),float(c[1].Num()),float(c[2].Num()),1);
    // Hue remains stable while dragging through black or an achromatic colour.
    double hue=spectrum->hue;spectrum->Set(current);if(result["hsb"][1].Num()<.001)spectrum->hue=hue;
    int at=0;for(auto key:{"hsb","rgb","lab","cmyk"})for(auto& v:result[key].a)numbers[at++]->Set(v.Num());
    hex->value=W(result["hex"].Str());bool out=result["outOfGamut"].Truthy();
    warning->text=out?i18n::Tr(L"Outside CMYK gamut"):L"";gamut->visible=out;ui::RequestFrame();
  }
  D2D1_SIZE_F Natural() override{return {740,510};}
  void Arrange() override {
    float x=target.left+20,y=target.top+18;
    children[0]->rect=RWH(x,y,650,30);spectrum->rect=RWH(x,y+50,294,256);
    for(int i=0;i<13;i++){int col=i<6?0:1,row=i<6?i:i-6;numbers[i]->rect=RWH(x+316+col*190,y+50+row*38,178,32);}
    hex->rect=RWH(x,y+340,290,32);web->rect=RWH(x,y+382,290,30);
    children[17]->rect=RWH(x+316,y+330,368,32);
    warning->rect=RWH(x+316,y+372,368,24);gamut->rect=RWH(x+316,y+402,368,28);
    size_t n=children.size();children[n-3]->rect=RWH(target.right-218,target.bottom-48,92,30);children[n-2]->rect=RWH(target.right-114,target.bottom-48,92,30);children[n-1]->rect=RWH(target.right-42,target.top+12,28,28);
  }
  void PaintContent() override {
    Rect preview=RWH(target.left+20,target.top+330,294,18);FillRound(preview,3,old);FillRound(RWH(preview.left,preview.top,147,18),3,current);
  }
  void Apply(){auto callback=done;auto color=current;Close();if(callback)callback(color);}
  bool OnKey(UINT vk,Mods m) override{if(vk==VK_RETURN){Apply();return true;}return Popover::OnKey(vk,m);}
};
std::unique_ptr<Picker> picker;
}

Rect ColorSpectrum::Field() const{return R(rect.left,rect.top,std::max(rect.left+1,rect.right-36),rect.bottom);}
void ColorSpectrum::Set(D2D1_COLOR_F c) {
  double hi=std::max({c.r,c.g,c.b}),lo=std::min({c.r,c.g,c.b}),d=hi-lo;
  brightness=hi;saturation=hi>0?d/hi:0;
  if(d>1e-7) hue=hi==c.r?60*std::fmod((c.g-c.b)/d+6,6):hi==c.g?60*((c.b-c.r)/d+2):60*((c.r-c.g)/d+4);
}
D2D1_COLOR_F ColorSpectrum::Color() const{return Hsb(hue,saturation,brightness);}
void ColorSpectrum::Paint() {
  Rect f=Field();
  if(!field || paintedHue!=hue) {
    std::vector<uint8_t> px(192*192*4);for(int y=0;y<192;y++)for(int x=0;x<192;x++){
      auto c=Hsb(hue,x/191.,1-y/191.);size_t i=(y*192+x)*4;px[i]=uint8_t(c.b*255+.5f);px[i+1]=uint8_t(c.g*255+.5f);px[i+2]=uint8_t(c.r*255+.5f);px[i+3]=255;
    }field=MakeBitmap(192,192,px.data(),192*4);paintedHue=hue;
  }
  if(field)DrawBitmap(field.Get(),f,1,nullptr,false);
  Rect stripBox=R(f.right+12,rect.top,rect.right,rect.bottom);
  for(int y=0;y<180;y++)FillRect(R(stripBox.left,stripBox.top+H(stripBox)*y/180,stripBox.right,stripBox.top+H(stripBox)*(y+1)/180+.5f),Hsb(y*2,1,1));
  float x=f.left+float(saturation)*W(f),y=f.top+float(1-brightness)*H(f);StrokeEllipse(x,y,5,5,Rgba(0,0,0,1),3);StrokeEllipse(x,y,5,5,Rgba(1,1,1,1),1.3f);
  y=rect.top+float(hue/360)*H(rect);StrokeRound(R(stripBox.left-2,y-3,stripBox.right+2,y+3),2,Rgba(1,1,1,1),2);
}
void ColorSpectrum::OnDown(const PointerEvent& e){if(e.button!=0)return;dragging=true;strip=e.x>Field().right;OnDrag(e);}
void ColorSpectrum::OnDrag(const PointerEvent& e){if(!dragging)return;auto f=Field();if(strip)hue=std::clamp(double((e.y-rect.top)/std::max(1.f,H(rect))),0.,.999999)*360;else{saturation=std::clamp(double((e.x-f.left)/std::max(1.f,W(f))),0.,1.);brightness=1-std::clamp(double((e.y-f.top)/std::max(1.f,H(f))),0.,1.);}if(change)change(Color(),false);ui::RequestFrame();}
void ColorSpectrum::OnUp(const PointerEvent& e,bool inside){Widget::OnUp(e,inside);if(!dragging)return;OnDrag(e);dragging=false;if(change)change(Color(),true);}
void OpenColorPicker(D2D1_COLOR_F c,std::function<void(D2D1_COLOR_F)> done,const Rect& anchor){picker=std::make_unique<Picker>(c,std::move(done));picker->Open(anchor,Side::Center);}
Json ColorPickerState(){return picker && picker->open?picker->converted:Json{};}
