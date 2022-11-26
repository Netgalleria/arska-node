/**
 * @file ArskaGeneric.h
 * @author your name (you@domain.com)
 * @brief
 * @version 0.1
 * @date 2022-11-25
 *
 * @copyright Copyright (c) 2022
 *
 */

#ifndef ArskaGeneric_h
#define ArskaGeneric_h

#include <stdint.h>
#include <time.h>

/**
 * @brief Return true if line is chunk size line in the http response
 *
 * @param line
 * @return true
 * @return false
 */
bool is_chunksize_line(String line)
{
    if (line.charAt(line.length() - 1) != 13) // garbage line ends with cr
        return false;
    if (line.length() < 6)
    { // It is probably buffer length in the beginning of chunk
        Serial.printf(PSTR("Garbage removed [%s] (%d)\n"), line.substring(0, line.length() - 1).c_str(), line.length());
        return true;
    }
    else
        return false;
}

/**
 * @brief Set the internal clock from RTC or browser
 *
 * @param epoch  epoch (seconds in GMT)
 * @param microseconds
 */
void setInternalTime(uint64_t epoch = 0, uint32_t us = 0)
{
    struct timeval tv;
    tv.tv_sec = epoch;
    tv.tv_usec = us;
    settimeofday(&tv, NULL);
}

// https://werner.rothschopf.net/microcontroller/202112_arduino_esp_ntp_rtc_en.htm
/**
 * @brief Get the Timestamp based of date/time components
 *
 * @param year
 * @param mon
 * @param mday
 * @param hour
 * @param min
 * @param sec
 * @return int64_t
 */
int64_t getTimestamp(int year, int mon, int mday, int hour, int min, int sec)
{
    const uint16_t ytd[12] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};                /* Anzahl der Tage seit Jahresanfang ohne Tage des aktuellen Monats und ohne Schalttag */
    int leapyears = ((year - 1) - 1968) / 4 - ((year - 1) - 1900) / 100 + ((year - 1) - 1600) / 400; /* Anzahl der Schaltjahre seit 1970 (ohne das evtl. laufende Schaltjahr) */
    int64_t days_since_1970 = (year - 1970) * 365 + leapyears + ytd[mon - 1] + mday - 1;
    if ((mon > 2) && (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0)))
        days_since_1970 += 1; /* +Schalttag, wenn Jahr Schaltjahr ist */
    return sec + 60 * (min + 60 * (hour + 24 * days_since_1970));
}

#endif
