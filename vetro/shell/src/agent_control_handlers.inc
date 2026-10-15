// Included after the shell handlers: control requests use those very handlers and widgets.
void AgentControlFileRoots(const std::wstring& read,const std::wstring& write);
void AgentFileRequest(const Json& params,bool save,AgentReply reply);
void RefreshAgentDocuments(){infoRevision=~0ull;Refresh();}
namespace {
struct AgentWidget{Widget* widget;std::weak_ptr<int> lifetime;};
std::map<uint64_t,AgentWidget> agentWidgets;
uint64_t nextAgentWidget=1;
std::string InspectAgentWidgets(){
  agentWidgets.clear();std::string out="[";
  std::set<Widget*> visited;
  std::function<void(Widget*,int)> visit=[&](Widget* widget,int depth){if(!widget||!widget->visible||depth>32||agentWidgets.size()>=4096||!visited.insert(widget).second)return;if(auto* popover=dynamic_cast<Popover*>(widget);popover&&!popover->open)return;
    std::wstring label;std::string kind="widget",value="null";
    if(auto* f=dynamic_cast<NumberField*>(widget)){label=f->label;kind="number";value=JsonNum(f->value);}
    else if(auto* f=dynamic_cast<TextField*>(widget)){label=f->label;kind="text";value=JsonQuote(Narrow(f->value));}
    else if(auto* f=dynamic_cast<Toggle*>(widget)){label=f->label;kind="toggle";value=f->on?"true":"false";}
    else if(auto* f=dynamic_cast<SelectList*>(widget)){label=f->label;kind="choice";value=std::to_string(f->index);}
    else if(auto* f=dynamic_cast<TextButton*>(widget)){label=f->label;kind="button";}
    else if(auto* f=dynamic_cast<Label*>(widget)){label=f->text;kind="label";}
    auto id=nextAgentWidget++;agentWidgets.emplace(id,AgentWidget{widget,widget->lifetime});if(out.size()>1)out+=",";
    auto item=JsonObject().Num("id",double(id)).Str("kind",kind).Str("label",Narrow(i18n::Tr(label))).Bool("enabled",widget->enabled).Add("value",value).Add("rect","["+JsonNum(widget->rect.left)+","+JsonNum(widget->rect.top)+","+JsonNum(W(widget->rect))+","+JsonNum(H(widget->rect))+"]").Num("depth",depth);
    if(auto* f=dynamic_cast<SelectList*>(widget)){std::string choices="[";for(auto& option:f->options){if(choices.size()>1)choices+=",";choices+=JsonQuote(Narrow(option));}item.Add("choices",choices+"]");}out+=item.Text();
    if(auto* c=dynamic_cast<Container*>(widget))for(auto& child:c->children)visit(child.get(),depth+1);
  };for(auto* layer:ui::RootLayers())visit(layer,0);if(auto* popover=ui::ActivePopover())visit(popover,0);return out+"]";
}
std::string InspectNative(){Refresh();Layout(true);return JsonObject().Add("documents",JsonText(sessionInfo["documents"])).Add("document",JsonText(docInfo)).Add("session",JsonText(sessionInfo)).Str("tool",current?current->key:"").Add("canvasWorkspace",JsonText(CanvasWorkspaceState())).Add("guidedUpright",JsonText(GuidedWorkspaceState())).Add("selectMask",JsonText(WorkspaceEditorState())).Add("dialog",JsonText(ApplicationDialogState())).Add("controls",InspectAgentWidgets()).Str("control",Narrow(AgentControlStatus())).Text();}
struct AgentPointerSequence:std::enable_shared_from_this<AgentPointerSequence>{
  Json events,modifiers,button;size_t next=0;AgentReply reply;
  void Step(){if(next>=events.size()){reply(true,InspectNative());return;}auto point=events[next++];auto kind=point["kind"].Str();if(kind.empty())kind="move";
    auto params=JsonObject().Str("type",kind).Add("x",JsonText(point["x"])).Add("y",JsonText(point["y"])).Str("space","document").Add("modifiers",JsonText(modifiers)).Add("button",JsonText(button));if(!point["pressure"].IsNull())params.Add("pressure",JsonText(point["pressure"]));
    auto request=Json::Parse(JsonObject().Str("method","ui.pointer").Add("params",params.Text()).Text());auto self=shared_from_this();DispatchAgentControl(request,[self](bool ok,const std::string& value){if(ok)self->Step();else self->reply(false,value);});
  }
};
}
void DispatchAgentControl(const Json& request,AgentReply reply){
  auto method=request["method"].Str();auto p=request["params"];if(p.IsNull())p=Json::Parse("{}");if(p.kind!=Json::Object){reply(false,"params must be an object");return;}
  struct Scope{bool previous;explicit Scope(bool active):previous(restrictedExec){if(active){restrictedExec=true;RestrictAgentPreviews();}}~Scope(){restrictedExec=previous;}} scope(method=="ui.menu.invoke"||method=="ui.set"||method=="ui.key"||method=="ui.pointer");
  if(method=="ui.inspect"||method=="app.state"){reply(true,InspectNative());return;}
  if(method=="app.documents"){reply(true,JsonText(sessionInfo["documents"]));return;}
  if(method=="engine.commands"){reply(true,TakeString(pc_commands()));return;}
  if(method=="engine.execute"){
    auto command=p["command"].Str();if(command.empty())command=p["id"].Str();if(command.empty()||command.find('\0')!=std::string::npos||(!p["params"].IsNull()&&p["params"].kind!=Json::Object)){reply(false,"command and object params are required");return;}if(Mode()){reply(false,"apply or cancel the active canvas workspace before executing an engine command");return;}if(p["wait"].kind==Json::Bool&&!p["wait"].Truthy()){reply(false,"native control currently requires wait:true");return;}auto params=p["params"].IsNull()?"{}":JsonText(p["params"]);auto denied=AuthorizeAgentCommand(command,params);if(!denied.empty()){reply(false,denied);return;}bool previous=restrictedExec;restrictedExec=true;Exec(command,params,std::move(reply),false);restrictedExec=previous;return;
  }
  if(method=="ui.menu.list"){std::string rows="[";for(auto& row:catalog){if(rows.size()>1)rows+=",";rows+=JsonObject().Str("command",row.id).Str("label",Narrow(CommandLabel(row.id))).Str("shortcut",row.shortcut).Bool("live",row.live).Text();}reply(true,rows+"]");return;}
  if(method=="ui.menu.invoke"){
    auto id=p["command"].Str();if(id.empty())id=p["id"].Str();auto denied=AuthorizeAgentCommand(id,"{}");if(!denied.empty()){reply(false,denied);return;}
    if(std::none_of(catalog.begin(),catalog.end(),[&](const CatalogRow& row){return row.id==id;})){reply(false,"unknown menu command");return;}
    RunCommand(id,CommandLabel(id));jobs::Run([]{},[reply]{reply(true,InspectNative());});return;
  }
  if(method=="ui.set"){
    for(auto& item:p.o)if(item.first!="control"&&item.first!="value"&&item.first!="tool"&&item.first!="zoom"){reply(false,"unsupported native UI field: "+item.first);return;}
    if(!p["tool"].IsNull()){bool found=false;for(auto& section:sections)for(auto& slot:section)for(auto& tool:slot)if(p["tool"].kind==Json::String&&tool.key==p["tool"].Str())found=true;if(!found){reply(false,"unknown tool");return;}}
    if(!p["zoom"].IsNull()&&(p["zoom"].kind!=Json::Number||!std::isfinite(p["zoom"].Num())||p["zoom"].Num()<=0||p["zoom"].Num()>64)){reply(false,"invalid zoom factor");return;}
    if(!p["control"].IsNull()){
      auto it=agentWidgets.find(uint64_t(p["control"].Int()));if(it==agentWidgets.end()||it->second.lifetime.expired()||!it->second.widget->visible||!it->second.widget->enabled){reply(false,"control is unavailable; inspect again");return;}
      auto* widget=it->second.widget;auto value=p["value"];
      if(auto* f=dynamic_cast<NumberField*>(widget)){if(value.kind!=Json::Number||!std::isfinite(value.Num())){reply(false,"expected a finite number");return;}f->Set(value.Num());if(f->onCommit)f->onCommit(f->value);else if(f->onChange)f->onChange(f->value);}
      else if(auto* f=dynamic_cast<Toggle*>(widget)){if(value.kind!=Json::Bool){reply(false,"expected boolean");return;}f->Set(value.Truthy());if(f->onChange)f->onChange(f->on);}
      else if(auto* f=dynamic_cast<TextField*>(widget)){if(value.kind!=Json::String){reply(false,"expected text");return;}f->value=W(value.Str());if(f->onChange)f->onChange(f->value);}
      else if(auto* f=dynamic_cast<SelectList*>(widget)){double n=value.Num(-1);if(value.kind!=Json::Number||!std::isfinite(n)||std::floor(n)!=n||n<0||n>=double(f->options.size())){reply(false,"choice is out of range");return;}int i=int(n);f->index=i;auto changed=f->onChange;if(changed)changed(i);}
      else {reply(false,"control has no editable value");return;}
    }
    if(!p["tool"].IsNull()){bool found=false;for(auto& section:sections)for(auto& slot:section)for(auto& tool:slot)if(tool.key==p["tool"].Str()){SelectTool(&tool);found=true;}if(!found){reply(false,"unknown tool");return;}}
    if(!p["zoom"].IsNull()){double zoom=p["zoom"].Num(-1);if(!std::isfinite(zoom)||zoom<=0||zoom>64){reply(false,"zoom must be a factor between 0 and 64");return;}canvas->ZoomTo(float(zoom),false);}
    jobs::Run([]{},[reply]{reply(true,InspectNative());});return;
  }
  if(method=="ui.key"){
    auto modifiers=p["modifiers"];Mods mods{p["ctrl"].Truthy()||modifiers["ctrl"].Truthy()||modifiers["command"].Truthy(),p["shift"].Truthy()||modifiers["shift"].Truthy(),p["alt"].Truthy()||modifiers["alt"].Truthy()};auto name=p["key"].Str();auto key=KeyCode(name);
    if(!p["text"].IsNull()){for(auto c:W(p["text"].Str()))if(!ui::Char(c))TextEditorChar(c);}
    else if(!key){reply(false,"unknown key");return;}else if(p["pressed"].kind==Json::Bool&&!p["pressed"].Truthy())ReleaseToolKey(key);else HandleKey(key,mods);
    jobs::Run([]{},[reply]{reply(true,InspectNative());});return;
  }
  if(method=="ui.pointer"){
    auto modifiersInput=p["modifiers"];if(!modifiersInput.IsNull()&&modifiersInput.kind!=Json::Object){reply(false,"modifiers must be an object");return;}
    if(!p["events"].IsNull()){
      auto events=p["events"];if(events.kind!=Json::Array||events.size()>256){reply(false,"events must be an array of at most 256 points");return;}
      for(auto& e:events.a){auto kind=e["kind"].Str();if(e.kind!=Json::Object||(!kind.empty()&&kind!="down"&&kind!="move"&&kind!="up")||e["x"].kind!=Json::Number||e["y"].kind!=Json::Number||!std::isfinite(e["x"].Num())||!std::isfinite(e["y"].Num())||std::abs(e["x"].Num())>300000||std::abs(e["y"].Num())>300000){reply(false,"invalid pointer event");return;}if(!e["pressure"].IsNull()&&(e["pressure"].kind!=Json::Number||!std::isfinite(e["pressure"].Num())||e["pressure"].Num()<0||e["pressure"].Num()>1)){reply(false,"pressure must be between 0 and 1");return;}if(!e["tiltX"].IsNull()||!e["tiltY"].IsNull()||!e["rotation"].IsNull()){reply(false,"native pointer supports position and pressure; pen tilt and rotation require headless painting");return;}}
      auto batch=std::make_shared<AgentPointerSequence>();batch->events=events;batch->modifiers=p["modifiers"];batch->button=p["button"];batch->reply=std::move(reply);batch->Step();return;
    }
    double x=p["x"].Num(NAN),y=p["y"].Num(NAN);if(!std::isfinite(x)||!std::isfinite(y)||std::abs(x)>300000||std::abs(y)>300000){reply(false,"invalid pointer coordinates");return;}
    auto modifiers=p["modifiers"];if(modifiers["space"].Truthy()){reply(false,"use middle-button pointer events to pan the native canvas");return;}
    PointerEvent e;e.x=float(x);e.y=float(y);e.mods={p["ctrl"].Truthy()||modifiers["ctrl"].Truthy()||modifiers["command"].Truthy(),p["shift"].Truthy()||modifiers["shift"].Truthy(),p["alt"].Truthy()||modifiers["alt"].Truthy()};e.button=int(p["button"].Int());if(p["button"].kind==Json::String){auto button=p["button"].Str();if(button=="right"||button=="secondary")e.button=1;else if(button=="middle")e.button=2;else if(button=="left"||button=="primary")e.button=0;else{reply(false,"unknown mouse button");return;}}e.timeMs=ui::NowMs();if(!p["pressure"].IsNull()){e.pressure=float(std::clamp(p["pressure"].Num(1),0.,1.));e.pen=true;}if(p["space"].Str()=="document"){auto at=canvas->ToScreen(x,y);e.x=at.x;e.y=at.y;}
    pointerX=e.x;pointerY=e.y;auto type=p["type"].Str();if(type=="down")ui::PointerDown(e);else if(type=="up")ui::PointerUp(e);else if(type=="move")ui::PointerMove(e);else{reply(false,"pointer type must be down, move or up");return;}
    jobs::Run([]{},[reply]{reply(true,InspectNative());});return;
  }
  if(method=="ui.screenshot"){Frame();auto png=AgentScreenshot(int(p["maxSide"].Int(1280)));reply(!png.empty(),png.empty()?"screenshot unavailable or exceeds preview budget":png);return;}
  if(method=="app.activateDocument"){auto id=uint64_t(p["document"].Int());if(DocumentIndex(id)<0){reply(false,"unknown document");return;}SwitchDocument(id);jobs::Run([]{},[reply]{reply(true,InspectNative());});return;}
  if(method=="app.open"||method=="app.save"){if(method=="app.open")PrepareDocumentChange();AgentFileRequest(p,method=="app.save",std::move(reply));return;}
  reply(false,"unsupported native control method: "+method);
}
