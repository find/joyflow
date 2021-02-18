#include "adapter.h"
#include "arginspector.h"
#include <nodegraph.h>
#include <core/error.h>
#include <core/traits.h>

#include <core/def.h>
#include <core/opdesc.h>
#include <core/opkernel.h>
#include <core/opgraph.h>
#include <core/opcontext.h>
#include <core/datatable.h>
#include <core/luabinding.h>
#include <core/stats.h>
#include <core/profiler.h>

#include <nlohmann/json.hpp>
#include <sol/sol.hpp>
#include <glm/glm.hpp>
#include <nfd.h>

#define IMGUI_DEFINE_MATH_OPERATORS
#include <imgui.h>
#include <imgui_internal.h>
#include <misc/cpp/imgui_stdlib.h>

#include <cinttypes>

class EditorNodeAdapter : public editorui::NodeGraphHook
{
private:
public:
  static void init(editorui::Graph* host, joyflow::OpGraph* graph) { host->setPayload(graph); }

  void* createNode(editorui::Graph*   host,
                   std::string const& type,
                   std::string const& name,
                   std::string&       realName) override
  {
    if (auto* opgraph = static_cast<joyflow::OpGraph*>(host->payload())) {
      auto opname = opgraph->addNode(type, name);
      realName    = opname;
      return opgraph->node(opname);
    } else {
      return nullptr;
    }
  }

  void* createGraph(editorui::Graph const* host) override
  {
    std::string name = fmt::format("graph_{}", static_cast<void const*>(host));
    return joyflow::newGraph(name);
  }

  bool onSave(editorui::Graph const* host, nlohmann::json& json, std::string const& path) override
  {
    if (auto* opgraph = static_cast<joyflow::OpGraph*>(host->payload())) {
      auto& mapping = json["mapping"];
      for (auto const& noderec : host->nodes()) {
        if (auto* opnode = static_cast<joyflow::OpNode*>(noderec.second.payload()))
          mapping[std::to_string(noderec.first)] = opnode->name();
      }
      return opgraph->save(json);
    }
    return false;
  }

  bool onLoad(editorui::Graph* host, nlohmann::json const& json, std::string const& path) override
  {
    if (auto* opgraph = static_cast<joyflow::OpGraph*>(host->payload())) {
      if (!opgraph->load(json))
        return false;
      std::unordered_set<std::string> nameset;

      auto const& mapping        = json.at("mapping");
      bool        hasMissingNode = false;
      for (auto const& m : mapping.items()) {
        char*       pEnd   = nullptr;
        size_t      nodeid = std::strtoull(m.key().c_str(), &pEnd, 10);
        std::string name   = m.value();
        nameset.insert(name);

        auto  itr    = host->nodes().find(nodeid);
        auto* opnode = opgraph->node(name);
        if (itr == host->nodes().end()) {
          spdlog::warn("node {} (aka {}) does not exist in op graph", nodeid, name);
          continue;
        }
        if (opnode == nullptr) {
          spdlog::warn("node {} (aka {}) does not exist in op graph", nodeid, name);
          continue;
        }
        itr->second.setPayload(opnode);
        itr->second.setHook(this);
      }
      for (auto const& name : opgraph->childNames()) {
        if (nameset.find(name) == nameset.end()) {
          spdlog::warn("node {} has no UI representation", name);
          hasMissingNode = true;
        }
      }
      return !hasMissingNode;
    }
    return false;
  }

  bool onPartialSave(editorui::Graph const* host, nlohmann::json& json, std::set<size_t> const& selection) override
  {
    if (selection.empty())
      return true;
    bool succ = true;
    auto &df = json["joyflow"];
    for (size_t nodeid : selection) {
      auto const& uinode = host->noderef(nodeid);
      if (uinode.payload()) {
        auto& nodedef = df[std::to_string(nodeid)];
        succ &= static_cast<joyflow::OpNode*>(uinode.payload())->save(nodedef);
        // don't save links, leave that to the editor
        nodedef["upstreams"].clear();
        nodedef["downstreams"].clear();
      }
    }
    return succ;
  }

  bool onPartialLoad(editorui::Graph* host, nlohmann::json const& json, std::set<size_t> const& selection, std::unordered_map<size_t, size_t> const& idmap) override
  {
    if (selection.empty())
      return true;
    bool succ = true;
    auto dfitr = json.find("joyflow");
    if (dfitr == json.end())
      return true;
    auto const& df = *dfitr;
    // idmap : from old(copy from) id to new(pasted) id
    // invIdMap : from new id to old one
    std::unordered_map<size_t, size_t> invIdMap = {};
    joyflow::HashMap<joyflow::String, joyflow::String> nameMap = {};
    for (auto const& item : idmap) {
      invIdMap[item.second] = item.first;
      if (host->nodes().find(item.first) == host->nodes().end()) // may happen when pasting to empty graph
        continue;
      nameMap[host->noderef(item.first).displayName()] = host->noderef(item.second).displayName();
    }
    for (size_t nodeid: selection) {
      if (auto mapitr = invIdMap.find(nodeid); mapitr!=invIdMap.end()) {
        auto *opnode = static_cast<joyflow::OpNode*>(host->noderef(mapitr->first).payload());
        if (auto itr = df.find(std::to_string(mapitr->second)); itr != df.end() && opnode) {
          succ &= opnode->load(*itr);
          // fix opref pathes
          for (size_t i = 0, n = opnode->argCount(); i < n; ++i) {
            if (opnode->arg(i).desc().type == joyflow::ArgType::OPREF) {
              auto oppath = opnode->arg(i).asString();
              if (auto itrMappedName = nameMap.find(oppath); itrMappedName!=nameMap.end()) {
                opnode->mutArg(opnode->argName(i)).setString(itrMappedName->second);
              }
            }
          }
        } else {
          spdlog::error("failed to load node of id {}, named {}", mapitr->second, opnode?opnode->name():"unknown");
          succ = false;
        }
      } else {
        spdlog::error("node {} has not been copied", nodeid);
        succ = false;
      }
    }
    return succ;
  }

  void onToolMenu(editorui::Graph* graph, editorui::GraphView const& gv) override
  {
    auto nodeSelection = gv.nodeSelection;
    if (gv.hoveredNode != -1 && nodeSelection.find(gv.hoveredNode) == nodeSelection.end())
      nodeSelection = { gv.hoveredNode };

    if (ImGui::MenuItem("Clear Data Cache", nullptr, nullptr)) {
      for (auto idx: nodeSelection) {
        if (auto* node = static_cast<joyflow::OpNode*>(graph->noderef(idx).payload())) {
          if (auto* ctx = node->context()) {
            node->setContext(nullptr);
          }
        }
      }
    }

    bool hasBypassedNodeInSelection = false;
    for (size_t idx : nodeSelection) {
      if (auto opnode=static_cast<joyflow::OpNode*>(graph->noderef(idx).payload())) {
        hasBypassedNodeInSelection |= opnode->isBypassed();
      }
    }
    if (ImGui::MenuItem("Bypass", nullptr, &hasBypassedNodeInSelection)) {
      for (size_t idx : nodeSelection) {
        if (auto opnode=static_cast<joyflow::OpNode*>(graph->noderef(idx).payload())) {
          opnode->setBypassed(hasBypassedNodeInSelection);
        }
      }
    }
  }

  void beforeDeleteNode(editorui::Node* uiNode) override
  {
    if (uiNode && uiNode->payload()) {
      auto* opnode = static_cast<joyflow::OpNode*>(uiNode->payload());
      opnode->parent()->removeNode(opnode->name());
    }
  }

  bool onNodeNameChanged(editorui::Node const* node,
                         std::string const&    original,
                         std::string&          newname) override
  {
    if (auto* opnode = static_cast<joyflow::OpNode*>(node->payload())) {
      if (auto* opgraph = opnode->parent())
        return opgraph->renameNode(original, newname, newname);
    }
    return false;
  }

  int getNodeMinInputCount(editorui::Node const* uiNode) override
  {
    using namespace joyflow;
    auto* opnode = static_cast<OpNode*>(uiNode->payload());
    ASSERT(opnode);
    return static_cast<int>(opnode->desc()->numRequiredInput);
  }

  int getNodeMaxInputCount(editorui::Node const* uiNode) override
  {
    using namespace joyflow;
    auto* opnode = static_cast<OpNode*>(uiNode->payload());
    ASSERT(opnode);
    return static_cast<int>(opnode->desc()->numMaxInput);
  }

  char const* getIcon(editorui::Node const* uiNode) override
  {
    auto* opnode = static_cast<joyflow::OpNode*>(uiNode->payload());
    if (opnode && opnode->desc() && !opnode->desc()->icon.empty())
      return opnode->desc()->icon.c_str();
    return nullptr;
  }

  int getNodeOutputCount(editorui::Node const* uiNode) override
  {
    using namespace joyflow;
    auto* opnode = static_cast<OpNode*>(uiNode->payload());
    ASSERT(opnode);
    return static_cast<int>(opnode->desc()->numOutputs);
  }

  char const* getPinDescription(editorui::Node const* uiNode, editorui::NodePin const& pin) override
  {
    using joyflow::AssertionFailure;
    auto* opnode = static_cast<joyflow::OpNode*>(uiNode->payload());
    ASSERT(opnode);
    if (pin.type == editorui::NodePin::INPUT && pin.pinNumber >= 0 && pin.pinNumber < opnode->desc()->inputPinNames.ssize())
      return opnode->desc()->inputPinNames[pin.pinNumber].c_str();
    else if (pin.type == editorui::NodePin::OUTPUT && pin.pinNumber >= 0 && pin.pinNumber < opnode->desc()->outputPinNames.ssize())
      return opnode->desc()->outputPinNames[pin.pinNumber].c_str();
    return nullptr;
  }

  void onLinkAttached(editorui::Node* source,
                      int             srcOutPin,
                      editorui::Node* dest,
                      int             dstInputPin) override
  {
    using namespace joyflow;
    auto* opsrc = static_cast<OpNode*>(source->payload());
    auto* opdst = static_cast<OpNode*>(dest->payload());

    ASSERT(opsrc->parent() == opdst->parent());
    ASSERT(opsrc->parent()->link(opsrc->name(), srcOutPin, opdst->name(), dstInputPin));
  }

  void onLinkDetached(editorui::Node* source,
                      int             srcOutPin,
                      editorui::Node* dest,
                      int             dstInputPin) override
  {
    using namespace joyflow;
    auto* opsrc = static_cast<OpNode*>(source->payload());
    auto* opdst = static_cast<OpNode*>(dest->payload());

    ASSERT(opsrc->parent() == opdst->parent());
    ASSERT(opsrc->parent()->unlink(opsrc->name(), srcOutPin, opdst->name(), dstInputPin));
  }

  std::vector<std::string> nodeClassList() override
  {
    std::vector<std::string> result;
    for (auto const& name : joyflow::OpRegistry::instance().list()) {
      result.push_back(name);
    }
    return result;
  }

  void onNodeDraw(editorui::Node const* node, editorui::GraphView const& gv) override
  {
    auto* opnode = static_cast<joyflow::OpNode*>(node->payload());
    if (opnode) {
      if (opnode->context() && opnode->context()->lastError()>=joyflow::OpErrorLevel::ERROR) {
        float const CROSS_SIZE = 16;
        auto const topleft = gv.canvasToScreen * glm::vec3(node->pos() - glm::vec2{ CROSS_SIZE, CROSS_SIZE }, 1);
        auto const topright = gv.canvasToScreen * glm::vec3(node->pos() + glm::vec2{ CROSS_SIZE,-CROSS_SIZE }, 1);
        auto const bottomleft = gv.canvasToScreen * glm::vec3(node->pos() - glm::vec2{ CROSS_SIZE, -CROSS_SIZE }, 1);
        auto const bottomright = gv.canvasToScreen * glm::vec3(node->pos() + glm::vec2{ CROSS_SIZE, CROSS_SIZE }, 1);
        ImGui::GetWindowDrawList()->AddLine({ topleft.x, topleft.y }, { bottomright.x, bottomright.y }, 0xFF004CFF, 12*gv.canvasScale);
        ImGui::GetWindowDrawList()->AddLine({ topright.x, topright.y }, { bottomleft.x, bottomleft.y }, 0xFF004CFF, 12*gv.canvasScale);
        // draw icon:
        /*
        editorui::FontScope scope(editorui::FontScope::LARGEICON);
        char const* skull = ICON_FA_SKULL;
        auto  fontsize = CROSS_SIZE * 3 * gv.canvasScale;
        auto* font = ImGui::GetFont();
        auto size = font->CalcTextSizeA(fontsize, 8192, 0, skull);
        auto center = gv.canvasToScreen * glm::vec3(node->pos(), 1);
        ImVec2 textpos = { center.x - size.x / 2, center.y - size.y / 2 };
        ImGui::GetWindowDrawList()->AddText(font, fontsize, textpos, 0xff004cff, skull);
        */
      }

      if (opnode->isBypassed()) {
        char const* bypassIcon = ICON_FA_ANGLE_DOUBLE_DOWN;
        editorui::FontScope scope(editorui::FontScope::LARGEICON);
        auto const fontsize = node->size().y * gv.canvasScale * 0.8f;
        auto* font = ImGui::GetFont();
        auto size = font->CalcTextSizeA(fontsize, 8192, 0, bypassIcon);
        auto center = gv.canvasToScreen * glm::vec3(node->pos()-node->size()*glm::vec2(0.5f, -0.5f), 1);
        ImVec2 iconcenter = { center.x, center.y };
        ImVec2 textpos = { center.x - size.x / 2, center.y - size.y / 2 };
        ImGui::GetWindowDrawList()->AddCircleFilled(iconcenter, fontsize*0.8f, 0x99ffffff);
        ImGui::GetWindowDrawList()->AddCircle(iconcenter, fontsize*0.8f, 0xff318cff, 0, 2*gv.canvasScale);
        ImGui::GetWindowDrawList()->AddText(font, fontsize, textpos, 0xff318cff, bypassIcon);
      }
    }
  }

  void onGraphDraw(editorui::Graph const* graph, editorui::GraphView const& gv) override
  {
    if (ImGui::BeginPopup("node-right-click-menu")) {
      onToolMenu(gv.graph, gv);
      if (gv.nodeSelection.empty() || ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_Escape)))
        ImGui::CloseCurrentPopup();
      ImGui::EndPopup();
    }
  }

  bool onClicked(editorui::Node const* node, int mouseButton) override
  {
    if (mouseButton == ImGuiMouseButton_Right) {
      if (node)
        ImGui::OpenPopup("node-right-click-menu");
      else
        ImGui::OpenPopup("Create Node");
    }
    return true;
  }

  bool onNodeInspect(editorui::Node* node, editorui::GraphView const& gv) override
  {
    PROFILER_SCOPE("UI::Inspect", 0xEACD76);
    auto* opnode = static_cast<joyflow::OpNode*>(node->payload());
    bool modified = false;
    if (opnode) {
      ImGui::PushItemWidth(std::max(-128.f, -ImGui::GetWindowContentRegionWidth()/4.f));
      try {
        for (auto i = 0; i < opnode->argCount(); ++i) {
          auto  name = opnode->argName(i);
          auto& arg  = opnode->mutArg(name);
          // TODO: update condition as an option
          if (!arg.desc().updateScript.empty() && opnode->context() &&
              arg.updateScriptEvaluatedVersion() < opnode->context()->evalCount()) {
            sol::state lua;
            joyflow::bindLuaTypes(lua, true);
            lua["ctx"]  = opnode->context();
            lua["self"] = &arg;
            spdlog::info("evaluation update script of node {}, arg {}", opnode->name(), name);
            arg.setUpdateScriptEvaluatedVersion(opnode->context()->evalCount());
            try {
              opnode->context()->beforeFrameEval(); // resolve dependency
              lua.safe_script(arg.desc().updateScript, sol::script_throw_on_error);
              arg.setErrorMessage("");
              opnode->context()->afterFrameEval();  // clear dependency
            } catch (sol::error const& e) {
              arg.setErrorMessage(e.what());
            }
          }
          std::string uiname =
              fmt::format("{}##ui_{}", arg.desc().label.empty() ? name : arg.desc().label, name);
          auto inspector = getArgInspector(arg.desc().type);
          if (inspector) {
            auto prevVersion = arg.version();
            inspector(uiname, opnode->context(), arg);
            modified = modified || arg.version() > prevVersion;
          }

          if (!arg.desc().description.empty()) {
            ImGui::SameLine();
            ImGui::TextDisabled(" ( ? )");
            if (ImGui::IsItemHovered()) {
              ImGui::BeginTooltip();
              ImGui::Text("%s", arg.desc().description.c_str());
              ImGui::EndTooltip();
            }
          }
          if (!arg.errorMessage().empty()) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(1,0.3f,0,1), " ( ! )");
            if (ImGui::IsItemHovered()) {
              ImGui::BeginTooltip();
              ImGui::TextColored(ImVec4(1, 0, 0, 1), "%s", arg.errorMessage().c_str());
              ImGui::EndTooltip();
            }
          }
        }
      } catch (sol::error const& e) {
        spdlog::error("lua error: {}", e.what());
      } catch (std::exception const& e) {
        spdlog::error("exception: {}", e.what());
      }
      ImGui::PopItemWidth();
    }
    return modified;
  }

  bool onInspectNodeData(editorui::Node* node, editorui::GraphView const& gv) override
  {
    PROFILER_SCOPE("UI::InspectData", 0xEEDEB0);
    using joyflow::AssertionFailure;
    auto* opnode  = static_cast<joyflow::OpNode*>(node->payload());
    auto* opgraph = static_cast<joyflow::OpGraph*>(gv.graph->payload());
    DEBUG_ASSERT(opnode == opgraph->node(opnode->name()));

    auto dc = opgraph->evalNode(opnode->name());
    auto* context = opnode->context();

    DEBUG_ASSERT(context != nullptr);

    if (context->lastError() >= joyflow::OpErrorLevel::ERROR) {
      if (!context->errorMessage().empty()) {
        ImGui::TextColored(ImVec4(1, 0.3f, 0, 1), "%s", opnode->context()->errorMessage().c_str());
      } 
      return false;
    } else if (!dc) {
      ImGui::Text("No data");
      return false;
    }
    if (ImGui::BeginTabBar("tables")) {
      for (joyflow::sint i = 0, n = dc->numTables(); i < n; ++i) {
        auto name = fmt::format("{}", i);
        if (ImGui::BeginTabItem(name.c_str())) {
          if (ImGui::BeginTabBar("data", ImGuiTabBarFlags_AutoSelectNewTabs)) {
            auto* table = dc->getTable(i);
            if (table && table->numColumns()>0 && ImGui::BeginTabItem("data")) {
              auto clientsize = ImGui::GetWindowContentRegionMax() - ImGui::GetWindowContentRegionMin();
              if (table->numColumns()>0 &&
                  ImGui::BeginTable("datatable", static_cast<int>(table->numColumns())+1,
                    ImGuiTableFlags_Borders | ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollX |
                    ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg | ImGuiTableFlags_NoSavedSettings,
                    clientsize, std::max(clientsize.x-ImGui::GetStyle().ScrollbarSize, table->numColumns()*300.f))) {
                auto columnNames = table->columnNames();
                float approxFontWidth = ImGui::GetFontSize() / 2;
                ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, 6*approxFontWidth);
                for (auto const& name : columnNames) {
                  float columnWidth = (name.length() + 2) * approxFontWidth;
                  if (table->numRows()>0) {
                    if (auto* col=table->getColumn(name)) {
                      auto txt = col->toString(table->getIndex(0));
                      columnWidth = std::min<int>(int(std::max(name.length(), txt.length())+2), 120) * approxFontWidth;
                    }
                  }
                  ImGui::TableSetupColumn(name.c_str(), ImGuiTableColumnFlags_WidthFixed, columnWidth);
                }
                ImGui::TableSetupScrollFreeze(1, 1);
                ImGui::TableHeadersRow();
                ImGui::TableNextRow(ImGuiTableRowFlags_None);
                ALWAYS_ASSERT(table->numRows() < INT_MAX);
                ImGuiListClipper clipper(static_cast<int>(table->numRows()));
                while (clipper.Step()) {
                  for (joyflow::sint row = clipper.DisplayStart, nrows = clipper.DisplayEnd;
                    row < nrows;
                    ++row) {
                    auto idx = table->getIndex(row);
                    if (!idx.valid()) {
                      // pass this row
                      nrows = std::min<joyflow::sint>(nrows + 1, table->numRows());
                      continue;
                    }
                    std::string idstr = std::to_string(row);
                    ImGui::TableNextColumn();
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(.3f, .3f, .3f, .8f));
                    ImGui::Selectable(idstr.c_str(), false, ImGuiSelectableFlags_SpanAllColumns);
                    ImGui::PopStyleColor();
                    for (auto const& name : columnNames) {
                      auto* col = table->getColumn(name);
                      auto  txt = col->toString(idx);
                      ImGui::TableNextColumn();
                      ImGui::Text("%s", txt.c_str());
                    }
                  }
                }

                ImGui::EndTable();
              }
              ImGui::EndTabItem();
            }
            if (table && !table->vars().empty() && ImGui::BeginTabItem("vars")) {
              auto const& varmap = table->vars();
              if (ImGui::BeginTable("variables", 2, ImGuiTableFlags_Borders)) {
                ImGui::TableSetupColumn("Key");
                ImGui::TableSetupColumn("Value");
                ImGui::TableHeadersRow();
                ImGui::TableNextRow(ImGuiTableRowFlags_None);
                for (auto const& kv: varmap) {
                  ImGui::TableNextColumn();
                  ImGui::Text("%s", kv.first.c_str());
                  ImGui::TableNextColumn();
                  if (!kv.second.has_value()) {
                    // bypass
                  }
                  auto const& tp = kv.second.type();
                  if (tp == typeid(uint8_t)) {
                    ImGui::Text("0x%02x", std::any_cast<uint8_t>(kv.second));
                  } else if (tp == typeid(int8_t)) {
                    ImGui::Text("0x%02x", std::any_cast<int8_t>(kv.second));
                  } else if (tp == typeid(uint16_t)) {
                    ImGui::Text("%" PRIu16, std::any_cast<uint16_t>(kv.second));
                  } else if (tp == typeid(int16_t)) {
                    ImGui::Text("%" PRId16, std::any_cast<int16_t>(kv.second));
                  } else if (tp == typeid(uint32_t)) {
                    ImGui::Text("%" PRIu32, std::any_cast<uint32_t>(kv.second));
                  } else if (tp == typeid(int32_t)) {
                    ImGui::Text("%" PRId32, std::any_cast<int32_t>(kv.second));
                  } else if (tp == typeid(uint64_t)) {
                    ImGui::Text("%" PRIu64, std::any_cast<uint64_t>(kv.second));
                  } else if (tp == typeid(int64_t)) {
                    ImGui::Text("%" PRId64, std::any_cast<int64_t>(kv.second));
                  } else if (tp == typeid(float)) {
                    ImGui::Text("%f", std::any_cast<float>(kv.second));
                  } else if (tp == typeid(double)) {
                    ImGui::Text("%f", std::any_cast<double>(kv.second));
                  } else if (tp == typeid(bool)) {
                    ImGui::Text(std::any_cast<bool>(kv.second) ? "true" : "false");
                  } else if (tp == typeid(joyflow::String)) {
                    ImGui::Text("%s", std::any_cast<joyflow::String>(kv.second).c_str());
                  } else if (tp == typeid(std::string_view)) {
                    ImGui::Text("%s", std::any_cast<std::string_view>(kv.second).data());
                  } else {
                    ImGui::Text("data of type \"%s\" (dont know how to display)", tp.name());
                  }
                }
                ImGui::EndTable();
              }
              ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("info")) {
              // error message
              ImGui::Text("Message: ");
              {
                auto errlvl = context->lastError();
                ImVec4 tcolor = { 1,1,1,1 };
                switch(errlvl) {
                case joyflow::OpErrorLevel::WARNING:
                  tcolor = { 0.8f,0.9f,0.1f,1.f }; break;
                case joyflow::OpErrorLevel::ERROR:
                  tcolor = { 1.f,0.3f,0.1f,1.f }; break;
                case joyflow::OpErrorLevel::FATAL:
                  tcolor = { 1.f,0.1f,0.1f,1.f }; break;
                default:
                  break;
                }
                ImGui::TextColored(tcolor, "%s", context->errorMessage().c_str());
              }
              ImGui::Separator();
              auto*  table       = dc->getTable(i);
              size_t sharedbytes = 0, unsharedbytes = 0;
              ImGui::Text("Whole Table Reference Count: %zu", table->shareCount());
              table->countMemory(sharedbytes, unsharedbytes);
              auto metainfo = fmt::format(
                  "Shared :   {} bytes\nUnshared : {} bytes", sharedbytes, unsharedbytes);
              ImGui::Text("Memory Usage:");
              ImGui::Text("%s", metainfo.c_str());
              ImGui::Separator();
              uint64_t nrows = table->numRows(), nidx = table->numIndices();
              ImGui::Text("Number of Rows    : %" PRIu64, nrows);
              ImGui::Text("Number of Indices : %" PRIu64, nidx);
              auto colnames = table->columnNames();
              if (colnames.size() > 0) {
                ImGui::Separator();
                ImGui::Text("Columns:");
                if (ImGui::BeginTable("Column Info", 7, ImGuiTableFlags_Borders)) {
                  ImGui::TableSetupColumn("Name");
                  ImGui::TableSetupColumn("Reference Count");
                  ImGui::TableSetupColumn("Type");
                  ImGui::TableSetupColumn("TupleSize");
                  ImGui::TableSetupColumn("ItemSize");
                  ImGui::TableSetupColumn("SharedBytes");
                  ImGui::TableSetupColumn("UnsharedBytes");
                  ImGui::TableHeadersRow();
                  ImGui::TableNextRow(ImGuiTableRowFlags_None);
                  for (auto const& name : colnames) {
                    size_t sbytes = 0, usbytes = 0;
                    auto* col = table->getColumn(name);
                    col->countMemory(sbytes, usbytes);
                    ImGui::TableNextColumn();
                    bool selected = false;
                    ImGui::Selectable(name.c_str(), &selected, ImGuiSelectableFlags_SpanAllColumns);
                    ImGui::TableNextColumn();
                    ImGui::Text("%zu", col->shareCount());
                    ImGui::TableNextColumn();
                    ImGui::Text("%s", joyflow::dataTypeName(col->dataType()));
                    ImGui::TableNextColumn();
                    ImGui::Text("%d", int(col->tupleSize()));
                    ImGui::TableNextColumn();
                    ImGui::Text("%zu", col->desc().elemSize); 
                    ImGui::TableNextColumn();
                    ImGui::Text("%zu", sbytes);
                    ImGui::TableNextColumn();
                    ImGui::Text("%zu", usbytes);
                  }
                  ImGui::EndTable();
                }
              }
              ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
          }
          ImGui::EndTabItem();
        }
      }
      ImGui::EndTabBar();
    }
    return false;
  }

  void onInspectGraphSummary(editorui::Graph* graph, editorui::GraphView const& gv) override
  {
    if (auto* opgraph = static_cast<joyflow::OpGraph*>(graph->payload())) {
      // TODO: per-graph summary
      uint64_t lc = joyflow::Stats::livingCount();
      ImGui::Text("Number of tracked living objects : %" PRIu64, lc);
      if (ImGui::TreeNode("Details:")) {
        joyflow::Stats::dumpLiving([](char const* msg, void* arg) { ImGui::Text("%s", msg); });
        ImGui::TreePop();
      }
      ImGui::Separator();
      if (ImGui::TreeNode("mimalloc stats: ")) {
        mi_stats_print_out([](char const* msg, void* arg) { ImGui::Text("%s", msg); }, nullptr);
        ImGui::TreePop();
      }
    }
  }
};

namespace joyflow {

editorui::NodeGraphHook* makeEditorAdaptor()
{
  return new EditorNodeAdapter;
}

}
