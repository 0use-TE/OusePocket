#include "pocket_platform.hpp"
#include "brookesia/hal_interface/device.hpp"
#include "brookesia/hal_interface/interfaces/power/battery.hpp"
namespace {
using namespace esp_brookesia;
class Battery final : public hal::power::BatteryIface {
public:
    Battery() : BatteryIface({.name="Waveshare AXP2101", .chemistry="Li-ion",
        .abilities={Ability::Voltage, Ability::Percentage, Ability::PowerSource, Ability::ChargeState}}) {}
    bool get_state(State &state) override {
        const auto power = ouse::pocket_power_state();
        if (!power.at("valid").as_bool()) return false;
        state = {};
        state.is_present = power.at("present").as_bool();
        state.power_source = power.at("external").as_bool() ? PowerSource::External : PowerSource::Battery;
        state.charge_state = power.at("charging").as_bool() ? ChargeState::Charging :
            power.at("full").as_bool() ? ChargeState::Full : ChargeState::NotCharging;
        if (auto percent = power.if_contains("percent")) {
            state.percentage = percent->to_number<uint8_t>(); state.level_source = LevelSource::FuelGauge;
            state.is_low = *state.percentage <= 15; state.is_critical = *state.percentage <= 5;
        }
        if (auto voltage = power.if_contains("millivolts")) state.voltage_mv = voltage->to_number<uint32_t>();
        return true;
    }
    bool get_charge_config(ChargeConfig &) override { return false; }
    bool set_charge_config(const ChargeConfig &) override { return false; }
    bool set_charging_enabled(bool) override { return false; }
};
class BatteryDevice final : public hal::Device {
public:
    BatteryDevice() : Device("OuseWavesharePower") {}
protected:
    bool probe() override { return true; }
    bool deinit_on_zero_references() const override { return false; }
    std::vector<hal::InterfaceSpec> get_interface_specs() const override {
        return {{hal::power::BatteryIface::NAME,"Ouse:Battery"}};
    }
    bool on_init() override { interfaces_["Ouse:Battery"] = std::make_shared<Battery>(); return true; }
    void on_deinit() override { interfaces_.clear(); }
};
BROOKESIA_PLUGIN_REGISTER_WITH_SYMBOL(hal::Device, BatteryDevice,
    std::string("OuseWavesharePower"), ouse_power_plugin_symbol);
}
