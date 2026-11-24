#include "GUI.hpp"
#include "Session.hpp"

#include <raylib.h>
#include <raymath.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <initializer_list>
#include <iostream>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

namespace {

Font uiFont{};
bool uiFontLoaded = false;

void drawUiText(const char* value, int x, int y, int size, Color color) {
    if (uiFontLoaded)
        DrawTextEx(uiFont, value, {static_cast<float>(x), static_cast<float>(y)},
                   static_cast<float>(size), 0.0f, color);
    else
        ::DrawText(value, x, y, size, color);
}

int measureUiText(const char* value, int size) {
    return uiFontLoaded
        ? static_cast<int>(std::ceil(MeasureTextEx(uiFont, value, static_cast<float>(size), 0.0f).x))
        : ::MeasureText(value, size);
}

enum class GuiTab : int {
    Components = 0,
    Execution,
    Info,
    Debugger,
    Controls
};

struct GuiComponent {
    std::string name;
    Vector3 position;
    Vector3 size;
    Color color;
};

struct GuiRoute {
    std::array<Vector3, 4> points{};
    std::size_t count = 4;
    Color color = GRAY;
};

struct GuiSfx {
    Sound click{};
    Sound hover{};
    Sound step{};
    Sound halt{};
    Sound problem{};
    Sound cacheHit{};
    Sound cacheMiss{};
};

struct ModelComponent {
    std::string name;
    Vector3 position;
    Vector3 size;
    int hitboxMeshIndex = -1;
};

struct ListingLine {
    std::uint32_t address = 0;
    std::uint32_t word = 0;
    std::uint32_t sourceLine = 0;
    std::string source;
};

struct GlbHitbox {
    std::string name;
    int meshIndex = -1;
    Vector3 position{};
    Vector3 scale{1.0f, 1.0f, 1.0f};
};

bool readGlbHitboxes(const char* path, std::vector<GlbHitbox>& hitboxes) {
    std::ifstream file(path, std::ios::binary);

    if (!file)
        return false;

    std::uint32_t magic = 0, version = 0, length = 0;
    file.read(reinterpret_cast<char*>(&magic), 4);
    file.read(reinterpret_cast<char*>(&version), 4);
    file.read(reinterpret_cast<char*>(&length), 4);

    if (magic != 0x46546C67)
        return false;

    std::uint32_t chunkLength = 0, chunkType = 0;
    file.read(reinterpret_cast<char*>(&chunkLength), 4);
    file.read(reinterpret_cast<char*>(&chunkType), 4);

    if (chunkType != 0x4E4F534A)
        return false;

    std::string json(chunkLength, '\0');
    file.read(json.data(), chunkLength);

    const std::regex nameRegex("\"name\"\\s*:\\s*\"([^\"]+)\"");

    for (std::sregex_iterator match(json.begin(), json.end(), nameRegex), end; match != end; ++match) {
        const std::string name = (*match)[1].str();

        if (name.find("_HITBOX") == std::string::npos)
            continue;

        const std::size_t namePosition = static_cast<std::size_t>(match->position());
        const std::size_t objectStart = json.rfind('{', namePosition);
        const std::size_t objectEnd = json.find('}', namePosition);

        if (objectStart == std::string::npos || objectEnd == std::string::npos)
            continue;

        const std::string object = json.substr(objectStart, objectEnd - objectStart + 1);

        GlbHitbox hitbox;
        hitbox.name = name.substr(0, name.size() - 7);

        if (hitbox.name == "+1")
            hitbox.name = "ONE";
        else if (hitbox.name == "-1")
            hitbox.name = "MONE";
        else if (hitbox.name == "0")
            hitbox.name = "ZERO";
        else if (hitbox.name == "A")
            hitbox.name = "REGISTER_A";
        else if (hitbox.name == "B")
            hitbox.name = "REGISTER_B";
        else if (hitbox.name == "C")
            hitbox.name = "REGISTER_C";
        else if (hitbox.name == "D")
            hitbox.name = "REGISTER_D";
        else if (hitbox.name == "E")
            hitbox.name = "REGISTER_E";
        else if (hitbox.name == "F")
            hitbox.name = "REGISTER_F";
        else if (hitbox.name == "CHIMPS_LOGO")
            hitbox.name.clear();

        std::regex meshRegex("\"mesh\"\\s*:\\s*(\\d+)");
        std::smatch meshMatch;

        if (std::regex_search(object, meshMatch, meshRegex))
            hitbox.meshIndex = std::stoi(meshMatch[1].str());

        std::regex translationRegex("\"translation\"\\s*:\\s*\\[\\s*([^,\\]]+),\\s*([^,\\]]+),\\s*([^\\]]+)\\]");
        std::smatch translationMatch;

        if (std::regex_search(object, translationMatch, translationRegex)) {
            hitbox.position = {
                std::stof(translationMatch[1].str()),
                std::stof(translationMatch[2].str()),
                std::stof(translationMatch[3].str())
            };
        }

        std::regex scaleRegex("\"scale\"\\s*:\\s*\\[\\s*([^,\\]]+),\\s*([^,\\]]+),\\s*([^\\]]+)\\]");
        std::smatch scaleMatch;

        if (std::regex_search(object, scaleMatch, scaleRegex)) {
            hitbox.scale = {
                std::stof(scaleMatch[1].str()),
                std::stof(scaleMatch[2].str()),
                std::stof(scaleMatch[3].str())
            };
        }

        if (hitbox.meshIndex >= 0)
            hitboxes.push_back(hitbox);
    }

    return true;
}

std::vector<ListingLine> readListing(const std::string& programPath) {
    std::vector<ListingLine> lines;
    std::filesystem::path listingPath(programPath);

    listingPath.replace_extension(".lst");

    std::ifstream file(listingPath);

    if (!file)
        return lines;

    std::string line;
    std::getline(file, line);

    while (std::getline(file, line)) {
        std::istringstream input(line);
        std::string addressText, wordText;
        ListingLine listing;

        if (!(input >> addressText >> wordText >> listing.sourceLine))
            continue;

        try {
            listing.address = static_cast<std::uint32_t>(std::stoul(addressText, nullptr, 16));
            listing.word = static_cast<std::uint32_t>(std::stoul(wordText, nullptr, 16));
        } catch (...) {
            continue;
        }

        std::getline(input, listing.source);

        if (!listing.source.empty() && listing.source.front() == ' ')
            listing.source.erase(0, 1);

        const std::size_t sourceStart = listing.source.find_first_not_of(" ");

        if (sourceStart != std::string::npos)
            listing.source.erase(0, sourceStart);

        lines.push_back(listing);
    }

    return lines;
}

std::string hex32(std::uint32_t value) {
    std::ostringstream output;
    output << "0x" << std::uppercase << std::hex << std::setw(8) << std::setfill('0') << value;
    return output.str();
}

std::string hex36(std::uint64_t value) {
    std::ostringstream output;
    output << "0x" << std::uppercase << std::hex << std::setw(9)
           << std::setfill('0') << (value & ((std::uint64_t{1} << 36) - 1));
    return output.str();
}

bool pointInRectangle(Vector2 point, Rectangle rectangle) {
    return point.x >= rectangle.x && point.x <= rectangle.x + rectangle.width &&
           point.y >= rectangle.y && point.y <= rectangle.y + rectangle.height;
}

void roundedPanel(Rectangle bounds, Color fill, const char* title = nullptr) {
    DrawRectangleRounded(bounds, 0.04f, 8, fill);
    DrawRectangleRoundedLinesEx(bounds, 0.04f, 8, 1.0f, Fade(RAYWHITE, 0.10f));

    if (title) {
        drawUiText(title, static_cast<int>(bounds.x) + 14,
                 static_cast<int>(bounds.y) + 12, 18, RAYWHITE);
    }
}

bool tabButton(Rectangle bounds, const char* label, bool active) {
    const Vector2 mouse = GetMousePosition();
    const bool hovered = pointInRectangle(mouse, bounds);
    const bool pressed = hovered && IsMouseButtonPressed(MOUSE_BUTTON_LEFT);

    const Color fill = active ? Color{66, 110, 166, 255}
                     : hovered ? Color{44, 55, 72, 255}
                               : Color{28, 34, 43, 255};

    DrawRectangleRounded(bounds, 0.16f, 6, fill);

    const int textWidth = measureUiText(label, 16);

    drawUiText(label, static_cast<int>(bounds.x + (bounds.width - textWidth) * 0.5f),
             static_cast<int>(bounds.y + 12), 16, RAYWHITE);

    return pressed;
}

bool controlButton(Rectangle bounds, const char* label, bool primary = false) {
    const Vector2 mouse = GetMousePosition();
    const bool hovered = pointInRectangle(mouse, bounds);
    const bool pressed = hovered && IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
    const Color fill = pressed ? Color{36, 77, 118, 255}
                     : hovered ? (primary ? Color{78, 137, 195, 255} : Color{68, 78, 91, 255})
                     : (primary ? Color{55, 103, 151, 255} : Color{48, 57, 68, 255});

    DrawRectangleRounded(bounds, 0.18f, 6, fill);

    const int textWidth = measureUiText(label, 15);

    drawUiText(label, static_cast<int>(bounds.x + (bounds.width - textWidth) * 0.5f),
             static_cast<int>(bounds.y + 8), 15, RAYWHITE);

    return pressed;
}

enum class DebugIcon { Play, Pause, StepForward, StepBack, Reset };

bool iconButton(Rectangle bounds, DebugIcon icon, const char* tooltip,
                bool primary = false, bool selected = false) {
    const Vector2 mouse = GetMousePosition();
    const bool hovered = pointInRectangle(mouse, bounds);
    const bool pressed = hovered && IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
    const Color fill = pressed ? Color{36, 77, 118, 255}
                     : selected ? Color{61, 116, 154, 255}
                     : hovered ? (primary ? Color{78, 137, 195, 255} : Color{68, 78, 91, 255})
                     : (primary ? Color{55, 103, 151, 255} : Color{48, 57, 68, 255});
    DrawRectangleRounded(bounds, 0.18f, 6, fill);

    const float cx = bounds.x + bounds.width * 0.5f;
    const float cy = bounds.y + bounds.height * 0.5f;
    if (icon == DebugIcon::Play || icon == DebugIcon::StepForward) {
        DrawTriangle({cx - 7.0f, cy - 8.0f}, {cx - 7.0f, cy + 8.0f},
                     {cx + 7.0f, cy}, RAYWHITE);
        if (icon == DebugIcon::StepForward)
            DrawRectangle(static_cast<int>(cx + 10), static_cast<int>(cy - 8), 3, 16, RAYWHITE);
    } else if (icon == DebugIcon::Pause) {
        DrawRectangle(static_cast<int>(cx - 7), static_cast<int>(cy - 8), 5, 16, RAYWHITE);
        DrawRectangle(static_cast<int>(cx + 2), static_cast<int>(cy - 8), 5, 16, RAYWHITE);
    } else if (icon == DebugIcon::StepBack) {
        DrawTriangle({cx + 7.0f, cy - 8.0f}, {cx - 7.0f, cy},
                     {cx + 7.0f, cy + 8.0f}, RAYWHITE);
        DrawRectangle(static_cast<int>(cx - 13), static_cast<int>(cy - 8), 3, 16, RAYWHITE);
    } else {
        DrawRing({cx, cy}, 6.0f, 8.5f, 40.0f, 310.0f, 30, RAYWHITE);
        DrawTriangle({cx + 6.0f, cy - 10.0f}, {cx + 13.0f, cy - 10.0f},
                     {cx + 10.0f, cy - 3.0f}, RAYWHITE);
    }
    if (hovered) {
        const int width = measureUiText(tooltip, 12) + 16;
        const int left = static_cast<int>(cx) - width / 2;
        DrawRectangleRounded({static_cast<float>(left), bounds.y - 31.0f,
                              static_cast<float>(width), 25.0f}, 0.18f, 5,
                             Color{9, 22, 36, 245});
        drawUiText(tooltip, left + 8, static_cast<int>(bounds.y) - 26, 12, RAYWHITE);
    }
    return pressed;
}

void drawTextRow(const char* label, const std::string& value, int x, int& y,
                 Color valueColor = RAYWHITE) {
    const int labelSize = measureUiText(label, 15) > 88 ? 12 : 15;
    const int valueWidth = 1242 - (x + 94);
    const int valueSize = measureUiText(value.c_str(), 15) <= valueWidth ? 15 :
                          measureUiText(value.c_str(), 13) <= valueWidth ? 13 : 11;
    drawUiText(label, x, y, labelSize, LIGHTGRAY);
    drawUiText(value.c_str(), x + 94, y, valueSize, valueColor);

    y += 26;
}

std::string registerName(unsigned index) {
    constexpr std::array<const char*, 16> names = {
        "ZERO", "PC", "AC", "SP", "IR", "TIR", "ONE", "MONE",
        "AMASK", "SMASK", "A", "B", "C", "D", "E", "F"
    };
    return index < names.size() ? names[index] : "R" + std::to_string(index);
}

const char* aluName(unsigned operation) {
    constexpr std::array<const char*, 4> names = {"A + B", "A AND B", "A", "NOT A"};
    return names[operation & 3];
}

const char* shiftName(unsigned operation) {
    constexpr std::array<const char*, 4> names = {"sem shift", "direita 1", "esquerda 1", "inválido"};
    return names[operation & 3];
}

const char* conditionName(unsigned condition) {
    constexpr std::array<const char*, 4> names = {"MPC + 1", "ADDR se N", "ADDR se Z", "ADDR sempre"};
    return names[condition & 3];
}

const char* macroName(std::uint32_t instruction) {
    constexpr std::array<const char*, 15> formatA = {
        "LODD", "STOD", "ADDD", "SUBD", "JPOS", "JZER", "JUMP", "LOCO",
        "LODL", "STOL", "ADDL", "SUBL", "JNEG", "JNZE", "CALL"
    };
    constexpr std::array<const char*, 8> formatB = {
        "PSHI", "POPI", "PUSH", "POP", "RETN", "SWAP", "INSP", "DESP"
    };
    const unsigned opcode = instruction >> 28;
    if (opcode < formatA.size())
        return formatA[opcode];
    const unsigned subopcode = (instruction >> 24) & 0xF;
    return subopcode % 2 == 0 ? formatB[subopcode / 2] : "inválida";
}

struct RamChange {
    std::uint32_t address = 0;
    Word newValue = 0;
    bool restored = false;
    float remaining = 0.0f;
};

Color ramChangeColor(const RamChange& change) {
    return change.restored ? SKYBLUE : change.newValue == 0 ? ORANGE : GREEN;
}

constexpr Rectangle stackPanelBounds{16.0f, 236.0f, 220.0f, 420.0f};
constexpr Rectangle stackScrollTrack{218.0f, 307.0f, 8.0f, 290.0f};
constexpr std::size_t stackVisibleCells = 10;
constexpr std::size_t stackCellCount = mic1::constants::initialStackPointer;
constexpr std::size_t stackMaxScroll = stackCellCount - stackVisibleCells;
constexpr Rectangle recentOutputBounds{1036.0f, 487.0f, 205.0f, 151.0f};
constexpr std::size_t recentOutputVisibleRows = 7;

std::size_t recentOutputMaxScroll(std::size_t count) {
    return count > recentOutputVisibleRows ? count - recentOutputVisibleRows : 0;
}

void drawRecentOutputs(const std::vector<std::string>& lines, std::size_t scroll) {
    DrawRectangleRounded(recentOutputBounds, 0.08f, 4, Color{13, 20, 29, 255});
    const std::size_t maxScroll = recentOutputMaxScroll(lines.size());
    const std::size_t first = maxScroll - std::min(scroll, maxScroll);
    BeginScissorMode(static_cast<int>(recentOutputBounds.x + 7),
                     static_cast<int>(recentOutputBounds.y + 6),
                     static_cast<int>(recentOutputBounds.width - 23),
                     static_cast<int>(recentOutputBounds.height - 12));
    if (lines.empty())
        drawUiText("Nenhuma saída ainda.", static_cast<int>(recentOutputBounds.x + 9),
                   static_cast<int>(recentOutputBounds.y + 10), 12, LIGHTGRAY);
    for (std::size_t row = 0; row < recentOutputVisibleRows && first + row < lines.size(); ++row)
        drawUiText(lines[first + row].c_str(), static_cast<int>(recentOutputBounds.x + 9),
                   static_cast<int>(recentOutputBounds.y + 10 + row * 19), 12, RAYWHITE);
    EndScissorMode();

    const Rectangle track{recentOutputBounds.x + recentOutputBounds.width - 10,
                          recentOutputBounds.y + 6, 5, recentOutputBounds.height - 12};
    DrawRectangleRounded(track, 0.5f, 4, Color{44, 54, 67, 255});
    const float thumbHeight = maxScroll == 0 ? track.height :
        std::max(24.0f, track.height * static_cast<float>(recentOutputVisibleRows) /
                           static_cast<float>(lines.size()));
    const float thumbY = track.y + (track.height - thumbHeight) *
        (maxScroll == 0 ? 0.0f : static_cast<float>(first) / static_cast<float>(maxScroll));
    DrawRectangleRounded({track.x, thumbY, track.width, thumbHeight}, 0.5f, 4, SKYBLUE);
}

void drawStackPanel(const RAM* memory, const CPURegisters* cpu, std::size_t scroll,
                    const RamChange& change, float elapsed) {
    roundedPanel(stackPanelBounds, Color{22, 28, 36, 255}, "Memória Principal");

    const std::uint32_t sp = cpu ? cpu->bank.read(RegisterBank::SP)
                                 : mic1::constants::initialStackPointer;
    const bool validSp = sp <= mic1::constants::initialStackPointer;
    const std::size_t depth = validSp ? mic1::constants::initialStackPointer - sp : 0;

    drawUiText(("SP: " + hex32(sp)).c_str(), 30, 276, 12, validSp ? SKYBLUE : ORANGE);
    drawUiText((std::to_string(depth) + " células ativas").c_str(), 30, 292, 11, LIGHTGRAY);

    for (std::size_t i = 0; i < stackVisibleCells; ++i) {
        const std::size_t address = stackCellCount - 1 - scroll - i;
        const int y = 310 + static_cast<int>(i) * 29;

        std::ostringstream addressText;
        addressText << "0x" << std::uppercase << std::hex << std::setw(3)
                    << std::setfill('0') << address;

        const bool active = validSp && address >= sp;
        const bool top = active && address == sp;
        const bool changed = change.remaining > 0.0f && address == change.address;
        const Color changeColor = ramChangeColor(change);

        drawUiText(addressText.str().c_str(), 30, y + 6, 12,
                   changed ? changeColor : top ? GOLD : active ? SKYBLUE : LIGHTGRAY);

        const Rectangle cell{78.0f, static_cast<float>(y), 134.0f, 26.0f};

        DrawRectangleRounded(cell, 0.15f, 4,
                             changed ? Fade(changeColor, 0.30f + 0.16f *
                                        (1.0f + std::sin(elapsed * 18.0f)) * 0.5f) :
                             top ? Color{92, 70, 37, 255} : active ? Color{45, 84, 113, 255}
                                                                    : Color{36, 44, 55, 255});
        DrawRectangleRoundedLinesEx(cell, 0.15f, 4, changed ? 2.5f : 1.0f,
                                    changed ? changeColor : top ? GOLD :
                                    active ? SKYBLUE : Fade(RAYWHITE, 0.18f));

        const std::string value = memory ? hex32(memory->read(address)) : "--------";

        drawUiText(value.c_str(), 84, y + 6, 12,
                   changed || active ? RAYWHITE : LIGHTGRAY);
    }

    DrawRectangleRounded(stackScrollTrack, 0.5f, 4, Color{42, 50, 62, 255});
    constexpr float thumbHeight = 24.0f;
    const float thumbTravel = stackScrollTrack.height - thumbHeight;
    const float thumbY = stackScrollTrack.y + thumbTravel *
                         (static_cast<float>(scroll) / static_cast<float>(stackMaxScroll));
    DrawRectangleRounded({stackScrollTrack.x, thumbY, stackScrollTrack.width, thumbHeight},
                         0.5f, 4, SKYBLUE);
    if (change.remaining > 0.0f) {
        std::ostringstream notice;
        notice << "0x" << std::uppercase << std::hex << std::setw(3)
               << std::setfill('0') << change.address << " "
               << (change.restored ? "restaurada" : change.newValue == 0 ? "limpa" : "gravada");
        drawUiText(notice.str().c_str(), 30, 608, 11, ramChangeColor(change));
    } else {
        drawUiText("Azul: ativa | Dourado: topo", 30, 608, 10, LIGHTGRAY);
    }
    drawUiText("Roda ou arraste a barra", 30, 627, 11, LIGHTGRAY);
}

void drawStatusDot(int x, int y, bool active, Color activeColor) {
    DrawCircle(x, y, 6, active ? activeColor : Color{64, 70, 78, 255});

    if (active)
        DrawCircleLines(x, y, 9, Fade(activeColor, 0.55f));
}

void drawValueLabel(Camera3D camera, Vector3 worldPosition, const char* label,
                    const char* value, bool active, int index) {
    Vector2 position = GetWorldToScreen(worldPosition, camera);
    position.y -= 48.0f + static_cast<float>((index & 1) * 8);

    const int width = measureUiText(value, 13) + 18;
    const Rectangle panel = {
        position.x - width * 0.5f,
        position.y,
        static_cast<float>(width),
        42.0f
    };

    if (panel.x < 0 || panel.y < 0)
        return;

    DrawRectangleRounded(panel, 0.18f, 5,
                         Fade(Color{25, 31, 39, 255}, 0.92f));
    drawUiText(label, static_cast<int>(panel.x) + 9, static_cast<int>(panel.y) + 5, 11, LIGHTGRAY);
    drawUiText(value, static_cast<int>(panel.x) + 9, static_cast<int>(panel.y) + 21, 13, RAYWHITE);
}

void drawWire(Vector3 start, Vector3 end, Color color, float thickness = 0.035f) {
    DrawCylinderEx(start, end, thickness, thickness, 8, Fade(color, 0.8f));
}

void drawWireRoute(const std::array<Vector3, 4>& points, std::size_t count,
                   Color color, float elapsed, bool energized) {
    for (std::size_t i = 1; i < count; ++i)
        drawWire(points[i - 1], points[i], color, 0.055f);

    if (!energized)
        return;

    float routeLength = 0.0f;
    for (std::size_t i = 1; i < count; ++i)
        routeLength += Vector3Distance(points[i - 1], points[i]);

    for (int marker = 0; marker < 3; ++marker) {
        float progress = std::fmod(elapsed * 0.45f + marker / 3.0f, 1.0f);
        float remaining = progress * routeLength;
        Vector3 markerPosition = points[count - 1];

        for (std::size_t i = 1; i < count; ++i) {
            const float segmentLength = Vector3Distance(points[i - 1], points[i]);
            if (remaining <= segmentLength) {
                markerPosition = Vector3Lerp(points[i - 1], points[i],
                                             segmentLength > 0.0f ? remaining / segmentLength : 0.0f);
                break;
            }
            remaining -= segmentLength;
        }

        DrawSphere(markerPosition, 0.10f, GOLD);
        DrawSphere(markerPosition, 0.20f, Fade(ORANGE, 0.25f));
    }
}

std::string layoutExport(const std::vector<GuiComponent>& components,
                         const std::vector<GuiRoute>& routes) {
    std::ostringstream output;
    output << "MIC1_LAYOUT_V1\n";
    output << "COMPONENTS\n";

    for (const GuiComponent& component : components) {
        output << component.name << "|"
               << component.position.x << "|" << component.position.y << "|" << component.position.z << "|"
               << component.size.x << "|" << component.size.y << "|" << component.size.z << "\n";
    }

    output << "WIRES\n";

    for (std::size_t routeIndex = 0; routeIndex < routes.size(); ++routeIndex) {
        const GuiRoute& route = routes[routeIndex];
        output << "route " << routeIndex << "\n";

        for (std::size_t pointIndex = 0; pointIndex < route.count; ++pointIndex) {
            const Vector3& point = route.points[pointIndex];
            output << "  " << pointIndex << "|" << point.x << "|" << point.y << "|" << point.z << "\n";
        }
    }

    return output.str();
}

}  // namespace

int runGUI(const std::string& initialProgram) {
    Session session;
    std::string message;
    std::string pathInput;
    std::string selectedComponent;
    bool editingPath = false;
    GuiTab tab = GuiTab::Components;
    std::string listingSourcePath;
    std::vector<ListingLine> listingLines;

    const auto refreshListing = [&]() {
        if (listingSourcePath == session.path())
            return;

        listingSourcePath = session.path();
        listingLines = readListing(session.path());
    };

    if (!initialProgram.empty()) {
        try {
            session.load(initialProgram);
            session.run();
        } catch (const std::exception& error) {
            message = error.what();
        }
    }

    refreshListing();

    InitWindow(1280, 720, "Chimps");
    SetWindowMinSize(960, 600);
    EnableCursor();
    SetMouseCursor(MOUSE_CURSOR_ARROW);
    SetTargetFPS(60);

    const char* fontPaths[] = {
        "/fonts/DejaVuSans.ttf",
        "fonts/DejaVuSans.ttf",
        "../fonts/DejaVuSans.ttf"
    };

    for (const char* path : fontPaths) {
        if (!FileExists(path))
            continue;

        std::array<int, 224> codepoints{};
        for (std::size_t i = 0; i < codepoints.size(); ++i)
            codepoints[i] = static_cast<int>(i) + 32;

        uiFont = LoadFontEx(path, 40, codepoints.data(), static_cast<int>(codepoints.size()));
        uiFontLoaded = uiFont.texture.id != 0;

        if (uiFontLoaded) {
            SetTextureFilter(uiFont.texture, TEXTURE_FILTER_BILINEAR);
            break;
        }
    }
    InitAudioDevice();

    GuiSfx sfx;
    const auto sfxPath = [](const char* name) {
        const std::string dockerPath = std::string("/sfxs/") + name;

        if (FileExists(dockerPath.c_str()))
            return dockerPath;

        return std::string("sfxs/") + name;
    };

    sfx.click = LoadSound(sfxPath("click.wav").c_str());
    sfx.hover = LoadSound(sfxPath("hover.wav").c_str());
    sfx.step = LoadSound(sfxPath("step.wav").c_str());
    sfx.halt = LoadSound(sfxPath("halt.wav").c_str());
    sfx.problem = LoadSound(sfxPath("problem.wav").c_str());
    sfx.cacheHit = LoadSound(sfxPath("cache_hit.wav").c_str());
    sfx.cacheMiss = LoadSound(sfxPath("cache_miss.wav").c_str());

    const auto playSfx = [](Sound sound) {
        if (sound.frameCount > 0)
            PlaySound(sound);
    };

    const auto togglePause = [&session]() {
        if (session.isRunning())
            session.pause();
        else
            session.run();
    };

    Camera3D camera{};
    camera.target = {0.0f, 0.2f, 0.0f};
    camera.up = {0.0f, 1.0f, 0.0f};
    camera.fovy = 42.0f;
    camera.projection = CAMERA_PERSPECTIVE;
    bool inspectingComponent = false;
    Vector3 inspectTarget = camera.target;

    Model boardModel{};
    bool boardModelLoaded = false;
    Vector3 boardModelPosition = {};
    float boardModelScale = 1.0f;
    std::vector<ModelComponent> modelComponents;

    const char* boardModelPath = FileExists("/models/mic1board.glb")
        ? "/models/mic1board.glb"
        : "models/mic1board.glb";

    boardModel = LoadModel(boardModelPath);

    if (boardModel.meshCount > 0) {
        boardModelLoaded = true;

        const BoundingBox modelBounds = GetModelBoundingBox(boardModel);
        const Vector3 modelSize = Vector3Subtract(modelBounds.max, modelBounds.min);
        const Vector3 modelCenter = Vector3Scale(Vector3Add(modelBounds.min, modelBounds.max), 0.5f);
        const float largestSize = std::max(modelSize.x, std::max(modelSize.y, modelSize.z));

        if (largestSize > 0.0001f)
            boardModelScale = 12.0f / largestSize;

        boardModelPosition = Vector3Scale(modelCenter, -boardModelScale);
        boardModelPosition.y += 0.35f;

        std::cout << "Loaded GLB: " << boardModelPath << ", meshes=" << boardModel.meshCount
                  << ", scale=" << boardModelScale << std::endl;

        const auto modelPoint = [&](float x, float y, float z) {
            return Vector3{
                x * boardModelScale + boardModelPosition.x,
                y * boardModelScale + boardModelPosition.y,
                z * boardModelScale + boardModelPosition.z
            };
        };

        const auto addComponent = [&](const char* name, float x, float y, float z,
                                      Vector3 size = {0.75f, 0.55f, 0.75f}) {
            modelComponents.push_back({name, modelPoint(x, y, z), size});
        };

        addComponent("REGISTER_BANK", -0.09078f, 0.00322f, -0.25607f);
        addComponent("PC", -0.09214f, -0.00009f, -0.20958f);
        addComponent("AC", -0.04643f, -0.00009f, -0.20958f);
        addComponent("SP", -0.00071f, -0.00009f, -0.20958f);
        addComponent("IR", 0.04501f, -0.00009f, -0.20958f);
        addComponent("TIR", -0.09214f, -0.00009f, -0.08125f);
        addComponent("ZERO", -0.04643f, -0.00009f, -0.08125f);
        addComponent("ONE", -0.00071f, -0.00009f, -0.08125f);
        addComponent("MONE", 0.04501f, -0.00009f, -0.08125f);
        addComponent("AMASK", -0.09214f, -0.00009f, 0.04709f);
        addComponent("SMASK", -0.04643f, -0.00009f, 0.04709f);
        addComponent("REGISTER_A", -0.00071f, -0.00009f, 0.04709f);
        addComponent("REGISTER_B", 0.04501f, -0.00009f, 0.04709f);
        addComponent("REGISTER_C", -0.09214f, -0.00009f, 0.17542f);
        addComponent("REGISTER_D", -0.04643f, -0.00009f, 0.17542f);
        addComponent("REGISTER_E", -0.00071f, -0.00009f, 0.17542f);
        addComponent("REGISTER_F", 0.04501f, -0.00009f, 0.17542f);
        addComponent("A_DECODER", -0.03627f, 0.00167f, -0.59602f);
        addComponent("B_DECODER", -0.03627f, 0.00167f, -0.66797f);
        addComponent("C_DECODER", -0.03450f, 0.00166f, -0.74107f);
        addComponent("A_LATCH", 0.04500f, 0.00283f, 0.07984f);
        addComponent("B_LATCH", 0.18602f, 0.00274f, 0.07912f);
        addComponent("AMUX", 0.02599f, 0.00306f, 0.30806f);
        addComponent("ALU", 0.12066f, 0.00322f, 0.47638f);
        addComponent("SHIFTER", 0.12059f, 0.00404f, 0.61321f);
        addComponent("MAR", -0.10102f, 0.00166f, 0.15970f);
        addComponent("MBR", -0.11032f, 0.00272f, 0.34622f);
        addComponent("RAM", -0.75411f, 0.02782f, 0.24327f);
        addComponent("CACHE", -0.19781f, 0.00196f, 0.26469f);
        addComponent("CLOCK", 0.34073f, 0.01130f, -0.21741f);
        addComponent("MMUX", 0.46356f, 0.00291f, -0.08844f);
        addComponent("MPC", 0.46353f, 0.00454f, -0.04069f);
        addComponent("MIR", 0.34985f, 0.00279f, 0.07990f);
        addComponent("INCREMENTER", 0.34151f, 0.00267f, -0.08453f);
        addComponent("CONTROL_STORE", 0.37086f, 0.00293f, 0.02264f);

        struct ComponentMeshInfo {
            const char* name;
            int meshIndex;
            float x;
            float y;
            float z;
        };

        const ComponentMeshInfo componentMeshes[] = {
            {"REGISTER_BANK", 63, -0.09078f, 0.00322f, -0.25607f},
            {"A_DECODER", 64, -0.03627f, 0.00167f, -0.59602f},
            {"B_DECODER", 72, -0.03627f, 0.00167f, -0.66797f},
            {"C_DECODER", 71, -0.03450f, 0.00166f, -0.74107f},
            {"A_LATCH", 1, 0.04500f, 0.00283f, 0.07984f},
            {"B_LATCH", 2, 0.18602f, 0.00274f, 0.07912f},
            {"AMUX", 3, 0.02599f, 0.00306f, 0.30806f},
            {"MAR", 4, -0.10102f, 0.00166f, 0.15970f},
            {"ALU", 5, 0.12066f, 0.00322f, 0.47638f},
            {"SHIFTER", 6, 0.12059f, 0.00404f, 0.61321f},
            {"RAM", 7, -0.75411f, 0.02782f, 0.24327f},
            {"MBR", 8, -0.11032f, 0.00272f, 0.34622f},
            {"CLOCK", 9, 0.39265f, 0.01223f, -0.36357f},
            {"MMUX", 10, 0.46356f, 0.00291f, -0.08844f},
            {"MPC", 11, 0.46353f, 0.00454f, -0.04069f},
            {"MIR", 12, 0.34985f, 0.00279f, 0.07990f},
            {"INCREMENTER", 13, 0.34151f, 0.00267f, -0.08453f},
            {"CONTROL_STORE", 14, 0.37086f, 0.00293f, 0.02264f},
            {"CACHE", 15, -0.19781f, 0.00196f, 0.26469f},
            {"PC", 55, -0.09214f, -0.00009f, -0.20958f},
            {"AC", 45, -0.04643f, -0.00009f, -0.20958f},
            {"SP", 60, -0.00071f, -0.00009f, -0.20958f},
            {"IR", 54, 0.04501f, -0.00009f, -0.20958f},
            {"TIR", 61, -0.09214f, -0.00009f, -0.08125f},
            {"ZERO", 43, -0.04643f, -0.00009f, -0.08125f},
            {"ONE", 41, -0.00071f, -0.00009f, -0.08125f},
            {"MONE", 42, 0.04501f, -0.00009f, -0.08125f},
            {"AMASK", 46, -0.09214f, -0.00009f, 0.04709f},
            {"SMASK", 59, -0.04643f, -0.00009f, 0.04709f},
            {"REGISTER_A", 44, -0.00071f, -0.00009f, 0.04709f},
            {"REGISTER_B", 48, 0.04501f, -0.00009f, 0.04709f},
            {"REGISTER_C", 49, -0.09214f, -0.00009f, 0.17542f},
            {"REGISTER_D", 51, -0.04643f, -0.00009f, 0.17542f},
            {"REGISTER_E", 52, -0.00071f, -0.00009f, 0.17542f},
            {"REGISTER_F", 53, 0.04501f, -0.00009f, 0.17542f}
        };

        for (ModelComponent& component : modelComponents) {
            for (const ComponentMeshInfo& info : componentMeshes) {
                if (component.name != info.name)
                    continue;

                if (info.meshIndex >= 0 && info.meshIndex < boardModel.meshCount) {
                    const BoundingBox bounds = GetMeshBoundingBox(boardModel.meshes[info.meshIndex]);
                    const Vector3 center = Vector3Scale(Vector3Add(bounds.min, bounds.max), 0.5f);
                    const Vector3 size = Vector3Subtract(bounds.max, bounds.min);

                    component.position = modelPoint(info.x + center.x,
                                                    info.y + center.y,
                                                    info.z + center.z);
                    component.size = Vector3Scale(size, boardModelScale);

                    if (component.size.x < 0.25f) component.size.x = 0.25f;
                    if (component.size.y < 0.25f) component.size.y = 0.25f;
                    if (component.size.z < 0.25f) component.size.z = 0.25f;
                }

                break;
            }
        }

        modelComponents.clear();

        std::vector<GlbHitbox> hitboxes;

        if (readGlbHitboxes(boardModelPath, hitboxes)) {
            for (const GlbHitbox& hitbox : hitboxes) {
                if (hitbox.name.empty() ||
                    hitbox.meshIndex < 0 ||
                    hitbox.meshIndex >= boardModel.meshCount)
                    continue;

                Vector3 size = {
                    hitbox.scale.x * 2.0f * boardModelScale,
                    hitbox.scale.y * 2.0f * boardModelScale,
                    hitbox.scale.z * 2.0f * boardModelScale
                };

                const bool registerAxis =
                    hitbox.name == "PC" || hitbox.name == "AC" || hitbox.name == "SP" ||
                    hitbox.name == "IR" || hitbox.name == "TIR" || hitbox.name == "ZERO" ||
                    hitbox.name == "ONE" || hitbox.name == "MONE" || hitbox.name == "AMASK" ||
                    hitbox.name == "SMASK" || hitbox.name == "A" || hitbox.name == "B" ||
                    hitbox.name == "C" || hitbox.name == "D" || hitbox.name == "E" ||
                    hitbox.name == "F" || hitbox.name == "REGISTER_A" ||
                    hitbox.name == "REGISTER_B" || hitbox.name == "REGISTER_C" ||
                    hitbox.name == "REGISTER_D" || hitbox.name == "REGISTER_E" ||
                    hitbox.name == "REGISTER_F";

                if (!registerAxis)
                    std::swap(size.x, size.z);

                const Vector3 position = modelPoint(hitbox.position.x,
                                                    hitbox.position.y,
                                                    hitbox.position.z);

                modelComponents.push_back({hitbox.name, position, size, hitbox.meshIndex});
            }

            std::cout << "Loaded GLB hitboxes=" << hitboxes.size() << std::endl;
        }

        std::cout << "GLB materials=" << boardModel.materialCount << std::endl;

        for (int materialIndex = 0; materialIndex < boardModel.materialCount; ++materialIndex) {
            const Texture2D& albedo = boardModel.materials[materialIndex].maps[MATERIAL_MAP_ALBEDO].texture;
            std::cout << "  material " << materialIndex << ": textureId=" << albedo.id
                      << ", size=" << albedo.width << "x" << albedo.height << std::endl;
        }
    } else {
        boardModel = {};
        message = "GLB not found; using fallback schematic";
    }

    float yaw = 0.55f;
    float pitch = 0.48f;
    float distance = 20.0f;

    const auto resetCamera = [&]() {
        yaw = 0.55f;
        pitch = 0.48f;
        distance = 20.0f;
        inspectTarget = {0.0f, 0.2f, 0.0f};
        inspectingComponent = false;
        selectedComponent.clear();
    };

    std::vector<GuiComponent> components = {{
        {"PC",    {4.7f, 0.65f, -4.2f}, {1.25f, 0.85f, 0.72f}, Color{65, 118, 170, 255}},
        {"AC",    {4.7f, 0.65f, -3.30f}, {1.25f, 0.85f, 0.72f}, Color{65, 118, 170, 255}},
        {"SP",    {4.7f, 0.65f, -2.40f}, {1.25f, 0.85f, 0.72f}, Color{65, 118, 170, 255}},
        {"IR",    {4.7f, 0.65f, -1.50f}, {1.25f, 0.85f, 0.72f}, Color{65, 118, 170, 255}},
        {"TIR",   {4.7f, 0.65f, -0.60f}, {1.25f, 0.85f, 0.72f}, Color{65, 118, 170, 255}},
        {"0",     {4.7f, 0.65f, 0.30f}, {1.25f, 0.85f, 0.72f}, Color{65, 118, 170, 255}},
        {"1",     {4.7f, 0.65f, 1.20f}, {1.25f, 0.85f, 0.72f}, Color{65, 118, 170, 255}},
        {"-1",    {4.7f, 0.65f, 2.10f}, {1.25f, 0.85f, 0.72f}, Color{65, 118, 170, 255}},
        {"AMASK", {4.7f, 0.65f, 3.00f}, {1.25f, 0.85f, 0.72f}, Color{65, 118, 170, 255}},
        {"SMASK", {4.7f, 0.65f, 3.90f}, {1.25f, 0.85f, 0.72f}, Color{65, 118, 170, 255}},
        {"A Latch", {-1.10f, 0.65f, -3.15f}, {1.85f, 0.95f, 0.95f}, Color{66, 143, 174, 255}},
        {"B Latch", {1.10f, 0.65f, -3.15f}, {1.85f, 0.95f, 0.95f}, Color{66, 143, 174, 255}},
        {"AMUX",    {-2.25f, 0.65f, -1.55f}, {1.45f, 0.95f, 0.95f}, Color{90, 154, 95, 255}},
        {"ALU",     {0.00f, 0.72f, -0.35f}, {3.10f, 1.15f, 1.65f}, Color{205, 146, 61, 255}},
        {"Shifter", {0.00f, 0.65f, 1.55f}, {2.15f, 0.95f, 1.05f}, Color{198, 111, 89, 255}},
        {"MAR", {-5.40f, 0.65f, 0.75f}, {1.65f, 0.95f, 0.95f}, Color{96, 126, 184, 255}},
        {"MBR", {-5.40f, 0.65f, 2.35f}, {1.65f, 0.95f, 0.95f}, Color{96, 126, 184, 255}},
        {"Control Store", {2.35f, 0.85f, -4.35f}, {2.60f, 1.15f, 1.05f}, Color{138, 105, 172, 255}},
        {"MPC",           {2.35f, 0.65f, -3.05f}, {1.50f, 0.95f, 0.95f}, Color{112, 121, 177, 255}},
        {"MIR",           {2.35f, 0.65f, -1.65f}, {2.15f, 0.95f, 0.95f}, Color{118, 134, 157, 255}},
        {"RAM",           {-5.40f, 0.65f, -2.35f}, {1.95f, 4.10f, 1.95f}, Color{84, 137, 133, 255}}
    }};

    std::vector<GuiRoute> routes;
    const auto makeRoute = [](std::initializer_list<Vector3> points, Color color) {
        GuiRoute route;
        route.count = points.size();
        route.color = color;
        std::size_t index = 0;

        for (const Vector3& point : points)
            route.points[index++] = point;

        return route;
    };

    routes.push_back(makeRoute({{-1.10f, 0.22f, -2.65f}, {-1.10f, 0.22f, -2.10f},
                                {-1.55f, 0.22f, -1.55f}}, SKYBLUE));
    routes.push_back(makeRoute({{1.10f, 0.22f, -2.65f}, {1.10f, 0.22f, -2.10f},
                                {-0.55f, 0.22f, -1.55f}}, SKYBLUE));
    routes.push_back(makeRoute({{-2.25f, 0.22f, -1.05f}, {-2.25f, 0.22f, -0.35f},
                                {-1.55f, 0.22f, -0.35f}}, GREEN));
    routes.push_back(makeRoute({{-1.55f, 0.22f, -0.35f}, {-0.75f, 0.22f, -0.35f},
                                {-0.75f, 0.22f, 0.45f}}, GOLD));
    routes.push_back(makeRoute({{0.75f, 0.22f, 0.45f}, {0.75f, 0.22f, 0.95f},
                                {0.75f, 0.22f, 1.55f}}, ORANGE));
    routes.push_back(makeRoute({{-5.40f, 0.22f, -0.20f}, {-5.40f, 0.22f, 0.75f},
                                {-4.55f, 0.22f, 0.75f}}, PURPLE));
    routes.push_back(makeRoute({{-5.40f, 0.22f, 1.55f}, {-5.40f, 0.22f, 2.35f},
                                {-4.55f, 0.22f, 2.35f}}, PURPLE));
    routes.push_back(makeRoute({{-4.35f, 0.22f, -1.55f}, {-3.45f, 0.22f, -1.55f},
                                {-3.45f, 0.22f, 0.75f}}, GREEN));
    routes.push_back(makeRoute({{2.35f, 0.22f, -3.55f}, {3.55f, 0.22f, -3.55f},
                                {3.55f, 0.22f, -0.35f}}, GRAY));

    bool layoutEditing = false;
    int selectedRoute = -1;
    int selectedRoutePoint = -1;

    std::uint64_t lastCycleCount = 0;
    float activityPulse = 0.0f;
    float elapsed = 0.0f;
    std::string previousHover;
    float hoverCooldown = 0.0f;
    bool previousHalt = false;
    float stepCooldown = 0.0f;

    constexpr float baseMicrocyclesPerSecond = 10.0f;
    constexpr std::array<float, 5> playbackSpeeds = {1.0f, 0.5f, 0.25f, 1.5f, 2.0f};
    constexpr std::array<const char*, 5> playbackLabels = {"1x", "0.5x", "0.25x", "1.5x", "2x"};
    std::size_t selectedSpeed = 0;
    float stepAccumulator = 0.0f;
    bool speedDialogOpen = false;

    std::uint64_t lastCacheHits = 0;
    std::uint64_t lastCacheMisses = 0;
    std::size_t stackScroll = 0;
    bool draggingStackScroll = false;
    std::size_t recentOutputScroll = 0;
    std::size_t knownOutputCount = session.outputs().size();
    bool draggingRecentOutputScroll = false;
    RamChange ramChange;

    auto markRamChange = [&](std::uint32_t address, Word value, bool restored) {
        ramChange = {address, value, restored, 1.6f};
        if (address < stackCellCount) {
            const std::size_t row = stackCellCount - 1 - address;
            if (row < stackScroll || row >= stackScroll + stackVisibleCells)
                stackScroll = std::min(row > 4 ? row - 4 : 0, stackMaxScroll);
        }
    };
    auto stepSession = [&]() {
        if (!session.step())
            return false;
        const auto& event = session.lastEvent();
        if (event.memoryWrite)
            markRamChange(event.memoryWriteAddress, event.memoryNewValue, false);
        return true;
    };
    auto backSession = [&]() {
        const MicroCycleEvent undone = session.lastEvent();
        if (!session.back())
            return false;
        if (undone.memoryWrite)
            markRamChange(undone.memoryWriteAddress,
                          session.memory()->read(undone.memoryWriteAddress), true);
        return true;
    };

    while (!WindowShouldClose()) {
        const float frameTime = GetFrameTime();
        refreshListing();
        elapsed += frameTime;

        activityPulse = std::max(0.0f, activityPulse - frameTime);
        ramChange.remaining = std::max(0.0f, ramChange.remaining - frameTime);
        hoverCooldown = std::max(0.0f, hoverCooldown - frameTime);
        stepCooldown = std::max(0.0f, stepCooldown - frameTime);

        try {
            if (speedDialogOpen) {
                if (IsKeyPressed(KEY_ESCAPE))
                    speedDialogOpen = false;
            } else if (editingPath) {
                for (int code; (code = GetCharPressed()) != 0;) {
                    if (code >= 32 && code <= 126 && pathInput.size() < 512)
                        pathInput.push_back(static_cast<char>(code));
                }
                if (IsKeyPressed(KEY_BACKSPACE) && !pathInput.empty())
                    pathInput.pop_back();
                if (IsKeyPressed(KEY_ESCAPE))
                    editingPath = false;
                if (IsKeyPressed(KEY_ENTER) && !pathInput.empty()) {
                    session.load(pathInput);
                    ramChange = {};
                    stackScroll = 0;
                    editingPath = false;
                    message = "Programa carregado";
                    playSfx(sfx.click);
                    refreshListing();
                }
            } else {
                if (IsKeyPressed(KEY_R)) {
                    session.run();
                    playSfx(sfx.click);
                }
                if (IsKeyPressed(KEY_P)) {
                    togglePause();
                    playSfx(sfx.click);
                }
                if (IsKeyPressed(KEY_B)) {
                    if (!backSession())
                        message = "Sem subciclo anterior";
                    playSfx(sfx.click);
                }
                if (IsKeyPressed(KEY_X)) {
                    session.reset();
                    ramChange = {};
                    stackScroll = 0;
                    playSfx(sfx.click);
                }
                if (IsKeyPressed(KEY_HOME))
                    resetCamera();
                if (IsKeyPressed(KEY_SPACE)) {
                    session.pause();
                    stepSession();
                    playSfx(sfx.step);
                }
                if (IsKeyPressed(KEY_L)) {
                    session.pause();
                    pathInput = session.path();
                    editingPath = true;
                }
            }
        } catch (const std::exception& error) {
            session.pause();
            message = error.what();
            playSfx(sfx.problem);
        }

        if (session.isRunning() && !editingPath && !speedDialogOpen) {
            // Acumula tempo real para manter a taxa de microciclos independente do FPS
            stepAccumulator += std::min(frameTime, 0.25f) *
                               baseMicrocyclesPerSecond * playbackSpeeds[selectedSpeed];

            for (int i = 0; i < 20 && stepAccumulator >= 1.0f && session.isRunning(); ++i) {
                try {
                    stepSession();
                    stepAccumulator -= 1.0f;
                } catch (const std::exception& error) {
                    session.pause();
                    message = error.what();
                    playSfx(sfx.problem);
                }
            }
        }

        if (!session.isRunning() || editingPath || speedDialogOpen)
            stepAccumulator = 0.0f;

        if (session.cycles() != lastCycleCount) {
            lastCycleCount = session.cycles();
            activityPulse = 0.28f;

            if (stepCooldown <= 0.0f) {
                playSfx(sfx.step);
                stepCooldown = 0.06f;
            }
        }

        const auto cacheStats = session.instructionCacheStats();
        const bool cacheHitThisFrame = cacheStats.hits > lastCacheHits;
        const bool cacheMissThisFrame = cacheStats.misses > lastCacheMisses;

        if (cacheMissThisFrame)
            playSfx(sfx.cacheMiss);
        else if (cacheHitThisFrame)
            playSfx(sfx.cacheHit);

        lastCacheHits = cacheStats.hits;
        lastCacheMisses = cacheStats.misses;

        const char* lastCacheAccess = "nenhum";

        switch (session.lastInstructionCacheAccess()) {
            case InstructionCache::Access::Hit: lastCacheAccess = "acerto"; break;
            case InstructionCache::Access::Miss: lastCacheAccess = "falha"; break;
            case InstructionCache::Access::Bypass: lastCacheAccess = "sem cache"; break;
            case InstructionCache::Access::None: break;
        }

        if (session.isHalted() && !previousHalt)
            playSfx(sfx.halt);

        previousHalt = session.isHalted();

        const std::size_t outputCount = session.outputs().size();
        if (outputCount > knownOutputCount && recentOutputScroll > 0)
            recentOutputScroll += outputCount - knownOutputCount;
        recentOutputScroll = std::min(recentOutputScroll, recentOutputMaxScroll(outputCount));
        knownOutputCount = outputCount;

        if (!speedDialogOpen && (IsKeyDown(KEY_LEFT_ALT) || IsKeyDown(KEY_RIGHT_ALT))) {
            if (IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
                Vector2 delta = GetMouseDelta();
                yaw -= delta.x * 0.006f;
                pitch = std::clamp(pitch + delta.y * 0.006f, 0.10f, 1.25f);
            }
        } else if (!speedDialogOpen && IsMouseButtonDown(MOUSE_BUTTON_MIDDLE)) {
            Vector2 delta = GetMouseDelta();
            yaw -= delta.x * 0.006f;
            pitch = std::clamp(pitch + delta.y * 0.006f, 0.10f, 1.25f);
        }

        if (!speedDialogOpen) {
            const float wheel = GetMouseWheelMove();
            const Vector2 mouse = GetMousePosition();
            if (tab == GuiTab::Execution && pointInRectangle(mouse, recentOutputBounds)) {
                const auto amount = 3 * static_cast<std::size_t>(std::ceil(std::abs(wheel)));
                if (wheel > 0.0f)
                    recentOutputScroll = std::min(recentOutputScroll + amount,
                                                  recentOutputMaxScroll(outputCount));
                else if (wheel < 0.0f)
                    recentOutputScroll = amount > recentOutputScroll ? 0 : recentOutputScroll - amount;
            } else if (pointInRectangle(mouse, stackPanelBounds)) {
                if (wheel < 0.0f)
                    stackScroll = std::min(stackScroll + 3 * static_cast<std::size_t>(std::ceil(-wheel)),
                                           stackMaxScroll);
                else if (wheel > 0.0f) {
                    const auto amount = 3 * static_cast<std::size_t>(std::ceil(wheel));
                    stackScroll = amount > stackScroll ? 0 : stackScroll - amount;
                }
            } else {
                distance -= wheel * 1.2f;
            }

            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) &&
                pointInRectangle(mouse, stackScrollTrack))
                draggingStackScroll = true;
            if (!IsMouseButtonDown(MOUSE_BUTTON_LEFT))
                draggingStackScroll = false;
            if (draggingStackScroll) {
                constexpr float thumbHeight = 24.0f;
                const float ratio = std::clamp(
                    (mouse.y - stackScrollTrack.y - thumbHeight * 0.5f) /
                    (stackScrollTrack.height - thumbHeight), 0.0f, 1.0f);
                stackScroll = static_cast<std::size_t>(std::lround(ratio * stackMaxScroll));
            }
            const Rectangle outputTrack{recentOutputBounds.x + recentOutputBounds.width - 14,
                                        recentOutputBounds.y, 14.0f, recentOutputBounds.height};
            if (tab == GuiTab::Execution && IsMouseButtonPressed(MOUSE_BUTTON_LEFT) &&
                pointInRectangle(mouse, outputTrack))
                draggingRecentOutputScroll = true;
            if (!IsMouseButtonDown(MOUSE_BUTTON_LEFT) || tab != GuiTab::Execution)
                draggingRecentOutputScroll = false;
            if (draggingRecentOutputScroll) {
                const auto maxScroll = recentOutputMaxScroll(outputCount);
                const float ratio = std::clamp(
                    (mouse.y - recentOutputBounds.y) / recentOutputBounds.height,
                    0.0f, 1.0f);
                recentOutputScroll = maxScroll - static_cast<std::size_t>(std::lround(ratio * maxScroll));
            }
        } else {
            draggingStackScroll = false;
            draggingRecentOutputScroll = false;
        }
        distance = inspectingComponent
            ? std::clamp(distance, 5.0f, 28.0f)
            : std::clamp(distance, 9.0f, 28.0f);
        if (inspectingComponent) {
            camera.target = Vector3Lerp(camera.target, inspectTarget, std::min(1.0f, frameTime * 3.5f));
            distance = std::clamp(distance, 5.0f, 28.0f);
        }

        camera.position = {
            camera.target.x + distance * std::cos(pitch) * std::sin(yaw),
            camera.target.y + distance * std::sin(pitch),
            camera.target.z + distance * std::cos(pitch) * std::cos(yaw)
        };

        BeginDrawing();
        ClearBackground(Color{13, 17, 23, 255});

        drawUiText("CHIMPS", 24, 18, 22, RAYWHITE);
        roundedPanel({16.0f, 66.0f, 220.0f, 160.0f}, Color{22, 28, 36, 255}, "I-Cache");
        drawUiText(("Acertos: " + std::to_string(cacheStats.hits)).c_str(), 30, 112, 16, RAYWHITE);
        drawUiText(("Falhas: " + std::to_string(cacheStats.misses)).c_str(), 30, 143, 16, RAYWHITE);
        drawUiText((std::string("Último: ") + lastCacheAccess).c_str(), 30, 174, 16, LIGHTGRAY);
        drawStackPanel(session.memory(), session.cpu(), stackScroll, ramChange, elapsed);

        const char* tabNames[] = {"Componentes", "Execução", "Informações", "Depurador", "Controles"};
        for (int i = 0; i < 5; ++i) {
            Rectangle tabBounds = {
                250.0f + i * 138.0f,
                12.0f,
                128.0f,
                42.0f
            };
            if (tabButton(tabBounds, tabNames[i], tab == static_cast<GuiTab>(i)) &&
                !speedDialogOpen && !editingPath)
                tab = static_cast<GuiTab>(i);
        }
        const std::string speedButtonLabel = std::string("Velocidade: ") + playbackLabels[selectedSpeed];
        if (controlButton({1022.0f, 20.0f, 234.0f, 28.0f}, speedButtonLabel.c_str()) &&
            !speedDialogOpen && !editingPath) {
            speedDialogOpen = true;
            stepAccumulator = 0.0f;
            playSfx(sfx.click);
        }

        const Rectangle viewport = {250.0f, 66.0f, 760.0f, 590.0f};
        roundedPanel(viewport, Color{18, 23, 31, 255});

        const CPURegisters* cpu = session.cpu();
        const MicroCycleEvent& event = session.lastEvent();
        std::uint64_t mir = cpu ? cpu->mir.read() : 0;
        const std::uint8_t mirA = static_cast<std::uint8_t>((mir >> 9) & 0x1F);
        const std::uint8_t mirB = static_cast<std::uint8_t>((mir >> 14) & 0x1F);
        const std::uint8_t mirC = static_cast<std::uint8_t>((mir >> 19) & 0x1F);
        const bool mirWriteC = ((mir >> 24) & 1) != 0;
        const bool mirUseMbr = ((mir >> 35) & 1) != 0;
        const bool mirWriteMbr = ((mir >> 28) & 1) != 0;
        const bool mirWriteMar = ((mir >> 27) & 1) != 0;
        const bool mirRead = ((mir >> 26) & 1) != 0;
        const bool mirWrite = ((mir >> 25) & 1) != 0;

        BeginScissorMode(static_cast<int>(viewport.x), static_cast<int>(viewport.y),
                         static_cast<int>(viewport.width), static_cast<int>(viewport.height));
        BeginMode3D(camera);
        // The GLB board replaces the procedural grid.

        const bool energized = session.isRunning() || activityPulse > 0.0f;
        if (boardModelLoaded) {
            DrawModelEx(boardModel, boardModelPosition, {0.0f, 1.0f, 0.0f},
                        0.0f, {boardModelScale, boardModelScale, boardModelScale}, WHITE);

            for (std::size_t i = 0; i < modelComponents.size(); ++i) {
                const ModelComponent& component = modelComponents[i];
                bool active = false;

                if (activityPulse > 0.0f) {
                    active = component.name == "ALU" || component.name == "SHIFTER" ||
                             component.name == "AMUX" || component.name == "MIR" ||
                             component.name == "MPC" || component.name == "CONTROL_STORE";

                    if (component.name == "A_LATCH" && mirA != RegisterBank::ZERO)
                        active = true;
                    if (component.name == "B_LATCH" && mirB != RegisterBank::ZERO)
                        active = true;
                    if (component.name == "MAR" && mirWriteMar)
                        active = true;
                    if (component.name == "MBR" && (mirWriteMbr || mirUseMbr))
                        active = true;
                    if (component.name == "RAM" && (mirRead || mirWrite))
                        active = true;
                    if (component.name == "CACHE" && (cacheHitThisFrame || cacheMissThisFrame))
                        active = true;
                    if (mirWriteC) {
                        if (mirC == RegisterBank::PC)
                            active = component.name == "PC";
                        else if (mirC == RegisterBank::AC)
                            active = component.name == "AC";
                        else if (mirC == RegisterBank::SP)
                            active = component.name == "SP";
                        else if (mirC == RegisterBank::IR)
                            active = component.name == "IR";
                        else if (mirC == RegisterBank::TIR)
                            active = component.name == "TIR";
                        else if (mirC == RegisterBank::ZERO)
                            active = component.name == "ZERO";
                        else if (mirC == RegisterBank::ONE)
                            active = component.name == "ONE";
                        else if (mirC == RegisterBank::MONE)
                            active = component.name == "MONE";
                        else if (mirC == RegisterBank::AMASK)
                            active = component.name == "AMASK";
                        else if (mirC == RegisterBank::SMASK)
                            active = component.name == "SMASK";
                    }
                }

                if (component.name == "RAM" && ramChange.remaining > 0.0f) {
                    DrawCubeWiresV(component.position, Vector3Scale(component.size, 1.10f),
                                   ramChangeColor(ramChange));
                } else if (active) {
                    Vector3 position = component.position;
                    position.x += std::sin(elapsed * 95.0f + i) * 0.025f;
                    position.y += std::cos(elapsed * 83.0f + i) * 0.018f;
                    DrawCubeWiresV(position, Vector3Scale(component.size, 1.08f), GOLD);
                }
            }
        } else {
            for (const GuiRoute& route : routes)
                drawWireRoute(route.points, route.count, route.color, elapsed, energized);
        }

        const Vector2 mouse = GetMousePosition();
        Ray ray = GetScreenToWorldRay(mouse, camera);
        const bool mouseInViewport = !speedDialogOpen && pointInRectangle(mouse, viewport);
        std::string hoverComponent;
        float nearestDistance = 1.0e9f;

        if (boardModelLoaded && mouseInViewport) {
            for (const ModelComponent& component : modelComponents) {
                const Vector3 minimum = Vector3Subtract(component.position,
                                                         Vector3Scale(component.size, 0.5f));
                const Vector3 maximum = Vector3Add(component.position,
                                                   Vector3Scale(component.size, 0.5f));
                const RayCollision collision = GetRayCollisionBox(
                    ray, {minimum.x, minimum.y, minimum.z, maximum.x, maximum.y, maximum.z});

                if (collision.hit && collision.distance < nearestDistance) {
                    nearestDistance = collision.distance;
                    hoverComponent = component.name;
                }
            }

            if (!hoverComponent.empty() && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                selectedComponent = hoverComponent;
                inspectingComponent = true;
                playSfx(sfx.click);

                for (const ModelComponent& component : modelComponents) {
                    if (component.name == selectedComponent) {
                        inspectTarget = component.position;
                        break;
                    }
                }
                distance = std::min(distance, 5.5f);
            }

            if (hoverComponent != previousHover && !hoverComponent.empty() && hoverCooldown <= 0.0f) {
                playSfx(sfx.hover);
                hoverCooldown = 0.12f;
            }
            previousHover = hoverComponent;

            if (!hoverComponent.empty()) {
                for (const ModelComponent& component : modelComponents) {
                    if (component.name == hoverComponent) {
                        DrawCubeWiresV(component.position,
                                       Vector3Scale(component.size, 1.10f),
                                       GOLD);
                        break;
                    }
                }
            }
        }

        if (!boardModelLoaded) {
            for (std::size_t i = 0; i < components.size(); ++i) {
                GuiComponent& component = components[i];
                bool active = false;
                if (activityPulse > 0.0f) {
                    active = component.name == "ALU" || component.name == "AMUX" ||
                             component.name == "Shifter" || component.name == "MIR" ||
                             component.name == "MPC" || component.name == "Control Store";

                    const auto isSourceRegister = [&](const std::string& componentName,
                                                       const char* registerName,
                                                       std::uint8_t index) {
                        return componentName == registerName && (mirA == index || mirB == index);
                    };

                    active = active ||
                             isSourceRegister(component.name, "PC", RegisterBank::PC) ||
                             isSourceRegister(component.name, "AC", RegisterBank::AC) ||
                             isSourceRegister(component.name, "SP", RegisterBank::SP) ||
                             isSourceRegister(component.name, "IR", RegisterBank::IR) ||
                             isSourceRegister(component.name, "TIR", RegisterBank::TIR) ||
                             isSourceRegister(component.name, "0", RegisterBank::ZERO) ||
                             isSourceRegister(component.name, "1", RegisterBank::ONE) ||
                             isSourceRegister(component.name, "-1", RegisterBank::MONE) ||
                             isSourceRegister(component.name, "AMASK", RegisterBank::AMASK) ||
                             isSourceRegister(component.name, "SMASK", RegisterBank::SMASK) ||
                             (component.name == "A Latch" && mirA != RegisterBank::ZERO) ||
                             (component.name == "B Latch" && mirB != RegisterBank::ZERO);

                    if (mirUseMbr)
                        active = active || component.name == "MBR";
                    if (mirWriteMbr)
                        active = active || component.name == "MBR";
                    if (mirWriteMar)
                        active = active || component.name == "MAR";
                    if (mirRead)
                        active = active || component.name == "MAR" || component.name == "RAM";
                    if (mirWrite)
                        active = active || component.name == "MBR" || component.name == "RAM";

                    if (mirWriteC) {
                        if (mirC == RegisterBank::PC)
                            active = component.name == "PC";
                        else if (mirC == RegisterBank::AC)
                            active = component.name == "AC";
                        else if (mirC == RegisterBank::SP)
                            active = component.name == "SP";
                        else if (mirC == RegisterBank::IR)
                            active = component.name == "IR";
                        else if (mirC == RegisterBank::TIR)
                            active = component.name == "TIR";
                        else if (mirC == RegisterBank::ZERO)
                            active = component.name == "0";
                        else if (mirC == RegisterBank::ONE)
                            active = component.name == "1";
                        else if (mirC == RegisterBank::MONE)
                            active = component.name == "-1";
                        else if (mirC == RegisterBank::AMASK)
                            active = component.name == "AMASK";
                        else if (mirC == RegisterBank::SMASK)
                            active = component.name == "SMASK";
                    }
                }

                Vector3 position = component.position;
                if (active) {
                    position.x += std::sin(elapsed * 95.0f + i) * 0.035f;
                    position.y += std::cos(elapsed * 83.0f + i) * 0.025f;
                }

                if (mouseInViewport) {
                    RayCollision collision = GetRayCollisionBox(ray,
                        {position.x - component.size.x * 0.5f, position.y - component.size.y * 0.5f,
                         position.z - component.size.z * 0.5f,
                         position.x + component.size.x * 0.5f, position.y + component.size.y * 0.5f,
                         position.z + component.size.z * 0.5f});
                    if (collision.hit && collision.distance < nearestDistance) {
                        nearestDistance = collision.distance;
                        hoverComponent = component.name;
                    }
                }

                const bool selected = component.name == selectedComponent;
                Color face = component.color;
                if (selected)
                    face = ColorAlpha(face, 0.82f);

                DrawCubeV(position, component.size, face);
                DrawCubeWiresV(position, component.size,
                               selected ? RAYWHITE : Fade(RAYWHITE, 0.42f));
                if (component.name == "RAM" && ramChange.remaining > 0.0f)
                    DrawCubeWiresV(position, Vector3Scale(component.size, 1.10f),
                                   ramChangeColor(ramChange));
            }
        }

        if (!hoverComponent.empty() && IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && !layoutEditing)
            selectedComponent = hoverComponent;

        if (layoutEditing && !hoverComponent.empty() && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            selectedComponent = hoverComponent;
            selectedRoute = -1;
            selectedRoutePoint = -1;
        }

        if (layoutEditing && IsMouseButtonDown(MOUSE_BUTTON_LEFT) &&
            (selectedRoute < 0 || selectedRoutePoint < 0) && !hoverComponent.empty()) {
            const Vector2 delta = GetMouseDelta();
            for (GuiComponent& component : components) {
                if (component.name == selectedComponent) {
                    component.position.x += delta.x * 0.025f;
                    component.position.z += delta.y * 0.025f;
                    break;
                }
            }
        }

        if (layoutEditing) {
            float nearestScreenDistance = 16.0f;
            for (int routeIndex = 0; routeIndex < static_cast<int>(routes.size()); ++routeIndex) {
                for (std::size_t pointIndex = 0; pointIndex < routes[routeIndex].count; ++pointIndex) {
                    const Vector2 screenPoint = GetWorldToScreen(routes[routeIndex].points[pointIndex], camera);
                    const float distanceToMouse = Vector2Distance(screenPoint, mouse);
                    if (distanceToMouse < nearestScreenDistance) {
                        nearestScreenDistance = distanceToMouse;
                        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                            selectedRoute = routeIndex;
                            selectedRoutePoint = static_cast<int>(pointIndex);
                            selectedComponent.clear();
                        }
                    }
                }
            }

            if (selectedRoute >= 0 && selectedRoutePoint >= 0 &&
                IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
                const Vector2 delta = GetMouseDelta();
                Vector3& point = routes[selectedRoute].points[selectedRoutePoint];
                point.x += delta.x * 0.025f;
                point.z += delta.y * 0.025f;
            }
        }

        EndMode3D();

        if (boardModelLoaded) {
            for (const ModelComponent& component : modelComponents) {
                if (component.name != hoverComponent)
                    continue;

                Vector2 labelPosition = GetWorldToScreen(
                    Vector3Add(component.position, {0.0f, component.size.y * 0.75f, 0.0f}),
                    camera);
                if (labelPosition.x < viewport.x || labelPosition.x > viewport.x + viewport.width ||
                    labelPosition.y < viewport.y || labelPosition.y > viewport.y + viewport.height)
                    continue;

                const int width = measureUiText(component.name.c_str(), 12) + 14;
                const Rectangle label = {
                    labelPosition.x - width * 0.5f,
                    labelPosition.y - 12.0f,
                    static_cast<float>(width),
                    24.0f
                };
                DrawRectangleRounded(label, 0.20f, 5, Fade(BLACK, 0.88f));
                drawUiText(component.name.c_str(),
                         static_cast<int>(label.x) + 7,
                         static_cast<int>(label.y) + 6,
                         12, RAYWHITE);
            }
        }

        EndScissorMode();

        if (!boardModelLoaded) {
            for (const GuiComponent& component : components) {
                if (component.name != "ALU")
                    continue;

                const Vector2 aluTop = GetWorldToScreen(
                    Vector3Add(component.position, Vector3{0.0f, component.size.y * 0.52f, 0.0f}),
                    camera);
                const Vector2 aluLeft = {aluTop.x - 38.0f, aluTop.y - 2.0f};
                const Vector2 aluRight = {aluTop.x + 38.0f, aluTop.y - 2.0f};
                const Vector2 aluBottom = {aluTop.x, aluTop.y + 34.0f};

                DrawLineEx(aluLeft, aluBottom, 5.0f, RAYWHITE);
                DrawLineEx(aluRight, aluBottom, 5.0f, RAYWHITE);
                drawUiText("ALU", static_cast<int>(aluBottom.x) - 14,
                         static_cast<int>(aluBottom.y) + 8, 12, RAYWHITE);
                break;
            }
        }

        if (!boardModelLoaded) {
            for (std::size_t i = 0; i < components.size(); ++i) {
                const GuiComponent& component = components[i];
                std::string value;
                bool active = false;

                if (cpu && component.name == "PC")
                    value = hex32(cpu->bank.read(RegisterBank::PC));
                else if (cpu && component.name == "AC")
                    value = hex32(cpu->bank.read(RegisterBank::AC));
                else if (cpu && component.name == "SP")
                    value = hex32(cpu->bank.read(RegisterBank::SP));
                else if (cpu && component.name == "IR")
                    value = hex32(cpu->bank.read(RegisterBank::IR));
                else if (cpu && component.name == "TIR")
                    value = hex32(cpu->bank.read(RegisterBank::TIR));
                else if (cpu && component.name == "0")
                    value = hex32(cpu->bank.read(RegisterBank::ZERO));
                else if (cpu && component.name == "1")
                    value = hex32(cpu->bank.read(RegisterBank::ONE));
                else if (cpu && component.name == "-1")
                    value = hex32(cpu->bank.read(RegisterBank::MONE));
                else if (cpu && component.name == "AMASK")
                    value = hex32(cpu->bank.read(RegisterBank::AMASK));
                else if (cpu && component.name == "SMASK")
                    value = hex32(cpu->bank.read(RegisterBank::SMASK));
                else if (component.name == "A Latch" && cpu)
                    value = hex32(cpu->a.read());
                else if (component.name == "B Latch" && cpu)
                    value = hex32(cpu->b.read());
                else if (component.name == "AMUX")
                    value = mirUseMbr ? "MBR" : "A";
                else if (component.name == "ALU" && cpu)
                    value = cpu->n ? "N=1" : (cpu->z ? "Z=1" : "A+B");
                else if (component.name == "Shifter")
                    value = mir ? (static_cast<unsigned>((mir >> 29) & 3) == 1 ? "S>>1" :
                                   static_cast<unsigned>((mir >> 29) & 3) == 2 ? "S<<1" : "Pass") : "Pass";
                else if (component.name == "MAR" && cpu)
                    value = hex32(cpu->mar.read());
                else if (component.name == "MBR" && cpu)
                    value = hex32(cpu->mbr.read());
                else if (component.name == "Control Store")
                    value = std::to_string(session.isLoaded() ? session.lastEvent().mpc : 0);
                else if (component.name == "MPC" && cpu)
                    value = std::to_string(cpu->mpc);
                else if (component.name == "MIR")
                    value = hex36(mir);
                else if (component.name == "RAM" && session.memory())
                    value = std::to_string(session.memory()->sizeWords()) + " words";

                if (component.name == "ALU" || component.name == "MIR" ||
                    component.name == "MPC" || component.name == "Control Store")
                    active = activityPulse > 0.0f;

                drawValueLabel(camera, Vector3Add(component.position,
                               Vector3{0.0f, component.size.y * 0.5f, 0.0f}),
                               component.name.c_str(), value.c_str(), active, static_cast<int>(i));
            }
        }

        if (!hoverComponent.empty())
            drawUiText(("Selecionar " + hoverComponent).c_str(),
                     static_cast<int>(viewport.x) + 18,
                     static_cast<int>(viewport.y) + 18, 14, RAYWHITE);

        if (layoutEditing) {
            DrawRectangleRounded({viewport.x + 18.0f, viewport.y + 38.0f, 410.0f, 48.0f},
                                 0.12f, 6, Fade(BLACK, 0.88f));
            drawUiText("LAYOUT EDITOR  F2 exit  F3 copy",
                     static_cast<int>(viewport.x) + 32,
                     static_cast<int>(viewport.y) + 48, 13, GOLD);
            drawUiText("Drag components or wire points; arrows nudge",
                     static_cast<int>(viewport.x) + 32,
                     static_cast<int>(viewport.y) + 68, 12, RAYWHITE);
        }

        const Rectangle inspector = {1022.0f, 66.0f, 234.0f, 590.0f};
        roundedPanel(inspector, Color{22, 28, 36, 255});

        if (tab == GuiTab::Components) {
            drawUiText("Componente", static_cast<int>(inspector.x) + 14, static_cast<int>(inspector.y) + 42, 16, RAYWHITE);
            drawUiText(selectedComponent.empty() ? "Nenhum selecionado" : selectedComponent.c_str(),
                     static_cast<int>(inspector.x) + 14,
                     static_cast<int>(inspector.y) + 68, 20,
                     selectedComponent.empty() ? LIGHTGRAY : SKYBLUE);

            int y = static_cast<int>(inspector.y) + 112;
            if (selectedComponent.empty()) {
                if (cpu) {
                    const int x = static_cast<int>(inspector.x) + 14;
                    drawUiText("Estado da CPU", x, y, 15, SKYBLUE);
                    y += 28;
                    drawTextRow("PC", hex32(cpu->bank.read(RegisterBank::PC)), x, y);
                    drawTextRow("AC", hex32(cpu->bank.read(RegisterBank::AC)), x, y);
                    drawTextRow("SP", hex32(cpu->bank.read(RegisterBank::SP)), x, y);
                    drawTextRow("IR", macroName(cpu->bank.read(RegisterBank::IR)), x, y);
                    y += 8;
                    drawUiText("Clique no diagrama para ver", x, y, 12, LIGHTGRAY);
                    drawUiText("um componente em detalhe.", x, y + 17, 12, LIGHTGRAY);
                } else {
                    drawUiText("Carregue um programa.", static_cast<int>(inspector.x) + 14,
                               y, 13, LIGHTGRAY);
                }
            } else if (cpu) {
                drawStatusDot(static_cast<int>(inspector.x) + 18, y + 7,
                              activityPulse > 0.0f, GOLD);
                drawTextRow("Estado", session.isRunning() ? "Executando" : session.isHalted() ? "HALT" : "Pausado",
                            static_cast<int>(inspector.x) + 34, y);

                if (selectedComponent == "PC")
                    drawTextRow("Valor", hex32(cpu->bank.read(RegisterBank::PC)),
                                static_cast<int>(inspector.x) + 34, y);
                else if (selectedComponent == "AC")
                    drawTextRow("Valor", hex32(cpu->bank.read(RegisterBank::AC)),
                                static_cast<int>(inspector.x) + 34, y);
                else if (selectedComponent == "SP")
                    drawTextRow("Valor", hex32(cpu->bank.read(RegisterBank::SP)),
                                static_cast<int>(inspector.x) + 34, y);
                else if (selectedComponent == "IR")
                    drawTextRow("Valor", hex32(cpu->bank.read(RegisterBank::IR)),
                                static_cast<int>(inspector.x) + 34, y);
                else if (selectedComponent == "TIR")
                    drawTextRow("Valor", hex32(cpu->bank.read(RegisterBank::TIR)),
                                static_cast<int>(inspector.x) + 34, y);
                else if (selectedComponent == "ZERO")
                    drawTextRow("Valor", "0x00000000",
                                static_cast<int>(inspector.x) + 34, y);
                else if (selectedComponent == "ONE")
                    drawTextRow("Valor", "0x00000001",
                                static_cast<int>(inspector.x) + 34, y);
                else if (selectedComponent == "MONE")
                    drawTextRow("Valor", "0xFFFFFFFF",
                                static_cast<int>(inspector.x) + 34, y);
                else if (selectedComponent == "AMASK")
                    drawTextRow("Valor", "0x00000FFF",
                                static_cast<int>(inspector.x) + 34, y);
                else if (selectedComponent == "SMASK")
                    drawTextRow("Valor", "0x000000FF",
                                static_cast<int>(inspector.x) + 34, y);
                else if (selectedComponent == "REGISTER_A")
                    drawTextRow("Valor", hex32(cpu->bank.read(10)),
                                static_cast<int>(inspector.x) + 34, y);
                else if (selectedComponent == "REGISTER_B")
                    drawTextRow("Valor", hex32(cpu->bank.read(11)),
                                static_cast<int>(inspector.x) + 34, y);
                else if (selectedComponent == "REGISTER_C")
                    drawTextRow("Valor", hex32(cpu->bank.read(12)),
                                static_cast<int>(inspector.x) + 34, y);
                else if (selectedComponent == "REGISTER_D")
                    drawTextRow("Valor", hex32(cpu->bank.read(13)),
                                static_cast<int>(inspector.x) + 34, y);
                else if (selectedComponent == "REGISTER_E")
                    drawTextRow("Valor", hex32(cpu->bank.read(14)),
                                static_cast<int>(inspector.x) + 34, y);
                else if (selectedComponent == "REGISTER_F")
                    drawTextRow("Valor", hex32(cpu->bank.read(15)),
                                static_cast<int>(inspector.x) + 34, y);
                else if (selectedComponent == "REGISTER_BANK")
                    drawTextRow("Regs", "32 x 32-bit",
                                static_cast<int>(inspector.x) + 34, y);
                else if (selectedComponent == "A_DECODER")
                    drawTextRow("Índice A", std::to_string(mirA),
                                static_cast<int>(inspector.x) + 34, y);
                else if (selectedComponent == "B_DECODER")
                    drawTextRow("Índice B", std::to_string(mirB),
                                static_cast<int>(inspector.x) + 34, y);
                else if (selectedComponent == "C_DECODER")
                    drawTextRow("Índice C", std::to_string(mirC),
                                static_cast<int>(inspector.x) + 34, y);
                else if (selectedComponent == "A_LATCH")
                    drawTextRow("Valor", hex32(cpu->a.read()),
                                static_cast<int>(inspector.x) + 34, y);
                else if (selectedComponent == "B_LATCH")
                    drawTextRow("Valor", hex32(cpu->b.read()),
                                static_cast<int>(inspector.x) + 34, y);
                else if (selectedComponent == "MAR")
                    drawTextRow("Endereço", hex32(cpu->mar.read()),
                                static_cast<int>(inspector.x) + 34, y);
                else if (selectedComponent == "MBR")
                    drawTextRow("Valor", hex32(cpu->mbr.read()),
                                static_cast<int>(inspector.x) + 34, y);
                else if (selectedComponent == "MIR")
                    drawTextRow("Palavra", hex36(cpu->mir.read()),
                                static_cast<int>(inspector.x) + 34, y);
                else if (selectedComponent == "MPC")
                    drawTextRow("Endereço", std::to_string(cpu->mpc),
                                static_cast<int>(inspector.x) + 34, y);
                else if (selectedComponent == "ALU")
                    drawTextRow("N / Z", std::string(cpu->n ? "1 " : "0 ") + (cpu->z ? "/ 1" : "/ 0"),
                                static_cast<int>(inspector.x) + 34, y);
                else if (selectedComponent == "AMUX")
                    drawTextRow("Seleção", mirUseMbr ? "MBR" : "A latch",
                                static_cast<int>(inspector.x) + 34, y);
                else if (selectedComponent == "SHIFTER")
                    drawTextRow("Modo", std::to_string((event.word >> 29) & 3),
                                static_cast<int>(inspector.x) + 34, y);
                else if (selectedComponent == "MMUX")
                    drawTextRow("Condição", std::to_string((event.word >> 33) & 3),
                                static_cast<int>(inspector.x) + 34, y);
                else if (selectedComponent == "CONTROL_STORE")
                    drawTextRow("Tamanho", "256 x 36",
                                static_cast<int>(inspector.x) + 34, y);
                else if (selectedComponent == "INCREMENTER")
                    drawTextRow("Operação", "PC/SP + 1",
                                static_cast<int>(inspector.x) + 34, y);
                else if (selectedComponent == "RAM") {
                    const int x = static_cast<int>(inspector.x) + 14;
                    drawTextRow("Palavras", std::to_string(session.memory()->sizeWords()),
                                x, y);
                    drawTextRow("Capacidade", std::to_string(RAM::sizeBytes / 1024) + " KiB",
                                x, y);
                }
                else if (selectedComponent == "CACHE") {
                    drawTextRow("Acertos", std::to_string(cacheStats.hits),
                                static_cast<int>(inspector.x) + 34, y);
                    drawTextRow("Falhas", std::to_string(cacheStats.misses),
                                static_cast<int>(inspector.x) + 34, y);
                    drawTextRow("Último", lastCacheAccess,
                                static_cast<int>(inspector.x) + 34, y);
                }
                else if (selectedComponent == "CLOCK")
                    drawTextRow("Ciclos", std::to_string(session.cycles()),
                                static_cast<int>(inspector.x) + 34, y);
                else
                    drawUiText("Sem valor disponível.", static_cast<int>(inspector.x) + 14, y, 13, LIGHTGRAY);
            } else {
                drawUiText("Carregue um programa.", static_cast<int>(inspector.x) + 14, y, 13, LIGHTGRAY);
            }
        } else if (tab == GuiTab::Execution) {
            drawUiText("Microinstrução", static_cast<int>(inspector.x) + 14,
                     static_cast<int>(inspector.y) + 42, 16, RAYWHITE);
            int y = static_cast<int>(inspector.y) + 74;
            drawUiText(("IR: " + std::string(cpu ? macroName(cpu->bank.read(RegisterBank::IR)) : "—"))
                           .c_str(), static_cast<int>(inspector.x) + 14, y, 13, SKYBLUE);
            y += 25;
            drawTextRow("MPC", std::to_string(event.mpc),
                        static_cast<int>(inspector.x) + 14, y);
            drawTextRow("Palavra", hex36(event.word),
                        static_cast<int>(inspector.x) + 14, y);
            drawTextRow("A", registerName(static_cast<unsigned>((event.word >> 9) & 0x1F)),
                        static_cast<int>(inspector.x) + 14, y);
            drawTextRow("B", registerName(static_cast<unsigned>((event.word >> 14) & 0x1F)),
                        static_cast<int>(inspector.x) + 14, y);
            drawTextRow("C", registerName(static_cast<unsigned>((event.word >> 19) & 0x1F)),
                        static_cast<int>(inspector.x) + 14, y);
            drawTextRow("ULA", aluName(static_cast<unsigned>((event.word >> 31) & 3)),
                        static_cast<int>(inspector.x) + 14, y);
            drawTextRow("Shift", shiftName(static_cast<unsigned>((event.word >> 29) & 3)),
                        static_cast<int>(inspector.x) + 14, y);
            drawTextRow("Desvio", conditionName(static_cast<unsigned>((event.word >> 33) & 3)),
                        static_cast<int>(inspector.x) + 14, y);
            drawTextRow("Memória", event.memoryStall ? "aguardando" :
                        ((event.word >> 26) & 1) ? "leitura" :
                        ((event.word >> 25) & 1) ? "escrita" : "sem acesso",
                        static_cast<int>(inspector.x) + 14, y);
            drawTextRow("Microciclos", std::to_string(session.cycles()),
                        static_cast<int>(inspector.x) + 14, y, SKYBLUE);
            drawTextRow("Instruções", std::to_string(session.instructions()),
                        static_cast<int>(inspector.x) + 14, y, SKYBLUE);
            y += 10;
            drawUiText("Saídas recentes", static_cast<int>(inspector.x) + 14, y, 16, RAYWHITE);
            drawRecentOutputs(session.outputs(), recentOutputScroll);
        } else if (tab == GuiTab::Info) {
            drawUiText("Arquitetura", static_cast<int>(inspector.x) + 14,
                     static_cast<int>(inspector.y) + 42, 16, RAYWHITE);
            int y = static_cast<int>(inspector.y) + 76;
            drawUiText("MIC-1 / 32-bit", static_cast<int>(inspector.x) + 14, y, 15, SKYBLUE);
            y += 30;
            drawUiText("Palavras de 32 bits (word)", static_cast<int>(inspector.x) + 14, y, 14, RAYWHITE);
            y += 24;
            drawUiText("Microinstruções de 36 bits", static_cast<int>(inspector.x) + 14, y, 14, RAYWHITE);
            y += 24;
            drawUiText("Histórico de 1024 ciclos", static_cast<int>(inspector.x) + 14, y, 14, RAYWHITE);
            y += 40;
            drawUiText("Programa", static_cast<int>(inspector.x) + 14, y, 16, RAYWHITE);
            y += 24;
            drawUiText(session.path().empty() ? "Nenhum programa" : session.path().c_str(),
                     static_cast<int>(inspector.x) + 14, y, 12, LIGHTGRAY);
        } else if (tab == GuiTab::Debugger) {
            drawUiText("Depurador", static_cast<int>(inspector.x) + 14, static_cast<int>(inspector.y) + 42, 16, RAYWHITE);
            int y = static_cast<int>(inspector.y) + 76;

            if (listingLines.empty()) {
                drawUiText("Arquivo .lst não encontrado.", static_cast<int>(inspector.x) + 14, y, 13, LIGHTGRAY);
                y += 24;
                drawUiText("Monte o programa para gerar", static_cast<int>(inspector.x) + 14, y, 12, LIGHTGRAY);
            } else {
                const std::uint32_t pc = cpu ? cpu->bank.read(RegisterBank::PC) : 0;
                std::size_t activeIndex = 0;
                for (std::size_t i = 0; i < listingLines.size(); ++i) {
                    if (listingLines[i].address == pc) {
                        activeIndex = i;
                        break;
                    }
                    if (listingLines[i].address < pc)
                        activeIndex = i;
                }

                const std::size_t start = activeIndex > 5 ? activeIndex - 5 : 0;
                const std::size_t end = listingLines.size() < start + 12
                    ? listingLines.size() : start + 12;

                drawTextRow("PC", hex32(pc),
                            static_cast<int>(inspector.x) + 14, y);
                y += 4;

                for (std::size_t i = start; i < end; ++i) {
                    const ListingLine& listing = listingLines[i];
                    const bool active = i == activeIndex;
                    const std::string lineNumber = std::to_string(listing.sourceLine);
                    std::string source = listing.source;
                    if (source.size() > 24)
                        source.resize(24);

                    if (active) {
                        DrawRectangle(static_cast<int>(inspector.x) + 8, y - 3,
                                      static_cast<int>(inspector.width) - 16, 20,
                                      Fade(GOLD, 0.28f));
                    }
                    drawUiText((lineNumber + "  " + source).c_str(),
                             static_cast<int>(inspector.x) + 14, y, 12,
                             active ? YELLOW : RAYWHITE);
                    y += 20;
                }
            }
        } else {
            drawUiText("Controles", static_cast<int>(inspector.x) + 14, static_cast<int>(inspector.y) + 42, 16, RAYWHITE);
            int y = static_cast<int>(inspector.y) + 76;
            const char* instructions[] = {
                "R  Executar", "P  Pausar/retomar", "Espaço  Avançar", "B  Voltar",
                "X  Reiniciar", "L  Carregar .bin", "Alt/BM  Girar", "Roda  Zoom", "Home  Restaurar câmera"
            };
            for (const char* instruction : instructions) {
                drawUiText(instruction, static_cast<int>(inspector.x) + 14, y, 14, RAYWHITE);
                y += 26;
            }
        }

        const Rectangle controlBar = {250.0f, 666.0f, 1006.0f, 42.0f};
        roundedPanel(controlBar, Color{22, 28, 36, 255});

        const float buttonY = 675.0f;
        const bool controlsEnabled = !editingPath && !speedDialogOpen;
        if (iconButton({262.0f, buttonY, 72.0f, 26.0f}, DebugIcon::Play,
                       "Executar (R)", true) && controlsEnabled) {
            session.run();
            playSfx(sfx.click);
        }

        if (iconButton({342.0f, buttonY, 72.0f, 26.0f}, DebugIcon::Pause,
                       "Pausar/retomar (P)",
                       false, session.isLoaded() && !session.isRunning() && !session.isHalted()) && controlsEnabled) {
            togglePause();
            playSfx(sfx.click);
        }

        if (iconButton({422.0f, buttonY, 72.0f, 26.0f}, DebugIcon::StepForward,
                       "Avançar (Espaço)") && controlsEnabled) {
            session.pause();
            stepSession();
            playSfx(sfx.step);
        }

        if (iconButton({502.0f, buttonY, 72.0f, 26.0f}, DebugIcon::StepBack,
                       "Voltar (B)") && controlsEnabled) {
            backSession();
            playSfx(sfx.click);
        }

        if (iconButton({582.0f, buttonY, 72.0f, 26.0f}, DebugIcon::Reset,
                       "Reiniciar (X)") && controlsEnabled) {
            session.reset();
            ramChange = {};
            stackScroll = 0;
            playSfx(sfx.click);
        }

        if (controlButton({662.0f, buttonY, 72.0f, 26.0f}, "Carregar") && controlsEnabled) {
            session.pause();
            pathInput = session.path();
            editingPath = true;
            playSfx(sfx.click);
        }

        drawUiText(message.c_str(), 858, 680, 13, ORANGE);

        if (editingPath) {
            DrawRectangle(0, 0, GetScreenWidth(), GetScreenHeight(), Fade(BLACK, 0.78f));
            Rectangle dialog = {
                static_cast<float>(GetScreenWidth()) * 0.5f - 310.0f,
                static_cast<float>(GetScreenHeight()) * 0.5f - 72.0f,
                620.0f,
                144.0f
            };
            roundedPanel(dialog, Color{35, 43, 54, 255}, "Carregar programa");
            drawUiText("Digite o caminho do .bin", static_cast<int>(dialog.x) + 18,
                     static_cast<int>(dialog.y) + 48, 14, LIGHTGRAY);
            DrawRectangleRounded({dialog.x + 18.0f, dialog.y + 74.0f, dialog.width - 36.0f, 34.0f},
                                 0.12f, 6, Color{12, 16, 22, 255});
            drawUiText(pathInput.c_str(), static_cast<int>(dialog.x) + 28,
                     static_cast<int>(dialog.y) + 82, 15, RAYWHITE);
        }

        if (speedDialogOpen) {
            DrawRectangle(0, 0, GetScreenWidth(), GetScreenHeight(), Fade(BLACK, 0.78f));
            const Rectangle dialog = {
                static_cast<float>(GetScreenWidth()) * 0.5f - 180.0f,
                static_cast<float>(GetScreenHeight()) * 0.5f - 160.0f,
                360.0f, 320.0f
            };
            roundedPanel(dialog, Color{35, 43, 54, 255}, "Velocidade da simulação");
            drawUiText("Microciclos por segundo (1x = 10)",
                     static_cast<int>(dialog.x) + 18, static_cast<int>(dialog.y) + 43,
                     14, LIGHTGRAY);
            for (std::size_t i = 0; i < playbackSpeeds.size(); ++i) {
                const std::string label = std::string(playbackLabels[i]) +
                    (i == selectedSpeed ? "  selecionado" : "");
                if (controlButton({dialog.x + 18.0f, dialog.y + 70.0f + i * 40.0f,
                                   dialog.width - 36.0f, 32.0f}, label.c_str(),
                                  i == selectedSpeed)) {
                    selectedSpeed = i;
                    stepAccumulator = 0.0f;
                    speedDialogOpen = false;
                    playSfx(sfx.click);
                }
            }

            if (controlButton({dialog.x + 18.0f, dialog.y + 276.0f,
                               dialog.width - 36.0f, 28.0f}, "Fechar")) {
                speedDialogOpen = false;
                playSfx(sfx.click);
            }
        }

        EndDrawing();
    }

    if (boardModelLoaded)
        UnloadModel(boardModel);

    UnloadSound(sfx.click);
    UnloadSound(sfx.hover);
    UnloadSound(sfx.step);
    UnloadSound(sfx.halt);
    UnloadSound(sfx.problem);
    UnloadSound(sfx.cacheHit);
    UnloadSound(sfx.cacheMiss);
    CloseAudioDevice();

    if (uiFontLoaded)
        UnloadFont(uiFont);

    CloseWindow();
    return 0;
}
