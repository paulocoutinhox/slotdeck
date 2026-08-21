#pragma once

#include "utils/Result.h"

#include <QDateTime>
#include <QFuture>
#include <QString>
#include <QStringList>
#include <QVector>

#include <atomic>
#include <memory>
#include <optional>

namespace slotdeck::plugins::system_information {

struct ProcessorCore final {
    quint64 id{0};
    quint64 l1DataBytes{0};
    quint64 l1InstructionBytes{0};
    quint64 l2Bytes{0};
    quint64 l3Bytes{0};
    quint64 regularFrequencyHz{0};
    quint64 maximumFrequencyHz{0};
    bool simultaneousMultithreading{false};
};

struct Processor final {
    quint32 id{0};
    QString vendor;
    QString model;
    quint64 physicalCoreCount{0};
    quint64 logicalCoreCount{0};
    QStringList flags;
    QVector<ProcessorCore> cores;
};

struct ProcessorUsage final {
    std::optional<double> utilization;
    QVector<double> threadUtilization;
    QVector<qint64> threadFrequencyHz;
};

struct MemoryModule final {
    quint32 id{0};
    QString vendor;
    QString name;
    QString model;
    QString serialNumber;
    quint64 sizeBytes{0};
    quint64 frequencyHz{0};
};

struct Memory final {
    quint64 totalBytes{0};
    quint64 freeBytes{0};
    quint64 availableBytes{0};
    QVector<MemoryModule> modules;
};

struct OperatingSystem final {
    QString hostName;
    QString name;
    QString version;
    QString kernel;
    int architectureBits{0};
    QString byteOrder;
};

struct GraphicsProcessor final {
    quint32 id{0};
    QString vendor;
    QString name;
    QString driverVersion;
    QString vendorId;
    QString deviceId;
    quint64 dedicatedMemoryBytes{0};
    quint64 sharedMemoryBytes{0};
    quint64 frequencyHz{0};
    quint64 coreCount{0};
};

struct Mainboard final {
    QString vendor;
    QString name;
    QString version;
    QString serialNumber;
};

struct DiskVolume final {
    QString mountPoint;
    quint64 freeBytes{0};
};

struct Disk final {
    quint32 id{0};
    QString vendor;
    QString model;
    QString serialNumber;
    QString interfaceName;
    quint64 sizeBytes{0};
    QVector<DiskVolume> volumes;
};

struct Battery final {
    quint32 id{0};
    QString vendor;
    QString model;
    QString serialNumber;
    QString technology;
    QString state;
    quint64 fullEnergy{0};
    quint64 currentEnergy{0};
    std::optional<double> capacityPercent;
};

struct NetworkInterface final {
    QString index;
    QString description;
    QString macAddress;
    QString ipv4Address;
    QString ipv6Address;
};

struct SystemSnapshot final {
    QDateTime capturedAtUtc;
    OperatingSystem operatingSystem;
    QVector<Processor> processors;
    ProcessorUsage processorUsage;
    Memory memory;
    QVector<GraphicsProcessor> graphicsProcessors;
    Mainboard mainboard;
    QVector<Disk> disks;
    QVector<Battery> batteries;
    QVector<NetworkInterface> networkInterfaces;
};

class SystemInformationProvider {
  public:
    virtual ~SystemInformationProvider() = default;
    [[nodiscard]] virtual utils::Result<SystemSnapshot> collect(const std::atomic_bool& cancelled) = 0;
};

class HwinfoSystemInformationProvider final : public SystemInformationProvider {
  public:
    [[nodiscard]] utils::Result<SystemSnapshot> collect(const std::atomic_bool& cancelled) override;
};

struct SystemInformationCollection final {
    QFuture<utils::Result<SystemSnapshot>> future;
    std::shared_ptr<std::atomic_bool> cancelled;
};

[[nodiscard]] SystemInformationCollection collectSystemInformation(std::shared_ptr<SystemInformationProvider> provider);

} // namespace slotdeck::plugins::system_information
