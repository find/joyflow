// Dear ImGui: standalone example application for GLFW + OpenGL2, using legacy fixed pipeline
// (GLFW is a cross-platform general purpose library for handling windows, inputs, OpenGL/Vulkan/Metal graphics context creation, etc.)
// If you are new to Dear ImGui, read documentation from the docs/ folder + read the top of imgui.cpp.
// Read online: https://github.com/ocornut/imgui/tree/master/docs

// **DO NOT USE THIS CODE IF YOUR CODE/ENGINE IS USING MODERN OPENGL (SHADERS, VBO, VAO, etc.)**
// **Prefer using the code in the example_glfw_opengl2/ folder**
// See imgui_impl_glfw.cpp for details.

#include <core/opdesc.h>
#include <core/opgraph.h>
#include <core/opkernel.h>
#include <core/opcontext.h>
#include <core/datatable.h>
#include <core/opbuiltin.h>
#include <core/error.h>
#include <core/log.h>
#include <core/oplib.h>

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl2.h"

#include "adapter.h"
#include <nodegraph.h>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/msvc_sink.h>
#include <stdio.h>
#ifdef __APPLE__
#define GL_SILENCE_DEPRECATION
#endif
#include <GLFW/glfw3.h>

#include <core/profiler.h>

#include <filesystem>

static void glfw_error_callback(int error, const char* description)
{
  spdlog::error("glfw error {}: {}", error, description);
}


class CreateTestArray : public joyflow::OpKernel
{
  void eval(joyflow::OpContext& ctx) const override
  {
    PROFILER_SCOPE("CreateTestArray", 0xff6a11);
    auto *dc = ctx.reallocOutput(0);
    auto tbid = dc->addTable();
    auto* tb = dc->getTable(tbid);
    int startIdx = int(ctx.arg("start_idx").asInt());
    tb->createColumn<int>("id",0);
    tb->createColumn<joyflow::String>("name");
    tb->addRows(1024);
    int* idarr = tb->getColumn("id")->asNumericData()->rawBufferRW<int>(joyflow::CellIndex(0), 1024);
    for (int i = 0; i < 1024; ++i) {
      idarr[i] = i+startIdx;
      tb->set<joyflow::String>("name", i, "test" + std::to_string(i+startIdx));
    }
  }
  
public:
  static joyflow::OpDesc mkDesc()
  {
    using namespace joyflow;
    return makeOpDesc<CreateTestArray>("testarray")
      .argDescs({ ArgDescBuilder("start_idx").type(ArgType::INT).label("Start Index").valueRange(0,1000) })
      .numRequiredInput(0)
      .numMaxInput(0);
  }
};

int main(int, char**)
{
#ifdef _WIN32
  spdlog::default_logger()->sinks().push_back(std::make_shared<spdlog::sinks::msvc_sink_mt>());
#endif
  spdlog::set_level(spdlog::level::debug);
  joyflow::setLogger(spdlog::default_logger());

  joyflow::registerBuiltinOps();
  for (auto const& entry: std::filesystem::directory_iterator(joyflow::defaultOpDir())) {
#ifdef _WIN32
    static std::string const dllext = ".dll";
#else
    static std::string const dllext = ".so";
#endif
    if (entry.is_regular_file() && entry.path().extension() == dllext) {
      joyflow::openOpLib(entry.path().string());
    }
  }
  joyflow::OpRegistry::instance().add(CreateTestArray::mkDesc());
  editorui::Graph graph;
  std::unique_ptr<editorui::NodeGraphHook> hook(joyflow::makeEditorAdaptor());
  std::unique_ptr<joyflow::OpGraph> opgraph(joyflow::newGraph("test_graph"));
  opgraph->newContext();
  graph.setPayload(opgraph.get());
  graph.setHook(hook.get());
  graph.addViewer();

  // Setup window
  glfwSetErrorCallback(glfw_error_callback);
  if (!glfwInit())
    return 1;
  GLFWwindow* window = glfwCreateWindow(1280, 720, "Joyflow", NULL, NULL);
  if (window == NULL)
    return 1;
  glfwMakeContextCurrent(window);
  glfwSwapInterval(1); // Enable vsync

  // Setup Dear ImGui context
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO(); (void)io;
  //io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
  //io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls
  io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;           // Enable Docking
  io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;         // Enable Multi-Viewport / Platform Windows
  io.ConfigFlags |= ImGuiConfigFlags_DpiEnableScaleViewports;
  io.ConfigFlags |= ImGuiConfigFlags_DpiEnableScaleFonts;

  // Setup Dear ImGui style
  ImGui::StyleColorsDark();
  ImVec4* colors = ImGui::GetStyle().Colors;
  colors[ImGuiCol_FrameBg] = ImVec4(0.28f, 0.28f, 0.28f, 0.54f);
  colors[ImGuiCol_FrameBgHovered] = ImVec4(0.68f, 0.67f, 0.64f, 0.40f);
  colors[ImGuiCol_FrameBgActive] = ImVec4(0.45f, 0.45f, 0.45f, 0.67f);
  colors[ImGuiCol_TitleBgActive] = ImVec4(0.19f, 0.19f, 0.19f, 1.00f);
  colors[ImGuiCol_CheckMark] = ImVec4(0.67f, 0.67f, 0.67f, 1.00f);
  colors[ImGuiCol_SliderGrab] = ImVec4(0.58f, 0.56f, 0.56f, 1.00f);
  colors[ImGuiCol_SliderGrabActive] = ImVec4(0.87f, 0.87f, 0.87f, 1.00f);
  colors[ImGuiCol_Button] = ImVec4(0.47f, 0.46f, 0.45f, 0.40f);
  colors[ImGuiCol_ButtonHovered] = ImVec4(0.50f, 0.50f, 0.50f, 1.00f);
  colors[ImGuiCol_ButtonActive] = ImVec4(0.69f, 0.69f, 0.69f, 1.00f);
  colors[ImGuiCol_Header] = ImVec4(0.32f, 0.32f, 0.32f, 1.00f);
  colors[ImGuiCol_HeaderHovered] = ImVec4(0.26f, 0.26f, 0.26f, 1.00f);
  colors[ImGuiCol_HeaderActive] = ImVec4(0.76f, 0.76f, 0.76f, 1.00f);
  colors[ImGuiCol_SeparatorHovered] = ImVec4(0.57f, 0.59f, 0.61f, 0.78f);
  colors[ImGuiCol_SeparatorActive] = ImVec4(0.58f, 0.58f, 0.58f, 1.00f);
  colors[ImGuiCol_ResizeGrip] = ImVec4(0.48f, 0.48f, 0.48f, 0.25f);
  colors[ImGuiCol_ResizeGripHovered] = ImVec4(0.60f, 0.60f, 0.60f, 0.67f);
  colors[ImGuiCol_ResizeGripActive] = ImVec4(0.82f, 0.82f, 0.82f, 0.95f);
  colors[ImGuiCol_Tab] = ImVec4(0.23f, 0.23f, 0.23f, 0.86f);
  colors[ImGuiCol_TabHovered] = ImVec4(0.28f, 0.28f, 0.28f, 0.80f);
  colors[ImGuiCol_TabActive] = ImVec4(0.32f, 0.32f, 0.32f, 1.00f);
  colors[ImGuiCol_TabUnfocused] = ImVec4(0.05f, 0.05f, 0.05f, 0.97f);
  colors[ImGuiCol_TabUnfocusedActive] = ImVec4(0.22f, 0.22f, 0.22f, 1.00f);
  colors[ImGuiCol_DockingPreview] = ImVec4(0.61f, 0.61f, 0.61f, 0.70f);
  colors[ImGuiCol_TextSelectedBg] = ImVec4(1.00f, 1.00f, 1.00f, 0.35f);
  colors[ImGuiCol_NavHighlight] = ImVec4(0.78f, 0.78f, 0.78f, 1.00f);

  // When viewports are enabled we tweak WindowRounding/WindowBg so platform windows can look identical to regular ones.
  ImGuiStyle& style = ImGui::GetStyle();
  if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
  {
    style.WindowRounding = 0.0f;
    style.Colors[ImGuiCol_WindowBg].w = 1.0f;
  }
  style.ChildRounding = 4.0f;

  // Setup Platform/Renderer backends
  ImGui_ImplGlfw_InitForOpenGL(window, true);
  ImGui_ImplOpenGL2_Init();

  // Load Fonts
  // - If no fonts are loaded, dear imgui will use the default font. You can also load multiple fonts and use ImGui::PushFont()/PopFont() to select them.
  // - AddFontFromFileTTF() will return the ImFont* so you can store it if you need to select the font among multiple.
  // - If the file cannot be loaded, the function will return NULL. Please handle those errors in your application (e.g. use an assertion, or display an error and quit).
  // - The fonts will be rasterized at a given size (w/ oversampling) and stored into a texture when calling ImFontAtlas::Build()/GetTexDataAsXXXX(), which ImGui_ImplXXXX_NewFrame below will call.
  // - Read 'docs/FONTS.md' for more instructions and details.
  // - Remember that in C/C++ if you want to include a backslash \ in a string literal you need to write a double backslash \\ !
  //io.Fonts->AddFontDefault();
  //io.Fonts->AddFontFromFileTTF("../../misc/fonts/Roboto-Medium.ttf", 16.0f);
  //io.Fonts->AddFontFromFileTTF("../../misc/fonts/Cousine-Regular.ttf", 15.0f);
  //io.Fonts->AddFontFromFileTTF("../../misc/fonts/DroidSans.ttf", 16.0f);
  //io.Fonts->AddFontFromFileTTF("../../misc/fonts/ProggyTiny.ttf", 10.0f);
  //ImFont* font = io.Fonts->AddFontFromFileTTF("c:\\Windows\\Fonts\\ArialUni.ttf", 18.0f, NULL, io.Fonts->GetGlyphRangesJapanese());
  //IM_ASSERT(font != NULL);
  editorui::init();

  // Our state
  ImVec4 clear_color = ImVec4(0.45f, 0.45f, 0.45f, 1.00f);

  // Main loop
  while (!glfwWindowShouldClose(window))
  {
    PROFILER_FRAME("Frame");
    // Poll and handle events (inputs, window resize, etc.)
    // You can read the io.WantCaptureMouse, io.WantCaptureKeyboard flags to tell if dear imgui wants to use your inputs.
    // - When io.WantCaptureMouse is true, do not dispatch mouse input data to your main application.
    // - When io.WantCaptureKeyboard is true, do not dispatch keyboard input data to your main application.
    // Generally you may always pass all inputs to dear imgui, and hide them from your application based on those two flags.
    glfwPollEvents();

    // Start the Dear ImGui frame
    ImGui_ImplOpenGL2_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
    ImGui::DockSpaceOverViewport();

    {
      PROFILER_SCOPE("UI", 0xFA8C35);
      editorui::edit(graph, "graphed");
    }

    // Rendering
    {
      PROFILER_SCOPE("ImGui::Render", 0xFF3300);
      ImGui::Render();
      int display_w, display_h;
      glfwGetFramebufferSize(window, &display_w, &display_h);
      glViewport(0, 0, display_w, display_h);
      glClearColor(clear_color.x, clear_color.y, clear_color.z, clear_color.w);
      glClear(GL_COLOR_BUFFER_BIT);

      // If you are using this code with non-legacy OpenGL header/contexts (which you should not, prefer using imgui_impl_opengl3.cpp!!),
      // you may need to backup/reset/restore current shader using the commented lines below.
      //GLint last_program;
      //glGetIntegerv(GL_CURRENT_PROGRAM, &last_program);
      //glUseProgram(0);
      {
        PROFILER_SCOPE("RenderDrawData", 0xB35C44);
        ImGui_ImplOpenGL2_RenderDrawData(ImGui::GetDrawData());
      }
      //glUseProgram(last_program);

      if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
      {
        GLFWwindow* backup_current_context = glfwGetCurrentContext();
        ImGui::UpdatePlatformWindows();
        ImGui::RenderPlatformWindowsDefault();
        glfwMakeContextCurrent(backup_current_context);
      }

      {
        PROFILER_SCOPE("SwapBuffer", 0x60281E);
        glfwSwapBuffers(window);
      }
    }
  }

  // Cleanup
  editorui::deinit();
  ImGui_ImplOpenGL2_Shutdown();
  ImGui_ImplGlfw_Shutdown();
  ImGui::DestroyContext();

  glfwDestroyWindow(window);
  glfwTerminate();

  return 0;
}

#ifdef _WIN32
#include <Windows.h>
int WinMain(
  HINSTANCE hInstance,
  HINSTANCE hPrevInstance,
  LPSTR     lpCmdLine,
  int       nShowCmd)
{
  return main(__argc, __argv);
}
#endif
