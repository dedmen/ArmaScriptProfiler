#include "AdapterBrofiler.hpp"
#include <random>
#include "Brofiler.h"
using namespace intercept::types;

uint32_t getRandColor() {
    static std::array<uint32_t,140> colors{
        Brofiler::Color::AliceBlue,
        Brofiler::Color::AntiqueWhite,
        Brofiler::Color::Aqua,
        Brofiler::Color::Aquamarine,
        Brofiler::Color::Azure,
        Brofiler::Color::Beige,
        Brofiler::Color::Bisque,
        Brofiler::Color::Black,
        Brofiler::Color::BlanchedAlmond,
        Brofiler::Color::Blue,
        Brofiler::Color::BlueViolet,
        Brofiler::Color::Brown,
        Brofiler::Color::BurlyWood,
        Brofiler::Color::CadetBlue,
        Brofiler::Color::Chartreuse,
        Brofiler::Color::Chocolate,
        Brofiler::Color::Coral,
        Brofiler::Color::CornflowerBlue,
        Brofiler::Color::Cornsilk,
        Brofiler::Color::Crimson,
        Brofiler::Color::Cyan,
        Brofiler::Color::DarkBlue,
        Brofiler::Color::DarkCyan,
        Brofiler::Color::DarkGoldenRod,
        Brofiler::Color::DarkGray,
        Brofiler::Color::DarkGreen,
        Brofiler::Color::DarkKhaki,
        Brofiler::Color::DarkMagenta,
        Brofiler::Color::DarkOliveGreen,
        Brofiler::Color::DarkOrange,
        Brofiler::Color::DarkOrchid,
        Brofiler::Color::DarkRed,
        Brofiler::Color::DarkSalmon,
        Brofiler::Color::DarkSeaGreen,
        Brofiler::Color::DarkSlateBlue,
        Brofiler::Color::DarkSlateGray,
        Brofiler::Color::DarkTurquoise,
        Brofiler::Color::DarkViolet,
        Brofiler::Color::DeepPink,
        Brofiler::Color::DeepSkyBlue,
        Brofiler::Color::DimGray,
        Brofiler::Color::DodgerBlue,
        Brofiler::Color::FireBrick,
        Brofiler::Color::FloralWhite,
        Brofiler::Color::ForestGreen,
        Brofiler::Color::Fuchsia,
        Brofiler::Color::Gainsboro,
        Brofiler::Color::GhostWhite,
        Brofiler::Color::Gold,
        Brofiler::Color::GoldenRod,
        Brofiler::Color::Gray,
        Brofiler::Color::Green,
        Brofiler::Color::GreenYellow,
        Brofiler::Color::HoneyDew,
        Brofiler::Color::HotPink,
        Brofiler::Color::IndianRed,
        Brofiler::Color::Indigo,
        Brofiler::Color::Ivory,
        Brofiler::Color::Khaki,
        Brofiler::Color::Lavender,
        Brofiler::Color::LavenderBlush,
        Brofiler::Color::LawnGreen,
        Brofiler::Color::LemonChiffon,
        Brofiler::Color::LightBlue,
        Brofiler::Color::LightCoral,
        Brofiler::Color::LightCyan,
        Brofiler::Color::LightGoldenRodYellow,
        Brofiler::Color::LightGray,
        Brofiler::Color::LightGreen,
        Brofiler::Color::LightPink,
        Brofiler::Color::LightSalmon,
        Brofiler::Color::LightSeaGreen,
        Brofiler::Color::LightSkyBlue,
        Brofiler::Color::LightSlateGray,
        Brofiler::Color::LightSteelBlue,
        Brofiler::Color::LightYellow,
        Brofiler::Color::Lime,
        Brofiler::Color::LimeGreen,
        Brofiler::Color::Linen,
        Brofiler::Color::Magenta,
        Brofiler::Color::Maroon,
        Brofiler::Color::MediumAquaMarine,
        Brofiler::Color::MediumBlue,
        Brofiler::Color::MediumOrchid,
        Brofiler::Color::MediumPurple,
        Brofiler::Color::MediumSeaGreen,
        Brofiler::Color::MediumSlateBlue,
        Brofiler::Color::MediumSpringGreen,
        Brofiler::Color::MediumTurquoise,
        Brofiler::Color::MediumVioletRed,
        Brofiler::Color::MidnightBlue,
        Brofiler::Color::MintCream,
        Brofiler::Color::MistyRose,
        Brofiler::Color::Moccasin,
        Brofiler::Color::NavajoWhite,
        Brofiler::Color::Navy,
        Brofiler::Color::OldLace,
        Brofiler::Color::Olive,
        Brofiler::Color::OliveDrab,
        Brofiler::Color::Orange,
        Brofiler::Color::OrangeRed,
        Brofiler::Color::Orchid,
        Brofiler::Color::PaleGoldenRod,
        Brofiler::Color::PaleGreen,
        Brofiler::Color::PaleTurquoise,
        Brofiler::Color::PaleVioletRed,
        Brofiler::Color::PapayaWhip,
        Brofiler::Color::PeachPuff,
        Brofiler::Color::Peru,
        Brofiler::Color::Pink,
        Brofiler::Color::Plum,
        Brofiler::Color::PowderBlue,
        Brofiler::Color::Purple,
        Brofiler::Color::Red,
        Brofiler::Color::RosyBrown,
        Brofiler::Color::RoyalBlue,
        Brofiler::Color::SaddleBrown,
        Brofiler::Color::Salmon,
        Brofiler::Color::SandyBrown,
        Brofiler::Color::SeaGreen,
        Brofiler::Color::SeaShell,
        Brofiler::Color::Sienna,
        Brofiler::Color::Silver,
        Brofiler::Color::SkyBlue,
        Brofiler::Color::SlateBlue,
        Brofiler::Color::SlateGray,
        Brofiler::Color::Snow,
        Brofiler::Color::SpringGreen,
        Brofiler::Color::SteelBlue,
        Brofiler::Color::Tan,
        Brofiler::Color::Teal,
        Brofiler::Color::Thistle,
        Brofiler::Color::Tomato,
        Brofiler::Color::Turquoise,
        Brofiler::Color::Violet,
        Brofiler::Color::Wheat,
        Brofiler::Color::White,
        Brofiler::Color::WhiteSmoke,
        Brofiler::Color::Yellow,
        Brofiler::Color::YellowGreen
    };



    static std::default_random_engine rng(std::random_device{}());
    std::uniform_int_distribution<size_t> colorsDist(0, colors.size() - 1);
    return colors[colorsDist(rng)];
}


AdapterBrofiler::AdapterBrofiler() {
    type = AdapterType::Brofiler;

    static Brofiler::ThreadScope mainThreadScope("Frame");
}


AdapterBrofiler::~AdapterBrofiler() {
    cleanup();
}

void AdapterBrofiler::perFrame() {
    if (frameEvent)
        Brofiler::Event::Stop(*frameEvent);

    Brofiler::NextFrame();
    static r_string frameName("Frame");
    static r_string profName("scriptProfiler.cpp");
    static Brofiler::EventDescription* autogenerated_description_276 = ::Brofiler::EventDescription::Create(frameName, profName, __LINE__);



    frameEvent = Brofiler::Event::Start(*autogenerated_description_276);
}

std::shared_ptr<ScopeInfo> AdapterBrofiler::createScope(r_string name, r_string filename, uint32_t fileline) {
    auto ret = std::make_shared<ScopeInfoBrofiler>();

    auto found = tempEventDescriptions.find({name,fileline});
	if (found != tempEventDescriptions.end()) {
		return found->second;
	}

    ret->eventDescription = Brofiler::EventDescription::Create(name,
        filename,
        fileline, getRandColor());

    return ret;
}

std::shared_ptr<ScopeTempStorage> AdapterBrofiler::enterScope(std::shared_ptr<ScopeInfo> scope) {
    
    auto brofilerInfo = std::dynamic_pointer_cast<ScopeInfoBrofiler>(scope);
    if (!brofilerInfo) return nullptr; //#TODO debugbreak? log error?

    if (brofilerInfo->eventDescription) {
        auto tempStorage = std::make_shared<ScopeTempStorageBrofiler>();
        tempStorage->evtDt = Brofiler::Event::Start(*brofilerInfo->eventDescription);
        return tempStorage;
    }
    return nullptr;
}

void AdapterBrofiler::leaveScope(std::shared_ptr<ScopeTempStorage> tempStorage) {
    auto tmpStorageBrofiler = std::dynamic_pointer_cast<ScopeTempStorageBrofiler>(tempStorage);
    if (!tmpStorageBrofiler) return; //#TODO debugbreak? log error?

    if (tmpStorageBrofiler->evtDt) {
        Brofiler::Event::Stop(*tmpStorageBrofiler->evtDt);
    }
}

void AdapterBrofiler::setThisArgs(std::shared_ptr<ScopeTempStorage> tempStorage, game_value thisArgs) {
    auto tmpStorageBrofiler = std::dynamic_pointer_cast<ScopeTempStorageBrofiler>(tempStorage);
    if (!tmpStorageBrofiler) return; //#TODO debugbreak? log error?

    if (tmpStorageBrofiler->evtDt)
        tmpStorageBrofiler->evtDt->thisArgs = thisArgs;
}

void AdapterBrofiler::cleanup() {
    if (frameEvent)
        Brofiler::Event::Stop(*frameEvent);
    tempEventDescriptions.clear();
}
