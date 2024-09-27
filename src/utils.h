#ifndef UTILS_H
#define UTILS_H

#include <fstream>
#include <random>

#include "nlohmann/json.hpp"
#include "spdlog/async.h"
#include "spdlog/spdlog.h"

using json = nlohmann::json;

struct TempPoint {
    double last;
    double cur;
};

struct TempZone {
    std::vector<double> X;
    std::vector<double> Y;
};

struct Parameters {
    const double density;
    const double radius;
    const double holeRadius;
    const double deltaR;
    const double scanCycle;
    const double surfaceFactor;
    const double centerFactor;
    const double freeFactor;
    const TempZone tcz; // Thermal Conductivity
    const TempZone shz; // Specific Heat
    const TempZone emz; // Elastic Modulus
    const TempZone prz; // Poisson's ratio
    const TempZone lecz; // Linear expansion coefficient
    const TempZone SN1, SN2, SN3; // 材料曲线插值
    const std::array<double, 2> sn; // SN曲线温度设定点
};

std::ostream& operator<<(std::ostream& os, const TempZone& tz);

std::ostream& operator<<(std::ostream& os, const Parameters& p);

std::vector<double> split_and_convert(const std::string& str);

Parameters loadParasFromRedis(const json& j, const std::string& key);

Parameters loadParasFromJson(const json& j, const std::string& key);

bool fileExists(const std::string& filename);

std::string generate_random_string_with_hyphens();

#endif // UTILS_H