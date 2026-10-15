#include "agent_control.h"
#include "agent_files.h"
#include "i18n.h"
#include <filesystem>
#include <shlobj.h>
#include <wincodec.h>
void RefreshAgentDocuments();
namespace {
std::shared_ptr<VcControl> server;
std::wstring tokenPath,status;
std::wstring readRoot,writeRoot;
int commandLinePort=-1,configuredPort=-1;
bool inFlight=false;
std::string ControlString(char* p){if(!p)return {};std::string s(p);vc_string_free(p);return s;}
}
void StopAgentControl(){KillTimer(hwnd,872);if(server)vc_stop(server.get());server.reset();configuredPort=-1;status=i18n::Tr(L"Disabled");}
std::wstring AgentControlStatus(){return status.empty()?i18n::Tr(L"Disabled"):status;}
void ConfigureAgentControl(int requestedPort,const std::wstring& file){
  if(requestedPort>=0)commandLinePort=requestedPort;if(!file.empty())tokenPath=file;
  auto info=Json::Parse(TakeString(pc_preferences_info(engine)));auto options=info["saved"]["integrations"];
  int port=commandLinePort>=0?commandLinePort:int(options["controlPort"].Int());bool enabled=options["allowAgentControl"].Truthy(true)&&(commandLinePort>=0||port>0);
  if(!enabled){StopAgentControl();return;}if(server&&configuredPort==port)return;StopAgentControl();
  if(tokenPath.empty()){PWSTR root=nullptr;if(SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData,0,nullptr,&root))){tokenPath=(std::filesystem::path(root)/L"PhotoCraft by Vetro"/L"control.token").wstring();CoTaskMemFree(root);}}
  auto config=JsonObject().Num("port",port).Str("tokenFile",Narrow(tokenPath)).Str("readRoot",Narrow(readRoot)).Str("writeRoot",Narrow(writeRoot)).Text();auto* started=vc_start(config.c_str());
  if(!started){status=W(ControlString(vc_error()));ui::Toast(status,true);return;}server=std::shared_ptr<VcControl>(started,vc_free);configuredPort=port;status=L"127.0.0.1:"+std::to_wstring(vc_port(started));SetTimer(hwnd,872,20,nullptr);
}
void AgentControlFileRoots(const std::wstring& read,const std::wstring& write){readRoot=read;writeRoot=write;}
void AgentFileRequest(const Json& params,bool save,AgentReply reply){
  if(!server){reply(false,"filesystem authority is absent");return;}if(!params["tgaBits"].IsNull()){reply(false,"tgaBits needs the headless export tool; native control saves with current defaults");return;}auto path=params["path"].Str();bool explicitPath=!path.empty();if(path.empty()&&save)path=sessionInfo["documents"][size_t(sessionInfo["active"].Int())]["path"].Str();if(path.empty()||path.find('\0')!=std::string::npos){reply(false,"provide a valid file path");return;}
  auto extension=std::filesystem::path(W(path)).extension().wstring();std::transform(extension.begin(),extension.end(),extension.begin(),towlower);bool layered=extension==L".pcraft"||extension==L".psd"||extension==L".psb";
  if(save&&!explicitPath&&!layered){reply(false,"provide a destination for a flattened or converted copy");return;}
  auto owner=server;auto result=std::make_shared<std::string>();auto error=std::make_shared<std::string>();bool copy=params["copy"].Truthy()||!layered;
  jobs::Run([owner,path,save,copy,result,error]{
    auto* named=vc_file_path(owner.get(),path.c_str(),save?1:0);if(!named){*error=ControlString(vc_error());return;}auto resolved=ControlString(named);
    if(save){std::unique_ptr<PcData,decltype(&pc_data_free)> data(pc_encode_data(engine,path.c_str()),pc_data_free);if(!data){*error=LastError();return;}
      auto* written=vc_write_file(owner.get(),path.c_str(),pc_data_bytes(data.get()),pc_data_size(data.get()));if(!written){*error=ControlString(vc_error());return;}*error=ControlString(written);if(!error->empty())return;
      auto warnings=TakeString(pc_data_warnings(data.get()));if(!copy&&!pc_mark_data_saved(engine,resolved.c_str())){*error=LastError();return;}*result=JsonObject().Str("path",resolved).Bool("copy",copy).Add("warnings",warnings.empty()?"[]":warnings).Text();
    }else {std::unique_ptr<VcData,decltype(&vc_data_free)> data(vc_read_file(owner.get(),path.c_str()),vc_data_free);if(!data){*error=ControlString(vc_error());return;}
      auto* imported=pc_open_data(engine,resolved.c_str(),vc_data_bytes(data.get()),vc_data_size(data.get()));if(!imported){*error=LastError();return;}*result=TakeString(imported);
    }
  },[result,error,reply]{RefreshAgentDocuments();reply(error->empty(),error->empty()?*result:*error);},save?L"Saving document":L"Opening document");
}
void AgentControlTick(){
  if(!server||inFlight||jobs::Busy()||canvas->Busy())return;auto request=Json::Parse(ControlString(vc_poll(server.get())));if(request.IsNull())return;
  inFlight=true;auto owner=server;auto ticket=uint64_t(request["_ticket"].Int());DispatchAgentControl(request,[owner,ticket](bool ok,const std::string& value){
    auto result=JsonObject().Bool("ok",ok);if(ok)result.Add("result",value.empty()?"null":value);else result.Str("error",value);auto text=result.Text();vc_reply(owner.get(),ticket,text.c_str());inFlight=false;ui::RequestFrame();
  });
}
std::string AuthorizeAgentCommand(const std::string& id,const std::string& params){auto* p=vc_authorize(id.c_str(),params.c_str());return p?ControlString(p):"command authorization failed";}
extern "C" int AgentAuthorizeThunk(const char* id,const char* params) noexcept{try{return AuthorizeAgentCommand(id?id:"",params?params:"{}").empty()?1:0;}catch(...){return 0;}}
std::string AgentScreenshot(int maxSide){
  if(maxSide<1||maxSide>2048)return {};Microsoft::WRL::ComPtr<ID2D1Image> image;Dc()->GetTarget(&image);Microsoft::WRL::ComPtr<ID2D1Bitmap1> source,cpu;if(!image||FAILED(image.As(&source)))return {};
  auto size=source->GetPixelSize();if(!size.width||!size.height||uint64_t(size.width)*size.height>(64ull<<20))return {};
  auto props=D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_CPU_READ|D2D1_BITMAP_OPTIONS_CANNOT_DRAW,source->GetPixelFormat());
  if(FAILED(Dc()->CreateBitmap(size,nullptr,0,&props,&cpu))||FAILED(cpu->CopyFromBitmap(nullptr,source.Get(),nullptr)))return {};D2D1_MAPPED_RECT mapped{};if(FAILED(cpu->Map(D2D1_MAP_OPTIONS_READ,&mapped)))return {};
  Microsoft::WRL::ComPtr<IWICImagingFactory> factory;Microsoft::WRL::ComPtr<IWICBitmap> bitmap;Microsoft::WRL::ComPtr<IWICBitmapScaler> scaler;Microsoft::WRL::ComPtr<IWICBitmapEncoder> encoder;Microsoft::WRL::ComPtr<IWICBitmapFrameEncode> frame;Microsoft::WRL::ComPtr<IStream> stream;
  HRESULT hr=CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory));
  if(SUCCEEDED(hr))hr=factory->CreateBitmapFromMemory(size.width,size.height,GUID_WICPixelFormat32bppBGRA,mapped.pitch,mapped.pitch*size.height,mapped.bits,&bitmap);
  double ratio=std::min(1.,double(maxSide)/std::max(size.width,size.height));UINT w=std::max(1u,UINT(std::round(size.width*ratio))),h=std::max(1u,UINT(std::round(size.height*ratio)));
  if(SUCCEEDED(hr))hr=factory->CreateBitmapScaler(&scaler);if(SUCCEEDED(hr))hr=scaler->Initialize(bitmap.Get(),w,h,WICBitmapInterpolationModeFant);
  if(SUCCEEDED(hr))hr=CreateStreamOnHGlobal(nullptr,TRUE,&stream);if(SUCCEEDED(hr))hr=factory->CreateEncoder(GUID_ContainerFormatPng,nullptr,&encoder);if(SUCCEEDED(hr))hr=encoder->Initialize(stream.Get(),WICBitmapEncoderNoCache);if(SUCCEEDED(hr))hr=encoder->CreateNewFrame(&frame,nullptr);if(SUCCEEDED(hr))hr=frame->Initialize(nullptr);if(SUCCEEDED(hr))hr=frame->SetSize(w,h);
  auto format=GUID_WICPixelFormat32bppBGRA;if(SUCCEEDED(hr))hr=frame->SetPixelFormat(&format);if(SUCCEEDED(hr))hr=frame->WriteSource(scaler.Get(),nullptr);if(SUCCEEDED(hr))hr=frame->Commit();if(SUCCEEDED(hr))hr=encoder->Commit();cpu->Unmap();if(FAILED(hr))return {};
  STATSTG stat{};if(FAILED(stream->Stat(&stat,STATFLAG_NONAME))||stat.cbSize.QuadPart>(5ull<<20))return {};HGLOBAL memory=nullptr;if(FAILED(GetHGlobalFromStream(stream.Get(),&memory)))return {};auto* bytes=static_cast<const uint8_t*>(GlobalLock(memory));if(!bytes)return {};auto json=ControlString(vc_png(bytes,size_t(stat.cbSize.QuadPart)));GlobalUnlock(memory);return json;
}
