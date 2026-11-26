#include "crown_module.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace {
struct Thresholds {
    int species, mini, silver, gold;
};
constexpr Thresholds thresholds[]{
#include "thresholds.inc"
};

uint32_t classify(const Thresholds& limits, float scale, float modifier) {
    if (!std::isfinite(scale) || !std::isfinite(modifier) ||
        scale <= 0 || modifier <= 0 || modifier >= 2) return ICEBORN_CROWN_NONE;
    const float size = std::nearbyint((scale / modifier) * 100.0f);
    if (!std::isfinite(size) || size<1) return ICEBORN_CROWN_NONE;
    if (size >= limits.gold) return ICEBORN_CROWN_LARGE_GOLD;
    if (size >= limits.silver) return ICEBORN_CROWN_LARGE_SILVER;
    if (size <= limits.mini) return ICEBORN_CROWN_MINI_GOLD;
    return ICEBORN_CROWN_NONE;
}

template<class T>
bool read(const IcebornFrame& frame, uint64_t base, uint64_t offset, T& value) {
    constexpr uint64_t limit = 0x800000000000;
    return base >= 0x10000 && base <= limit - sizeof(T) &&
           offset <= limit - sizeof(T) - base &&
           frame.read_exact(frame.context, base + offset, &value, static_cast<uint32_t>(sizeof(T)));
}

bool stage(const IcebornFrame& frame, int32_t& value) {
    uint64_t zone{};
    return read(frame, frame.image_base, 0x0500eca0, zone) && read(frame, zone, 0xaed0, value);
}

bool readCrown(const IcebornFrame& frame, uint64_t component, IcebornCrownRow& result) {
    uint64_t monster{}, status{}, check{};
    int32_t species{}, finalSpecies{};
    if (!read(frame, component, 0x138, monster)) return false;
    if (!monster) return true;
    if (!read(frame, monster, 0x12280, species)) return false;
    const auto* limits = std::lower_bound(std::begin(thresholds), std::end(thresholds), species,
        [](const Thresholds& value, int32_t id) { return value.species < id; });
    if (limits == std::end(thresholds) || limits->species != species) return true;

    float scale{}, modifier{};
    if (!read(frame, monster, 0x1bc48, status) ||
        !read(frame, status, 0x148, check) || check != monster ||
        !read(frame, monster, 0x184, scale) ||
        !read(frame, monster, 0x7730, modifier) ||
        !read(frame, monster, 0x12280, finalSpecies) || finalSpecies != species ||
        !read(frame, monster, 0x1bc48, check) || check != status ||
        !read(frame, component, 0x138, check) || check != monster) return false;

    result = {monster, component, species, classify(*limits, scale, modifier)};
    return true;
}

int32_t sample(const IcebornFrame* frame, IcebornCrownRow* out, uint32_t capacity) {
    if (!frame || frame->size != sizeof(*frame) || frame->profile != 421810 ||
        !frame->read_exact || !out || !capacity) return -1;
    if (!frame->generation) return 0;
    const bool quest = frame->quest > 0 && frame->state == 2;
    const bool expedition = frame->state == 0 || (frame->state == 2 && frame->quest <= 0);
    if (!quest && !expedition) return 0;
    int32_t currentStage{};
    if (!quest) {
        if (!stage(*frame, currentStage)) return -1;
        if ((currentStage < 101 || currentStage > 105) &&
            currentStage != 108 && currentStage != 109) return 0;
    }

    std::array<uint64_t, ICEBORN_MAX_CROWNS> slots{}, finalSlots{};
    uint64_t manager{}, finalManager{};
    if (!read(*frame, frame->image_base, 0x0500cf40, manager) ||
        !read(*frame, manager, 0x38, slots)) return -1;
    auto components = slots;
    std::sort(components.begin(), components.end());
    const auto end = std::unique(components.begin(), components.end());
    std::array<IcebornCrownRow, ICEBORN_MAX_CROWNS> rows{};
    uint32_t count = 0;
    bool incomplete = false;
    for (auto it = components.begin(); it != end; ++it) {
        if (!*it) continue;
        IcebornCrownRow row{};
        if (!readCrown(*frame, *it, row)) {
            incomplete = true;
            continue;
        }
        if (!row.kind) continue;
        const auto collected = rows.begin() + count;
        if (std::none_of(rows.begin(), collected,
                         [&](const IcebornCrownRow& other) { return other.instance == row.instance; }))
            rows[count++] = row;
    }
    int32_t finalStage{};
    if (incomplete || count > capacity ||
        !read(*frame, manager, 0x38, finalSlots) || finalSlots != slots ||
        !read(*frame, frame->image_base, 0x0500cf40, finalManager) || finalManager != manager ||
        (!quest && (!stage(*frame, finalStage) || finalStage != currentStage))) return -1;

    std::copy_n(rows.begin(), count, out);
    return static_cast<int32_t>(count);
}

constexpr IcebornCrownModule module{sizeof(IcebornCrownModule), ICEBORN_CROWN_ABI, 200, sample};
}

extern "C" __declspec(dllexport) const IcebornCrownModule* IcebornQueryCrownModule(uint32_t abi) {
    return abi == ICEBORN_CROWN_ABI ? &module : nullptr;
}
