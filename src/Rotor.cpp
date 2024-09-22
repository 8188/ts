#include "Rotor.h"

Rotor::Rotor(const std::string& name, const std::string& unit, const Parameters& para, const int controlWord,
    std::shared_ptr<MyRedis> redis, std::shared_ptr<MyMQTT> MQTTCli, std::unique_ptr<MyModbusClient> modbusCli)
    : m_name { name }
    , m_unit { unit }
    , m_para { para }
    , m_controlWord { controlWord }
    , m_redis { redis }
    , m_MQTTCli { MQTTCli }
    , m_ModbusCli { std::move(modbusCli) }
{
    init();
}

void Rotor::run()
{
    get_control_command();

    temp_field();
    thermal_stress();
    life();

    surfaceTemp.cur = get_surface_temp(0, 600);
}

void Rotor::send_message()
{
    double lr = m_redis->m_hget("TS" + m_unit + ":Mechanism:RotorLife", "life" + m_name);
    double olr = m_redis->m_hget("TS" + m_unit + ":Mechanism:RotorLife", "overhaulLife" + m_name);

    json j;
    j["lifeRatio"] = lr;
    j["overhaulLifeRatio"] = olr;
    if (lr > 0.75) {
        j["alert"] = "建议转子大修";
    } else if (olr > 0.06) {
        j["alert"] = "建议转子报废";
    }
    j["ts"] = surfaceTemp.cur;
    j["temperature"] = fieldmHR;
    j["t0"] = centerTemp.cur;
    j["centerThermalStress"] = centerThermalStress;
    j["surfaceThermalStress"] = surfaceThermalStress;
    j["thermalStress"] = thermalStress;
    j["thermalStressMargin"] = thermalStressMargin;

    const std::string jsonString = j.dump();
    m_MQTTCli->publish("TS" + m_unit + "/Rotor" + m_name, jsonString, QOS);
    m_redis->m_hset("TS" + m_unit + ":Mechanism:SendMessage", m_name, jsonString);
}

void Rotor::get_control_command()
{
    std::vector<uint16_t> registers;
    try {
        registers = m_ModbusCli.get()->read_registers(m_controlWord, 1);
    } catch (const std::exception& e) {
        spdlog::warn("Exception from get_control_command: {}", e.what());
    }

    uint16_t value = registers[0];
    bool firstBit = value & 0x1;
    bool secondBit = value & 0x2;

    if (firstBit) {
        m_redis->m_hset("TS" + m_unit + ":Mechanism:RotorLife", "life" + m_name, "0");
    }
    if (secondBit) {
        m_redis->m_hset("TS" + m_unit + ":Mechanism:RotorLife", "overhaulLife" + m_name, "0");
    }
}

double Rotor::get_surface_temp(double min, double max)
{
    std::vector<uint16_t> registers;
    try {
        registers = m_ModbusCli.get()->read_registers(0, 10);
        for (std::size_t i { 0 }; i < registers.size(); ++i) {
            std::cout << registers[i] << " ";
        }
    } catch (const std::exception& e) {
        spdlog::warn("Exception from get_surface_temp: {}", e.what());
    }
    std::cout << '\n';
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<> dis(min, max);
    return dis(gen);
}

template <typename T>
double Rotor::interpolation(double temp, const T& data, std::size_t pointNum) const
{
    if (pointNum == 0 || pointNum > data.X.size()) {
        return 0.0;
    }

    if (temp < data.X[0]) {
        return data.Y[0];
    } else if (temp >= data.X[pointNum - 1]) {
        return data.Y[pointNum - 1];
    } else {
        for (std::size_t i { 1 }; i < pointNum; ++i) {
            if (temp < data.X[i]) {
                double value = data.Y[i - 1] + (data.Y[i] - data.Y[i - 1]) * (temp - data.X[i - 1]) / (data.X[i] - data.X[i - 1]);
                return value;
            }
        }
    }
    return 0;
}

// 计算T1, T2~T19, T20
void Rotor::cal_T()
{
    for (std::size_t i { 0 }; i < 20; ++i) {
        tc = interpolation(field[i].last, m_para.tcz);
        sh = interpolation(field[i].last, m_para.shz);
        double ri = m_para.radius - m_para.deltaR * i;

        double term1 = 2 * tc * m_para.scanCycle;
        double term2, term3, term4;
        if (i == 0) {
            term2 = 2 * (ri - m_para.deltaR / 4) * surfaceTemp.last;
            term3 = 3 * (ri - m_para.deltaR / 2) * field[i].last;
            term4 = (ri - m_para.deltaR) * field[i + 1].last;
        } else if (i != 20) {
            term2 = ri * field[i - 1].last;
            term3 = (2 * ri - m_para.deltaR) * field[i].last;
            term4 = (ri - m_para.deltaR) * field[i + 1].last;
        } else {
            term2 = ri * field[i - 1].last;
            term3 = 3 * (ri - m_para.deltaR / 2) * field[i].last;
            term4 = 2 * (ri - 3 * m_para.deltaR / 4) * centerTemp.last;
        }
        double denominator = (2 * ri - m_para.deltaR) * m_para.density * sh * m_para.deltaR * m_para.deltaR * 1000;

        double T = term1 * (term2 - term3 + term4) / denominator;
        T += field[i].last;
        field[i].cur = T;
    }
    // 计算中心孔温度
    centerTemp.cur = (3 * field[19].cur - field[18].cur) / 2;
}

void Rotor::cal_average_T()
{
    double temp { 0 }, temp1 { 0 }, temp2 { 0 }, temp3 { 0 };
    double ri, denominator, numerator;

    for (std::size_t i { 0 }; i < 20; ++i) {
        ri = m_para.radius - m_para.deltaR * i;
        denominator = 2 * ri * m_para.deltaR - m_para.deltaR * m_para.deltaR;
        numerator = denominator * field[i].cur;
        temp += numerator;
        temp1 += denominator;
        temp2 += numerator;
        temp3 += denominator;
        if (i % 2 != 0) {
            fieldmHR[(i - 1) / 2] = temp2 / temp3;
            temp2 = 0;
            temp3 = 0;
        }
    }
    aveTemp = temp / temp1;
}

void Rotor::update_T()
{
    for (std::size_t i { 0 }; i < 20; ++i) {
        field[i].last = field[i].cur;
    }
    centerTemp.last = centerTemp.cur;
    surfaceTemp.last = surfaceTemp.cur;
}

void Rotor::init()
{
    lifeRatio = m_redis->m_hget("TS" + m_unit + ":Mechanism:RotorLife", "life" + m_name);
    overhaulLifeRatio = m_redis->m_hget("TS" + m_unit + ":Mechanism:RotorLife", "overhaulLife" + m_name);

    double flangeTemp { get_surface_temp(0, 600) };
    for (std::size_t i { 0 }; i < 20; ++i) {
        field[i].last = flangeTemp;
    }
    surfaceTemp.last = flangeTemp;
    centerTemp.last = flangeTemp;
}

void Rotor::temp_field()
{
    cal_T();
    cal_average_T();
    update_T();
}

void Rotor::thermal_stress()
{
    double em { interpolation(aveTemp, m_para.emz) };
    double pr { interpolation(aveTemp, m_para.prz) };
    double lec { interpolation(aveTemp, m_para.lecz, m_para.lecz.X.size()) };

    surfaceThermalStress = m_para.surfaceFactor * em * lec * (aveTemp - surfaceTemp.cur) / 1000 / (1 - pr);
    centerThermalStress = m_para.centerFactor * em * lec * (aveTemp - centerTemp.cur) / 1000 / (1 - pr);
    thermalStress = std::max(fabs(surfaceThermalStress), fabs(centerThermalStress));
    thermalStressMargin = 100.0 * (1 - thermalStress / m_para.freeFactor);
}

void Rotor::life(double K)
{
    // 最小寿命消耗率对应的应力
    double thermalStressMin;
    TempZone SNx;
    double life, lifeConsumptionRate;

    if (aveTemp < m_para.sn[0]) {
        SNx = m_para.SN1;
    } else if (aveTemp > m_para.sn[1]) {
        SNx = m_para.SN3;
    } else {
        SNx = m_para.SN2;
    }
    thermalStressMin = fabs(SNx.X[0]);

    if (fabs(thermalStress) >= thermalStressMin) {
        if (thermalStress > thermalStressMax) {
            thermalStressMax = thermalStress;
        }
    } else {
        if (thermalStressMax > thermalStressMin) {
            life = interpolation(K * thermalStressMax, SNx, SNx.X.size());
            lifeConsumptionRate = 1.0 / life;
            lifeRatio += lifeConsumptionRate;
            overhaulLifeRatio += lifeConsumptionRate;
            thermalStressMax = 0;

            m_redis->m_hset("TS" + m_unit + ":Mechanism:RotorLife", "life" + m_name, std::to_string(lifeRatio));
            m_redis->m_hset("TS" + m_unit + ":Mechanism:RotorLife", "overhaulLife" + m_name, std::to_string(overhaulLifeRatio));
        }
    }
    // std::cout << "lifeRatio: " << lifeRatio << "\toverhaulLifeRatio: " << overhaulLifeRatio << '\n';
}
