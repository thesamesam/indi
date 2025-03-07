/*******************************************************************************
  Copyright(c) 2015 Jasem Mutlaq. All rights reserved.

  INDI GPS Device Class

  This program is free software; you can redistribute it and/or modify it
  under the terms of the GNU General Public License as published by the Free
  Software Foundation; either version 2 of the License, or (at your option)
  any later version.

  This program is distributed in the hope that it will be useful, but WITHOUT
  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
  more details.

  You should have received a copy of the GNU Library General Public License
  along with this library; see the file COPYING.LIB.  If not, write to
  the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
  Boston, MA 02110-1301, USA.

  The full GNU General Public License is included in this distribution in the
  file called LICENSE.
*******************************************************************************/

#include "indigps.h"

#include <cstring>

namespace INDI
{

//////////////////////////////////////////////////////////////////////
///
//////////////////////////////////////////////////////////////////////
bool GPS::initProperties()
{
    DefaultDevice::initProperties();

    time(&m_GPSTime);

    PeriodNP[0].fill("PERIOD", "Period (s)", "%.f", 0, 3600, 60.0, 0);
    PeriodNP.fill(getDeviceName(), "GPS_REFRESH_PERIOD", "Refresh", MAIN_CONTROL_TAB, IP_RW, 0, IPS_IDLE);

    RefreshSP[0].fill("REFRESH", "GPS", ISS_OFF);
    RefreshSP.fill(getDeviceName(), "GPS_REFRESH", "Refresh", MAIN_CONTROL_TAB, IP_RW, ISR_ATMOST1, 0, IPS_IDLE);

    LocationNP[LOCATION_LATITUDE].fill("LAT", "Lat (dd:mm:ss)", "%010.6m", -90, 90, 0, 0.0);
    LocationNP[LOCATION_LONGITUDE].fill("LONG", "Lon (dd:mm:ss)", "%010.6m", 0, 360, 0, 0.0);
    LocationNP[LOCATION_ELEVATION].fill("ELEV", "Elevation (m)", "%g", -200, 10000, 0, 0);
    LocationNP.fill(getDeviceName(), "GEOGRAPHIC_COORD", "Location", MAIN_CONTROL_TAB, IP_RO, 60, IPS_IDLE);

    // System Time Settings
    SystemTimeUpdateSP[UPDATE_NEVER].fill("UPDATE_NEVER", "Never", ISS_OFF);
    SystemTimeUpdateSP[UPDATE_ON_STARTUP].fill("UPDATE_ON_STARTUP", "On Startup", ISS_ON);
    SystemTimeUpdateSP[UPDATE_ON_REFRESH].fill("UPDATE_ON_REFRESH", "On Refresh", ISS_OFF);
    SystemTimeUpdateSP.fill(getDeviceName(), "SYSTEM_TIME_UPDATE", "System Time", MAIN_CONTROL_TAB, IP_RW, ISR_1OFMANY, 60, IPS_IDLE);
    SystemTimeUpdateSP.load();

    TimeTP[0].fill("UTC", "UTC Time", nullptr);
    TimeTP[1].fill("OFFSET", "UTC Offset", nullptr);
    TimeTP.fill(getDeviceName(), "TIME_UTC", "UTC", MAIN_CONTROL_TAB, IP_RO, 60, IPS_IDLE);

    setDefaultPollingPeriod(2000);

    // Default interface, can be overridden by child
    setDriverInterface(GPS_INTERFACE);

    return true;
}

//////////////////////////////////////////////////////////////////////
///
//////////////////////////////////////////////////////////////////////
bool GPS::Disconnect()
{
    m_SystemTimeUpdated = false;
    return INDI::DefaultDevice::Disconnect();
}

//////////////////////////////////////////////////////////////////////
///
//////////////////////////////////////////////////////////////////////
bool GPS::updateProperties()
{
    DefaultDevice::updateProperties();

    if (isConnected())
    {
        // Update GPS and send values to client
        IPState state = updateGPS();

        LocationNP.setState(state);
        defineProperty(LocationNP);
        TimeTP.setState(state);
        defineProperty(TimeTP);
        RefreshSP.setState(state);
        defineProperty(RefreshSP);
        defineProperty(PeriodNP);
        defineProperty(SystemTimeUpdateSP);

        if (state != IPS_OK)
        {
            if (state == IPS_BUSY)
                LOG_INFO("GPS fix is in progress...");

            timerID = SetTimer(getCurrentPollingPeriod());
        }
        else if (PeriodNP[0].getValue() > 0)
            timerID = SetTimer(PeriodNP[0].getValue());
    }
    else
    {
        deleteProperty(LocationNP);
        deleteProperty(TimeTP);
        deleteProperty(RefreshSP);
        deleteProperty(PeriodNP);
        deleteProperty(SystemTimeUpdateSP);

        if (timerID > 0)
        {
            RemoveTimer(timerID);
            timerID = -1;
        }
    }

    return true;
}

//////////////////////////////////////////////////////////////////////
///
//////////////////////////////////////////////////////////////////////
void GPS::TimerHit()
{
    if (!isConnected())
    {
        timerID = SetTimer(getCurrentPollingPeriod());
        return;
    }

    IPState state = updateGPS();

    LocationNP.setState(state);
    TimeTP.setState(state);
    RefreshSP.setState(state);

    switch (state)
    {
        // Ok
        case IPS_OK:
            LocationNP.apply();
            TimeTP.apply();
            // We got data OK, but if we are required to update once in a while, we'll call it.
            if (PeriodNP[0].getValue() > 0)
                timerID = SetTimer(PeriodNP[0].getValue() * 1000);

            // Update system time
            // This ideally should be done only ONCE
            switch (SystemTimeUpdateSP.findOnSwitchIndex())
            {
                case UPDATE_ON_STARTUP:
                    if (m_SystemTimeUpdated == false)
                    {
                        setSystemTime(m_GPSTime);
                        m_SystemTimeUpdated = true;
                    }
                    break;

                case UPDATE_ON_REFRESH:
                    setSystemTime(m_GPSTime);
                    break;

                default:
                    break;
            }

            return;
            break;

        // GPS fix is in progress or alert
        case IPS_ALERT:
            LocationNP.apply();
            TimeTP.apply();
            break;

        default:
            break;
    }

    timerID = SetTimer(getCurrentPollingPeriod());
}

IPState GPS::updateGPS()
{
    LOG_ERROR("updateGPS() must be implemented in GPS device child class to update TIME_UTC and GEOGRAPHIC_COORD properties.");
    return IPS_ALERT;
}

//////////////////////////////////////////////////////////////////////
///
//////////////////////////////////////////////////////////////////////
bool GPS::setSystemTime(time_t &raw_time)
{
#ifdef __linux__
#if defined(__GNU_LIBRARY__)
#if (__GLIBC__ >= 2) && (__GLIBC_MINOR__ > 30)
    timespec sTime = {};
    sTime.tv_sec = raw_time;
    auto rc = clock_settime(CLOCK_REALTIME, &sTime);
    if (rc)
        LOGF_WARN("Failed to update system time: %s", strerror(rc));
#else
    stime(&raw_time);
#endif
#else
    stime(&raw_time);
#endif
#else
    INDI_UNUSED(raw_time);
#endif
    return true;
}

//////////////////////////////////////////////////////////////////////
///
//////////////////////////////////////////////////////////////////////
bool GPS::ISNewSwitch(const char *dev, const char *name, ISState *states, char *names[], int n)
{
    if (dev != nullptr && strcmp(dev, getDeviceName()) == 0)
    {
        // Refresh
        if (RefreshSP.isNameMatch(name))
        {
            RefreshSP[0].s = ISS_OFF;
            RefreshSP.setState(IPS_OK);
            RefreshSP.apply();

            // Manual trigger
            GPS::TimerHit();
            return true;
        }
        // System Time Update
        else if (SystemTimeUpdateSP.isNameMatch(name))
        {
            SystemTimeUpdateSP.update(states, names, n);
            SystemTimeUpdateSP.setState(IPS_OK);
            SystemTimeUpdateSP.apply();
            if (SystemTimeUpdateSP.findOnSwitchIndex() == UPDATE_ON_REFRESH)
                LOG_WARN("Updating system time on refresh may lead to undesirable effects on system time accuracy.");
            saveConfig(true, SystemTimeUpdateSP.getName());
            return true;
        }
    }

    return DefaultDevice::ISNewSwitch(dev, name, states, names, n);
}

//////////////////////////////////////////////////////////////////////
///
//////////////////////////////////////////////////////////////////////
bool GPS::ISNewNumber(const char *dev, const char *name, double values[], char *names[], int n)
{
    if (dev != nullptr && strcmp(dev, getDeviceName()) == 0)
    {
        if (PeriodNP.isNameMatch(name))
        {
            double prevPeriod = PeriodNP[0].getValue();
            PeriodNP.update(values, names, n);
            // Do not remove timer if GPS update is still in progress
            if (timerID > 0 && RefreshSP.getState() != IPS_BUSY)
            {
                RemoveTimer(timerID);
                timerID = -1;
            }

            if (PeriodNP[0].getValue() == 0)
            {
                LOG_INFO("GPS Update Timer disabled.");
            }
            else
            {
                timerID = SetTimer(PeriodNP[0].value * 1000);
                // Need to warn user this is not recommended. Startup values should be enough
                if (prevPeriod == 0)
                    LOG_INFO("GPS Update Timer enabled. Warning: Updating system-wide time repeatedly may lead to undesirable side-effects.");
            }

            PeriodNP.setState(IPS_OK);
            PeriodNP.apply();

            return true;
        }
    }

    return DefaultDevice::ISNewNumber(dev, name, values, names, n);
}

//////////////////////////////////////////////////////////////////////
///
//////////////////////////////////////////////////////////////////////
bool GPS::saveConfigItems(FILE *fp)
{
    DefaultDevice::saveConfigItems(fp);

    PeriodNP.save(fp);
    SystemTimeUpdateSP.save(fp);
    return true;
}
}
