Json AgentCall(const std::string& method,const std::string& params="{}",bool expected=true){bool done=false,ok=false;std::string result;auto request=Json::Parse(JsonObject().Str("method",method).Add("params",params).Text());DispatchAgentControl(request,[&](bool success,const std::string& text){done=true;ok=success;result=text;});Drain();Check(done,"agent request did not complete");if(ok!=expected)throw std::runtime_error(method+": "+result);return ok?Json::Parse(result):Json::Parse(JsonQuote(result));}
void NativeAgentControl(){
  NewDocument();auto before=docInfo["layers"].size();auto inspected=AgentCall("ui.inspect");Check(inspected["document"]["id"].Int()==int64_t(ActiveDocumentId())&&inspected["controls"].size()>10,"agent inspected another session or no native widgets");
  AgentCall("engine.execute",R"({"command":"layer.new.layer","params":{"name":"Agent layer"}})");Check(docInfo["layers"].size()==before+1,"agent command did not reach the visible document");AgentCall("ui.key",R"({"key":"Z","ctrl":true})");Check(docInfo["layers"].size()==before,"agent key did not run the shell handler");
  AgentCall("engine.execute",R"({"command":"file.open","params":{"path":"C:/Windows/win.ini"}})",false);
  AgentCall("engine.execute",R"({"command":"vetro.exportLayer","params":{"path":"C:/denied.png"}})",false);
  auto denied=std::filesystem::temp_directory_path()/(L"VetroAgentDenied-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64())+L".pcraft");Check(!std::filesystem::exists(denied),"private action fixture already exists");
  Command("actions.record",R"({"name":"Native authority regression"})");Command("actions.stop");
  auto action=JsonObject().Str("action","Native authority regression").Str("id","file.saveAs").Add("params",JsonObject().Str("path",Narrow(denied.wstring())).Text()).Text();Command("actions.insert",action);
  auto played=AgentCall("engine.execute",R"({"command":"actions.play","params":{"action":"Native authority regression"}})");Check(played["failed"]["error"].Str().find("native automation denied")!=std::string::npos&&!std::filesystem::exists(denied),"nested action bypassed native authority");
  restrictedExec=true;auto preview=std::make_shared<LivePreview>("actions.play");restrictedExec=false;preview->Show(R"({"action":"Native authority regression"})");Drain();Check(!std::filesystem::exists(denied),"native preview bypassed nested action authority");preview->Finish(false,"{}");Drain();
  AgentCall("app.open",R"({"path":"C:/Windows/win.ini"})",false);AgentCall("ui.pointer",R"({"x":"bad","y":1,"type":"down"})",false);
  AgentCall("ui.set",R"({"tool":"move","zoom":2})");Check(current&&current->key=="move"&&std::abs(canvas->Zoom()-2)<.01,"agent view/tool update missed shell state");
  AgentCall("ui.menu.invoke",R"({"command":"file.new"})");Check(ApplicationDialog()&&ApplicationDialog()->open,"agent did not open native New dialog");auto state=AgentCall("ui.inspect");uint64_t width=0;for(auto& c:state["controls"].a)if(c["kind"].Str()=="number"&&c["label"].Str()==Narrow(i18n::Tr(L"Width")))width=uint64_t(c["id"].Int());Check(width!=0,"New document width missing from agent controls");AgentCall("ui.set",JsonObject().Num("control",double(width)).Num("value",123).Text());Check(ApplicationDialogState()["params"]["width"].Int()==123,"agent form update did not edit native field");AgentCall("ui.key",R"({"key":"Escape"})");Check(!ApplicationDialog()->open,"agent could not cancel native dialog");
  Layout(true);for(int i=0;i<80;i++){Frame();Sleep(2);}auto png=AgentCall("ui.screenshot",R"({"maxSide":256})");Check(!png["png"].Str().empty(),"agent screenshot PNG missing");
}
void AgentSettings(){
  auto token=std::filesystem::temp_directory_path()/(L"VetroAgentSettings-"+std::to_wstring(GetCurrentProcessId())+L".token");
  Command("prefs.set",R"({"values":{"integrations.allowAgentControl":true,"integrations.controlPort":0}})");ConfigureAgentControl(-1,token.wstring());
  auto config=JsonObject().Num("port",0).Str("tokenFile",Narrow(token.wstring())).Text();auto* probe=vc_start(config.c_str());Check(probe!=nullptr,"private settings port probe failed");int port=vc_port(probe);vc_stop(probe);vc_free(probe);
  auto fields=[](){auto* form=ApplicationDialog();Check(form&&form->open,"integration preferences dialog missing");auto* body=dynamic_cast<ScrollArea*>(form->children[0].get());Check(body!=nullptr,"integration preferences body missing");return body;};
  RunCommand("edit.preferences.integrations",L"Preferences");Drain();NumberField* number=nullptr;Toggle* allow=nullptr;
  for(auto& child:fields()->children){if(auto* f=dynamic_cast<NumberField*>(child.get());f&&f->label==i18n::Tr(L"Control Port"))number=f;if(auto* f=dynamic_cast<Toggle*>(child.get());f&&f->label==i18n::Tr(L"Allow Agent Control"))allow=f;}
  Check(number&&allow&&number->min==0&&number->max==65535,"agent settings are hidden or have an invalid port range");number->Set(port);number->onCommit(port);allow->Set(true);allow->onChange(true);Key(VK_RETURN);
  Check(AgentControlStatus()==L"127.0.0.1:"+std::to_wstring(port),"Preferences OK did not enable the local channel");
  RunCommand("edit.preferences.integrations",L"Preferences");Drain();allow=nullptr;for(auto& child:fields()->children)if(auto* f=dynamic_cast<Toggle*>(child.get());f&&f->label==i18n::Tr(L"Allow Agent Control"))allow=f;
  Check(allow!=nullptr,"agent permission switch missing");allow->Set(false);allow->onChange(false);Key(VK_RETURN);Check(AgentControlStatus()==i18n::Tr(L"Disabled"),"Preferences OK did not stop the local channel");
  Command("prefs.set",R"({"values":{"integrations.allowAgentControl":true,"integrations.controlPort":0}})");ConfigureAgentControl();std::error_code ec;std::filesystem::remove(token,ec);
}
