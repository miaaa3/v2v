#include "ui/ImGuiLayer.h"
#include "graphics/Window.h"
#include "utils/Log.h"

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

namespace v2v::ui {

ImGuiLayer::~ImGuiLayer()
{
    if (!m_initialized) return;
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    m_initialized = false;
}

bool ImGuiLayer::init(graphics::Window& window, const char* glslVersion)
{
    if (m_initialized) return true;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename     = nullptr;   // don't write imgui.ini next to the binary
    io.MouseDrawCursor = false;     // use the OS cursor only — never draw a soft one

    // ----------------------------------------------------------------------
    //  Dark "smart-transportation dashboard" theme (deep navy + neon cyan)
    // ----------------------------------------------------------------------
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding     = 8.0f;
    style.ChildRounding      = 6.0f;
    style.FrameRounding      = 4.0f;
    style.PopupRounding      = 6.0f;
    style.ScrollbarRounding  = 6.0f;
    style.GrabRounding       = 4.0f;
    style.TabRounding        = 4.0f;
    style.WindowBorderSize   = 1.0f;
    style.FrameBorderSize    = 0.0f;
    style.WindowPadding      = ImVec2(12.0f, 10.0f);
    style.FramePadding       = ImVec2(8.0f, 5.0f);
    style.ItemSpacing        = ImVec2(8.0f, 6.0f);
    style.ItemInnerSpacing   = ImVec2(6.0f, 4.0f);
    style.GrabMinSize        = 12.0f;
    style.WindowTitleAlign   = ImVec2(0.0f, 0.5f);

    ImVec4* c = style.Colors;
    auto rgba = [](int r,int g,int b,int a){
        return ImVec4(r/255.0f,g/255.0f,b/255.0f,a/255.0f);
    };
    const ImVec4 bg0       = rgba(  8, 14, 28, 245); // app background
    const ImVec4 bg1       = rgba( 18, 26, 44, 245); // windows
    const ImVec4 bg2       = rgba( 26, 38, 60, 255); // frames
    const ImVec4 bg3       = rgba( 34, 50, 76, 255); // hovered
    const ImVec4 border    = rgba( 60, 90, 140, 90);
    const ImVec4 accentDim = rgba( 10, 180, 220,  60);
    const ImVec4 accent    = rgba( 14, 200, 240, 180);
    const ImVec4 accentHi  = rgba( 80, 220, 255, 230);
    const ImVec4 text      = rgba(220, 230, 245, 255);
    const ImVec4 textDim   = rgba(140, 156, 184, 255);

    c[ImGuiCol_WindowBg]            = bg1;
    c[ImGuiCol_ChildBg]             = bg0;
    c[ImGuiCol_PopupBg]             = bg1;
    c[ImGuiCol_Border]              = border;
    c[ImGuiCol_FrameBg]             = bg2;
    c[ImGuiCol_FrameBgHovered]      = bg3;
    c[ImGuiCol_FrameBgActive]       = bg3;
    c[ImGuiCol_TitleBg]             = bg2;
    c[ImGuiCol_TitleBgActive]       = bg3;
    c[ImGuiCol_TitleBgCollapsed]    = bg1;
    c[ImGuiCol_MenuBarBg]           = bg2;
    c[ImGuiCol_Header]              = accentDim;
    c[ImGuiCol_HeaderHovered]       = ImVec4(accent.x, accent.y, accent.z, 0.40f);
    c[ImGuiCol_HeaderActive]        = ImVec4(accent.x, accent.y, accent.z, 0.55f);
    c[ImGuiCol_Tab]                 = bg2;
    c[ImGuiCol_TabHovered]          = ImVec4(accent.x, accent.y, accent.z, 0.55f);
    c[ImGuiCol_TabActive]           = ImVec4(accent.x, accent.y, accent.z, 0.80f);
    c[ImGuiCol_TabUnfocused]        = bg2;
    c[ImGuiCol_TabUnfocusedActive]  = bg3;
    c[ImGuiCol_Button]              = bg3;
    c[ImGuiCol_ButtonHovered]       = ImVec4(accent.x, accent.y, accent.z, 0.55f);
    c[ImGuiCol_ButtonActive]        = ImVec4(accent.x, accent.y, accent.z, 0.75f);
    c[ImGuiCol_CheckMark]           = accentHi;
    c[ImGuiCol_SliderGrab]          = accent;
    c[ImGuiCol_SliderGrabActive]    = accentHi;
    c[ImGuiCol_ResizeGrip]          = accentDim;
    c[ImGuiCol_ResizeGripHovered]   = accent;
    c[ImGuiCol_ResizeGripActive]    = accentHi;
    c[ImGuiCol_Separator]           = border;
    c[ImGuiCol_SeparatorHovered]    = accent;
    c[ImGuiCol_PlotLines]           = accent;
    c[ImGuiCol_PlotLinesHovered]    = accentHi;
    c[ImGuiCol_PlotHistogram]       = accent;
    c[ImGuiCol_PlotHistogramHovered]= accentHi;
    c[ImGuiCol_TextSelectedBg]      = ImVec4(accent.x, accent.y, accent.z, 0.40f);
    c[ImGuiCol_Text]                = text;
    c[ImGuiCol_TextDisabled]        = textDim;
    c[ImGuiCol_TableHeaderBg]       = bg2;
    c[ImGuiCol_TableBorderLight]    = border;
    c[ImGuiCol_TableBorderStrong]   = border;
    c[ImGuiCol_TableRowBg]          = ImVec4(0,0,0,0);
    c[ImGuiCol_TableRowBgAlt]       = ImVec4(1,1,1,0.025f);
    c[ImGuiCol_NavHighlight]        = accent;

    if (!ImGui_ImplGlfw_InitForOpenGL(window.handle(), /*install_callbacks=*/false)) {
        V2V_LOG_ERROR("ImGui_ImplGlfw_InitForOpenGL failed");
        ImGui::DestroyContext();
        return false;
    }
    if (!ImGui_ImplOpenGL3_Init(glslVersion)) {
        V2V_LOG_ERROR("ImGui_ImplOpenGL3_Init failed");
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
        return false;
    }

    m_initialized = true;
    return true;
}

void ImGuiLayer::beginFrame()
{
    if (!m_initialized) return;
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
}

void ImGuiLayer::endFrame()
{
    if (!m_initialized) return;
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

bool ImGuiLayer::wantCaptureMouse() const
{
    if (!m_initialized) return false;
    return ImGui::GetIO().WantCaptureMouse;
}

bool ImGuiLayer::wantCaptureKeyboard() const
{
    if (!m_initialized) return false;
    return ImGui::GetIO().WantCaptureKeyboard;
}

} // namespace v2v::ui
