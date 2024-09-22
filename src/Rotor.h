#ifndef ROTOR_H
#define ROTOR_H

#include "spdlog/async.h"
#include "spdlog/spdlog.h"

#include "myMQTT.h"
#include "myModbus.h"
#include "myRedis.h"
#include "utils.h"

constexpr const int QOS { 1 };

class Rotor {
public:
    Rotor(const std::string& name, const std::string& unit, const Parameters& para, const int controlWord,
        std::shared_ptr<MyRedis> redis, std::shared_ptr<MyMQTT> MQTTCli, std::unique_ptr<MyModbusClient> modbusCli);

    void run();
    void send_message();

private:
    const std::string m_name;
    const std::string m_unit;
    const Parameters& m_para;
    const int m_controlWord;

    double tc;
    double sh;
    double aveTemp;
    double surfaceThermalStress;
    double centerThermalStress;
    double thermalStress;
    double thermalStressMargin;
    double thermalStressMax; // 最大寿命消耗率对应的应力
    double lifeRatio { 0 };
    double overhaulLifeRatio { 0 };

    TempPoint surfaceTemp {};
    TempPoint centerTemp {};
    std::array<TempPoint, 20> field {};

    std::shared_ptr<MyRedis> m_redis;
    std::shared_ptr<MyMQTT> m_MQTTCli;
    std::unique_ptr<MyModbusClient> m_ModbusCli;

    // 输出
    std::array<double, 10> fieldmHR;
    void get_control_command();
    double get_surface_temp(double min, double max);
    template <typename T>
    double interpolation(double temp, const T& data, std::size_t pointNum = 8) const;
    // 计算T1, T2~T19, T20
    void cal_T();
    void cal_average_T();
    void update_T();
    void init();
    void temp_field();
    void thermal_stress();
    void life(double K = 1.0 /*热应力集中系数*/);
};

#endif // ROTOR_H