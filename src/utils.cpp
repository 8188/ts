#include "utils.h"

std::ostream& operator<<(std::ostream& os, const TempZone& tz)
{
    os << "X: ";
    for (const auto& x : tz.X)
        os << x << " ";
    os << "\nY: ";
    for (const auto& y : tz.Y)
        os << y << " ";
    return os;
}

std::ostream& operator<<(std::ostream& os, const Parameters& p)
{
    os << "Density: " << p.density << "\n"
       << "Radius: " << p.radius << "\n"
       << "Hole Radius: " << p.holeRadius << "\n"
       << "Delta R: " << p.deltaR << "\n"
       << "Scan Cycle: " << p.scanCycle << "\n"
       << "Surface Factor: " << p.surfaceFactor << "\n"
       << "Center Factor: " << p.centerFactor << "\n"
       << "Free Factor: " << p.freeFactor << "\n"
       << "Tcz: " << p.tcz << "\n"
       << "Shz: " << p.shz << "\n"
       << "Emz: " << p.emz << "\n"
       << "Prz: " << p.prz << "\n"
       << "Lecz: " << p.lecz << "\n"
       << "SN1: " << p.SN1 << "\n"
       << "SN2: " << p.SN2 << "\n"
       << "SN3: " << p.SN3 << "\n"
       << "Sn: [" << p.sn[0] << ", " << p.sn[1] << "]";
    return os;
}

std::vector<double> split_and_convert(const std::string& str)
{
    std::vector<double> result;
    std::stringstream ss(str);
    std::string item;
    while (std::getline(ss, item, ',')) {
        try {
            result.push_back(std::stod(item));
        } catch (const std::invalid_argument&) {
            spdlog::error("Invalid number format in string: {}", item);
            std::terminate();
        }
    }
    return result;
}

Parameters loadParasFromRedis(const json& j, const std::string& key)
{
    auto get_value_as_double = [&j, &key](const std::string& subkey) -> double {
        auto it = j[key].find(subkey);
        if (it != j[key].end() && it->is_string()) {
            try {
                return std::stod(it->get<std::string>());
            } catch (const std::invalid_argument&) {
                spdlog::error("Invalid number format for key: {}", subkey);
                std::terminate();
            }
        }
        spdlog::error("Invalid or missing parameters: {}", subkey);
        std::terminate();
    };

    auto get_vector_of_doubles = [&j, &key](const std::string& subkey) -> std::vector<double> {
        auto it = j[key].find(subkey);
        if (it != j[key].end() && it->is_string()) {
            try {
                return split_and_convert(it->get<std::string>());
            } catch (const std::exception&) {
                spdlog::error("Error converting string to vector for key: {}", subkey);
                std::terminate();
            }
        }
        spdlog::error("Invalid or missing parameters: {}", subkey);
        std::terminate();
    };

    try {
        auto sn_vector = get_vector_of_doubles("sn");

        return {
            get_value_as_double("density"),
            get_value_as_double("radius"),
            get_value_as_double("holeRadius"),
            get_value_as_double("deltaR"),
            get_value_as_double("scanCycle"),
            get_value_as_double("surfaceFactor"),
            get_value_as_double("centerFactor"),
            get_value_as_double("freeFactor"),
            { get_vector_of_doubles("tcz_X"), get_vector_of_doubles("tcz_Y") },
            { get_vector_of_doubles("shz_X"), get_vector_of_doubles("shz_Y") },
            { get_vector_of_doubles("emz_X"), get_vector_of_doubles("emz_Y") },
            { get_vector_of_doubles("prz_X"), get_vector_of_doubles("prz_Y") },
            { get_vector_of_doubles("lecz_X"), get_vector_of_doubles("lecz_Y") },
            { get_vector_of_doubles("SN1_X"), get_vector_of_doubles("SN1_Y") },
            { get_vector_of_doubles("SN2_X"), get_vector_of_doubles("SN2_Y") },
            { get_vector_of_doubles("SN3_X"), get_vector_of_doubles("SN3_Y") },
            { sn_vector[0], sn_vector[1] }
        };
    } catch (const std::exception& e) {
        spdlog::error("Error loading parameters: {}", e.what());
        std::terminate();
    }
}

Parameters loadParasFromJson(const json& j, const std::string& key)
{
    return {
        j[key]["density"].get<double>(),
        j[key]["radius"].get<double>(),
        j[key]["holeRadius"].get<double>(),
        j[key]["deltaR"].get<double>(),
        j[key]["scanCycle"].get<double>(),
        j[key]["surfaceFactor"].get<double>(),
        j[key]["centerFactor"].get<double>(),
        j[key]["freeFactor"].get<double>(),
        { j[key]["tcz"]["X"].get<std::vector<double>>(), j[key]["tcz"]["Y"].get<std::vector<double>>() },
        { j[key]["shz"]["X"].get<std::vector<double>>(), j[key]["shz"]["Y"].get<std::vector<double>>() },
        { j[key]["emz"]["X"].get<std::vector<double>>(), j[key]["emz"]["Y"].get<std::vector<double>>() },
        { j[key]["prz"]["X"].get<std::vector<double>>(), j[key]["prz"]["Y"].get<std::vector<double>>() },
        { j[key]["lecz"]["X"].get<std::vector<double>>(), j[key]["lecz"]["Y"].get<std::vector<double>>() },
        { j[key]["SN1"]["X"].get<std::vector<double>>(), j[key]["SN1"]["Y"].get<std::vector<double>>() },
        { j[key]["SN2"]["X"].get<std::vector<double>>(), j[key]["SN2"]["Y"].get<std::vector<double>>() },
        { j[key]["SN3"]["X"].get<std::vector<double>>(), j[key]["SN3"]["Y"].get<std::vector<double>>() },
        j[key]["sn"].get<std::array<double, 2>>()
    };
}

bool fileExists(const std::string& filename)
{
    std::ifstream file(filename);
    return file.good();
}

std::string generate_random_string_with_hyphens()
{
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, 15);

    std::stringstream ss;
    const char* hex_chars = "0123456789abcdef";
    for (std::size_t i { 0 }; i < 32; ++i) {
        if (i == 8 || i == 12 || i == 16 || i == 20) {
            ss << "-";
        }
        int index = dis(gen);
        ss << hex_chars[index];
    }

    return ss.str();
}
