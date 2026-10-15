#pragma once
#include "shell.h"
void ConfigureAgentControl(int requestedPort=-1,const std::wstring& tokenFile=L"");
void StopAgentControl();
void AgentControlTick();
std::wstring AgentControlStatus();
using AgentReply=std::function<void(bool,const std::string&)>;
void DispatchAgentControl(const Json& request,AgentReply reply);
std::string AgentScreenshot(int maxSide=1280);
std::string AuthorizeAgentCommand(const std::string& id,const std::string& params);
extern "C" {
struct VcControl;
VcControl* vc_start(const char* config);
void vc_stop(VcControl*);
void vc_free(VcControl*);
uint16_t vc_port(VcControl*);
char* vc_poll(VcControl*);
void vc_reply(VcControl*,uint64_t ticket,const char* reply);
char* vc_authorize(const char* id,const char* params);
char* vc_error();
char* vc_png(const uint8_t*,size_t len);
void vc_string_free(char*);
}
