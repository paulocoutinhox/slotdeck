#pragma once

#include "utils/Result.h"

#include <QBitArray>
#include <QDateTime>
#include <QString>
#include <QTimeZone>

namespace slotdeck::plugins::ai {

class CronExpression final {
  public:
    [[nodiscard]] static utils::Result<CronExpression> parse(const QString& expression);
    [[nodiscard]] utils::Result<QDateTime> nextAfter(const QDateTime& afterUtc, const QTimeZone& timeZone) const;
    [[nodiscard]] bool matches(const QDateTime& localDateTime) const;
    [[nodiscard]] bool matchesDate(const QDate& date) const;

  private:
    CronExpression(QBitArray minutes, QBitArray hours, QBitArray monthDays, QBitArray months, QBitArray weekDays, bool monthDayWildcard, bool weekDayWildcard);

    QBitArray m_minutes;
    QBitArray m_hours;
    QBitArray m_monthDays;
    QBitArray m_months;
    QBitArray m_weekDays;
    bool m_monthDayWildcard{false};
    bool m_weekDayWildcard{false};
};

} // namespace slotdeck::plugins::ai
