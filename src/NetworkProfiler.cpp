#include "NetworkProfiler.hpp"

#include "AdapterTracy.hpp"
#include "scriptProfiler.hpp"

#define TRACY_ENABLE
#define TRACY_ON_DEMAND
#include <Tracy.hpp>
#include <pointers.hpp>

extern void diag_log(r_string msg);

class ClassEntrySizeCounter : public param_archive_entry {
    friend class ClassEntryRef;
public:
    r_string name;
    uint64_t* sizeCounter;
    ClassEntrySizeCounter* parent = nullptr;
    std::vector<ClassEntrySizeCounter*> activeSubs;
    std::string* data;

    ClassEntrySizeCounter(r_string name_, uint64_t* size) : sizeCounter(size) {
        *sizeCounter += name_.length();
    };

    ClassEntrySizeCounter(r_string name_, uint64_t* size, std::string* data_) : sizeCounter(size), data(data_) {
        *sizeCounter += name_.length();
        if (data) {
            *data += name_;
            data->push_back('/');
        }
    };

    ClassEntrySizeCounter(r_string name_, ClassEntrySizeCounter* parent_) : sizeCounter(parent_->sizeCounter), parent(parent_), data(parent_->data) {
        *sizeCounter += name_.length();
        parent->activeSubs.push_back(this);
    };

    //! virtual destructor
    virtual ~ClassEntrySizeCounter() {
        if (parent) {
            auto newEnd = std::remove(parent->activeSubs.begin(), parent->activeSubs.end(), this);
            parent->activeSubs.erase(newEnd, parent->activeSubs.end());
        }
        auto copy = activeSubs;
        for (auto& it : copy)
            delete it;
    }

    // generic entry
    int entry_count() const override { return 0; }


    param_archive_entry* get_entry_by_index(int i) const override { return nullptr; }

    r_string current_entry_name() override { return ""sv; }


    param_archive_entry* get_entry_by_name(const r_string& name) const override { return nullptr; }
    operator float() const override { return 0; }
    operator int() const override { return 0; }

    operator int64_t() const override { return 0; }

    operator r_string() const override { return ""sv; }

    operator bool() const override { return false; }
    //GetContext
    r_string _placeholder1(uint32_t member = NULL) const override { return ""sv; }

    // array
    void reserve(int count) override {}

    void add_array_entry(float val) override {
        if (data) {
            *data += std::to_string(val);
            data->push_back('/');
        }
        *sizeCounter += 4;
    }

    void add_array_entry(int val) override {
        if (data) {
            *data += std::to_string(val);
            data->push_back('/');
        }
        *sizeCounter += 4;
    }

    void add_array_entry(int64_t val) override {
        if (data) {
            *data += std::to_string(val);
            data->push_back('/');
        }
        *sizeCounter += 8;
    }

    //void add_array_entry(bool val) override {
    //    std::stringstream str;
    //    str << this << " AddValue " << val << "\n";
    //    OutputDebugStringA(str.str().c_str());
    //}
    void add_array_entry(const r_string& val) override {
        if (data) {
            *data += val;
            data->push_back('/');
        }
        *sizeCounter += val.length();
    }

    int count() const override { return 0; }

    param_archive_array_entry* operator [](int i) const override { return nullptr; }

    param_archive_entry* add_entry_class(const r_string& name, bool guaranteedUnique = false) override {
        if (data) {
            *data += name;
            data->push_back('/');
        }
        *sizeCounter += name.length();
        return new ClassEntrySizeCounter(""sv, this);
    }

    param_archive_entry* add_entry_array(const r_string& name) override {
        if (data) {
            *data += name;
            data->push_back('/');
        }
        *sizeCounter += name.length();
        return new ClassEntrySizeCounter(""sv, this);
    }

    void add_entry(const r_string& name, const r_string& val) override {
        if (data) {
            *data += name;
            data->push_back('=');
            *data += val;
            data->push_back('/');
        }
        *sizeCounter += name.length() + val.length();
    }

    void add_entry(const r_string& name, float val) override {
        if (data) {
            *data += name;
            data->push_back('=');
            *data += std::to_string(val);
            data->push_back('/');
        }
        *sizeCounter += name.length() + 4; }

    void add_entry(const r_string& name, int val) override {
        if (data) {
            *data += name;
            data->push_back('=');
            *data += std::to_string(val);
            data->push_back('/');
        }
        *sizeCounter += name.length() + 4;
    }

    void add_entry(const r_string& name, int64_t val) override {
        if (data) {
            *data += name;
            data->push_back('=');
            *data += std::to_string(val);
            data->push_back('/');
        }
        *sizeCounter += name.length() + 8;
    }

    void compress() override {};

    //! Delete the entry. Note: it could be used in rare cases only!
    void _placeholder(const r_string& name) override {};
};

void NetworkProfiler::init() {
    auto iface = client::host::request_plugin_interface("BIDebugEngine_getCallstack", 1);
    if (!iface) {
        diag_log("ASP: Network statistics failed to enable because ArmaDebugEngine is missing"sv);
        return;
    }
    getCallstackRaw = reinterpret_cast<decltype(getCallstackRaw)>(*iface);

    static auto _pubVar = client::host::register_sqf_command("publicVariable", "Profiler redirect", [](game_state& gs, game_value_parameter arg) -> game_value {
        static tracy::SourceLocationData info {
            "publicVariable",
            "publicVariable",
            "",
            0
        };
        static int64_t publicVarSize;
        auto callstack = NetworkProfiler::getCallstackRaw(&gs);

        r_string varName = arg;
        auto varValue = sqf::get_variable(sqf::current_namespace(), varName);

        std::string data;
        publicVarSize += getVariableSize(varValue, logPacketContent ? &data : nullptr);
        publicVarSize += varName.size();

        tracy::ScopedZone zone(&info, tracy::t_withCallstack{});
        AdapterTracy::sendCallstack(callstack);
        if (logPacketContent)
            zone.Text(data.c_str(), data.size());
        else
            zone.Text(varName.c_str(), varName.size());

        tracy::Profiler::PlotData("publicVariable", publicVarSize);

        sqf::public_variable(arg);

        return {};
    }, game_data_type::NOTHING, game_data_type::STRING);

    static auto _pubVarCli = client::host::register_sqf_command("publicVariableClient", "Profiler redirect", [](game_state& gs, game_value_parameter cli, game_value_parameter arg) -> game_value {
        static tracy::SourceLocationData info {
            "publicVariableClient",
            "publicVariableClient",
            "",
            0
        };
        static int64_t publicVarSize;
        auto callstack = NetworkProfiler::getCallstackRaw(&gs);

        r_string varName = arg;
        auto varValue = sqf::get_variable(sqf::current_namespace(), varName);

        std::string data;
        publicVarSize += getVariableSize(varValue, logPacketContent ? &data : nullptr);
        publicVarSize += varName.size();
        publicVarSize += 4; //clientID

        tracy::ScopedZone zone(&info, tracy::t_withCallstack{});
        AdapterTracy::sendCallstack(callstack);
        if (logPacketContent)
            zone.Text(data.c_str(), data.size());
        else
            zone.Text(varName.c_str(), varName.size());

        tracy::Profiler::PlotData("publicVariableClient", publicVarSize);

        sqf::public_variable_client(cli, arg);

        return {};
    }, game_data_type::NOTHING, game_data_type::SCALAR, game_data_type::STRING);

    static auto _pubVarSrv = client::host::register_sqf_command("publicVariableServer", "Profiler redirect", [](game_state& gs, game_value_parameter arg) -> game_value {
        static tracy::SourceLocationData info {
            "publicVariableServer",
            "publicVariableServer",
            "",
            0
        };
        static int64_t publicVarSize;
        auto callstack = NetworkProfiler::getCallstackRaw(&gs);

        r_string varName = arg;
        auto varValue = sqf::get_variable(sqf::current_namespace(), varName);

        std::string data;
        publicVarSize += getVariableSize(varValue, logPacketContent ? &data : nullptr);
        publicVarSize += varName.size();

        tracy::ScopedZone zone(&info, tracy::t_withCallstack{});
        AdapterTracy::sendCallstack(callstack);
        if (logPacketContent)
            zone.Text(data.c_str(), data.size());
        else
            zone.Text(varName.c_str(), varName.size());

        tracy::Profiler::PlotData("publicVariableServer", publicVarSize);

        sqf::public_variable_server(arg);

        return {};
    }, game_data_type::NOTHING, game_data_type::STRING);

    static auto _remoteExecUn = client::host::register_sqf_command("remoteExec", "Profiler redirect", [](game_state& gs, game_value_parameter arg) -> game_value {
        static tracy::SourceLocationData info {
            "remoteExec",
            "remoteExec",
            "",
            0
        };
        auto callstack = NetworkProfiler::getCallstackRaw(&gs);

        std::string data;
        remoteExecSize += getVariableSize(arg, logPacketContent ? &data : nullptr);

        r_string name = arg[0];

        tracy::ScopedZone zone(&info, tracy::t_withCallstack{});
        AdapterTracy::sendCallstack(callstack);
        if (logPacketContent)
            zone.Text(data.c_str(), data.size());
        else
            zone.Text(name.c_str(), name.size());

        tracy::Profiler::PlotData("remoteExec", remoteExecSize);

        return client::host::functions.invoke_raw_unary(__sqf::unary__remoteexec__array__ret__any, arg);
    }, game_data_type::ANY, game_data_type::ARRAY);

    static auto _remoteExecBin = client::host::register_sqf_command("remoteExec", "Profiler redirect", [](game_state& gs, game_value_parameter par, game_value_parameter arg) -> game_value {
        static tracy::SourceLocationData info {
            "remoteExec",
            "remoteExec",
            "",
            0
        };
        auto callstack = NetworkProfiler::getCallstackRaw(&gs);

        std::string data;
        remoteExecSize += getVariableSize(par, logPacketContent ? &data : nullptr);
        remoteExecSize += getVariableSize(arg, logPacketContent ? &data : nullptr);

        r_string name = arg[0];

        tracy::ScopedZone zone(&info, tracy::t_withCallstack{});
        AdapterTracy::sendCallstack(callstack);
        if (logPacketContent)
            zone.Text(data.c_str(), data.size());
        else
            zone.Text(name.c_str(), name.size());

        tracy::Profiler::PlotData("remoteExec", remoteExecSize);
        return  host::functions.invoke_raw_binary(__sqf::binary__remoteexec__any__array__ret__any, par, arg);
    }, game_data_type::ANY, game_data_type::ANY, game_data_type::ARRAY);

    static auto _remoteExecCallUn = client::host::register_sqf_command("remoteExecCall", "Profiler redirect", [](game_state& gs, game_value_parameter arg) -> game_value {
        static tracy::SourceLocationData info {
            "remoteExecCall",
            "remoteExecCall",
            "",
            0
        };
        auto callstack = NetworkProfiler::getCallstackRaw(&gs);

        std::string data;
        remoteExecSize += getVariableSize(arg, logPacketContent ? &data : nullptr);

        r_string name = arg[0];

        tracy::ScopedZone zone(&info, tracy::t_withCallstack{});
        AdapterTracy::sendCallstack(callstack);
        if (logPacketContent)
            zone.Text(data.c_str(), data.size());
        else
            zone.Text(name.c_str(), name.size());

        tracy::Profiler::PlotData("remoteExec", remoteExecSize);

        return client::host::functions.invoke_raw_unary(__sqf::unary__remoteexeccall__array__ret__any, arg);
    }, game_data_type::ANY, game_data_type::ARRAY);

    static auto _remoteExecCallBin = client::host::register_sqf_command("remoteExecCall", "Profiler redirect", [](game_state& gs, game_value_parameter par, game_value_parameter arg) -> game_value {
        static tracy::SourceLocationData info {
            "remoteExecCall",
            "remoteExecCall",
            "",
            0
        };

        auto callstack = NetworkProfiler::getCallstackRaw(&gs);

        std::string data;
        remoteExecSize += getVariableSize(par, logPacketContent ? &data : nullptr);
        remoteExecSize += getVariableSize(arg, logPacketContent ? &data : nullptr);

        r_string name = arg[0];

        tracy::ScopedZone zone(&info, tracy::t_withCallstack{});
        AdapterTracy::sendCallstack(callstack);
        if (logPacketContent)
            zone.Text(data.c_str(), data.size());
        else
            zone.Text(name.c_str(), name.size());

        tracy::Profiler::PlotData("remoteExec", remoteExecSize);
        return  host::functions.invoke_raw_binary(__sqf::binary__remoteexeccall__any__array__ret__any, par, arg);
    }, game_data_type::ANY, game_data_type::ANY, game_data_type::ARRAY);

    static auto _setVariableObj = client::host::register_sqf_command("setVariable", "Profiler redirect", [](game_state& gs, game_value_parameter par, game_value_parameter arg) -> game_value {
        static tracy::SourceLocationData info {
            "setVariable",
            "setVariable",
            "",
            0
        };

        if (arg.size() != 3 || arg[2].type_enum() != game_data_type::BOOL || !static_cast<bool>(arg[2]))
            return host::functions.invoke_raw_binary(__sqf::binary__setvariable__object__array__ret__nothing, par, arg);

        auto callstack = NetworkProfiler::getCallstackRaw(&gs);

        std::string data;
        setVariableSize += getVariableSize(par, logPacketContent ? &data : nullptr);
        setVariableSize += getVariableSize(arg, logPacketContent ? &data : nullptr);

        r_string name = arg[0];

        tracy::ScopedZone zone(&info, tracy::t_withCallstack{});
        AdapterTracy::sendCallstack(callstack);
        if (logPacketContent)
            zone.Text(data.c_str(), data.size());
        else
            zone.Text(name.c_str(), name.size());

        tracy::Profiler::PlotData("setVariable", setVariableSize);
        return host::functions.invoke_raw_binary(__sqf::binary__setvariable__object__array__ret__nothing, par, arg);
    }, game_data_type::NOTHING, game_data_type::OBJECT, game_data_type::ARRAY);

    static auto _setVariableNS = client::host::register_sqf_command("setVariable", "Profiler redirect", [](game_state& gs, game_value_parameter par, game_value_parameter arg) -> game_value {
        static tracy::SourceLocationData info {
            "setVariable",
            "setVariable",
            "",
            0
        };

        if (arg.size() != 3 || arg[2].type_enum() != game_data_type::BOOL || !static_cast<bool>(arg[2]))
            return host::functions.invoke_raw_binary(__sqf::binary__setvariable__namespace__array__ret__nothing, par, arg);

        auto callstack = NetworkProfiler::getCallstackRaw(&gs);

        std::string data;
        setVariableSize += getVariableSize(par, logPacketContent ? &data : nullptr);
        setVariableSize += getVariableSize(arg, logPacketContent ? &data : nullptr);

        r_string name = arg[0];

        tracy::ScopedZone zone(&info, tracy::t_withCallstack{});
        AdapterTracy::sendCallstack(callstack);
        if (logPacketContent)
            zone.Text(data.c_str(), data.size());
        else
            zone.Text(name.c_str(), name.size());

        tracy::Profiler::PlotData("setVariable", setVariableSize);
        return host::functions.invoke_raw_binary(__sqf::binary__setvariable__namespace__array__ret__nothing, par, arg);
    }, game_data_type::NOTHING, game_data_type::NAMESPACE, game_data_type::ARRAY);

    static auto _setVariableLoc = client::host::register_sqf_command("setVariable", "Profiler redirect", [](game_state& gs, game_value_parameter par, game_value_parameter arg) -> game_value {
        static tracy::SourceLocationData info {
            "setVariable",
            "setVariable",
            "",
            0
        };

        if (arg.size() != 3 || arg[2].type_enum() != game_data_type::BOOL || !static_cast<bool>(arg[2]))
            return host::functions.invoke_raw_binary(__sqf::binary__setvariable__location__array__ret__nothing, par, arg);

        auto callstack = NetworkProfiler::getCallstackRaw(&gs);

        std::string data;
        setVariableSize += getVariableSize(par, logPacketContent ? &data : nullptr);
        setVariableSize += getVariableSize(arg, logPacketContent ? &data : nullptr);

        r_string name = arg[0];

        tracy::ScopedZone zone(&info, tracy::t_withCallstack{});
        AdapterTracy::sendCallstack(callstack);
        if (logPacketContent)
            zone.Text(data.c_str(), data.size());
        else
            zone.Text(name.c_str(), name.size());

        tracy::Profiler::PlotData("setVariable", setVariableSize);
        return host::functions.invoke_raw_binary(__sqf::binary__setvariable__location__array__ret__nothing, par, arg);
    }, game_data_type::NOTHING, game_data_type::LOCATION, game_data_type::ARRAY);

    static auto _setVariableGrp = client::host::register_sqf_command("setVariable", "Profiler redirect", [](game_state& gs, game_value_parameter par, game_value_parameter arg) -> game_value {
        static tracy::SourceLocationData info{
            "setVariable",
            "setVariable",
            "",
            0
        };

        if (arg.size() != 3 || arg[2].type_enum() != game_data_type::BOOL || !static_cast<bool>(arg[2]))
            return host::functions.invoke_raw_binary(__sqf::binary__setvariable__group__array__ret__nothing, par, arg);

        auto callstack = NetworkProfiler::getCallstackRaw(&gs);

        std::string data;
        setVariableSize += getVariableSize(par, logPacketContent ? &data : nullptr);
        setVariableSize += getVariableSize(arg, logPacketContent ? &data : nullptr);

        r_string name = arg[0];

        tracy::ScopedZone zone(&info, tracy::t_withCallstack{});
        AdapterTracy::sendCallstack(callstack);
        if (logPacketContent)
            zone.Text(data.c_str(), data.size());
        else
            zone.Text(name.c_str(), name.size());

        tracy::Profiler::PlotData("setVariable", setVariableSize);
        return host::functions.invoke_raw_binary(__sqf::binary__setvariable__group__array__ret__nothing, par, arg);
    }, game_data_type::NOTHING, game_data_type::GROUP, game_data_type::ARRAY);
}

uint32_t NetworkProfiler::getVariableSize(const game_value& var, std::string* data) {
    auto ncnst = const_cast<game_value&>(var);
    uint64_t size = 0;
    if (data) {
        param_archive ar(rv_allocator<ClassEntrySizeCounter>::create_single(""sv, &size, data));

        (&ncnst)->serialize(ar);
    } else {
        param_archive ar(rv_allocator<ClassEntrySizeCounter>::create_single(""sv, &size));

        (&ncnst)->serialize(ar);
    }

    return size;
}
