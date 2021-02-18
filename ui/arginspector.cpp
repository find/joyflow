#include "arginspector.h"

#include <core/utility.h>
#include <core/opgraph.h>
#include <core/opcontext.h>
#include <core/luabinding.h>
#include <core/stats.h>

#include <nodegraph.h>

#include <sol/sol.hpp>
#include <unordered_map>

#include <nfd.h> 
#define IMGUI_DEFINE_MATH_OPERATORS
#include <imgui.h>
#include <imgui_internal.h>
#include <misc/cpp/imgui_stdlib.h>
#include <TextEditor.h>

using namespace joyflow;

///////////////////////////// DEFAULT INSPECTORS ///////////////////////////////

static bool floatInspector(std::string_view name, OpContext*, ArgValue& arg)
{
  bool modified = false;
  switch (arg.desc().tupleSize) {
  case 1: {
    float val = static_cast<float>(arg.asReal());
    if (ImGui::SliderFloat(
            name.data(), &val, float(arg.desc().valueRange[0]), float(arg.desc().valueRange[1]))) {
      arg.setReal(val);
      modified = true;
    }
    break;
  }
  case 2: {
    auto  rval   = arg.asReal2();
    float val[2] = {float(rval.x), float(rval.y)};
    if (ImGui::SliderFloat2(
            name.data(), val, float(arg.desc().valueRange[0]), float(arg.desc().valueRange[1]))) {
      arg.setReal(val[0], 0);
      arg.setReal(val[1], 1);
      modified = true;
    }
    break;
  }
  case 3: {
    auto  rval   = arg.asReal3();
    float val[3] = {float(rval.x), float(rval.y), float(rval.z)};
    if (ImGui::SliderFloat3(
            name.data(), val, float(arg.desc().valueRange[0]), float(arg.desc().valueRange[1]))) {
      arg.setReal(val[0], 0);
      arg.setReal(val[1], 1);
      arg.setReal(val[2], 2);
      modified = true;
    }
    break;
  }
  case 4: {
    auto  rval   = arg.asReal4();
    float val[4] = {float(rval.x), float(rval.y), float(rval.z), float(rval.w)};
    if (ImGui::SliderFloat4(
            name.data(), val, float(arg.desc().valueRange[0]), float(arg.desc().valueRange[1]))) {
      arg.setReal(val[0], 0);
      arg.setReal(val[1], 1);
      arg.setReal(val[2], 2);
      arg.setReal(val[3], 4);
      modified = true;
    }
    break;
  }
  default:
    spdlog::warn("argument {} has strange tupleSize of {}", name, arg.desc().tupleSize);
    return false;
  }
  return modified;
}

static bool intInspector(std::string_view name, OpContext*, ArgValue &arg)
{
  bool modified = false;
  switch (arg.desc().tupleSize) {
  case 1: {
    int val = static_cast<int>(arg.asInt());
    if (ImGui::SliderInt(
      name.data(), &val, int(arg.desc().valueRange[0]), int(arg.desc().valueRange[1]))) {
      arg.setInt(val);
      modified = true;
    }
    break;
  }
  case 2: {
    auto rval = arg.asInt2();
    int  val[] = { int(rval.x), int(rval.y) };
    if (ImGui::SliderInt2(
      name.data(), val, int(arg.desc().valueRange[0]), int(arg.desc().valueRange[1]))) {
      arg.setInt(val[0], 0);
      arg.setInt(val[1], 1);
      modified = true;
    }
    break;
  }
  case 3: {
    auto rval = arg.asInt3();
    int  val[] = { int(rval.x), int(rval.y), int(rval.z) };
    if (ImGui::SliderInt3(
      name.data(), val, int(arg.desc().valueRange[0]), int(arg.desc().valueRange[1]))) {
      arg.setInt(val[0], 0);
      arg.setInt(val[1], 1);
      arg.setInt(val[2], 2);
      modified = true;
    }
    break;
  }
  case 4: {
    auto rval = arg.asInt4();
    int  val[] = { int(rval.x), int(rval.y), int(rval.z), int(rval.w) };
    if (ImGui::SliderInt4(
      name.data(), val, int(arg.desc().valueRange[0]), int(arg.desc().valueRange[1]))) {
      arg.setInt(val[0], 0);
      arg.setInt(val[1], 1);
      arg.setInt(val[2], 2);
      arg.setInt(val[3], 3);
      modified = true;
    }
    break;
  }
  default:
    spdlog::warn("argument {} has strange tupleSize of {}", name, arg.desc().tupleSize);
    return false;
  }
  return modified;
}

static bool stringInspector(std::string_view name, OpContext*, ArgValue &arg)
{
  bool modified = false;
  std::string val = arg.asString();
  if (ImGui::InputText(name.data(), &val, ImGuiInputTextFlags_EnterReturnsTrue)) {
    arg.setString(val);
    modified = true;
  }
  return modified;
}

static bool toggleInspector(std::string_view name, OpContext*, ArgValue &arg)
{
  bool val = arg.asBool();
  if (ImGui::Checkbox(name.data(), &val)) {
    arg.setBool(val);
    return true;
  }
  return false;
}

static bool menuInspector(std::string_view name, OpContext*, ArgValue &arg)
{
  bool modified = false;
  if (!arg.desc().menu.empty()) {
    int sel = static_cast<int>(arg.asInt());
    auto strVal = arg.asString();

    std::vector<char const*> selections;
    for (auto const& item : arg.desc().menu)
      selections.push_back(item.c_str());

    // previously selected a menu item which doesn't exist now
    if (std::find(arg.desc().menu.begin(), arg.desc().menu.end(), strVal) == arg.desc().menu.end()) {
      selections.push_back(strVal.c_str());
      sel = int(selections.size()) - 1;
    }
    if (ImGui::Combo(name.data(), &sel, &selections[0], static_cast<int>(selections.size())) ||
      selections[sel] != arg.asString()) {
      arg.setMenu(selections[sel]);
      modified = true;
    }
  } else {
    int               sel = 0;
    char const* const items[1] = { "" };
    ImGui::Combo(name.data(), &sel, items, 0, -1);
  }
  return modified;
}

static bool multiMenuInspector(std::string_view name, OpContext*, ArgValue &arg)
{
  bool modified = false;
  Vector<String> items;
  Vector<bool>   selected;
  for (auto const& name: arg.desc().menu) {
    items.push_back(name);
    selected.push_back(std::find(arg.asStringList().begin(), arg.asStringList().end(), name) != arg.asStringList().end());
  }
  for (auto const& prevSelected: arg.asStringList()) {
    if (!prevSelected.empty() && std::find(items.begin(), items.end(), prevSelected)==items.end()) {
      items.push_back(prevSelected);
      selected.push_back(true);
    }
  }
  
  ImGui::Text("%s: ", arg.desc().label.empty() ? arg.desc().name.c_str() : arg.desc().label.c_str());
  ImGui::BeginChild(name.data(), ImVec2(0,128), true);
  for (size_t i=0;i<items.size();++i) {
    if (ImGui::Selectable(items[i].c_str(), &selected[i])) {
      modified = true;
    }
  }
  ImGui::EndChild();
  if (modified) {
    Vector<String> selectedItems;
    for (size_t i=0;i<items.size();++i) {
      if (selected[i])
        selectedItems.push_back(items[i]);
    }
    arg.setStringList(selectedItems);
  }
  return modified;
}

static bool colorInspector(std::string_view name, OpContext*, ArgValue &arg)
{
  auto  cc = arg.asReal4();
  float val[4] = { float(cc.x), float(cc.y), float(cc.z), float(cc.w) };
  if (ImGui::ColorEdit4(name.data(), val, ImGuiColorEditFlags_HDR)) {
    arg.setReal(val[0], 0);
    arg.setReal(val[1], 1);
    arg.setReal(val[2], 2);
    arg.setReal(val[3], 3);
    return true;
  }
  return false;
}

static bool buttonInspector(std::string_view name, OpContext* ctx, ArgValue &arg)
{
  if (!arg.desc().callback.empty()) {
    if (ImGui::Button(name.data())) {
      sol::state lua;
      joyflow::bindLuaTypes(lua, true);
      lua["ctx"] = ctx;
      lua["self"] = &arg;
      spdlog::info("evaluation callback script of node {}, arg {}", ctx->node()->name(), name);
      try {
        lua.safe_script(arg.desc().callback, sol::script_throw_on_error);
        arg.setErrorMessage("");
      } catch (sol::error const& e) {
        arg.setErrorMessage(e.what());
      }
      return true;
    }
  }
  return false;
}

class ColorTextEditorAttachment : public ArgAttachment, public joyflow::ObjectTracker<ColorTextEditorAttachment>
{
  TextEditor* editor_;
public:
  TextEditor* editor() { return editor_; }
  ColorTextEditorAttachment(TextEditor* ed) :editor_(ed) {}
  ~ColorTextEditorAttachment() { delete editor_; }
};
static bool codeInspector(std::string_view name, OpContext* ctx, ArgValue &arg)
{
  std::string buf = arg.asString();
  editorui::FontScope monoscope(editorui::FontScope::MONOSPACE);
  // TODO: replace with a more decent syntax-highlighting editor
  /*
  if (ImGui::InputTextMultiline(name.data(),
    &buf,
    ImVec2(0, std::min(800, int(std::count(buf.begin(), buf.end(), '\n'))*24)),
    ImGuiInputTextFlags_AllowTabInput | ImGuiInputTextFlags_EnterReturnsTrue)) {
    arg.setString(buf);
    return true;
  }
  */
  bool selected = false;
  if (ImGui::Selectable(name.data(), &selected)) {
    // TODO: toggle between expression and evaluated string
  }
  if (arg.attachment()==nullptr) {
    auto* editor = new TextEditor;
    auto const& lang = arg.desc().codeLanguage();
    if (lang == "lua")
      editor->SetLanguageDefinition(TextEditor::LanguageDefinition::Lua());
    else if (lang == "cpp")
      editor->SetLanguageDefinition(TextEditor::LanguageDefinition::CPlusPlus());
    editor->SetPalette(editor->GetDarkPalette());
    editor->SetColorizerEnable(true);
    editor->SetTabSize(2);
    editor->SetText(arg.getRawExpr(0));
    editor->SetImGuiChildIgnored(true);
    arg.setAttachment(new ColorTextEditorAttachment(editor));
  }
  auto* attachment = static_cast<ColorTextEditorAttachment*>(arg.attachment());
  auto editorsize = ImGui::GetWindowContentRegionMax() - ImGui::GetWindowContentRegionMin();
  auto modified = false;
  editorsize.y = std::min(800.f, std::max(editorsize.y-64, 100.f));
  ImGui::BeginChild(name.data(), editorsize, true);
  attachment->editor()->Render(name.data(), editorsize, true);
  if (ImGui::IsWindowFocused() && ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_Enter)) && ImGui::GetIO().KeyCtrl) {
    arg.setRawExpr(attachment->editor()->GetText(), 0);
    modified = true;
    arg.eval(ctx);
  }
  ImGui::EndChild();
  return modified;
}

static bool filepathInspector(std::string_view name, OpContext*, ArgValue &arg)
{
  bool modified = false;
  std::string oldpath = arg.asString();
  auto displayname = arg.desc().label.empty() ? arg.desc().name : arg.desc().label;
  ImGui::Text("%s", displayname.c_str());
  ImGui::SameLine();
  std::string hiddenname = fmt::format("##ui_{}", name);
  if (ImGui::InputText(hiddenname.c_str(), &oldpath, ImGuiInputTextFlags_EnterReturnsTrue)) {
    arg.setString(oldpath);
  }
  ImGui::SameLine();
  if (ImGui::Button("...")) {
    nfdchar_t* filepath = nullptr;
    joyflow::String filter = arg.desc().fileFilter();
    if (filter.empty())
      filter = "*.*";
    switch (arg.desc().type) {
    case joyflow::ArgType::FILEPATH_OPEN:
      if (NFD_OpenDialog(filter.c_str(), nullptr, &filepath) == NFD_OKAY) {
        arg.setString(filepath);
        modified = true;
      }
      break;
    case joyflow::ArgType::FILEPATH_SAVE:
      if (NFD_SaveDialog(filter.c_str(), nullptr, &filepath) == NFD_OKAY) {
        arg.setString(filepath);
        modified = true;
      }
      break;
    default:
      throw joyflow::TypeError("unknown file dialog type, should not happen");
    }
    if (filepath)
      free(filepath);
  }
  return modified;
}

static bool dirpathInspector(std::string_view name, OpContext*, ArgValue &arg)
{
  bool modified = false;
  std::string oldpath = arg.asString();
  auto displayname = arg.desc().label.empty() ? arg.desc().name : arg.desc().label;
  ImGui::Text("%s", displayname.c_str());
  ImGui::SameLine();
  std::string hiddenname = fmt::format("##ui_{}", name);
  if (ImGui::InputText(hiddenname.c_str(), &oldpath, ImGuiInputTextFlags_EnterReturnsTrue)) {
    arg.setString(oldpath);
  }
  ImGui::SameLine();
  if (ImGui::Button("...")) {
    nfdchar_t* dirpath = nullptr;
    joyflow::String filter = arg.desc().fileFilter();
    if (NFD_PickFolder(filter.c_str(), &dirpath) == NFD_OKAY) {
      arg.setString(dirpath);
      modified = true;
    }
    if (dirpath)
      free(dirpath);
  }
  return modified;
}



////////////////////////////////////////////////////////////////////////////////

static std::unordered_map<joyflow::ArgType, ArgInspector>& inspectorRegistry()
{
  static std::unordered_map<joyflow::ArgType, ArgInspector> theRegistry;
  return theRegistry;
}

void setArgInspector(joyflow::ArgType type, ArgInspector inspector)
{
  inspectorRegistry()[type] = inspector;
}

ArgInspector getArgInspector(joyflow::ArgType type)
{
  if (auto itr = inspectorRegistry().find(type); itr != inspectorRegistry().end())
    return itr->second;

  switch (type)
  {
  case joyflow::ArgType::REAL:
    return floatInspector; break;
  case joyflow::ArgType::INT:
    return intInspector; break;
  case joyflow::ArgType::BOOL:
    return toggleInspector; break;
  case joyflow::ArgType::COLOR:
    return colorInspector; break;
  case joyflow::ArgType::MENU:
    return menuInspector; break;
  case joyflow::ArgType::MULTI_MENU:
    return multiMenuInspector; break;
  case joyflow::ArgType::STRING:
    return stringInspector; break;
  case joyflow::ArgType::CODEBLOCK:
    return codeInspector; break;
  case joyflow::ArgType::DIRPATH:
    return dirpathInspector; break;
  case joyflow::ArgType::FILEPATH_OPEN:
  case joyflow::ArgType::FILEPATH_SAVE:
    return filepathInspector; break;
  case joyflow::ArgType::OPREF:
    // TODO
    return stringInspector;
    break;
  case joyflow::ArgType::BUTTON:
    return buttonInspector; break;
  case joyflow::ArgType::TOGGLE:
    return toggleInspector; break;
  default:
    break;
  }
  return nullptr;
}

